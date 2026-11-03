#!/bin/bash
# Fetch FBOSS service logs from local and (optionally) remote switches for link diagnosis.
#
# Usage:
#   bash fetch_logs.sh <local_hostname> <remote_hostname> <start_time> <end_time> [output_dir]
#
# Arguments:
#   local_hostname  - Local switch hostname (e.g., switch-a.example.net)
#   remote_hostname - Remote side switch hostname (e.g., switch-b.example.net)
#   start_time      - Start time (any format accepted by `date -d`, e.g.,
#                     "2026-03-30 14:00", "yesterday 6am", "2 hours ago")
#   end_time        - End time (same format as start_time, or "now")
#   output_dir      - Optional. Defaults to /tmp/fboss-logs/<local_hostname>
#
# For each of wedge_agent.log, wedge_agent_snapshots.log, qsfp_service.log,
# and qsfp_service_snapshots.log, the script picks the archive(s) whose data
# range intersects [start_time, end_time], plus the current log when the
# window extends past the most recent rollover. Selection mirrors
# fboss/lib/link_snapshots/facebook/snapshot_lib.py
# (FbossFileBasedSSHClient.filter_by_timestamp + EOS current-log gate).

set -euo pipefail

if [[ $# -lt 4 ]]; then
    echo "Usage: bash fetch_logs.sh <local_hostname> <remote_hostname> <start_time> <end_time> [output_dir]"
    echo ""
    echo "Examples:"
    echo "  bash fetch_logs.sh switch-a.example.net switch-b.example.net '2026-03-30 00:00' '2026-03-30 23:59'"
    echo "  bash fetch_logs.sh switch-a.example.net switch-b.example.net '3 hours ago' 'now' /tmp/my-logs"
    exit 1
fi

LOCAL_HOSTNAME="$1"
REMOTE_HOSTNAME="$2"
START_TIME="$3"
END_TIME="$4"
OUTPUT_BASE="${5:-/tmp/fboss-logs/${LOCAL_HOSTNAME}}"

# Convert times to epoch seconds
START_EPOCH=$(date -d "$START_TIME" +%s 2>/dev/null) || {
    echo "ERROR: Cannot parse start_time '$START_TIME'"
    exit 1
}
END_EPOCH=$(date -d "$END_TIME" +%s 2>/dev/null) || {
    echo "ERROR: Cannot parse end_time '$END_TIME'"
    exit 1
}

# Environment-specific settings. facebook/env.sh is present only in internal
# checkouts; without it the generic defaults below apply.
_SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
[[ -f "$_SKILL_DIR/facebook/env.sh" ]] && source "$_SKILL_DIR/facebook/env.sh"

SSH_CMD="${FBOSS_SSH:-ssh}"
SCP_CMD="${FBOSS_SCP:-scp}"
LOG_BASE="${FBOSS_LOG_BASE:-/var/log/fboss}"
# Directory of a git repo on the switch holding pushed config. Empty disables
# the config-change step.
CONFIG_REPO="${FBOSS_CONFIG_REPO:-}"
ARCHIVE_DIR="$LOG_BASE/archive"
LOG_PREFIXES=(wedge_agent.log wedge_agent_snapshots.log qsfp_service.log qsfp_service_snapshots.log)

# Parse a YYYYMMDD-HHMMSS / YYYYMMDDHHMM / YYYYMMDD timestamp out of an archive
# filename and echo the UTC epoch. Echoes nothing if no recognized timestamp.
parse_archive_epoch() {
    local filename="$1"
    if [[ "$filename" =~ ([0-9]{8})-([0-9]{6}) ]]; then
        date -u -d "${BASH_REMATCH[1]} ${BASH_REMATCH[2]:0:2}:${BASH_REMATCH[2]:2:2}:${BASH_REMATCH[2]:4:2} UTC" +%s 2>/dev/null
    elif [[ "$filename" =~ ([0-9]{8})([0-9]{4}) ]]; then
        date -u -d "${BASH_REMATCH[1]} ${BASH_REMATCH[2]:0:2}:${BASH_REMATCH[2]:2:2}:00 UTC" +%s 2>/dev/null
    elif [[ "$filename" =~ ([0-9]{8}) ]]; then
        date -u -d "${BASH_REMATCH[1]} UTC" +%s 2>/dev/null
    fi
}

# fetch_logs_from_host <hostname> <output_dir>
fetch_logs_from_host() {
    local host="$1"
    local outdir="$2"

    echo "=== Fetching logs from $host ==="
    echo "  Output dir: $outdir"
    echo ""
    mkdir -p "$outdir"

    # Per-prefix selection follows fboss/lib/link_snapshots/facebook/snapshot_lib.py
    # `FbossFileBasedSSHClient.filter_by_timestamp` and the EOS current-log gate:
    #   - Archive filename timestamps mark the ROLLOVER time (= end of the data
    #     range inside that archive). `wedge_agent.log-202604200004.gz` was
    #     rolled at 2026-04-20 00:04 UTC and contains the prior ~24h.
    #   - For each prefix, sort archives by epoch and pick:
    #       * every archive whose epoch is in [START, END]
    #       * the first archive whose epoch is > END (it begins before END so
    #         its data straddles the upper boundary)
    #       * the last archive whose epoch is < START (defensive — covers off-
    #         cycle rolls and clock skew between switch and devserver)
    #   - The current log spans [latest_archive_epoch, now]. Fetch it only when
    #     END >= latest_archive_epoch (i.e., the window extends past the most
    #     recent rollover); otherwise the current log starts after END and is
    #     entirely outside the window.

    for prefix in "${LOG_PREFIXES[@]}"; do
        echo "--- ${prefix} ---"
        local archive_list
        archive_list=$($SSH_CMD \
            "$host" "ls -1 ${ARCHIVE_DIR}/${prefix}-*.gz 2>/dev/null" 2>/dev/null) || {
            echo "    WARN: Could not list ${prefix} archives"
            continue
        }

        # Pair each archive with its parsed epoch, then sort by epoch.
        local pairs=()
        local unparsed=()
        if [[ -n "$archive_list" ]]; then
            for filepath in $archive_list; do
                local fname epoch
                fname=$(basename "$filepath")
                epoch=$(parse_archive_epoch "$fname")
                if [[ -z "$epoch" ]]; then
                    unparsed+=("$filepath")
                else
                    pairs+=("${epoch}|${filepath}")
                fi
            done
        fi
        local sorted_pairs=()
        if [[ ${#pairs[@]} -gt 0 ]]; then
            mapfile -t sorted_pairs < <(printf '%s\n' "${pairs[@]}" | sort -n)
        fi

        # The latest archive in the directory (used for the current-log gate
        # below) is the one with the greatest epoch — independent of the loop
        # below, which may break early once it sees an archive past END_EPOCH.
        local latest_epoch=""
        if [[ ${#sorted_pairs[@]} -gt 0 ]]; then
            latest_epoch="${sorted_pairs[-1]%%|*}"
        fi

        # Walk sorted archives; collect the snapshot_lib.py selection set.
        local to_fetch=()
        local last_before_start=""
        local p epoch filepath
        for p in "${sorted_pairs[@]}"; do
            epoch="${p%%|*}"
            filepath="${p#*|}"
            if [[ $epoch -lt $START_EPOCH ]]; then
                last_before_start="$filepath"
            else
                to_fetch+=("$filepath")
                if [[ $epoch -ge $END_EPOCH ]]; then
                    break
                fi
            fi
        done
        if [[ -n "$last_before_start" ]]; then
            to_fetch=("$last_before_start" "${to_fetch[@]}")
        fi

        # Current-log gate: include only if the window reaches past the latest
        # archive's rollover. With no archives at all, default to including it.
        local include_current="false"
        if [[ -z "$latest_epoch" || $END_EPOCH -ge $latest_epoch ]]; then
            include_current="true"
        fi

        # Unparsed archives — fetch to be safe rather than skip silently.
        for filepath in "${unparsed[@]}"; do
            to_fetch+=("$filepath")
        done

        local fetched=0
        local skipped=$(( ${#sorted_pairs[@]} - ${#to_fetch[@]} + ${#unparsed[@]} ))
        for filepath in "${to_fetch[@]}"; do
            local fname
            fname=$(basename "$filepath")
            $SCP_CMD \
                "${host}:${filepath}" \
                "${outdir}/${fname}" 2>/dev/null && fetched=$((fetched+1)) || \
                echo "    WARN: Failed to fetch ${fname}"
        done

        if [[ "$include_current" == "true" ]]; then
            echo "    Fetching current ${prefix} (window extends past latest rollover)"
            $SCP_CMD \
                "${host}:${LOG_BASE}/${prefix}" \
                "${outdir}/${prefix}" 2>/dev/null && fetched=$((fetched+1)) || \
                echo "    WARN: Could not fetch current ${prefix}"
        else
            echo "    Skipping current ${prefix} (starts after window end)"
        fi

        echo "    Fetched: $fetched, Skipped (out of range): $skipped"
    done

    # Decompress
    echo ""
    echo "--- Decompressing archived logs ---"
    for gz in "${outdir}"/*.gz; do
        [[ -f "$gz" ]] || continue
        gunzip -fk "$gz" 2>/dev/null && echo "  Decompressed: $(basename "$gz")" || \
            echo "  WARN: Could not decompress $(basename "$gz")"
    done

    # Fetch config changes, if this environment keeps pushed config in a git
    # repo on the switch. Skipped when FBOSS_CONFIG_REPO is unset.
    echo ""
    if [[ -z "$CONFIG_REPO" ]]; then
        echo "--- Skipping config changes (FBOSS_CONFIG_REPO not set) ---"
    else
        echo "--- Fetching config changes from ${CONFIG_REPO} ---"
        local config_file="${outdir}/config_changes.log"
        local start_fmt
        start_fmt=$(date -d "@$START_EPOCH" '+%Y-%m-%d %H:%M:%S' 2>/dev/null)
        local end_fmt
        end_fmt=$(date -d "@$END_EPOCH" '+%Y-%m-%d %H:%M:%S' 2>/dev/null)
        $SSH_CMD "$host" \
            "cd ${CONFIG_REPO} && git log --since '$start_fmt' --until '$end_fmt' -p" \
            > "$config_file" 2>/dev/null && {
            local lines
            lines=$(wc -l < "$config_file")
            if [[ $lines -gt 0 ]]; then
                echo "  OK: $lines lines of config changes"
            else
                echo "  No config changes in time range"
            fi
        } || echo "  WARN: Could not fetch config changes from ${CONFIG_REPO}"
    fi

    echo ""
    echo "Files:"
    ls -lhS "$outdir" 2>/dev/null
    echo ""
}

echo "=== FBOSS Log Fetcher ==="
echo "Local host:  $LOCAL_HOSTNAME"
echo "Remote host: $REMOTE_HOSTNAME"
echo "Start time:  $(date -d "@$START_EPOCH" '+%Y-%m-%d %H:%M:%S %Z') (epoch: $START_EPOCH)"
echo "End time:    $(date -d "@$END_EPOCH" '+%Y-%m-%d %H:%M:%S %Z') (epoch: $END_EPOCH)"
echo "Output base: $OUTPUT_BASE"
echo ""

# Fetch local side
fetch_logs_from_host "$LOCAL_HOSTNAME" "${OUTPUT_BASE}/local"

# Fetch remote side
fetch_logs_from_host "$REMOTE_HOSTNAME" "${OUTPUT_BASE}/remote"

echo "=== Done ==="
echo "Local logs:  ${OUTPUT_BASE}/local"
echo "Remote logs: ${OUTPUT_BASE}/remote"
