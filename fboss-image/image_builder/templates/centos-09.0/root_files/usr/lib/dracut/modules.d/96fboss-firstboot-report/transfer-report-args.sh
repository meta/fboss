#!/bin/bash

fboss_report_transfer_args() {
    local event_url asset_id boot_options
    event_url=$(getarg fboss.distro.report.url= || true)
    asset_id=$(getarg fboss.distro.report.asset_id= || true)

    if [[ -z "$event_url" && -z "$asset_id" ]]; then
        return 0
    fi
    if [[ ! "$asset_id" =~ ^[0-9]+$ ||
          ! "$event_url" =~ ^https://[^/?#]+([/?#].*)?$ ||
          "$asset_id$event_url" == *[[:space:][:cntrl:]]* ||
          "$asset_id$event_url" == *\"* || "$asset_id$event_url" == *\\* ]]; then
        printf 'Invalid first-boot reporting arguments: both a numeric asset ID and a safe HTTPS URL are required\n' >&2
        return 1
    fi

    boot_options=${FBOSS_REPORT_BOOTOPTIONS:-/config.bootoptions}
    [[ -f "$boot_options" ]] || return 1
    printf ' fboss.distro.report.asset_id=%s fboss.distro.report.url=%s\n' \
        "$asset_id" "$event_url" >> "$boot_options"
}

fboss_report_transfer_args || warn 'Failed to transfer optional first-boot reporting arguments; continuing installation'
