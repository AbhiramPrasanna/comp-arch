#!/usr/bin/env bash
# =============================================================================
# start_monitor.sh  —  Run on MONITOR node (10.30.1.6)
#
# USAGE (on 10.30.1.6):
#   chmod +x start_monitor.sh
#   ./start_monitor.sh [workload]   (default: f)
#
# Use this for manual single-experiment testing.
# For automated policy sweeps, use run_experiments.sh instead.
# Order: start_memory.sh first → start_monitor.sh → start_compute.sh
# =============================================================================

WORKLOAD="${1:-f}"          # default workload f (50% read + 50% scan)
WORKLOAD_PREFIX="./workload/data/"
MEMORY_NUM=1
COMPUTE_NUM=1
LOAD_THREADS=56
RUN_THREADS=56
CORO_NUM=1
MEM_MB=1024
TH_MB=10
BUCKET=256
RUN_MAX_REQUEST=100000

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Starting bin/monitor for workload=${WORKLOAD}"
echo "[$(date '+%H:%M:%S')] Waiting for memory (10.30.1.9) and compute (10.30.1.6) to connect..."

bin/monitor \
    --test_func=0 \
    --memory_num="${MEMORY_NUM}" \
    --compute_num="${COMPUTE_NUM}" \
    --load_thread_num="${LOAD_THREADS}" \
    --run_thread_num="${RUN_THREADS}" \
    --coro_num="${CORO_NUM}" \
    --mem_mb="${MEM_MB}" \
    --th_mb="${TH_MB}" \
    --workload_prefix="${WORKLOAD_PREFIX}" \
    --workload_load="${WORKLOAD}_load" \
    --workload_run="${WORKLOAD}_run" \
    --bucket="${BUCKET}" \
    --run_max_request="${RUN_MAX_REQUEST}"
