#!/usr/bin/env bash
#
# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.
#
# Load the SAI kernel modules and create their device nodes.

set -euo pipefail

SRC_DIR=/usr/local/lib/modules
KVER=$(uname -r)
DEST_DIR="/lib/modules/${KVER}/extra/sai"

shopt -s nullglob
modules=("${SRC_DIR}"/*.ko)
if [ ${#modules[@]} -eq 0 ]; then
  echo "No SAI kernel modules found in ${SRC_DIR}" >&2
  exit 1
fi

# modprobe only searches under /lib/modules/$(uname -r).
mkdir -p "${DEST_DIR}"
for ko in "${modules[@]}"; do
  install -m 0644 "${ko}" "${DEST_DIR}/"
done
depmod -a "${KVER}"

# The module set differs per SDK variant -- XGS drops ship linux-bcm-knet, DNX
# drops do not, SDKLT drops add the ngknet family, Leaba ships leaba_module --
# so load whichever modules the package actually carries. Load order comes from
# the depends= each .ko declares (leaba_module pulls in uio), which is what
# modprobe resolves and insmod cannot.
failed=()
for ko in "${modules[@]}"; do
  name=$(basename "${ko}" .ko)
  modprobe "${name}" || failed+=("${name}")
done
if [ ${#failed[@]} -gt 0 ]; then
  echo "Failed to load SAI kernel modules: ${failed[*]}" >&2
  exit 1
fi

# The modules register character devices but do not create the nodes, and udev
# is not running this early (the unit orders itself Before=sysinit.target).
# Majors are read back from the kernel rather than hardcoded, because they
# differ across SDK variants. leaba_module needs none: it registers UIO devices,
# and devtmpfs creates /dev/uioN itself.
while read -r major name; do
  case "${name}" in
  linux-bcm-knet | linux-kernel-bde | linux-user-bde | linux_ng* | linux_bcmgenl)
    [ -c "/dev/${name}" ] || mknod "/dev/${name}" c "${major}" 0
    chmod 666 "/dev/${name}"
    ;;
  esac
done < <(sed -n '/^Character devices:/,/^$/p' /proc/devices)
