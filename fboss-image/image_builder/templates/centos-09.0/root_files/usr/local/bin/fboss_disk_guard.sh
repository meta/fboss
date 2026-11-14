#!/bin/bash
# Ensure root-fs headroom before platform_manager starts (rpmdb dies below ~128KB).
set -f
MIN_FREE_KB=${MIN_FREE_KB:-102400}
BALLAST=/var/lib/fboss/.ballast
BALLAST_MB=${BALLAST_MB:-$((MIN_FREE_KB / 1024))} # ballast >= threshold so rm always recovers

free_kb() { df --output=avail -k / | tail -1 | tr -d ' '; }
log() { echo "$*" >&2; }
alert() { log "FBOSS_ALERT $1 <event:$2> <service:fboss_disk_guard> <mount:/> <free_kb:$(free_kb)> <min_free_kb:$MIN_FREE_KB>"; }

start=$(free_kb)
if [ "$start" -ge "$MIN_FREE_KB" ]; then
  if [ ! -s "$BALLAST" ] && [ "$start" -ge $((MIN_FREE_KB * 10)) ]; then
    mkdir -p "$(dirname "$BALLAST")" && fallocate -l "${BALLAST_MB}M" "$BALLAST" &&
      log "ballast ${BALLAST_MB}MB created at $BALLAST"
  fi
  exit 0
fi

alert "Root filesystem low before platform_manager start: ${start}KB free" low_disk_space
steps=(
  "journalctl --vacuum-size=16M"
  "rm -f /var/lib/systemd/coredump/*"
  "find /var/log -xdev -type f ( -name *.gz -o -name *.[0-9] -o -name *.old ) -delete"
  "rm -f $BALLAST"
)
for s in "${steps[@]}"; do
  $s >/dev/null 2>&1
  sync
  for _ in 1 2 3 4 5 6 7 8 9 10; do # btrfs frees unlinked extents asynchronously
    now=$(free_kb)
    [ "$now" -ge "$MIN_FREE_KB" ] && break
    sleep 1
  done
  if [ "$now" -ge "$MIN_FREE_KB" ]; then
    alert "Recovered ${start}KB -> ${now}KB via: $s" low_disk_space_recovered
    exit 0
  fi
done
alert "Still low after cleanup: $(free_kb)KB free" low_disk_space_unrecovered
exit 0
