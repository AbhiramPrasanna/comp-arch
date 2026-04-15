#!/usr/bin/env bash
# =============================================================================
# run_memory.sh  —  Run on MEMORY node (10.30.1.6)
#
# USAGE:
#   ./run_memory.sh
#
# Start this FIRST before run_monitor.sh and run_compute.sh.
# It loops and restarts bin/memory automatically after each experiment.
# =============================================================================

MONITOR_IP="10.30.1.9"
NIC_INDEX=0
IB_PORT=1

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Setting hugepages..."
sudo sysctl -w vm.nr_hugepages=1024

echo "[$(date '+%H:%M:%S')] Memory ready. Will connect to monitor at ${MONITOR_IP}:9898"
echo "[$(date '+%H:%M:%S')] Now start run_monitor.sh and run_compute.sh on ${MONITOR_IP} (10.30.1.9)"
echo ""

while true; do
    echo "[$(date '+%H:%M:%S')] Starting bin/memory..."
    bin/memory \
        --monitor_addr="${MONITOR_IP}:9898" \
        --nic_index="${NIC_INDEX}" \
        --ib_port="${IB_PORT}"
    echo "[$(date '+%H:%M:%S')] Memory exited. Restarting in 3s..."
    sleep 3
done
