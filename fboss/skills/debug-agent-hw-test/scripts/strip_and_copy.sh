#!/bin/bash
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#
# Strip a binary locally for deployment to a switch.
# The remote upload is handled by lab_ssh_upload MCP tool (see references/build-and-load.md).
#
# Usage: bash strip_and_copy.sh <source_binary_path> <dest_binary_name> [output_dir]
#
# Output: Stripped binary at <output_dir>/<dest_binary_name> (default /tmp) with md5 printed.
# If your upload tool only accepts local paths under $HOME, pass an output_dir there (e.g. ~/scratch).
#
# Example:
#   bash strip_and_copy.sh \
#     buck-out/v2/gen/fbcode/.../sai_agent_hw_test-brcm-12.2.0.0_dnx_odp \
#     sai_agent_hw_test-brcm-12.2.0.0_dnx_odp

set -euo pipefail

SOURCE_BINARY="$1"
DEST_BINARY_NAME="$2"
OUTPUT_DIR="${3:-/tmp}"

mkdir -p "${OUTPUT_DIR}"
STRIPPED="${OUTPUT_DIR}/${DEST_BINARY_NAME}"

echo "Stripping ${SOURCE_BINARY} -> ${STRIPPED}"
strip -o "${STRIPPED}" "${SOURCE_BINARY}"

LOCAL_HASH=$(md5sum "${STRIPPED}" | awk '{print $1}')
echo "md5: ${LOCAL_HASH}"
echo "Stripped binary ready at: ${STRIPPED}"
echo "Upload to switch with: lab_ssh_upload(host=<switch>, local_path=\"${STRIPPED}\", remote_path=\"/tmp/${DEST_BINARY_NAME}\")"
