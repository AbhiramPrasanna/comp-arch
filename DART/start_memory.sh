#!/usr/bin/env bash
# =============================================================================
# start_memory.sh  —  Run on MEMORY node (10.30.1.9)
#
# USAGE (on 10.30.1.9):
#   chmod +x start_memory.sh
#   ./start_memory.sh
#
# Keeps restarting bin/memory after each experiment automatically.
# Leave this terminal open the entire time experiments are running.
# =============================================================================

MONITOR_IP="10.30.1.6"
NIC_INDEX=0
IB_PORT=1

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date '+%H:%M:%S')] Setting hugepages..."
sudo sysctl -w vm.nr_hugepages=1024

echo "[$(date '+%H:%M:%S')] Memory node ready — will connect to monitor at ${MONITOR_IP}:9898"
echo "[$(date '+%H:%M:%S')] Now run run_experiments.sh on ${MONITOR_IP}"
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
