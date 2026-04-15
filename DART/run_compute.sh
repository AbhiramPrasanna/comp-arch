#!/usr/bin/env bash
# =============================================================================
# run_compute.sh  —  Run on COMPUTE node (10.30.1.6)
#
# USAGE:
#   ./run_compute.sh
#
# Start this AFTER run_memory.sh but BEFORE or alongside run_monitor.sh.
# It loops and restarts bin/compute automatically after each experiment.
# The binary is rebuilt by run_monitor.sh between policy changes — this
# script always picks up the latest bin/compute on each restart.
# =============================================================================

MONITOR_IP="10.30.1.6"
NIC_INDEX=0
IB_PORT=1
NUMA_TOTAL=2
NUMA_GROUP=0

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Compute ready. Will connect to monitor at ${MONITOR_IP}:9898"
echo ""

while true; do
    echo "[$(date '+%H:%M:%S')] Starting bin/compute..."
    START=$SECONDS
    bin/compute \
        --monitor_addr="${MONITOR_IP}:9898" \
        --nic_index="${NIC_INDEX}" \
        --ib_port="${IB_PORT}" \
        --numa_node_total_num="${NUMA_TOTAL}" \
        --numa_node_group="${NUMA_GROUP}"
    ELAPSED=$(( SECONDS - START ))

    if [[ $ELAPSED -lt 10 ]]; then
        # Fast exit = monitor not up yet (connection refused or build in progress)
        echo "[$(date '+%H:%M:%S')] Monitor not ready, retrying in 15s..."
        sleep 15
    else
        # Slow exit = experiment finished normally
        echo "[$(date '+%H:%M:%S')] Experiment done. Restarting in 3s..."
        sleep 3
    fi
done
