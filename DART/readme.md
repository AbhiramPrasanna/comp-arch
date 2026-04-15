# DART — Disaggregated ART Index with Criticality-Aware Caching

## Cluster Setup

| Node | IP | Role |
|------|----|------|
| cs-dis-srv09s | 10.30.1.9 | Memory node |
| cs-dis-srv06s | 10.30.1.6 | Compute + Monitor node |

---

## Build

```bash
cd ~/comp-arch/DART

# Clone submodules if missing
ls gflags/CMakeLists.txt     2>/dev/null || git clone https://github.com/gflags/gflags.git gflags
ls magic_enum/CMakeLists.txt 2>/dev/null || git clone https://github.com/Neargye/magic_enum.git magic_enum

chmod +x build.sh
./build.sh

# Verify binaries exist
ls -lh bin/memory bin/compute bin/monitor
```

---

## Changing the Caching Policy

Edit `src/main/compute.cc` around **line 51**. Exactly **one** policy must be uncommented:

```cpp
// #define POLICY_STATIC        // original DART: structural DFS skip entries
// #define POLICY_HOTNESS       // top-K nodes by raw RDMA read frequency
#define POLICY_CRITICALITY      // top-K nodes by depth_sum (RTT savings) ← recommended
// #define POLICY_HYBRID        // weighted blend: 0.5*hotness + 0.5*criticality
```

Set the skip-table budget at **line 58**:

```cpp
static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;  // try 500, 1000, 5000
```

After any edit, rebuild:

```bash
./build.sh
```

---

## Manual Execution

Start binaries in this exact order: **memory first → monitor second → compute last**

### Terminal 1 — Memory node (10.30.1.9)

```bash
sudo sysctl -w vm.nr_hugepages=1024

bin/memory \
  --monitor_addr=10.30.1.6:9898 \
  --nic_index=0 \
  --ib_port=1
```

Leave this running. It registers its RDMA pool with the monitor and waits.

---

### Terminal 2 — Monitor (10.30.1.6) — start before compute

```bash
bin/monitor \
  --test_func=0 \
  --memory_num=1 \
  --compute_num=1 \
  --load_thread_num=56 \
  --run_thread_num=56 \
  --coro_num=1 \
  --mem_mb=1024 \
  --th_mb=10 \
  --workload_prefix=./workload/data/ \
  --workload_load=f_load \
  --workload_run=f_run \
  --bucket=256 \
  --run_max_request=100000
```

Monitor blocks and waits for both memory and compute to connect before starting.

---

### Terminal 3 — Compute (10.30.1.6) — start after monitor is listening

```bash
bin/compute \
  --monitor_addr=10.30.1.6:9898 \
  --nic_index=0 \
  --ib_port=1 \
  --numa_node_total_num=2 \
  --numa_node_group=0
```

Once all three are connected the experiment runs. Results print in the compute terminal.

---

## Running a Different Workload

Change `--workload_load` and `--workload_run` in the monitor command:

| Workload | Description |
|----------|-------------|
| `f` | 50% read + 50% scan, zipfian distribution |
| `c` | 100% read, zipfian |
| `g` | 100% read, uniform |
| `d` | 100% read, latest |
| `e` | 100% scan, zipfian |
| `a` | 100% read, zipfian |
| `b` | 100% read, zipfian |

Example — run workload c:
```bash
bin/monitor ... --workload_load=c_load --workload_run=c_run ...
```

---

## Running a Full Policy Experiment Manually

### Step 1 — Set policy in compute.cc and rebuild

```bash
# Edit src/main/compute.cc — uncomment the desired policy, comment the rest
nano src/main/compute.cc   # or vim / any editor

./build.sh
```

### Step 2 — Kill any leftover processes

```bash
killall -9 compute monitor memory 2>/dev/null || true
```

### Step 3 — Start memory (10.30.1.9)

```bash
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

### Step 4 — Start monitor (10.30.1.6)

```bash
bin/monitor \
  --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ \
  --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

### Step 5 — Start compute (10.30.1.6)

