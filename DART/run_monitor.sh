#!/usr/bin/env bash
# =============================================================================
# run_monitor.sh  —  Run on MONITOR/COMPUTE node (10.30.1.6)
#
# USAGE:
#   ./run_monitor.sh [--dry-run]
#
# This is the ORCHESTRATOR. It:
#   1. Iterates through every experiment in the matrix
#   2. Rebuilds bin/compute when the policy changes
#   3. Starts bin/monitor for each experiment (blocking until done)
#   4. Saves results to results/<timestamp>/
#
# BEFORE running this:
#   - Start run_memory.sh on 10.30.1.9
#   - Start run_compute.sh in another terminal on this machine
#
# When monitor finishes each experiment, compute and memory exit automatically
# and their loop scripts restart them for the next experiment.
# =============================================================================
set -uo pipefail

# -----------------------------------------------------------------------------
# CONFIGURATION
# -----------------------------------------------------------------------------
MONITOR_IP="10.30.1.6"
MEMORY_NUM=1
COMPUTE_NUM=1

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

# Wait for compute+memory to restart between experiments
SLEEP_BETWEEN_EXPERIMENTS=8

# -----------------------------------------------------------------------------
# LOCAL PATHS
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPUTE_CC="$SCRIPT_DIR/src/main/compute.cc"
ART_NODE_CC="$SCRIPT_DIR/src/prheart/art-node.cc"

TIMESTAMP="$(date '+%Y%m%d_%H%M%S')"
RESULTS_DIR="$SCRIPT_DIR/results/$TIMESTAMP"
SUMMARY_FILE="$RESULTS_DIR/summary.txt"

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

# -----------------------------------------------------------------------------
# EXPERIMENT MATRIX
#
# Format: "POLICY  workload  max_entries  th_mb"
#   POLICY       — POLICY_STATIC / POLICY_HOTNESS / POLICY_CRITICALITY / POLICY_HYBRID
#   workload     — workload letter (must have workload/data/{x}_load and {x}_run)
#   max_entries  — skip table budget
#   th_mb        — per-thread local cache size in MiB
# -----------------------------------------------------------------------------

# Policy comparison (workload f = 50% read + 50% scan, zipfian)
POLICY_SWEEP=(
    "POLICY_STATIC      f  5000  10"
    "POLICY_HOTNESS     f  5000  10"
    "POLICY_CRITICALITY f  5000  10"
    "POLICY_HYBRID      f  5000  10"
)

