#!/usr/bin/env bash
# =============================================================================
# run_experiments.sh  —  DART Phase 3 automated experiment runner
#
# Runs on the COMPUTE/MONITOR node (10.30.1.6).
# Memory node (10.30.1.9) must already be running start_memory.sh.
#
# USAGE
#   ./run_experiments.sh [--dry-run]
#
# OUTPUT
#   results/<timestamp>/
#     summary.txt          ← one-line metrics for every run
#     <exp-name>.txt       ← full monitor+compute log per run
# =============================================================================
set -uo pipefail

# -----------------------------------------------------------------------------
# CONFIGURATION
# -----------------------------------------------------------------------------
MONITOR_IP="10.30.1.6"          # this machine
MEMORY_NUM=1                     # number of memory nodes
COMPUTE_NUM=1                    # number of compute nodes

NIC_INDEX=0
IB_PORT=1
NUMA_TOTAL=2
NUMA_GROUP=0

MEM_MB=1024
LOAD_THREADS=56
RUN_THREADS=56
CORO_NUM=1
BUCKET=256
RUN_MAX_REQUEST=100000

WORKLOAD_PREFIX="./workload/data/"

# Seconds to wait after starting compute before starting monitor
SLEEP_AFTER_COMPUTE=3

# Seconds to wait at start of each experiment for memory node to be ready
# (start_memory.sh restarts memory automatically — it needs ~3s after exit)
SLEEP_FOR_MEMORY=5

# -----------------------------------------------------------------------------
# LOCAL PATHS
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPUTE_CC="$SCRIPT_DIR/src/main/compute.cc"
ART_NODE_CC="$SCRIPT_DIR/src/prheart/art-node.cc"
LOGS_DIR="$SCRIPT_DIR/logs"

TIMESTAMP="$(date '+%Y%m%d_%H%M%S')"
RESULTS_DIR="$SCRIPT_DIR/results/$TIMESTAMP"
SUMMARY_FILE="$RESULTS_DIR/summary.txt"

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

# -----------------------------------------------------------------------------
# EXPERIMENT MATRIX
#
# Format: "POLICY  workload  max_entries  th_mb"
# -----------------------------------------------------------------------------

# Section 1: policy comparison (fixed budget=5000, th_mb=10)
POLICY_SWEEP=(
    "POLICY_STATIC      f  5000  10"
    "POLICY_HOTNESS     f  5000  10"
    "POLICY_CRITICALITY f  5000  10"
    "POLICY_HYBRID      f  5000  10"
)

# Section 2: cache pressure sweep (CRITICALITY, workload f)
CACHE_SWEEP=(
    "POLICY_CRITICALITY f   500   2"
    "POLICY_CRITICALITY f  1000   2"
    "POLICY_CRITICALITY f  5000   2"
    "POLICY_CRITICALITY f   500  10"
    "POLICY_CRITICALITY f  1000  10"
    "POLICY_CRITICALITY f  5000  10"
    "POLICY_CRITICALITY f   500  50"
    "POLICY_CRITICALITY f  1000  50"
    "POLICY_CRITICALITY f  5000  50"
)

