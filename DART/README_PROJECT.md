# DART — Performance-Criticality-Aware Caching Policies
## Exploring Skip-Table Construction Strategies in a DART-Based RDMA Index

> **Paper base**: DART: A Lock-free Two-layer Hashed ART Index for Disaggregated Memory (SIGMOD'26)  
> **Project extension**: Phase 2 access tracking + Phase 3 policy-aware skip-table construction (HOTNESS / CRITICALITY / HYBRID)

---

## Table of Contents

1. [System Architecture Overview](#1-system-architecture-overview)
2. [Hardware Requirements](#2-hardware-requirements)
3. [Software Dependencies](#3-software-dependencies)
4. [Repository Setup and Build](#4-repository-setup-and-build)
5. [Cluster Network Configuration](#5-cluster-network-configuration)
6. [Workload Generation](#6-workload-generation)
7. [Running the Cluster — Startup Sequence](#7-running-the-cluster--startup-sequence)
   - [7.1 Monitor Node](#71-monitor-node)
   - [7.2 Memory Node(s)](#72-memory-nodes)
   - [7.3 Compute Node(s)](#73-compute-nodes)
8. [Phase 1 — Baseline (Static Skip Table)](#8-phase-1--baseline-static-skip-table)
9. [Phase 2 — Access Tracking and Instrumentation](#9-phase-2--access-tracking-and-instrumentation)
10. [Phase 3 — Policy-Aware Skip Table Experiments](#10-phase-3--policy-aware-skip-table-experiments)
11. [Understanding the Output](#11-understanding-the-output)
12. [Utility Scripts](#12-utility-scripts)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. System Architecture Overview

DART uses a **three-role cluster**:

```
┌─────────────┐   TCP (port 9898)    ┌─────────────────────────────────────────┐
│   Monitor   │◄────────────────────►│  Compute Node(s)  (active RDMA client)  │
│  (1 node)   │                      │  - Runs ART traversal                   │
│  TCP coord  │◄────────────────────►│  - Builds/queries skip table (RACE)     │
└─────────────┘                      │  - Issues RDMA READ/WRITE/CAS           │
                                     └────────────────┬────────────────────────┘
                                                      │ RDMA (InfiniBand)
                                     ┌────────────────▼────────────────────────┐
                                     │  Memory Node(s)   (passive RDMA store)  │
                                     │  - Registers HugePage memory region     │
                                     │  - CPU never touches ART data directly  │
                                     └─────────────────────────────────────────┘
```

**Key concepts:**

| Term | Meaning |
|------|---------|
| **fptr** | 64-bit fake pointer: bits[47:44] = memory machine ID, bits[43:0] = offset |
| **RTT** | One RDMA READ or WRITE = one round-trip; the primary latency metric |
| **Skip table** | RACE hash entries mapping key prefixes → deep ART nodes; reduces RTTs |
| **now_pos** | Byte depth in the key at which a node sits; = RTTs saved by a skip entry |
| **Hotness** | Read frequency: how many times a node was accessed |
| **Criticality** | `depth_sum` = sum of `now_pos` at every read = total RTT savings potential |

---

## 2. Hardware Requirements

- **InfiniBand NICs** on all nodes (RoCE v2 or IB), connected via InfiniBand switch
- At least **1 Monitor node** (lightweight; any Linux machine with network access)
- At least **1 Memory node** (needs large RAM, e.g. 64–256 GB; hugepages required)
- At least **1 Compute node** (multi-core; NUMA-aware pinning recommended)
- Operating System: **Ubuntu 20.04 / 22.04** (Linux only — uses `libibverbs`, hugepages, `execinfo`, `cxxabi`)

> The code does **not** compile on Windows or macOS. If you see VS Code IntelliSense errors about missing `infiniband/verbs.h` on a Windows workstation, those are false positives — the actual compilation must happen on Linux.

---

## 3. Software Dependencies

Run on **every node** (monitor, memory, compute):

```bash
# Core build tools
sudo apt update
sudo apt install -y cmake g++ git

# Boost coroutines (for yield-based async)
sudo apt install -y libboost-context-dev libboost-coroutine-dev

# RDMA userspace libraries
sudo apt install -y libibverbs-dev librdmacm-dev

# GFlags (command-line parsing)
sudo apt install -y libgflags-dev

# Python 2 is required for YCSB workload generation
sudo apt install -y python2

# Python 3 (for workload splitting scripts)
sudo apt install -y python3 python3-pip
```

For the **Zipfian workload generator** (optional; needed for email/skewed workloads):

```bash
pip3 install pybind11
```

---

## 4. Repository Setup and Build

Run on **all nodes** (each node needs its own compiled binaries):

```bash
# 1. Clone the repository
git clone <this-repo-url> DART
cd DART

# 2. Pull all submodules (RACE, gflags, etc.)
git submodule update --init --recursive

# 3. Install dependencies (see Section 3)

# 4. Build
cmake -B build
cmake --build build -j$(nproc)
```

After a successful build you will have:

```
bin/monitor    — coordinator process
bin/memory     — passive memory store
bin/compute    — ART index client
```

**Quick rebuild** (after editing source):

```bash
./build.sh
# which is equivalent to: cmake -B build && cmake --build build
```

---

## 5. Cluster Network Configuration

### Hard-coded IPs (edit before compiling)

In [src/main/compute.cc](src/main/compute.cc) (line 61):

```cpp
const char* ips[] = {"192.168.98.74", "192.168.98.72", "192.168.98.70"};
```

These are the InfiniBand IP addresses of your **memory nodes** in order (machine 0, 1, 2, ...). Update them to match your cluster, then recompile.

### Hugepages (on every Memory node)

The memory node registers a large RDMA memory region backed by hugepages. Configure before starting `memory`:

```bash
# 16384 pages × 2 MiB = 32 GiB total hugepage pool
sudo sysctl -w vm.nr_hugepages=16384

# Verify
grep -i hugepages /proc/meminfo
```

Adjust `nr_hugepages` to match the `--mem_mb` parameter you pass to `monitor`.

### SSH key distribution (for workload splitting)

`split_and_send_workload.py` uses `scp` and `ssh`. Set up passwordless SSH from your orchestration machine to all compute nodes:

```bash
ssh-keygen -t ed25519  # if you don't have a key
ssh-copy-id user@192.168.98.70
ssh-copy-id user@192.168.98.72
ssh-copy-id user@192.168.98.74
```

---

## 6. Workload Generation

### 6.1 Standard YCSB Workloads (a–g)

```bash
# Step 1: Download YCSB
./script/workload_download.py

# Step 2: Generate all workloads a through g
./script/workload_gen.sh
```

This produces files under `workload/data/`:

| File | Contents |
|------|----------|
| `a_load`, `a_run` | 50% read / 50% update, Zipfian |
| `b_load`, `b_run` | 95% read / 5% update, Zipfian |
| `c_load`, `c_run` | 100% read, Zipfian |
| `d_load`, `d_run` | 95% read / 5% insert, Latest |
| `e_load`, `e_run` | Scan-heavy |
| `f_load`, `f_run` | 50% read / 50% RMW |
| `g_load`, `g_run` | Custom |

### 6.2 Split and Send to Multiple Compute Nodes

When running with multiple compute nodes, each node needs its own slice of the workload:

```bash
# Example: split workloads a_load and a_run across 3 compute nodes
# with IPs ending in 70, 72, 74 (prefix 192.168.98.)
python3 ./script/split_and_send_workload.py \
    --inputs a_load a_run \
    --outputs a_load_split a_run_split \
    --ip_prefix 192.168.98. \
    --ips 70 72 74
```

The script will:
1. Divide the workload file evenly (N records per node)
2. `scp` each slice to `~/DART/workload/split/` on each compute node

**After sending**, clean up binary buffer files:

```bash
./clean_buff.sh
```

### 6.3 Zipfian / Email Workloads (optional)

For skewed email-key workloads (demonstrates hotness vs. criticality divergence most clearly):

```bash
# Build the pybind11 extension first
cd script/zipfian
python3 -m pip install pybind11
python3 setup.py build_ext --inplace
cd ../..

# Generate a Zipfian email workload (zipf_para 0~1; higher = more skewed)
script/zipfian/gen_email.py \
    --input /opt/email_filtered.txt \
    --max 100000 \
    --zipf_para 0.99
```

> `email_filtered.txt` must be prepared separately — it is a plain-text file of email addresses, one per line.

---

## 7. Running the Cluster — Startup Sequence

**Critical**: always start in this order:  
**Monitor** → **Memory node(s)** → **Compute node(s)**

The monitor is the TCP rendezvous point. Memory nodes connect first and register their RDMA memory. Compute nodes connect last, receive all connection metadata, then begin the load + run phases.

---

### 7.1 Monitor Node

The monitor orchestrates the entire experiment. Run this **first**, on whichever machine you designate as the coordinator.

```bash
bin/monitor \
    --test_func=0 \
    --memory_num=1 \
    --compute_num=1 \
    --load_thread_num=56 \
    --run_thread_num=56 \
    --coro_num=1 \
    --mem_mb=8192 \
    --th_mb=10 \
    --workload_load=a_load \
    --workload_run=a_run \
    --bucket=256 \
    --run_max_request=6000000
```

**Parameter reference:**

| Flag | Meaning | Typical value |
|------|---------|---------------|
| `--test_func` | Which test function to run (0 = standard YCSB) | `0` |
| `--memory_num` | How many memory nodes to wait for | `1` to `3` |
| `--compute_num` | How many compute nodes to wait for | `1` to `3` |
| `--load_thread_num` | Threads per compute node for the load phase | `56` (match core count) |
| `--run_thread_num` | Threads per compute node for the run phase | `56` |
| `--coro_num` | Coroutines per thread (1 = synchronous) | `1` |
| `--mem_mb` | Total remote memory in MiB to allocate | `8192` (= 8 GiB) |
| `--th_mb` | Per-thread local memory buffer in MiB | `10` |
| `--workload_load` | Load-phase workload filename (under `workload/`) | `a_load` or `a_load_split` |
| `--workload_run` | Run-phase workload filename | `a_run` or `a_run_split` |
| `--bucket` | RACE hash table bucket count | `256` |
| `--run_max_request` | Maximum operations in run phase | `6000000` |

The monitor blocks until all memory and compute nodes have connected, then broadcasts the experiment configuration and waits for results.

---

### 7.2 Memory Node(s)

Run on each machine that will serve as remote memory. Run this **after** the monitor is up.

```bash
# Ensure hugepages are configured first
sudo sysctl -w vm.nr_hugepages=16384

bin/memory \
    --monitor_addr=192.168.98.100:9898 \
    --nic_index=1 \
    --ib_port=1
```

**Parameter reference:**

| Flag | Meaning | Typical value |
|------|---------|---------------|
| `--monitor_addr` | `host:port` of the monitor node | IP of your monitor machine, port `9898` |
| `--nic_index` | Which NIC to use for RDMA (0-indexed) | `1` (commonly the IB NIC is index 1) |
| `--ib_port` | InfiniBand port number on the NIC | `1` |

The memory binary registers a hugepage-backed memory region, exchanges RDMA connection info (QP numbers, LIDs, GIDs) with all compute nodes via the monitor, then **sits idle** — the CPU on memory nodes is never involved in serving requests. All data is read/written by the compute nodes via RDMA.

**If you have multiple memory nodes**, run the binary on each one, all pointing to the same `--monitor_addr`. The monitor waits until `--memory_num` nodes have connected before proceeding.

---

### 7.3 Compute Node(s)

Run on each compute machine **after both** the monitor and all memory nodes are up.

```bash
bin/compute \
    --monitor_addr=192.168.98.100:9898 \
    --nic_index=0 \
    --ib_port=1 \
    --numa_node_total_num=2 \
    --numa_node_group=0
```

**Parameter reference:**

| Flag | Meaning | Typical value |
|------|---------|---------------|
| `--monitor_addr` | `host:port` of the monitor | Same as memory nodes |
| `--nic_index` | RDMA NIC index (compute typically uses NIC 0) | `0` |
| `--ib_port` | IB port on the NIC | `1` |
| `--numa_node_total_num` | Total NUMA groups on the machine | `2` (dual-socket) |
| `--numa_node_group` | Which NUMA group this instance uses | `0` or `1` |

On a dual-socket machine with 2 compute nodes sharing it, run one instance with `--numa_node_group=0` and another with `--numa_node_group=1` to avoid cross-NUMA memory traffic. The binary uses `sched_setaffinity` to pin each thread to the correct cores.

**What the compute node does (in order):**

1. **Connect** to monitor (TCP) → receive experiment config + RDMA connection info
2. **RDMA handshake** with all memory nodes (exchange QPs)
3. **Load phase** — insert all keys from `workload_load` into the remote ART using multiple threads
4. **Skip-table construction** — walk the ART (DFS) and insert RACE entries for high-value nodes
5. **Reset access tracker** (clears load-phase statistics so run-phase is clean)
6. **Run phase** — execute YCSB operations from `workload_run`, reporting throughput and latency
7. **Report** — prints per-thread stats (RTTs, throughput MOps/s, latency µs) and access tracker summaries

---

## 8. Phase 1 — Baseline (Static Skip Table)

This is the original DART behaviour. No changes required.

**In [src/main/compute.cc](src/main/compute.cc)**, ensure:

```cpp
#define POLICY_STATIC
// #define POLICY_HOTNESS
// #define POLICY_CRITICALITY
// #define POLICY_HYBRID
```

**In [src/prheart/art-node.cc](src/prheart/art-node.cc)**, optionally comment out tracking to reduce overhead:

```cpp
// #define ENABLE_ACCESS_TRACKING
```

Build and run as described in Section 7. The skip table is built via structural DFS analysis — every ART node at depth ≥ threshold gets a RACE entry, without any regard for access frequency or criticality.

**Baseline metrics to record:**

- Throughput (MOps/s) — reported by each compute thread
- Average latency (µs)
- RTT count per operation (reported via `rtt` thread-local)
- Number of skip entries inserted (printed after `create_skip_table()`)

---

## 9. Phase 2 — Access Tracking and Instrumentation

Phase 2 adds per-node RDMA access tracking. It is enabled by a single `#define`.

**In [src/prheart/art-node.cc](src/prheart/art-node.cc)**:

```cpp
#define ENABLE_ACCESS_TRACKING
```

This hooks into every `rdma_read_real_data()` and `rdma_write_real_data()` call for inner nodes (leaves excluded), recording:

- `read_count` — raw RDMA read frequency
- `depth_sum` — cumulative sum of `now_pos` across all reads (= RTT savings potential)
- `write_count` — RDMA writes (insert/upgrade pressure)

Keep `POLICY_STATIC` in `compute.cc` for Phase 2 — you are not yet changing policy, just collecting data.

**After running**, the compute node will print (automatically, at end of load phase and end of run phase):

```
=== AccessTracker Summary  (policy=HOTNESS) ===
  Distinct inner nodes tracked: 12847
  Rank | fptr               | reads    | depth_sum  | writes | hotness | criticality
  ----|--------------------|---------|-----------|---------|---------|-----------
     1 | 0x0001_0000_0000   |    98432 |    1478272 |     12 |   98432 |    1478272
     2 | 0x0001_0000_0080   |    76210 |     342945 |      0 |   76210 |     342945
  ...
=== Policy Comparison  (top-100) ===
  HOTNESS top-100:     100 nodes
  CRITICALITY top-100: 100 nodes
  Overlap:              42 nodes (42.0%)
  HOTNESS-only  (frequent but shallow): 58
  CRITICALITY-only (deep critical path): 58
  => Divergence detected. CRITICALITY policy may reduce latency for 58 deep nodes.
```

**Interpreting the policy comparison:**

- High overlap (>80%): hotness and criticality agree; any policy will perform similarly
- Low overlap (<50%): significant divergence — a shallow, frequently-accessed node (high hotness) may save fewer RTTs than a deeply-accessed node visited less often; switching to CRITICALITY may yield measurable latency improvements

---

## 10. Phase 3 — Policy-Aware Skip Table Experiments

Phase 3 compares skip-table construction strategies. Each policy is a single `#define` change + recompile.

### Switching Policies

**In [src/main/compute.cc](src/main/compute.cc)**, uncomment exactly ONE:

```cpp
// Option A — original DART (no tracking needed)
#define POLICY_STATIC

// Option B — top-K nodes by raw read frequency
// #define POLICY_HOTNESS

// Option C — top-K nodes by total depth_sum (RTT savings)
// #define POLICY_CRITICALITY

// Option D — weighted blend (0.5 × hotness + 0.5 × criticality)
// #define POLICY_HYBRID
```

Also set the skip-table budget:

```cpp
static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;
```

Lower values (e.g. 500, 1000) simulate a tighter RACE table budget. Higher values allow more skip entries but increase RACE lookup cost.

**POLICY_HOTNESS, CRITICALITY, and HYBRID all require `ENABLE_ACCESS_TRACKING`** in `art-node.cc`.

### Recommended Experiment Matrix

For each policy, run workload C (100% read, Zipfian) first — it gives the cleanest latency signal since there are no concurrent inserts.

| Run | Policy | workload | Expected outcome |
|-----|--------|----------|-----------------|
| 1 | POLICY_STATIC | c_run | Baseline RTTs and throughput |
| 2 | POLICY_HOTNESS | c_run | May improve throughput for hot root-adjacent nodes |
| 3 | POLICY_CRITICALITY | c_run | May reduce average latency by skipping deep nodes |
| 4 | POLICY_HYBRID | c_run | Balanced; compare against runs 2 & 3 |
| 5 | POLICY_CRITICALITY | a_run | Mixed read+update (more realistic) |

For each run:
1. Edit the `#define` in `compute.cc`
2. `./build.sh`
3. Distribute new binary to all compute nodes: `scp bin/compute user@<node>:~/DART/bin/`
4. Restart the cluster (Section 7)
5. Record throughput MOps/s, avg latency µs, RTT/op, skip table entries inserted

### How Policy Selection Works Internally

```
Load phase completes
        │
        ▼
AccessTracker::print_summary()          ← see which nodes dominated
AccessTracker::print_policy_comparison()← measure HOTNESS vs CRITICALITY divergence
        │
        ├─ POLICY_STATIC:     PrheartTree::create_skip_table()
        │                     (structural DFS, depth threshold)
        │
        ├─ POLICY_HOTNESS:    AccessTracker::get_top_k_set(k, HOTNESS)
        │                     → PrheartNode::add_shortcut_policy(target_fptrs)
        │
        ├─ POLICY_CRITICALITY: AccessTracker::get_top_k_set(k, CRITICALITY)
        │                     → PrheartNode::add_shortcut_policy(target_fptrs)
        │
        └─ POLICY_HYBRID:     AccessTracker::get_top_k_set(k, HYBRID, alpha=0.5)
                              → PrheartNode::add_shortcut_policy(target_fptrs)
        │
        ▼
AccessTracker::reset()                  ← clear load-phase data
        │
        ▼
Run phase executes
        │
        ▼
AccessTracker::print_summary()          ← run-phase access distribution
AccessTracker::print_policy_comparison()← validate that tracked nodes match prediction
```

---

## 11. Understanding the Output

### Per-thread throughput and latency

Each compute thread prints at the end of the run phase:

```
[thread 0] ops=107142  time=1.00s  throughput=0.107 MOps/s  lat=9.34us  rtt=4.2
```

- **throughput**: million operations per second (higher = better)
- **lat**: average end-to-end latency in microseconds (lower = better)
- **rtt**: average RDMA round-trips per operation (lower = better; a good skip table entry eliminates multiple RTTs)

### Skip table construction summary

```
[create_skip_table_policy] policy=CRITICALITY  entries_inserted=4873  budget=5000
```

If `entries_inserted` is significantly less than `budget`, it means the ART has fewer eligible inner nodes than the budget allows — reduce `POLICY_MAX_ENTRIES` or increase the dataset size.

### AccessTracker output

Printed automatically at two points (if `ENABLE_ACCESS_TRACKING` is defined):

1. **After load phase** — shows which nodes were structurally important during insertion
2. **After run phase** — shows which nodes were hot/critical during query execution

The policy comparison output (Section 9) is the key diagnostic. A large CRITICALITY-only count (nodes that are deep but infrequent) is evidence that POLICY_STATIC and POLICY_HOTNESS are leaving RTT savings on the table.

---

## 12. Utility Scripts

| Script | Purpose | Usage |
|--------|---------|-------|
| `./kill.sh` | Kill all DART processes on this node | `./kill.sh` |
| `./net.sh` | Check if monitor port 9898 is in use | `./net.sh` |
| `./build.sh` | Rebuild binaries | `./build.sh` |
| `./clean_buff.sh` | Remove binary workload buffer files from `workload/split/` | `./clean_buff.sh` |
| `./init-build.sh` | Full clean rebuild including submodule init | `./init-build.sh` |
| `script/workload_download.py` | Download YCSB tool | `./script/workload_download.py` |
| `script/workload_gen.sh` | Generate workloads a–g using YCSB | `./script/workload_gen.sh` |
| `script/split_and_send_workload.py` | Split workload and distribute via scp | See Section 6.2 |

### Kill all processes on all nodes

From your orchestration machine (requires SSH access):

```bash
for ip in 192.168.98.70 192.168.98.72 192.168.98.74; do
    ssh user@${ip} "sudo killall -9 compute memory monitor 2>/dev/null; echo done on ${ip}"
done
```

---

## 13. Troubleshooting

### Monitor hangs waiting for connections

The monitor blocks until `--memory_num` memory nodes and `--compute_num` compute nodes have all connected. Check:

```bash
# Verify monitor is listening on port 9898
./net.sh
# or
sudo netstat -pa | grep 9898

# Check memory node can reach monitor
ssh user@<memory-node> ping <monitor-ip>
```

### "Cannot allocate memory" on memory node

Hugepages are not configured or are insufficient:

```bash
grep -i hugepages /proc/meminfo
# AnonHugePages should be > 0; HugePages_Free should be > 0

sudo sysctl -w vm.nr_hugepages=16384
```

Also check that `--mem_mb` on the monitor is consistent with the hugepage pool size.

### RDMA connection errors / QP failures

1. Verify the IB interface is up: `ibstat` or `ip link show`
2. Check `--nic_index` and `--ib_port` match what `ibstat` reports
3. If using RoCE, verify the Ethernet interface MTU ≥ 4096: `ip link set <dev> mtu 4096`
4. The hard-coded IPs in `compute.cc` must match the actual IB IP addresses of your memory nodes

### IntelliSense errors in VS Code (on Windows)

Errors like `cannot open source file "infiniband/verbs.h"` or `"boost/coroutine2/all.hpp"` are **Windows IntelliSense false positives**. This project requires Linux. The source code is correct; VS Code on Windows cannot find Linux-only system headers. Compile and run on Linux only.

### Tracker shows "No reads recorded"

`ENABLE_ACCESS_TRACKING` is not defined in `art-node.cc`. Uncomment the `#define` at the top of the file and recompile.

### Policy builds fail to link

If you switch to `POLICY_HOTNESS/CRITICALITY/HYBRID` but forgot to enable `ENABLE_ACCESS_TRACKING`, the `AccessTracker::instance()` calls in `art-node.cc` will compile but the tracker will always be empty. The skip table will be built with zero entries. Always set both defines together.

### Skip table entries = 0

- If using a policy-based method, confirm `ENABLE_ACCESS_TRACKING` is defined
- Check that the load phase actually completed before `create_skip_table_policy` was called
- Confirm `POLICY_MAX_ENTRIES` is > 0
- Check that the RACE table bucket count (`--bucket` on monitor) is large enough

### Workload split fails

```
Error: kline is too large or ycsb workload is too small.
```

Remove `--kline` from the `split_and_send_workload.py` call to use all available lines, or reduce `--kline` to a smaller value.

---

## Quick Reference — Single-Node Test (All on Localhost)

For development/debugging without a real RDMA cluster:

```bash
# Terminal 1: Monitor
bin/monitor \
    --test_func=0 --memory_num=1 --compute_num=1 \
    --load_thread_num=4 --run_thread_num=4 --coro_num=1 \
    --mem_mb=512 --th_mb=10 \
    --workload_load=c_load --workload_run=c_run \
    --bucket=256 --run_max_request=100000

# Terminal 2: Memory (after monitor is up)
sudo sysctl -w vm.nr_hugepages=512
bin/memory --monitor_addr=127.0.0.1:9898

# Terminal 3: Compute (after memory is up)
bin/compute --monitor_addr=127.0.0.1:9898
```

> Loopback RDMA requires a software RDMA driver (rxe or siw). Real InfiniBand hardware is needed for production performance numbers.

---

## Project-Specific Implementation Notes

### New files added (Phase 2/3 extensions)

| File | Purpose |
|------|---------|
| [include/prheart/access-tracker.hpp](include/prheart/access-tracker.hpp) | `CachePolicy` enum, `NodeAccessRecord`, `AccessTracker` singleton |
| [src/prheart/access-tracker.cc](src/prheart/access-tracker.cc) | Full implementation with 256-shard mutex design |

### Modified files

| File | What changed |
|------|-------------|
| [src/prheart/art-node.cc](src/prheart/art-node.cc) | `ENABLE_ACCESS_TRACKING` hook in `rdma_read_real_data` / `rdma_write_real_data`; `add_shortcut_policy()` implementation |
| [include/prheart/art-node.hpp](include/prheart/art-node.hpp) | `add_shortcut_policy()` and `create_skip_table_policy()` declarations |
| [include/prheart/prheart.hpp](include/prheart/prheart.hpp) | Added `#include "prheart/access-tracker.hpp"` |
| [src/main/compute.cc](src/main/compute.cc) | Policy `#define` block; tracker print and reset calls around `create_skip_table` |

### Scoring functions (in access-tracker.hpp)

```cpp
// HOTNESS: rank by raw access frequency
double hotness_score()     = read_count

// CRITICALITY: rank by total RTT savings potential
double criticality_score() = depth_sum   // = Σ now_pos across all reads

// HYBRID: tunable blend
double hybrid_score(alpha) = alpha * hotness + (1-alpha) * criticality
```

---

*DART SIGMOD'26 base implementation by the original authors. Phase 2/3 access tracking and policy-aware skip table extensions added for the Comparch project.*
