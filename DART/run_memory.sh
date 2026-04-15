#!/usr/bin/env bash
# =============================================================================
# run_memory.sh  —  Run on the MEMORY node (10.30.1.9)
#
# USAGE (on 10.30.1.9):
#   ./run_memory.sh
#
# Automatically restarts bin/memory after each experiment so it is always
# ready for the next one without manual intervention.
# =============================================================================

MONITOR_IP="10.30.1.6"   # IP of the compute/monitor machine
NIC_INDEX=0
IB_PORT=1

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Setting hugepages..."
sudo sysctl -w vm.nr_hugepages=1024

echo "[$(date '+%H:%M:%S')] Memory node ready. Connecting to monitor at ${MONITOR_IP}:9898"
echo "[$(date '+%H:%M:%S')] Leave this running. Start run_experiments.sh on ${MONITOR_IP}."
echo ""

while true; do
    echo "[$(date '+%H:%M:%S')] Starting bin/memory..."
    bin/memory \
        --monitor_addr="${MONITOR_IP}:9898" \
        --nic_index="${NIC_INDEX}" \
        --ib_port="${IB_PORT}"
    echo "[$(date '+%H:%M:%S')] Memory exited (experiment done). Restarting in 3s..."
    sleep 3
done
