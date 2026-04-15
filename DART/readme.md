# DART — Disaggregated ART Index with Criticality-Aware Caching

## Cluster Setup

| Server | IP | Role |
|--------|----|------|
| cs-dis-srv09s | **10.30.1.9** | Memory node — runs `bin/memory` |
| cs-dis-srv06s | **10.30.1.6** | Compute + Monitor node — runs `bin/compute` and `bin/monitor` |

All commands below must be run from `~/comp-arch/DART` on the respective server.

---

## 1. First-Time Setup (both servers)

```bash
# On BOTH 10.30.1.9 and 10.30.1.6
cd ~
git clone https://github.com/AbhiramPrasanna/comp-arch.git
cd comp-arch/DART

# Clone missing submodules
ls gflags/CMakeLists.txt     2>/dev/null || git clone https://github.com/gflags/gflags.git gflags
ls magic_enum/CMakeLists.txt 2>/dev/null || git clone https://github.com/Neargye/magic_enum.git magic_enum

chmod +x build.sh run_memory.sh run_compute.sh run_monitor.sh
./build.sh

# Verify binaries
ls -lh bin/memory bin/compute bin/monitor
```

---

## 2. Workload Generation (on 10.30.1.6 — compute node only)

Workload files live in `workload/data/` on the compute node. Only needs to be done once.

```bash
# On 10.30.1.6
cd ~/comp-arch/DART

# Download YCSB 0.17.0 (streaming — avoids disk waste)
mkdir -p workload/ycsb
wget -qO- https://github.com/brianfrankcooper/YCSB/releases/download/0.17.0/ycsb-0.17.0.tar.gz \
    | tar -xz -C workload/ycsb --strip-components=1

# Build classpath
CP=$(find workload/ycsb -name "*.jar" | tr '\n' ':')
mkdir -p workload/data

# Generate workload f (50% read + 50% scan, zipfian, 100K ops)
java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
    -P workload_spec/f -s -load > workload/data/f_load
java -cp "$CP" site.ycsb.Client -db site.ycsb.BasicDB \
    -P workload_spec/f -s -t   > workload/data/f_run

# Verify (should each show ~100000 lines)
echo "f_load: $(wc -l < workload/data/f_load) lines"
echo "f_run:  $(wc -l < workload/data/f_run) lines"
```

To generate other workloads replace `f` with `a b c d e g` in the commands above.

---

## 3. Changing the Caching Policy

Edit `src/main/compute.cc` on **10.30.1.6**. Find lines 51–54 and uncomment **exactly one** policy:

```cpp
// #define POLICY_STATIC        // original DART: structural DFS (no tracking needed)
// #define POLICY_HOTNESS       // top-K nodes by raw RDMA read count
#define POLICY_CRITICALITY      // top-K nodes by depth_sum — RTT savings (recommended)
// #define POLICY_HYBRID        // 0.5 * hotness + 0.5 * criticality
```

Change the skip-table budget on **line 58**:

```cpp
static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;  // options: 500, 1000, 5000
```

After every edit, rebuild on **10.30.1.6**:

```bash
# On 10.30.1.6
cd ~/comp-arch/DART
./build.sh
```

---

## 4. Manual Single Experiment

Run binaries in this exact order: **memory → monitor → compute**

### Kill leftover processes first

```bash
# On 10.30.1.9
killall -9 memory 2>/dev/null || true

# On 10.30.1.6
killall -9 monitor compute 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
```

---

### Step 1 — Start memory on 10.30.1.9

Open a terminal on **10.30.1.9**:

```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024

bin/memory \
  --monitor_addr=10.30.1.6:9898 \
  --nic_index=0 \
  --ib_port=1
```

Leave this running. Memory connects to monitor at `10.30.1.6:9898` and waits.

---

### Step 2 — Start monitor on 10.30.1.6

Open a terminal on **10.30.1.6** and start monitor **before** compute:

```bash
cd ~/comp-arch/DART

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

Monitor blocks and listens on port 9898. It waits for both memory and compute to connect.

---

### Step 3 — Start compute on 10.30.1.6

Open another terminal on **10.30.1.6**:

```bash
cd ~/comp-arch/DART

