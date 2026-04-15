# DART — Disaggregated ART Index with Criticality-Aware Caching

## Cluster Setup

| Server | IP | Role |
|--------|----|------|
| cs-dis-srv06s | **10.30.1.6** | Memory node — runs `bin/memory` |
| cs-dis-srv09s | **10.30.1.9** | Compute + Monitor node — runs `bin/compute` and `bin/monitor` |

> All commands below are run from `~/comp-arch/DART` on the respective server unless stated otherwise.

---

## 1. First-Time Setup (both servers)

Run on **both 10.30.1.6 and 10.30.1.9**:

```bash
cd ~
git clone https://github.com/AbhiramPrasanna/comp-arch.git
cd comp-arch/DART

# Clone missing submodules if not present
ls gflags/CMakeLists.txt     2>/dev/null || git clone https://github.com/gflags/gflags.git gflags
ls magic_enum/CMakeLists.txt 2>/dev/null || git clone https://github.com/Neargye/magic_enum.git magic_enum

chmod +x build.sh run_memory.sh run_compute.sh run_monitor.sh
./build.sh

# Verify binaries
ls -lh bin/memory bin/compute bin/monitor
```

---

## 2. Workload Generation (on 10.30.1.9 — compute node only)

Workload files live in `workload/data/` on the **compute node (10.30.1.9)**. Only needs to be done once.

```bash
# On 10.30.1.9
cd ~/comp-arch/DART

# Download YCSB 0.17.0
mkdir -p workload/ycsb
wget -qO- https://github.com/brianfrankcooper/YCSB/releases/download/0.17.0/ycsb-0.17.0.tar.gz \
    | tar -xz -C workload/ycsb --strip-components=1

# Build classpath
CP=$(find workload/ycsb -name "*.jar" | tr '\n' ':')
mkdir -p workload/data

# Generate all workloads (1M ops each — ~120 MB per file, ~840 MB total for all)
for WL in a b c d e f g; do
    echo "=== Generating workload $WL ==="
    java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
        -P workload_spec/$WL -s -load > workload/data/${WL}_load
    java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
        -P workload_spec/$WL -s -t   > workload/data/${WL}_run
    echo "  load: $(wc -l < workload/data/${WL}_load) lines"
    echo "  run:  $(wc -l < workload/data/${WL}_run) lines"
done

# Check disk usage
du -sh workload/data/
```

---

## 3. Changing the Caching Policy

Edit `src/main/compute.cc` on **10.30.1.9**. Find lines ~51–54 and uncomment **exactly one** policy:

```cpp
// #define POLICY_STATIC        // original DART: structural DFS (no tracking needed)
// #define POLICY_HOTNESS       // top-K nodes by raw RDMA read count
#define POLICY_CRITICALITY      // top-K nodes by depth_sum — RTT savings (recommended)
// #define POLICY_HYBRID        // 0.5 * hotness + 0.5 * criticality
```

Change the skip-table budget on the line below:

```cpp
static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;  // options: 500, 1000, 5000
```

After every edit, rebuild on **10.30.1.9**:

```bash
# On 10.30.1.9
cd ~/comp-arch/DART
./build.sh
```

---

## 4. Startup Order (always follow this)

```
10.30.1.6  →  start bin/memory   (passive, waits for RDMA connections)
10.30.1.9  →  start bin/monitor  (coordinator, listens on port 9898)
10.30.1.9  →  start bin/compute  (active client, drives the benchmark)
```

---

## 5. Kill Leftover Processes (before every experiment)

```bash
# On 10.30.1.6
killall -9 memory 2>/dev/null || true

# On 10.30.1.9
killall -9 monitor compute 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
sleep 2
```

---

## 6. All 13 Experiments — Full Manual Commands

### Experiment 1 — POLICY_STATIC, budget=5000, th_mb=10

**On 10.30.1.9 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_STATIC$|#define POLICY_STATIC|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 2 — POLICY_HOTNESS, budget=5000, th_mb=10

**On 10.30.1.9 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_HOTNESS$|#define POLICY_HOTNESS|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 3 — POLICY_CRITICALITY, budget=5000, th_mb=10

**On 10.30.1.9 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_CRITICALITY$|#define POLICY_CRITICALITY|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 4 — POLICY_HYBRID, budget=5000, th_mb=10

**On 10.30.1.9 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_HYBRID$|#define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

> Experiments 5–13 all use **POLICY_CRITICALITY**. Only `budget` (rebuild needed when it changes) and `--th_mb` vary.

---

### Experiment 5 — POLICY_CRITICALITY, budget=500, th_mb=2

**On 10.30.1.9 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_CRITICALITY$|#define POLICY_CRITICALITY|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 6 — POLICY_CRITICALITY, budget=1000, th_mb=2

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 7 — POLICY_CRITICALITY, budget=5000, th_mb=2

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 8 — POLICY_CRITICALITY, budget=500, th_mb=10

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 9 — POLICY_CRITICALITY, budget=1000, th_mb=10

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 10 — POLICY_CRITICALITY, budget=5000, th_mb=10

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 11 — POLICY_CRITICALITY, budget=500, th_mb=50

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 12 — POLICY_CRITICALITY, budget=1000, th_mb=50

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 13 — POLICY_CRITICALITY, budget=5000, th_mb=50

