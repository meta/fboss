#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#
# Stage pre-built artifacts for the FBOSS distro image build
# as defined in manifests/generic.json.
#
# Each artifact is copied to /tmp/fboss-image-staging/ with the filename
# expected by the manifest's "download": "file:///tmp/fboss-image-staging/<name>" entries.
#
# Usage:
#   stage_artifacts.sh \
#     --platform <path/to/platform_fboss_bins.tar.zst> \
#     --bsp <path/to/fboss_bsp_kmods.tar> \
#     --sai <path/to/fboss_sai_kmods.tar> \
#     --agent <path/to/agent_fboss_bins.tar.zst> \
#     --fsdb <path/to/fsdb_fboss_bins.tar.zst> \
#     --qsfp <path/to/qsfp_fboss_bins.tar.zst> \
#     --fboss2 <path/to/fboss2_fboss_bins.tar.zst>

set -euo pipefail

STAGING_DIR="/tmp/fboss-image-staging"

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Stage pre-built artifacts for the FBOSS distro image build.
Artifacts are copied to ${STAGING_DIR}/ with filenames matching
the download paths in manifests/generic.json.

All options are required:
  --platform PATH   Path to platform_fboss_bins.tar.zst
  --bsp PATH        Path to fboss_bsp_kmods.tar
  --sai PATH        Path to fboss_sai_kmods.tar
  --agent PATH      Path to agent_fboss_bins.tar.zst
  --fsdb PATH       Path to fsdb_fboss_bins.tar.zst
  --qsfp PATH       Path to qsfp_fboss_bins.tar.zst
  --fboss2 PATH     Path to fboss2_fboss_bins.tar.zst
  --led PATH        Path to led_fboss_bins.tar.zst
  -h, --help        Show this help message
EOF
    exit 1
}

PLATFORM=""
BSP=""
SAI=""
AGENT=""
FSDB=""
QSFP=""
FBOSS2=""
LED=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform) PLATFORM="$2"; shift 2 ;;
        --bsp)      BSP="$2"; shift 2 ;;
        --sai)      SAI="$2"; shift 2 ;;
        --agent)    AGENT="$2"; shift 2 ;;
        --fsdb)     FSDB="$2"; shift 2 ;;
        --qsfp)     QSFP="$2"; shift 2 ;;
        --fboss2)   FBOSS2="$2"; shift 2 ;;
        --led)      LED="$2"; shift 2 ;;
        -h|--help)  usage ;;
        *)          echo "Error: Unknown option: $1"; usage ;;
    esac
done

# Validate all required arguments are provided
missing=()
[[ -z "$PLATFORM" ]] && missing+=("--platform")
[[ -z "$BSP" ]]      && missing+=("--bsp")
[[ -z "$SAI" ]]      && missing+=("--sai")
[[ -z "$AGENT" ]]    && missing+=("--agent")
[[ -z "$FSDB" ]]     && missing+=("--fsdb")
[[ -z "$QSFP" ]]     && missing+=("--qsfp")
[[ -z "$FBOSS2" ]]   && missing+=("--fboss2")
[[ -z "$LED" ]]      && missing+=("--led")

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Error: Missing required arguments: ${missing[*]}"
    echo ""
    usage
fi

# Use parallel arrays to avoid duplicate-key issues with associative arrays
# (if two args point to the same source path, both must still be staged)
SRCS=("$PLATFORM" "$BSP" "$SAI" "$AGENT" "$FSDB" "$QSFP" "$FBOSS2" "$LED")
DESTS=(
    "platform_fboss_bins.tar.zst"
    "fboss_bsp_kmods.tar"
    "fboss_sai_kmods.tar"
    "agent_fboss_bins.tar.zst"
    "fsdb_fboss_bins.tar.zst"
    "qsfp_fboss_bins.tar.zst"
    "fboss2_fboss_bins.tar.zst"
    "led_fboss_bins.tar.zst"
)

# Validate all source files exist
errors=0
for src in "${SRCS[@]}"; do
    if [[ ! -f "$src" ]]; then
        echo "Error: File not found: $src"
        errors=1
    fi
done
if [[ $errors -ne 0 ]]; then
    exit 1
fi

# Create staging directory if needed
mkdir -p "$STAGING_DIR"

# Copy each artifact to the staging directory
for i in "${!SRCS[@]}"; do
    dest="${STAGING_DIR}/${DESTS[$i]}"
    echo "Staging: ${SRCS[$i]} -> $dest"
    cp "${SRCS[$i]}" "$dest"
done

echo ""
echo "All artifacts staged to ${STAGING_DIR}/"
