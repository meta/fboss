#!/bin/bash
# Trace a transceiver's programming path through qsfp_service and wedge_agent logs.
#
# Usage:
#   bash trace_transceiver.sh <log_dir> <transceiver_id> [time_filter]
#
# Arguments:
#   log_dir        - Directory containing fetched logs (from fetch_logs.sh)
#   transceiver_id - Numeric transceiver ID (port eth1/N/x → transceiver ID = N-1)
#   time_filter    - Optional: "HH:MM" or "Mon DD HH:MM" to filter to a specific window
#
# Output:
#   1. Warmboot events (agent + qsfp_service) with PID tracking
#   2. State machine transitions for the transceiver
#   3. Programming actions (Speed matches vs Trying to set application code)
#   4. IPHY programming check (skip vs applied)
#   5. Link state changes for ports on this transceiver
#   6. PHY fault status from snapshots

set -uo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: bash trace_transceiver.sh <log_dir> <transceiver_id> [time_filter]"
    echo ""
    echo "Examples:"
    echo "  bash trace_transceiver.sh /tmp/fboss-logs/switch-a/local 5"
    echo "  bash trace_transceiver.sh /tmp/fboss-logs/switch-a/local 5 '10:27'"
    echo "  bash trace_transceiver.sh /tmp/fboss-logs/switch-a/local 5 'Mar 30 10:27'"
    exit 1
fi

LOG_DIR="$1"
TCVR_ID="$2"
TIME_FILTER="${3:-}"

# Derive port prefix: transceiver N → eth1/(N+1)/
PORT_NUM=$((TCVR_ID + 1))
PORT_PREFIX="eth1/${PORT_NUM}/"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Helper: apply optional time filter to grep output
filter_time() {
    if [[ -n "$TIME_FILTER" ]]; then
        grep -F "$TIME_FILTER" || true
    else
        cat
    fi
}

# Helper: cat all non-gzipped log files matching a pattern
cat_logs() {
    local pattern="$1"
    for f in "$LOG_DIR"/${pattern}; do
        [[ -f "$f" && ! "$f" =~ \.gz$ ]] || continue
        cat "$f"
    done
}

echo "============================================"
echo "  TRANSCEIVER ${TCVR_ID} TRACE"
echo "  Port prefix: ${PORT_PREFIX}*"
echo "  Log dir: ${LOG_DIR}"
[[ -n "$TIME_FILTER" ]] && echo "  Time filter: ${TIME_FILTER}"
echo "============================================"
echo ""

# --- 1. Warmboot Events ---
echo "--- WARMBOOT EVENTS (agent + qsfp_service) ---"
{
    cat_logs "wedge_agent.log*" | grep -F "Will attempt" | grep "HwSwitchWarmBootHelper" || true
    cat_logs "qsfp_service.log*" | grep -F "Will attempt" | grep "TransceiverManager" || true
} | filter_time | sort -t' ' -k3,3 | while IFS= read -r line; do
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
    pid=$(echo "$line" | grep -oP '\[\K[0-9]+(?=\])' | head -1)
    svc=$(echo "$line" | grep -oP '(fboss_sw_agent|fboss_hw_agent|qsfp_service|wedge_agent)')
    boot_type=$(echo "$line" | grep -oP '(WARM|COLD)')
    echo "  ${ts}  ${svc:-unknown}[${pid}]  ${boot_type}_BOOT"
done
echo ""

# --- 2. State Machine Transitions ---
echo "--- STATE MACHINE: TransceiverID(${TCVR_ID}) ---"
cat_logs "qsfp_service.log*" | grep "TransceiverID(${TCVR_ID})" | \
    grep -E "Successfully applied|Failed to apply" | \
    filter_time | sort -t' ' -k3,3 | while IFS= read -r line; do
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
    pid=$(echo "$line" | grep -oP 'qsfp_service\[\K[0-9]+(?=\])' || echo "?")
    if echo "$line" | grep -q "Successfully"; then
        transition=$(echo "$line" | grep -oP 'Event:\K[^ \]]+')
        states=$(echo "$line" | grep -oP 'from \K.*')
        echo "  ${ts}  [${pid}]  OK    ${transition}  ${states}"
    else
        event=$(echo "$line" | grep -oP 'Event:\K[^ \]]+')
        echo "  ${ts}  [${pid}]  FAIL  ${event}"
    fi