ALL_EXPERIMENTS=("${POLICY_SWEEP[@]}" "${CACHE_SWEEP[@]}")
TOTAL_EXPERIMENTS=${#ALL_EXPERIMENTS[@]}

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
log()  { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$SUMMARY_FILE"; }
info() { echo "    $*" | tee -a "$SUMMARY_FILE"; }
die()  { echo "FATAL: $*" >&2; exit 1; }

kill_local_dart() {
    [[ $DRY_RUN -eq 1 ]] && return
    log "Killing local compute/monitor..."
    killall -9 compute monitor 2>/dev/null || true
    sleep 2
}

# -----------------------------------------------------------------------------
# POLICY / BUILD MANAGEMENT
# -----------------------------------------------------------------------------

set_access_tracking() {
    local enable="$1"
    if [[ "$enable" == "on" ]]; then
        sed -i 's|^// #define ENABLE_ACCESS_TRACKING$|#define ENABLE_ACCESS_TRACKING|' "$ART_NODE_CC"
    else
        sed -i 's|^#define ENABLE_ACCESS_TRACKING$|// #define ENABLE_ACCESS_TRACKING|' "$ART_NODE_CC"
    fi
}

set_policy() {
    local policy="$1"
    local max_entries="$2"

    sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|'           "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|'         "$COMPUTE_CC"
    sed -i 's|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|' "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|'           "$COMPUTE_CC"

    sed -i "s|^// #define ${policy}$|#define ${policy}|" "$COMPUTE_CC"
    sed -i "s|static constexpr uint64_t POLICY_MAX_ENTRIES = [0-9]*;|static constexpr uint64_t POLICY_MAX_ENTRIES = ${max_entries};|" "$COMPUTE_CC"

    set_access_tracking on
    log "Policy → ${policy}  max_entries=${max_entries}"
}

build_binary() {
    [[ $DRY_RUN -eq 1 ]] && { log "[dry-run] would build"; return; }
    log "Building..."
    cd "$SCRIPT_DIR"
    ./build.sh 2>&1 || die "Build failed."
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
    local compute_log="$LOGS_DIR/compute_${exp_name}.txt"

    log "------------------------------------------------------------"
    log "RUN: ${exp_name}"
    log "------------------------------------------------------------"

    if [[ $DRY_RUN -eq 1 ]]; then
        info "[dry-run] policy=${policy} workload=${workload} max_entries=${max_entries} th_mb=${th_mb}"
        return
    fi

    kill_local_dart

    # Wait for start_memory.sh to restart memory on 10.30.1.9
    log "Waiting ${SLEEP_FOR_MEMORY}s for memory node to be ready..."
    sleep "$SLEEP_FOR_MEMORY"

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

    # --- Start compute (local, background) ---
    log "Starting bin/compute..."
    mkdir -p "$LOGS_DIR"
    bin/compute \
        --monitor_addr="${MONITOR_IP}:9898" \
        --nic_index="${NIC_INDEX}" \
        --ib_port="${IB_PORT}" \
        --numa_node_total_num="${NUMA_TOTAL}" \
        --numa_node_group="${NUMA_GROUP}" \
        > "$compute_log" 2>&1 &
    COMPUTE_PID=$!

    sleep "$SLEEP_AFTER_COMPUTE"

    # --- Run monitor (blocking — exits when experiment finishes) ---
    log "Starting bin/monitor: workload=${workload} th_mb=${th_mb}..."
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

    # --- Append compute log ---
    {
        echo ""
        echo "--- COMPUTE OUTPUT ---"
        cat "$compute_log" 2>/dev/null || echo "(no compute log)"
    } >> "$exp_log"

    kill_local_dart

    # --- Extract metrics ---
    local throughput latency rtt shortcuts

    throughput="$(grep -oP 'throughput: \K[\d.]+(?= MOps)' "$exp_log" | tail -1)"
    latency="$(grep -oP 'latency: \K[\d.]+(?= us)' "$exp_log" | tail -1)"
    rtt="$(grep -oP 'avg rtt count: \K[\d.]+' "$exp_log" | tail -1)"
    shortcuts="$(grep -oP '(?<=num shortcuts:)\s*\K[0-9]+' "$exp_log" | tail -1)"

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
mkdir -p "$RESULTS_DIR" "$LOGS_DIR"

{
    echo "DART Phase 3 — Experiment Results"
    echo "Date        : $(date)"
    echo "Monitor/Compute : ${MONITOR_IP}"
    echo "Memory node     : 10.30.1.9 (managed by start_memory.sh)"
    echo "Threads     : load=${LOAD_THREADS}  run=${RUN_THREADS}"
    echo "Mem pool    : ${MEM_MB} MiB"
    echo "Max requests: ${RUN_MAX_REQUEST}"
    echo ""
    echo "Columns: throughput(MOps/s)  latency(us)  avg-rtt/op  skip-table-entries"
    echo "=========================================================================="
} > "$SUMMARY_FILE"

LAST_BUILT_POLICY=""
LAST_BUILT_ENTRIES=""
CURRENT=0

for exp in "${ALL_EXPERIMENTS[@]}"; do
    read -r policy workload max_entries th_mb <<< "$exp"
    CURRENT=$(( CURRENT + 1 ))
    log "Progress: ${CURRENT}/${TOTAL_EXPERIMENTS}"

    if [[ "$policy" != "$LAST_BUILT_POLICY" || "$max_entries" != "$LAST_BUILT_ENTRIES" ]]; then
        set_policy "$policy" "$max_entries"
        build_binary
        LAST_BUILT_POLICY="$policy"
        LAST_BUILT_ENTRIES="$max_entries"
    fi

    run_experiment "$policy" "$workload" "$max_entries" "$th_mb"
done

{
    echo ""
    echo "=========================================================================="
    echo "All ${TOTAL_EXPERIMENTS} experiments complete."
    echo "Results: ${RESULTS_DIR}"
    echo "Date: $(date)"
} | tee -a "$SUMMARY_FILE"

echo ""
echo "Summary: $SUMMARY_FILE"
echo "Logs:    $RESULTS_DIR/"