# Cache pressure sweep (CRITICALITY, workload f)
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
TOTAL=${#ALL_EXPERIMENTS[@]}

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
log()  { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$SUMMARY_FILE"; }
die()  { echo "FATAL: $*" >&2; exit 1; }

set_policy() {
    local policy="$1"
    local max_entries="$2"

    # Comment out all policies
    sed -i 's|^#define POLICY_STATIC$|// #define POLICY_STATIC|'           "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HOTNESS$|// #define POLICY_HOTNESS|'         "$COMPUTE_CC"
    sed -i 's|^#define POLICY_CRITICALITY$|// #define POLICY_CRITICALITY|' "$COMPUTE_CC"
    sed -i 's|^#define POLICY_HYBRID$|// #define POLICY_HYBRID|'           "$COMPUTE_CC"

    # Uncomment the desired policy
    sed -i "s|^// #define ${policy}$|#define ${policy}|" "$COMPUTE_CC"

    # Update max entries
    sed -i "s|static constexpr uint64_t POLICY_MAX_ENTRIES = [0-9]*;|static constexpr uint64_t POLICY_MAX_ENTRIES = ${max_entries};|" "$COMPUTE_CC"

    # Enable access tracking
    sed -i 's|^// #define ENABLE_ACCESS_TRACKING$|#define ENABLE_ACCESS_TRACKING|' "$ART_NODE_CC"

    log "Policy → ${policy}  max_entries=${max_entries}"
}

build_binary() {
    [[ $DRY_RUN -eq 1 ]] && { log "[dry-run] would build"; return; }
    log "Building..."
    cd "$SCRIPT_DIR"
    ./build.sh 2>&1 || die "Build failed."
    log "Build done."
}

# -----------------------------------------------------------------------------
# MAIN
# -----------------------------------------------------------------------------
mkdir -p "$RESULTS_DIR"

{
    echo "DART Phase 3 — Experiment Results"
    echo "Date            : $(date)"
    echo "Monitor/Compute : ${MONITOR_IP}"
    echo "Memory node     : 10.30.1.9  (run_memory.sh)"
    echo "Threads         : load=${LOAD_THREADS}  run=${RUN_THREADS}"
    echo "Mem pool        : ${MEM_MB} MiB"
    echo "Max requests    : ${RUN_MAX_REQUEST}"
    echo ""
    echo "Columns: throughput(MOps/s)  latency(us)  avg-rtt/op  skip-table-entries"
    echo "=========================================================================="
} > "$SUMMARY_FILE"

LAST_POLICY=""
LAST_ENTRIES=""
CURRENT=0

for exp in "${ALL_EXPERIMENTS[@]}"; do
    read -r policy workload max_entries th_mb <<< "$exp"
    CURRENT=$(( CURRENT + 1 ))

    exp_name="${policy}_wl${workload}_ent${max_entries}_thmb${th_mb}"
    exp_log="$RESULTS_DIR/${exp_name}.txt"

    log "=========================================="
    log "Progress: ${CURRENT}/${TOTAL}  —  ${exp_name}"
    log "=========================================="

    if [[ $DRY_RUN -eq 1 ]]; then
        log "[dry-run] policy=${policy} workload=${workload} max_entries=${max_entries} th_mb=${th_mb}"
        continue
    fi

    # Rebuild only when policy or budget changes
    if [[ "$policy" != "$LAST_POLICY" || "$max_entries" != "$LAST_ENTRIES" ]]; then
        set_policy "$policy" "$max_entries"
        build_binary
        LAST_POLICY="$policy"
        LAST_ENTRIES="$max_entries"
        log "Waiting ${SLEEP_BETWEEN_EXPERIMENTS}s for compute to restart with new binary..."
        sleep "$SLEEP_BETWEEN_EXPERIMENTS"
    fi

    {
        echo "================================================================"
        echo "Experiment : ${exp_name}"
        echo "Policy     : ${policy}"
        echo "Workload   : ${workload}"
        echo "MaxEntries : ${max_entries}"
        echo "th_mb      : ${th_mb} MiB"
        echo "Date       : $(date)"
        echo "================================================================"
        echo ""
    } > "$exp_log"

    log "Starting bin/monitor  workload=${workload}  th_mb=${th_mb}..."

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

    # Extract metrics
    throughput="$(grep -oP 'throughput: \K[\d.]+(?= MOps)' "$exp_log" | tail -1)"
    latency="$(grep -oP 'latency: \K[\d.]+(?= us)' "$exp_log" | tail -1)"
    rtt="$(grep -oP 'avg rtt count: \K[\d.]+' "$exp_log" | tail -1)"
    shortcuts="$(grep -oP '(?<=num shortcuts:)\s*\K[0-9]+' "$exp_log" | tail -1)"

    printf "%-55s  throughput=%8s MOps  latency=%8s us  rtt=%6s  shortcuts=%6s\n" \
        "$exp_name" \
        "${throughput:-N/A}" "${latency:-N/A}" "${rtt:-N/A}" "${shortcuts:-N/A}" \
        | tee -a "$SUMMARY_FILE"

    log "Log: ${exp_log}"
    log "Waiting ${SLEEP_BETWEEN_EXPERIMENTS}s for compute+memory to restart..."
    sleep "$SLEEP_BETWEEN_EXPERIMENTS"

done

{
    echo ""
    echo "=========================================================================="
    echo "All ${TOTAL} experiments complete."
    echo "Results: ${RESULTS_DIR}"
    echo "Date: $(date)"
} | tee -a "$SUMMARY_FILE"

echo ""
echo "Summary : $SUMMARY_FILE"
echo "Logs    : $RESULTS_DIR/"
