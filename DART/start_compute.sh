#!/usr/bin/env bash
# =============================================================================
# start_compute.sh  —  Run on COMPUTE node (10.30.1.6)
#
# USAGE (on 10.30.1.6):
#   chmod +x start_compute.sh
#   ./start_compute.sh
#
# Use this for manual single-experiment testing.
# For automated policy sweeps, use run_experiments.sh instead.
# Order: start_memory.sh first → start_monitor.sh → start_compute.sh
# =============================================================================

MONITOR_IP="10.30.1.6"
NIC_INDEX=0
IB_PORT=1
NUMA_TOTAL=2
NUMA_GROUP=0

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Starting bin/compute → connecting to monitor at ${MONITOR_IP}:9898"

bin/compute \
    --monitor_addr="${MONITOR_IP}:9898" \
    --nic_index="${NIC_INDEX}" \
    --ib_port="${IB_PORT}" \
    --numa_node_total_num="${NUMA_TOTAL}" \
    --numa_node_group="${NUMA_GROUP}"