done
echo ""

# --- 3. Programming Actions ---
echo "--- PROGRAMMING: Transceiver ${TCVR_ID} ---"
cat_logs "qsfp_service.log*" | grep -E "Transceiver:${TCVR_ID}[^0-9]|${PORT_PREFIX}" | \
    grep -iE "Speed matches|Trying to set application code|configureModule|Power override|Rx Equalizer|DATA_PATH_DEINIT|starting datapath|resetting data path" | \
    grep -vE "LINK_SNAPSHOT" | \
    filter_time | sort -t' ' -k3,3 | while IFS= read -r line; do
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
    pid=$(echo "$line" | grep -oP 'qsfp_service\[\K[0-9]+(?=\])' || echo "?")
    # Extract the key message
    msg=$(echo "$line" | grep -oP '(Speed matches.*|Trying to set application code.*|configureModule.*|Power override.*|Rx Equalizer.*|DATA_PATH_DEINIT.*|starting datapath.*|resetting data path.*)' | head -1)
    echo "  ${ts}  [${pid}]  ${msg}"
done
echo ""

# --- 4. IPHY Programming Check ---
echo "--- IPHY PROGRAMMING: Transceiver ${TCVR_ID} ---"
cat_logs "wedge_agent.log*" | grep "programInternalPhyPorts" | \
    grep -E "Transceiver:${TCVR_ID}[^0-9]|Transceiver=${TCVR_ID}[^0-9]" | \
    filter_time | sort -t' ' -k3,3 | while IFS= read -r line; do
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
    if echo "$line" | grep -q "Skip"; then
        echo "  ${ts}  SKIPPED (matches current SwitchState)"
    else
        echo "  ${ts}  APPLIED (IPHY reprogrammed)"
    fi
done
echo ""

# --- 5. Link State Changes ---
echo "--- LINK STATE: ${PORT_PREFIX}* ---"
cat_logs "wedge_agent.log*" | grep "LinkState:" | grep "${PORT_PREFIX}" | \
    filter_time | sort -t' ' -k3,3 | while IFS= read -r line; do
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
    port=$(echo "$line" | grep -oP 'Port \K[^ ]+')
    direction=$(echo "$line" | grep -oP '(Up|Down)$')
    echo "  ${ts}  ${port}  ${direction}"
done
echo ""

# --- 6. PHY Fault Status from Snapshots ---
echo "--- PHY FAULT STATUS (from wedge_agent_snapshots) ---"
cat_logs "wedge_agent_snapshots.log*" | grep "${PORT_PREFIX}" | \
    grep -oP '"linkFaultStatus":\{[^}]+\}' | head -1 > /dev/null 2>&1 && {
    cat_logs "wedge_agent_snapshots.log*" | grep "${PORT_PREFIX}" | \
        filter_time | while IFS= read -r line; do
        ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2} [0-9]+ [0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1/')
        local_fault=$(echo "$line" | grep -oP '"localFault":(true|false)' | head -1 || echo "n/a")
        remote_fault=$(echo "$line" | grep -oP '"remoteFault":(true|false)' | head -1 || echo "n/a")
        sig_detect=$(echo "$line" | grep -oP '"signalDetectLive":(true|false)' | head -1 || echo "n/a")
        cdr_lock=$(echo "$line" | grep -oP '"cdrLockLive":(true|false)' | head -1 || echo "n/a")
        echo "  ${ts}  ${local_fault}  ${remote_fault}  signalDetect=${sig_detect}  cdrLock=${cdr_lock}"
    done | head -20
} || echo "  No PHY snapshot data found for ${PORT_PREFIX}*"
echo ""

echo "============================================"
echo "  TRACE COMPLETE"
echo "============================================"
