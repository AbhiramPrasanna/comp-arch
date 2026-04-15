#!/usr/bin/env bash
# =============================================================================
# run_experiments.sh  —  DART Phase 3 automated experiment runner
#
# Iterates over every policy × workload × cache-size combination, rebuilds the
# binary between each policy change, and writes all output to a timestamped
# results directory.
#
# USAGE
#   ./run_experiments.sh [--dry-run]
#
#   --dry-run  Print each experiment config without actually running anything.
#
# OUTPUT
#   results/<timestamp>/
#     summary.txt                      ← one-line metrics for every run
#     <exp-name>.txt                   ← full compute+monitor log per run
# =============================================================================
set -uo pipefail

# -----------------------------------------------------------------------------
# CLUSTER CONFIGURATION  ←  edit this block for your machines
# -----------------------------------------------------------------------------

MONITOR_IP="10.30.1.9"           # IP where bin/monitor runs (this machine)

# Memory nodes: space-separated IPs
MEMORY_IPS=("10.30.1.6")

# Compute nodes: space-separated IPs  (can include MONITOR_IP for co-located)
COMPUTE_IPS=("10.30.1.9")

SSH_USER="shiroko"                   # SSH user on all remote nodes
DART_REMOTE_DIR="~/DART"            # DART root on every remote node

MEMORY_NIC=1                         # NIC index on memory nodes
COMPUTE_NIC=0                        # NIC index on compute nodes
IB_PORT=1
NUMA_TOTAL=2                         # --numa_node_total_num
NUMA_GROUP=0                         # --numa_node_group

MEM_MB=8192                          # Remote memory pool (MiB) on memory node
LOAD_THREADS=56                      # Threads used during load phase
RUN_THREADS=56                       # Threads used during run phase
CORO_NUM=1
BUCKET=256
RUN_MAX_REQUEST=1000000

# Path to workload files on the compute node.
# If monitor and compute are the same machine: use ./workload/data/
# If you pre-split with split_and_send_workload.py: use ./workload/split/
WORKLOAD_PREFIX="./workload/data/"

# How long (seconds) to wait between starting memory and starting compute.
# Increase if memory node takes longer to register its hugepages.
SLEEP_AFTER_MEMORY=3

# -----------------------------------------------------------------------------
# LOCAL PATHS  (derived — do not edit)
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPUTE_CC="$SCRIPT_DIR/src/main/compute.cc"
ART_NODE_CC="$SCRIPT_DIR/src/prheart/art-node.cc"

TIMESTAMP="$(date '+%Y%m%d_%H%M%S')"
RESULTS_DIR="$SCRIPT_DIR/results/$TIMESTAMP"
SUMMARY_FILE="$RESULTS_DIR/summary.txt"