```bash
bin/compute \
  --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

Repeat Steps 1–5 for each policy (STATIC → HOTNESS → CRITICALITY → HYBRID).

---

## Automated Full Experiment Sweep

Three terminals, start in order:

**Terminal 1 — on 10.30.1.9:**
```bash
chmod +x run_memory.sh && ./run_memory.sh
```

**Terminal 2 — on 10.30.1.6:**
```bash
chmod +x run_compute.sh && ./run_compute.sh
```

**Terminal 3 — on 10.30.1.6 (drives the sweep):**
```bash
chmod +x run_monitor.sh && ./run_monitor.sh
```

`run_monitor.sh` iterates all 13 experiments, rebuilds the binary for each policy change, and saves results to `results/<timestamp>/summary.txt`.

Use `--dry-run` to preview the experiment list without running:
```bash
./run_monitor.sh --dry-run
```

---

## Experiment Matrix

### Section 1: Policy Comparison (workload f, budget=5000, th_mb=10)

| Policy | Workload | Budget | th_mb |
|--------|----------|--------|-------|
| POLICY_STATIC | f | 5000 | 10 |
| POLICY_HOTNESS | f | 5000 | 10 |
| POLICY_CRITICALITY | f | 5000 | 10 |
| POLICY_HYBRID | f | 5000 | 10 |

### Section 2: Cache Pressure Sweep (POLICY_CRITICALITY, workload f)

| Budget | th_mb |
|--------|-------|
| 500 | 2 |
| 1000 | 2 |
| 5000 | 2 |
| 500 | 10 |
| 1000 | 10 |
| 5000 | 10 |
| 500 | 50 |
| 1000 | 50 |
| 5000 | 50 |

---

## Workload Generation

Requires YCSB 0.17.0. Workload specs are in `workload_spec/`.

```bash
# Download YCSB (streaming extract — avoids large disk usage)
mkdir -p workload/ycsb
wget -qO- https://github.com/brianfrankcooper/YCSB/releases/download/0.17.0/ycsb-0.17.0.tar.gz \
    | tar -xz -C workload/ycsb --strip-components=1

CP=$(find workload/ycsb -name "*.jar" | tr '\n' ':')
mkdir -p workload/data

# Generate workload f (50% read + 50% scan)
java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
    -P workload_spec/f -s -load > workload/data/f_load
java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
    -P workload_spec/f -s -t   > workload/data/f_run

echo "load: $(wc -l < workload/data/f_load) lines"
echo "run:  $(wc -l < workload/data/f_run) lines"
```

---

## Key Parameters

| Parameter | Where | Default | Description |
|-----------|-------|---------|-------------|
| `POLICY_*` | `src/main/compute.cc:51` | CRITICALITY | Active caching policy |
| `POLICY_MAX_ENTRIES` | `src/main/compute.cc:58` | 5000 | Skip-table budget |
| `--mem_mb` | monitor flag | 1024 | Remote memory pool (MiB) |
| `--th_mb` | monitor flag | 10 | Per-thread local cache (MiB) |
| `--run_max_request` | monitor flag | 100000 | Operations in run phase |
| `--load_thread_num` | monitor flag | 56 | Threads for load phase |
| `--run_thread_num` | monitor flag | 56 | Threads for benchmark |

---

## Output Metrics

```
throughput: X.XX MOps      ← million ops/sec          (higher is better)
latency: XX.X us           ← average latency           (lower is better)
avg rtt count: X.XX        ← RDMA round-trips per op   (lower is better)
num shortcuts: XXXX        ← skip-table entries built
```

---

## Troubleshooting

**`connect to monitor error`** — Monitor is not running yet. Start monitor before compute.

**`ib device wasn't found`** — Wrong NIC index. Use `--nic_index=0`.

**`Address already in use` on port 9898** — Kill leftover monitor:
```bash
killall -9 monitor compute memory 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
```

**Workload 0 records** — Stale binary cache. Delete and regenerate:
```bash
rm -f workload/data/*__bin_*
```

**Disk full** — Remove build intermediates:
```bash
rm -rf build/CMakeFiles build/src
```
