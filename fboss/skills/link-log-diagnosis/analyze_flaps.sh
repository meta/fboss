#!/bin/bash
# Analyze link flaps and warmboot events from fetched FBOSS logs.
#
# Usage:
#   bash analyze_flaps.sh <log_dir> [port1 port2 ...]
#
# Arguments:
#   log_dir  - Directory containing fetched logs (from fetch_logs.sh)
#   port(s)  - Optional port name(s) to filter (e.g., eth1/6/1). If omitted, shows all ports.
#
# Output:
#   1. Merged timeline of warmboot events and link-down events
#   2. Per-flap attribution: whether each flap occurred within a window after a warmboot
#   3. Summary statistics

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: bash analyze_flaps.sh <log_dir> [port1 port2 ...]"
    exit 1
fi

LOG_DIR="$1"
shift
PORTS=("$@")

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# --- Extract warmboot events from wedge_agent and qsfp_service logs ---
# Use the GLOG timestamp (e.g., "V0330 08:41:01.019836") for precision.
# Deduplicate by taking only the HwSwitchWarmBootHelper line (one per boot).
for f in "$LOG_DIR"/wedge_agent.log*; do
    [[ -f "$f" && ! "$f" =~ \.gz$ ]] || continue
    grep -h "Will attempt" "$f" 2>/dev/null || true
# `|| true` keeps `set -euo pipefail` from aborting silently when grep finds no matches.
done | { grep "HwSwitchWarmBootHelper" || true; } | \
    sed -E 's/^.*([A-Z][a-z]{2}) ([0-9]{1,2}) ([0-9]{2}:[0-9]{2}:[0-9]{2}).*Will attempt (WARM|COLD) boot.*/\1 \2 \3 \4_BOOT/' \
    > "$TMPDIR/warmboot_raw.txt"

# Also check qsfp_service for warmboot events (TransceiverManager)
for f in "$LOG_DIR"/qsfp_service.log*; do
    [[ -f "$f" && ! "$f" =~ \.gz$ ]] || continue
    grep -h "Will attempt" "$f" 2>/dev/null || true
done | { grep "TransceiverManager" || true; } | \
    sed -E 's/^.*([A-Z][a-z]{2}) ([0-9]{1,2}) ([0-9]{2}:[0-9]{2}:[0-9]{2}).*Will attempt (WARM|COLD) boot.*/\1 \2 \3 \4_BOOT_QSFP/' \
    >> "$TMPDIR/warmboot_raw.txt"

# --- Extract LinkState events ---
# Use SwSwitch.cpp:52 lines only (avoid duplicate from :88)
for f in "$LOG_DIR"/wedge_agent.log*; do
    [[ -f "$f" && ! "$f" =~ \.gz$ ]] || continue
    grep -h "LinkState:" "$f" 2>/dev/null || true
done | { grep "SwSwitch.cpp:53" || true; } | while read -r line; do
    # Extract: timestamp, port, direction
    ts=$(echo "$line" | sed -E 's/^.*([A-Z][a-z]{2}) ([0-9]{1,2}) ([0-9]{2}:[0-9]{2}:[0-9]{2}).*/\1 \2 \3/')
    port=$(echo "$line" | grep -oP 'Port \K[^ ]+')
    direction=$(echo "$line" | grep -oP '(Up|Down)$')

    # Apply port filter if specified
    if [[ ${#PORTS[@]} -gt 0 ]]; then
        match=0
        for p in "${PORTS[@]}"; do
            if [[ "$port" == "$p" ]]; then
                match=1
                break
            fi
        done
        [[ $match -eq 1 ]] || continue
    fi

    echo "$ts LINK_${direction^^} $port"
done > "$TMPDIR/linkstate_raw.txt"

# --- Merge and sort by timestamp ---
cat "$TMPDIR/warmboot_raw.txt" "$TMPDIR/linkstate_raw.txt" | sort -k1,1M -k2,2n -k3,3 > "$TMPDIR/timeline.txt"

TOTAL_EVENTS=$(wc -l < "$TMPDIR/timeline.txt")
if [[ "$TOTAL_EVENTS" -eq 0 ]]; then
    echo "No warmboot or link-state events found."
    if [[ ${#PORTS[@]} -gt 0 ]]; then
        echo "  (filtered to ports: ${PORTS[*]})"
    fi
    exit 0
fi

# --- Print timeline ---
echo "============================================"
echo "  TIMELINE: Warmboot & Link Events"
if [[ ${#PORTS[@]} -gt 0 ]]; then
    echo "  Filtered to ports: ${PORTS[*]}"
fi
echo "============================================"
printf "%-15s  %-20s  %s\n" "TIMESTAMP" "EVENT" "DETAILS"
echo "--------------------------------------------"
while IFS= read -r line; do
    ts=$(echo "$line" | awk '{print $1, $2, $3}')
    event=$(echo "$line" | awk '{print $4}')
    details=$(echo "$line" | awk '{$1=$2=$3=$4=""; print}' | sed 's/^ *//')
    printf "%-15s  %-20s  %s\n" "$ts" "$event" "$details"
done < "$TMPDIR/timeline.txt"

# --- Attribute flaps to warmboot events ---
# A flap is "warmboot-related" if a LINK_DOWN occurs within WINDOW_SEC seconds
# after a warmboot event.
WINDOW_SEC=300  # 5-minute window

echo ""
echo "============================================"
echo "  FLAP ATTRIBUTION (window=${WINDOW_SEC}s)"
echo "============================================"

# Convert month names to numbers for date arithmetic
month_to_num() {
    case "$1" in
        Jan) echo 01;; Feb) echo 02;; Mar) echo 03;; Apr) echo 04;;
        May) echo 05;; Jun) echo 06;; Jul) echo 07;; Aug) echo 08;;
        Sep) echo 09;; Oct) echo 10;; Nov) echo 11;; Dec) echo 12;;
    esac
}

