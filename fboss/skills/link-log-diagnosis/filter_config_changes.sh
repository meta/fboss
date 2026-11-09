#!/bin/bash
# Filter switch config patches to show only hardware-affecting changes.
#
# Config patches are typically 10-100MB+ and dominated by routing policy
# changes (community sets, route policies, local-pref rules). This script
# extracts only the sections that can affect link state:
#
#   - TX/RX SerDes settings (pre, post, main, pre2, post2, post3, pre3) in platform_mapping
#   - Port speed, FEC, lane, interface, and transceiver configuration
#   - Drain/undrain operations
#   - Agent config version changes (to correlate with triggerAgentConfigChangeEvent)
#
# Usage:
#   bash filter_config_changes.sh <config_changes.log> [port_filter]
#
# Arguments:
#   config_changes.log - Path to the raw config_changes.log from fetch_logs.sh
#   port_filter        - Optional: port name (e.g., "eth1/6") to filter to specific port
#
# Output:
#   1. Commit timeline (all config pushes with timestamps)
#   2. Agent config changes — the actual config the agent reads
#   3. Platform mapping TX/RX changes (config input only — usually NOT consumed
#      at runtime)
#   4. Port/interface/speed/FEC/lane changes
#   5. Drain operations
#   6. Agent config version changes
#   7. Files changed
#
# Sections 2, 3 and 6 need paths inside the config repo, which vary by
# deployment. Set FBOSS_AGENT_CONFIG_PATH, FBOSS_PLATFORM_MAPPING_PATH and
# FBOSS_CONFIG_VERSION_PATH to enable them; they are skipped when unset.
# Sections 1, 4, 5 and 7 match on FBOSS config fields and always run.

set -uo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: bash filter_config_changes.sh <config_changes.log> [port_filter]"
    echo ""
    echo "Examples:"
    echo "  bash filter_config_changes.sh /tmp/fboss-logs/switch-a/local/config_changes.log"
    echo "  bash filter_config_changes.sh /tmp/fboss-logs/switch-a/local/config_changes.log eth1/6"
    exit 1
fi

CONFIG_LOG="$1"
PORT_FILTER="${2:-}"

# Paths inside the config repo. These depend on how the deployment lays out
# pushed config, so they are supplied by the environment. facebook/env.sh sets
# them for Meta; sections that need an unset path are skipped.
_SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
[[ -f "$_SKILL_DIR/facebook/env.sh" ]] && source "$_SKILL_DIR/facebook/env.sh"

AGENT_CONFIG_PATH="${FBOSS_AGENT_CONFIG_PATH:-}"
PLATFORM_MAPPING_PATH="${FBOSS_PLATFORM_MAPPING_PATH:-}"
CONFIG_VERSION_PATH="${FBOSS_CONFIG_VERSION_PATH:-}"
# Directory holding pipeline metadata rather than applied config. Excluded from
# the changed-files summary when set.
METADATA_DIR="${FBOSS_METADATA_DIR:-}"

if [[ ! -f "$CONFIG_LOG" ]]; then
    echo "ERROR: File not found: $CONFIG_LOG"
    exit 1
fi

FILE_SIZE=$(stat -c%s "$CONFIG_LOG" 2>/dev/null || stat -f%z "$CONFIG_LOG" 2>/dev/null)
echo "============================================"
echo "  CONFIG CHANGE ANALYSIS"
echo "  File: $CONFIG_LOG ($(numfmt --to=iec "$FILE_SIZE" 2>/dev/null || echo "${FILE_SIZE} bytes"))"
[[ -n "$PORT_FILTER" ]] && echo "  Port filter: $PORT_FILTER"
echo "============================================"
echo ""

# --- 1. Commit Timeline ---
echo "--- CONFIG PUSH TIMELINE ---"
grep -E "^(commit |Date:)" "$CONFIG_LOG" | paste - - | while IFS=$'\t' read -r commit_line date_line; do
    hash=$(echo "$commit_line" | awk '{print substr($2,1,12)}')
    date=${date_line/Date:  /}
    # Get commit message (next non-blank line after Date)
    echo "  $date  $hash"
done
echo ""
# Also show commit messages
echo "--- COMMIT MESSAGES ---"
grep -A4 "^commit " "$CONFIG_LOG" | grep -E "^    " | sed 's/^    /  /' | head -30
echo ""

# --- 2. Agent Config Changes ---
if [[ -z "$AGENT_CONFIG_PATH" ]]; then
    echo "--- AGENT CONFIG (skipped: FBOSS_AGENT_CONFIG_PATH not set) ---"
    HAS_AGENT=0