**On 10.30.1.9 — change budget and rebuild:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.6 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.9 — terminal 1 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=2048 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=1000000
```

**On 10.30.1.9 — terminal 2 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.9:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

## 7. Reading Results

In the compute terminal you will see:

```
=== ART Tree Stats ===
  Tree height (max now_pos / RTT depth) : 12
  Total distinct inner nodes tracked    : 48230
  Nodes per depth level:
    depth  1 : 3 nodes
    depth  2 : 21 nodes
    ...

throughput: 0.396568 MOps     ← million ops/sec        (higher is better)
latency: 141.212 us           ← average op latency      (lower is better)
avg rtt count: 4.50           ← RDMA round-trips/op     (lower is better)
num shortcuts: 4118           ← skip-table entries built
```

---

## 8. Experiment Matrix

### Policy Comparison (workload f = 50% read + 50% scan, zipfian)

| Exp | Policy | Budget | th_mb | What it tests |
|-----|--------|--------|-------|---------------|
| 1 | POLICY_STATIC | 5000 | 10 | Baseline — structural skip entries |
| 2 | POLICY_HOTNESS | 5000 | 10 | Frequency-based caching |
| 3 | POLICY_CRITICALITY | 5000 | 10 | RTT-savings-based caching |
| 4 | POLICY_HYBRID | 5000 | 10 | Blend of hotness + criticality |

### Cache Pressure Sweep (POLICY_CRITICALITY, workload f)

| Exp | Budget | th_mb | What it tests |
|-----|--------|-------|---------------|
| 5 | 500 | 2 | Tiny budget, tight cache |
| 6 | 1000 | 2 | Small budget, tight cache |
| 7 | 5000 | 2 | Large budget, tight cache |
| 8 | 500 | 10 | Tiny budget, medium cache |
| 9 | 1000 | 10 | Small budget, medium cache |
| 10 | 5000 | 10 | Large budget, medium cache |
| 11 | 500 | 50 | Tiny budget, large cache |
| 12 | 1000 | 50 | Small budget, large cache |
| 13 | 5000 | 50 | Large budget, large cache |

---

## 9. Key Parameters Reference

| Parameter | Location | Default | Description |
|-----------|----------|---------|-------------|
| `POLICY_*` | `src/main/compute.cc:~53` | CRITICALITY | Active caching policy |
| `POLICY_MAX_ENTRIES` | `src/main/compute.cc:~58` | 5000 | Max skip-table entries |
| `--mem_mb` | monitor flag | 2048 | Remote memory pool in MiB (1M records @ fieldlen=100 needs ~1.22 GB; use 2048 for safe margin) |
| `--th_mb` | monitor flag | 10 | Per-thread local RDMA cache in MiB |
| `--run_max_request` | monitor flag | 1000000 | Operations in run phase |
| `--load_thread_num` | monitor flag | 56 | Threads for loading data |
| `--run_thread_num` | monitor flag | 56 | Threads for benchmark run |
| `--bucket` | monitor flag | 256 | RACE hash table bucket size |
| `--nic_index` | memory/compute flag | 0 | InfiniBand NIC index |
| `--ib_port` | memory/compute flag | 1 | InfiniBand port number |

---

## 10. Workload Reference

| Letter | Read% | Scan% | Insert% | Distribution | Notes |
|--------|-------|-------|---------|--------------|-------|
| `f` | 50 | 50 | 0 | zipfian | mixed read+scan — **used for all 13 experiments** |
| `a` | 100 | 0 | 0 | zipfian | read-only, skewed |
| `b` | 100 | 0 | 0 | zipfian | read-only, skewed |
| `c` | 100 | 0 | 0 | zipfian | read-only, skewed |
| `d` | 100 | 0 | 0 | latest | read recent keys |
| `e` | 0 | 100 | 0 | zipfian | scan-only |
| `g` | 100 | 0 | 0 | uniform | read-only, uniform |

---

## 11. Troubleshooting

**`connect to monitor error` on compute**
Monitor is not running yet. Always start monitor before compute.

**`ib device wasn't found`**
Wrong NIC index. Run `ibstat` to find available devices, use `--nic_index=0`.

**`Address already in use` on port 9898**
```bash
# On 10.30.1.9
killall -9 monitor compute 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
sleep 2
```

**Workload shows 0 records / stale binary cache**
```bash
rm -f workload/data/*__bin_*
```

**Disk full on 10.30.1.9**
```bash
rm -rf build/CMakeFiles build/src
df -h .
```

**`git pull` blocked by local changes**
```bash
git stash && git pull && git stash drop
```

**Tree stats not appearing (`=== ART Tree Stats ===` missing)**
Check that `ENABLE_ACCESS_TRACKING` is defined (not commented out) in both:
- `src/prheart/art-node.cc` line 24
- `src/main/compute.cc` (added after the `#include "race/race.h"` block)

Then rebuild.