# Collect warmboot timestamps as epoch seconds
declare -a WARMBOOT_EPOCHS=()
while IFS= read -r line; do
    event=$(echo "$line" | awk '{print $4}')
    if [[ "$event" == *"BOOT"* && "$event" != *"QSFP"* ]]; then
        mon=$(echo "$line" | awk '{print $1}')
        day=$(echo "$line" | awk '{print $2}')
        time=$(echo "$line" | awk '{print $3}')
        mon_num=$(month_to_num "$mon")
        epoch=$(date -d "$(date +%Y)-${mon_num}-${day} ${time}" +%s 2>/dev/null) || continue
        WARMBOOT_EPOCHS+=("$epoch")
    fi
done < "$TMPDIR/timeline.txt"

# Analyze each LINK_DOWN event
TOTAL_DOWNS=0
WARMBOOT_RELATED=0
NOT_WARMBOOT=0

printf "%-15s  %-15s  %-10s  %s\n" "FLAP TIME" "PORT" "CAUSE" "NEAREST WARMBOOT"
echo "--------------------------------------------"

while IFS= read -r line; do
    event=$(echo "$line" | awk '{print $4}')
    [[ "$event" == "LINK_DOWN" ]] || continue

    TOTAL_DOWNS=$((TOTAL_DOWNS + 1))
    mon=$(echo "$line" | awk '{print $1}')
    day=$(echo "$line" | awk '{print $2}')
    time=$(echo "$line" | awk '{print $3}')
    port=$(echo "$line" | awk '{print $5}')
    mon_num=$(month_to_num "$mon")
    flap_epoch=$(date -d "$(date +%Y)-${mon_num}-${day} ${time}" +%s 2>/dev/null) || continue

    # Find the most recent warmboot before this flap
    best_delta=""
    best_wb_time=""
    for wb_epoch in "${WARMBOOT_EPOCHS[@]}"; do
        delta=$((flap_epoch - wb_epoch))
        if [[ $delta -ge 0 ]]; then
            if [[ -z "$best_delta" || $delta -lt $best_delta ]]; then
                best_delta=$delta
                best_wb_time=$(date -d "@$wb_epoch" "+%b %d %H:%M:%S")
            fi
        fi
    done

    if [[ -n "$best_delta" && $best_delta -le $WINDOW_SEC ]]; then
        cause="WARMBOOT"
        WARMBOOT_RELATED=$((WARMBOOT_RELATED + 1))
        printf "%-15s  %-15s  %-10s  %s (%ds after)\n" "$mon $day $time" "$port" "$cause" "$best_wb_time" "$best_delta"
    else
        cause="OTHER"
        NOT_WARMBOOT=$((NOT_WARMBOOT + 1))
        if [[ -n "$best_delta" ]]; then
            printf "%-15s  %-15s  %-10s  %s (%ds after)\n" "$mon $day $time" "$port" "$cause" "$best_wb_time" "$best_delta"
        else
            printf "%-15s  %-15s  %-10s  %s\n" "$mon $day $time" "$port" "$cause" "no prior warmboot"
        fi
    fi
done < "$TMPDIR/timeline.txt"

# --- Summary ---
echo ""
echo "============================================"
echo "  SUMMARY"
echo "============================================"
echo "Total warmboot events:      ${#WARMBOOT_EPOCHS[@]}"
echo "Total link-down events:     $TOTAL_DOWNS"
echo "Warmboot-related flaps:     $WARMBOOT_RELATED"
echo "Non-warmboot flaps:         $NOT_WARMBOOT"
if [[ $TOTAL_DOWNS -gt 0 ]]; then
    pct=$((WARMBOOT_RELATED * 100 / TOTAL_DOWNS))
    echo "Warmboot-related percentage: ${pct}%"
fi
echo ""

if [[ $WARMBOOT_RELATED -gt 0 && $WARMBOOT_RELATED -eq $TOTAL_DOWNS ]]; then
    echo "CONCLUSION: ALL link flaps appear to be warmboot-related."
    echo "  This is a SOFTWARE issue — warmboot should be hitless."
    echo "  Investigate: transceiver reprogramming during warmboot"
    echo "  (look for 'Trying to set application code' after warmboot)"
elif [[ $WARMBOOT_RELATED -gt 0 ]]; then
    echo "CONCLUSION: ${pct}% of flaps are warmboot-related (SOFTWARE)."
    echo "  The remaining flaps may have other causes."
elif [[ $TOTAL_DOWNS -gt 0 ]]; then
    echo "CONCLUSION: No flaps appear to be warmboot-related."
    echo "  Investigate other causes (hardware, remediation loops, config push)."
fi