else
echo "--- AGENT CONFIG (${AGENT_CONFIG_PATH}) ---"
HAS_AGENT=$(grep -c "diff --git a/${AGENT_CONFIG_PATH} " "$CONFIG_LOG" 2>/dev/null || true)
if [[ "$HAS_AGENT" -gt 0 ]]; then
    echo "  *** ${AGENT_CONFIG_PATH} CHANGED — check for port speed/profile/state diffs ***"
    grep -A 200 "diff --git a/${AGENT_CONFIG_PATH} " "$CONFIG_LOG" | \
        grep -E "^[+-].*\"(speed|profileID|portSpeed|state|fecMode)\"" | \
        head -20
else
    echo "  ${AGENT_CONFIG_PATH} unchanged (port config identical across pushes)"
fi
fi
echo ""

# --- 3. Platform Mapping TX/RX Changes ---
if [[ -z "$PLATFORM_MAPPING_PATH" ]]; then
    echo "--- PLATFORM MAPPING (skipped: FBOSS_PLATFORM_MAPPING_PATH not set) ---"
    HAS_PM=0
else
echo "--- PLATFORM MAPPING: TX/RX SerDes CHANGES (config input only) ---"
echo "  NOTE: This is a config-pipeline input, NOT the running agent config."
echo "  Agent typically uses compiled-in platform mapping."
echo "  Only relevant if --platform_mapping_override_path is set."
HAS_PM=$(grep -c "diff --git a/${PLATFORM_MAPPING_PATH}" "$CONFIG_LOG" 2>/dev/null || true)
if [[ "$HAS_PM" -gt 0 ]]; then
    # Count changed lanes
    CHANGED_MAIN=$(grep -c "^-.*\"main\":" "$CONFIG_LOG" 2>/dev/null || true)

    if [[ "$CHANGED_MAIN" -gt 0 ]]; then
        echo "  TX SerDes settings changed across $CHANGED_MAIN lanes (in config input)"
        echo ""
        echo "  Sample changes (first lanes):"
        grep -B5 -A1 "^[+-].*\"\\(main\\|pre\\|pre2\\|pre3\\|post\\|post2\\|post3\\)\":" "$CONFIG_LOG" | \
            grep -E "\"(main|pre|pre2|pre3|post|post2|post3|lane)\":|^[+-]" | \
            grep -v "^--$" | \
            head -50
        echo ""
        echo "  ... ($CHANGED_MAIN total lanes changed)"
    else
        echo "  Platform mapping changed but no TX SerDes value changes"
    fi
else
    echo "  No platform_mapping changes found"
fi
fi
echo ""

# --- 4. Port/Interface/Speed/FEC/Lane Changes ---
echo "--- PORT/INTERFACE CONFIG CHANGES ---"
grep -n "^[+-]" "$CONFIG_LOG" | \
    grep -iE "\"speed\"|\"fecMode\"|\"portSpeed\"|\"iPhyLinkFaultStatus\"|\"medium\"|\"interfaceType\"|\"portProfileID\"|profileConfig|portProfileConfig" | \
    grep -v "^[+-]{3}" | \
    if [[ -n "$PORT_FILTER" ]]; then grep -i "$PORT_FILTER" || true; else cat; fi | \
    head -30
if ! grep -q "^[+-].*\"speed\"" "$CONFIG_LOG" 2>/dev/null; then
    echo "  No port speed/FEC/interface changes found"
fi
echo ""

# --- 5. Drain Operations ---
echo "--- DRAIN OPERATIONS ---"
grep -i "drain" "$CONFIG_LOG" | grep -E "^    |forcePickup|thrift" | head -10
if ! grep -qi "drain" "$CONFIG_LOG" 2>/dev/null; then
    echo "  No drain operations found"
fi
echo ""

# --- 6. Agent Config Version Changes ---
if [[ -z "$CONFIG_VERSION_PATH" ]]; then
    echo "--- AGENT CONFIG VERSION (skipped: FBOSS_CONFIG_VERSION_PATH not set) ---"
else
    echo "--- AGENT CONFIG VERSION (${CONFIG_VERSION_PATH}) ---"
    grep -A2 "diff --git a/${CONFIG_VERSION_PATH}" "$CONFIG_LOG" | head -5
    grep "^[+-].*current_version\|^[+-].*desired_version\|^[+-].*current.*cfld:" "$CONFIG_LOG" | head -10
fi
echo ""

# --- 7. Files Changed ---
echo "--- FILES CHANGED ---"
if [[ -n "$METADATA_DIR" ]]; then
    grep "^diff --git" "$CONFIG_LOG" | grep -v "$METADATA_DIR" | sed 's|diff --git a/||;s| b/.*||' | sort -u | head -20
else
    grep "^diff --git" "$CONFIG_LOG" | sed 's|diff --git a/||;s| b/.*||' | sort -u | head -20
fi
echo ""

echo "============================================"
echo "  FILTER COMPLETE"
echo "============================================"