MEMORY_NUM=${#MEMORY_IPS[@]}
COMPUTE_NUM=${#COMPUTE_IPS[@]}

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

# -----------------------------------------------------------------------------
# EXPERIMENT MATRIX
#
# Format of each row:   "POLICY   workload   max_entries   th_mb"
#
#   POLICY       — one of POLICY_STATIC / POLICY_HOTNESS /
#                         POLICY_CRITICALITY / POLICY_HYBRID
#   workload     — one of a b c d e f g  (must have {x}_load and {x}_run)
#   max_entries  — POLICY_MAX_ENTRIES  (skip table budget)
#   th_mb        — per-thread local RDMA MR size in MiB (compute cache size)
# -----------------------------------------------------------------------------

# --- Section 1: policy comparison  (fixed budget=5000, th_mb=10) ------------
POLICY_SWEEP=(
    "POLICY_STATIC      c  5000  10"
    "POLICY_HOTNESS     c  5000  10"
    "POLICY_CRITICALITY c  5000  10"
    "POLICY_HYBRID      c  5000  10"
    "POLICY_STATIC      g  5000  10"
    "POLICY_CRITICALITY g  5000  10"
    "POLICY_CRITICALITY e  5000  10"
    "POLICY_CRITICALITY d  5000  10"
)

# --- Section 2: cache pressure sweep  (CRITICALITY, workload c) -------------
CACHE_SWEEP=(
    "POLICY_CRITICALITY c   500   2"
    "POLICY_CRITICALITY c  1000   2"
    "POLICY_CRITICALITY c  5000   2"
    "POLICY_CRITICALITY c   500  10"
    "POLICY_CRITICALITY c  1000  10"
    "POLICY_CRITICALITY c  5000  10"
    "POLICY_CRITICALITY c   500  50"
    "POLICY_CRITICALITY c  1000  50"
    "POLICY_CRITICALITY c  5000  50"
)

# --- Section 3: workload distribution comparison  (best policy from above) --
DIST_SWEEP=(
    "POLICY_CRITICALITY a  5000  10"
    "POLICY_CRITICALITY b  5000  10"
    "POLICY_CRITICALITY f  5000  10"
)

ALL_EXPERIMENTS=("${POLICY_SWEEP[@]}" "${CACHE_SWEEP[@]}" "${DIST_SWEEP[@]}")
TOTAL_EXPERIMENTS=${#ALL_EXPERIMENTS[@]}

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
log()  { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$SUMMARY_FILE"; }
info() { echo "    $*" | tee -a "$SUMMARY_FILE"; }
die()  { echo "FATAL: $*" >&2; exit 1; }

# Check whether a node is the local machine (skip SSH for local commands)
is_local() {
    local ip="$1"
    [[ "$ip" == "127.0.0.1" || "$ip" == "localhost" || "$ip" == "$MONITOR_IP" ]]
}

run_remote() {
    local ip="$1"; shift
    if is_local "$ip"; then
        bash -c "$*"
    else
        ssh -o ConnectTimeout=10 -o BatchMode=yes "${SSH_USER}@${ip}" "$*"
    fi
}

run_remote_bg() {
    local ip="$1"
    local logfile="$2"
    shift 2
    if is_local "$ip"; then
        bash -c "$*" >> "$logfile" 2>&1 &
    else
        # Run in background on remote, redirect to remote log; fetch log later
        ssh -f -o ConnectTimeout=10 -o BatchMode=yes "${SSH_USER}@${ip}" \
            "mkdir -p $(dirname ${DART_REMOTE_DIR}/logs/x) 2>/dev/null; $* > ${logfile} 2>&1"
    fi
}

kill_all_dart() {
    [[ $DRY_RUN -eq 1 ]] && return
    log "Killing any leftover DART processes on all nodes..."
    for ip in "${MEMORY_IPS[@]}" "${COMPUTE_IPS[@]}"; do
        run_remote "$ip" "killall -9 compute memory monitor 2>/dev/null; true" 2>/dev/null || true
    done
    killall -9 monitor compute memory 2>/dev/null || true
    sleep 2
}

# -----------------------------------------------------------------------------
# POLICY / BUILD MANAGEMENT
# -----------------------------------------------------------------------------

# Toggle ENABLE_ACCESS_TRACKING in art-node.cc
set_access_tracking() {
    local enable="$1"   # "on" or "off"
    if [[ "$enable" == "on" ]]; then
        # Ensure the line is uncommented
        sed -i 's|^// #define ENABLE_ACCESS_TRACKING$|#define ENABLE_ACCESS_TRACKING|' "$ART_NODE_CC"
    else
        # Comment it out
        sed -i 's|^#define ENABLE_ACCESS_TRACKING$|// #define ENABLE_ACCESS_TRACKING|' "$ART_NODE_CC"
    fi
}

# Switch the active policy #define in compute.cc and set POLICY_MAX_ENTRIES
set_policy() {
    local policy="$1"       # e.g. POLICY_CRITICALITY
    local max_entries="$2"

    # Step 1: comment out whichever policy is currently active
    sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|'           "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|'         "$COMPUTE_CC"
    sed -i 's|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|' "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|'           "$COMPUTE_CC"

    # Step 2: uncomment the desired policy
    sed -i "s|^// #define ${policy}$|#define ${policy}|" "$COMPUTE_CC"

    # Step 3: update POLICY_MAX_ENTRIES
    sed -i "s|static constexpr uint64_t POLICY_MAX_ENTRIES = [0-9]*;|static constexpr uint64_t POLICY_MAX_ENTRIES = ${max_entries};|" "$COMPUTE_CC"

    # Step 4: ENABLE_ACCESS_TRACKING must be on for any policy-based build
    if [[ "$policy" == "POLICY_STATIC" ]]; then
        # Still leave tracking on — Phase 2 output is informative even here
        set_access_tracking on
    else
        set_access_tracking on
    fi

    log "Policy → ${policy}  max_entries=${max_entries}"
}

# Rebuild and push the new compute binary to all compute nodes
build_and_distribute() {
    [[ $DRY_RUN -eq 1 ]] && { log "[dry-run] would build and scp compute binary"; return; }
    log "Building..."
    cd "$SCRIPT_DIR"
    if ! ./build.sh 2>&1; then
        die "Build failed. Fix errors before continuing."
    fi
    log "Distributing bin/compute to compute nodes..."
    for ip in "${COMPUTE_IPS[@]}"; do
        if ! is_local "$ip"; then
            scp -q bin/compute "${SSH_USER}@${ip}:${DART_REMOTE_DIR}/bin/compute" \
                || die "scp to ${ip} failed"
        fi
    done
}

# -----------------------------------------------------------------------------
# SINGLE EXPERIMENT
# -----------------------------------------------------------------------------
run_experiment() {
    local policy="$1"
    local workload="$2"
    local max_entries="$3"
    local th_mb="$4"

    local exp_name="${policy}_wl${workload}_ent${max_entries}_thmb${th_mb}"
    local exp_log="$RESULTS_DIR/${exp_name}.txt"

    log "------------------------------------------------------------"
    log "RUN: ${exp_name}"
    log "------------------------------------------------------------"

    if [[ $DRY_RUN -eq 1 ]]; then
        info "[dry-run] policy=${policy} workload=${workload} max_entries=${max_entries} th_mb=${th_mb}"
        return
    fi

    kill_all_dart

    # --- Print experiment header to the per-experiment log ---
    {
        echo "================================================================"
        echo "Experiment : ${exp_name}"
        echo "Policy     : ${policy}"
        echo "Workload   : ${workload}"
        echo "MaxEntries : ${max_entries}"
        echo "th_mb      : ${th_mb} MiB"
        echo "Date       : $(date)"
        echo "================================================================"
    } > "$exp_log"

    # --- Start memory node(s) ---
    log "Starting ${MEMORY_NUM} memory node(s)..."
    for ip in "${MEMORY_IPS[@]}"; do
        run_remote_bg "$ip" "${DART_REMOTE_DIR}/logs/memory_${exp_name}.txt" \
            "cd ${DART_REMOTE_DIR} && \
             sudo sysctl -w vm.nr_hugepages=8192 >/dev/null 2>&1; \
             bin/memory \
               --monitor_addr=${MONITOR_IP}:9898 \
               --nic_index=${MEMORY_NIC} \
               --ib_port=${IB_PORT}"
    done

    sleep "$SLEEP_AFTER_MEMORY"

    # --- Start compute node(s) ---
    log "Starting ${COMPUTE_NUM} compute node(s)..."
    for ip in "${COMPUTE_IPS[@]}"; do
        run_remote_bg "$ip" "${DART_REMOTE_DIR}/logs/compute_${exp_name}.txt" \
            "cd ${DART_REMOTE_DIR} && \
             bin/compute \
               --monitor_addr=${MONITOR_IP}:9898 \
               --nic_index=${COMPUTE_NIC} \
               --ib_port=${IB_PORT} \
               --numa_node_total_num=${NUMA_TOTAL} \
               --numa_node_group=${NUMA_GROUP}"
    done

    # --- Run monitor (blocking — exits when experiment finishes) ---
    log "Monitor: workload=${workload}  th_mb=${th_mb}..."
    {
        echo ""
        echo "--- MONITOR OUTPUT ---"
    } >> "$exp_log"

    bin/monitor \
        --test_func=0 \
        --memory_num="${MEMORY_NUM}" \
        --compute_num="${COMPUTE_NUM}" \
        --load_thread_num="${LOAD_THREADS}" \
        --run_thread_num="${RUN_THREADS}" \
        --coro_num="${CORO_NUM}" \
        --mem_mb="${MEM_MB}" \
        --th_mb="${th_mb}" \
        --workload_prefix="${WORKLOAD_PREFIX}" \
        --workload_load="${workload}_load" \
        --workload_run="${workload}_run" \
        --bucket="${BUCKET}" \
        --run_max_request="${RUN_MAX_REQUEST}" \
    2>&1 | tee -a "$exp_log"

    # --- Fetch compute node logs ---
    {
        echo ""
        echo "--- COMPUTE NODE OUTPUT ---"
    } >> "$exp_log"

    for ip in "${COMPUTE_IPS[@]}"; do
        local remote_log="${DART_REMOTE_DIR}/logs/compute_${exp_name}.txt"
        if is_local "$ip"; then
            cat "${DART_REMOTE_DIR}/logs/compute_${exp_name}.txt" >> "$exp_log" 2>/dev/null || true
        else
            ssh -o ConnectTimeout=10 "${SSH_USER}@${ip}" "cat ${remote_log}" \
                >> "$exp_log" 2>/dev/null || true
        fi
    done

    kill_all_dart

    # --- Extract metrics and append one-line summary ---
    local throughput latency rtt shortcuts

    throughput="$(grep -oP 'throughput: \K[\d.]+(?= MOps)' "$exp_log" \
                  | tail -1)"
    latency="$(grep -oP 'latency: \K[\d.]+(?= us)' "$exp_log" \
               | tail -1)"
    rtt="$(grep -oP 'avg rtt count: \K[\d.]+' "$exp_log" \
           | tail -1)"
    shortcuts="$(grep -oP '(?<=num shortcuts:)\s*\K[0-9]+' "$exp_log" \
                 | tail -1)"

    throughput="${throughput:-N/A}"
    latency="${latency:-N/A}"
    rtt="${rtt:-N/A}"
    shortcuts="${shortcuts:-N/A}"

    local summary_line
    printf -v summary_line \
        "%-55s  throughput=%8s MOps  latency=%8s us  rtt=%6s  shortcuts=%6s" \
        "$exp_name" "$throughput" "$latency" "$rtt" "$shortcuts"

    echo "$summary_line" | tee -a "$SUMMARY_FILE"
    log "Full log: ${exp_log}"

    sleep 2
}

# -----------------------------------------------------------------------------
# MAIN
# -----------------------------------------------------------------------------
mkdir -p "$RESULTS_DIR"

{
    echo "DART Phase 3 — Full Experiment Results"
    echo "Date        : $(date)"
    echo "Monitor     : ${MONITOR_IP}"
    echo "Memory nodes: ${MEMORY_IPS[*]}"
    echo "Compute nodes: ${COMPUTE_IPS[*]}"
    echo "Threads     : load=${LOAD_THREADS}  run=${RUN_THREADS}"
    echo "Remote mem  : ${MEM_MB} MiB"
    echo "Workloads   : c(read/zipf) g(read/uniform) d(read/latest) e(scan/zipf)"
    echo "             a(read/zipf) b(read/zipf) f(read+scan/zipf)"
    echo "Policies    : STATIC  HOTNESS  CRITICALITY  HYBRID"
    echo ""
    echo "Columns: throughput(MOps/s)  latency(us)  avg-rtt/op  skip-table-entries"
    echo "=========================================================================="
} > "$SUMMARY_FILE"

# Ensure log directories exist on remote nodes
if [[ $DRY_RUN -eq 0 ]]; then
    for ip in "${MEMORY_IPS[@]}" "${COMPUTE_IPS[@]}"; do
        run_remote "$ip" "mkdir -p ${DART_REMOTE_DIR}/logs" 2>/dev/null || true
    done
fi

# Remember the last policy that was built so we only rebuild when policy changes
LAST_BUILT_POLICY=""
LAST_BUILT_ENTRIES=""
CURRENT=0

for exp in "${ALL_EXPERIMENTS[@]}"; do
    read -r policy workload max_entries th_mb <<< "$exp"
    CURRENT=$(( CURRENT + 1 ))
    log "Progress: ${CURRENT}/${TOTAL_EXPERIMENTS}"

    # Only rebuild when the policy or max_entries actually changes
    if [[ "$policy" != "$LAST_BUILT_POLICY" || "$max_entries" != "$LAST_BUILT_ENTRIES" ]]; then
        set_policy "$policy" "$max_entries"
        build_and_distribute
        LAST_BUILT_POLICY="$policy"
        LAST_BUILT_ENTRIES="$max_entries"
    fi

    run_experiment "$policy" "$workload" "$max_entries" "$th_mb"
done

{
    echo ""
    echo "=========================================================================="
    echo "All ${TOTAL_EXPERIMENTS} experiments complete."
    echo "Results directory: ${RESULTS_DIR}"
    echo "Run date: $(date)"
} | tee -a "$SUMMARY_FILE"

echo ""
echo "Summary: $SUMMARY_FILE"
echo "Logs:    $RESULTS_DIR/"