bin/compute \
  --monitor_addr=10.30.1.6:9898 \
  --nic_index=0 \
  --ib_port=1 \
  --numa_node_total_num=2 \
  --numa_node_group=0
```

Once compute connects, the experiment starts automatically. Results appear in the compute terminal when it finishes.

---

### Step 4 — Read the results

In the compute terminal you will see per-thread and aggregate output:

```
throughput: 0.396568 MOps     ← million ops/sec        (higher is better)
latency: 141.212 us           ← average latency         (lower is better)
avg rtt count: 4.50           ← RDMA round-trips/op     (lower is better)
num shortcuts: 128            ← skip-table entries built
```

---

## 5. Running All Four Policies Manually

Repeat the following for each policy: **STATIC → HOTNESS → CRITICALITY → HYBRID**

```bash
# On 10.30.1.6 — edit policy then rebuild
nano src/main/compute.cc    # uncomment desired POLICY_* line, comment the rest
./build.sh

# Kill leftovers
killall -9 monitor compute 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
```

```bash
# On 10.30.1.9 — restart memory
killall -9 memory 2>/dev/null || true
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

```bash
# On 10.30.1.6 — start monitor
bin/monitor \
  --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ \
  --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

```bash
# On 10.30.1.6 — start compute
bin/compute \
  --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

## 6. All 13 Experiments — Full Manual Commands

For every experiment the steps are the same:
1. Set policy + budget in `compute.cc` via `sed` (on 10.30.1.6)
2. Rebuild (on 10.30.1.6)
3. Kill leftover processes (both servers)
4. Start memory (10.30.1.9)
5. Start monitor (10.30.1.6) — **before** compute
6. Start compute (10.30.1.6)

---

### Helper — kill everything before each experiment

```bash
# On 10.30.1.9
killall -9 memory 2>/dev/null || true

# On 10.30.1.6
killall -9 monitor compute 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
sleep 2
```

---

### Experiment 1 — POLICY_STATIC, budget=5000, th_mb=10

**On 10.30.1.6 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_STATIC$|#define POLICY_STATIC|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 2 — POLICY_HOTNESS, budget=5000, th_mb=10

**On 10.30.1.6 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_HOTNESS$|#define POLICY_HOTNESS|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 3 — POLICY_CRITICALITY, budget=5000, th_mb=10

**On 10.30.1.6 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_CRITICALITY$|#define POLICY_CRITICALITY|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 4 — POLICY_HYBRID, budget=5000, th_mb=10

**On 10.30.1.6 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_HYBRID$|#define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

> Experiments 5–13 all use **POLICY_CRITICALITY**. Only `budget` and `--th_mb` change.
> The build only needs to be redone when the budget changes.

---

### Experiment 5 — POLICY_CRITICALITY, budget=500, th_mb=2

**On 10.30.1.6 — set policy and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|;s|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|;s|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|;s|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|' src/main/compute.cc
sed -i 's|^// #define POLICY_CRITICALITY$|#define POLICY_CRITICALITY|' src/main/compute.cc
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 6 — POLICY_CRITICALITY, budget=1000, th_mb=2

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 7 — POLICY_CRITICALITY, budget=5000, th_mb=2

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=2 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 8 — POLICY_CRITICALITY, budget=500, th_mb=10

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 9 — POLICY_CRITICALITY, budget=1000, th_mb=10

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 10 — POLICY_CRITICALITY, budget=5000, th_mb=10

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=10 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 11 — POLICY_CRITICALITY, budget=500, th_mb=50

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 500|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 12 — POLICY_CRITICALITY, budget=1000, th_mb=50

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 1000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

### Experiment 13 — POLICY_CRITICALITY, budget=5000, th_mb=50

**On 10.30.1.6 — set budget and build:**
```bash
cd ~/comp-arch/DART
sed -i 's|POLICY_MAX_ENTRIES = [0-9]*|POLICY_MAX_ENTRIES = 5000|' src/main/compute.cc
./build.sh
```

**On 10.30.1.9 — start memory:**
```bash
cd ~/comp-arch/DART
sudo sysctl -w vm.nr_hugepages=1024
bin/memory --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1
```

**On 10.30.1.6 — start monitor:**
```bash
cd ~/comp-arch/DART
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 \
  --load_thread_num=56 --run_thread_num=56 --coro_num=1 \
  --mem_mb=1024 --th_mb=50 \
  --workload_prefix=./workload/data/ --workload_load=f_load --workload_run=f_run \
  --bucket=256 --run_max_request=100000
```

**On 10.30.1.6 — start compute:**
```bash
cd ~/comp-arch/DART
bin/compute --monitor_addr=10.30.1.6:9898 --nic_index=0 --ib_port=1 \
  --numa_node_total_num=2 --numa_node_group=0
```

---

## 7. Experiment Matrix

### Policy Comparison (workload f = 50% read + 50% scan)

| Policy | Workload | Budget | th_mb | What it tests |
|--------|----------|--------|-------|---------------|
| POLICY_STATIC | f | 5000 | 10 | Baseline — structural skip entries |
| POLICY_HOTNESS | f | 5000 | 10 | Frequency-based caching |
| POLICY_CRITICALITY | f | 5000 | 10 | RTT-savings-based caching |
| POLICY_HYBRID | f | 5000 | 10 | Blend of hotness + criticality |

### Cache Pressure Sweep (POLICY_CRITICALITY, workload f)

| Budget | th_mb | What it tests |
|--------|-------|---------------|
| 500 | 2 | Small budget, small cache |
| 1000 | 2 | Medium budget, small cache |
| 5000 | 2 | Large budget, small cache |
| 500 | 10 | Small budget, medium cache |
| 1000 | 10 | Medium budget, medium cache |
| 5000 | 10 | Large budget, medium cache |
| 500 | 50 | Small budget, large cache |
| 1000 | 50 | Medium budget, large cache |
| 5000 | 50 | Large budget, large cache |

---

## 8. Key Parameters Reference

| Parameter | Location | Default | Description |
|-----------|----------|---------|-------------|
| `POLICY_*` | `src/main/compute.cc:51` | CRITICALITY | Active caching policy |
| `POLICY_MAX_ENTRIES` | `src/main/compute.cc:58` | 5000 | Max skip-table entries |
| `--mem_mb` | monitor flag | 1024 | Remote memory pool in MiB |
| `--th_mb` | monitor flag | 10 | Per-thread local RDMA cache in MiB |
| `--run_max_request` | monitor flag | 100000 | Operations in run phase |
| `--load_thread_num` | monitor flag | 56 | Threads for loading data |
| `--run_thread_num` | monitor flag | 56 | Threads for benchmark run |
| `--bucket` | monitor flag | 256 | RACE hash table bucket size |
| `--nic_index` | memory/compute flag | 0 | InfiniBand NIC index (`ibp59s0`) |
| `--ib_port` | memory/compute flag | 1 | InfiniBand port number |

---

## 9. Workload Reference

| Letter | Read% | Scan% | Distribution | Notes |
|--------|-------|-------|--------------|-------|
| `f` | 50 | 50 | zipfian | mixed read+scan — **used for all experiments** |
| `c` | 100 | 0 | zipfian | read-heavy, skewed |
| `g` | 100 | 0 | uniform | read-heavy, uniform |
| `d` | 100 | 0 | latest | read recent keys |
| `e` | 0 | 100 | zipfian | scan-only |
| `a` | 100 | 0 | zipfian | same as c |
| `b` | 100 | 0 | zipfian | same as c |

---

## 10. Troubleshooting

**`connect to monitor error`**
Monitor is not running yet. Always start monitor before compute.

**`ib device wasn't found`**
Wrong NIC index. Always use `--nic_index=0`.

**`Address already in use` on port 9898**
```bash
killall -9 monitor compute memory 2>/dev/null || true
sudo fuser -k 9898/tcp 2>/dev/null || true
sleep 2
```

**Workload shows 0 records**
Stale binary cache files. Delete them:
```bash
rm -f workload/data/*__bin_*
```

**Disk full on 10.30.1.6**
Free space by removing build intermediates:
```bash
rm -rf build/CMakeFiles build/src
rm -f workload/data/*  # remove old workload files before regenerating
```

**`git pull` blocked by local changes**
```bash
git stash && git pull && git stash drop
```

**Compute keeps failing to connect after an experiment**
`run_compute.sh` retries every 15s while monitor is rebuilding. This is normal — wait for `run_monitor.sh` to finish the build and start the next monitor.
