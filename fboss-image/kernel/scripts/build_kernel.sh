#!/usr/bin/env bash
#
# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.
#
set -euo pipefail

# Internal script to build kernel RPMs inside the container.
# Args:
#   $1: Kernel version (required)
#   $2: Output directory inside container (required), e.g. /output

set -x
KERNEL_VERSION="${1:?kernel version required}"
OUT_DIR="${2:?output dir required}"

# Compute container paths
# Determine KERNEL_ROOT from the script's location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONTAINER_BUILD_ROOT="$KERNEL_ROOT/.build"
CONTAINER_SPECS_DIR="$KERNEL_ROOT/specs"
CONTAINER_CONFIGS_DIR="$KERNEL_ROOT/configs"
CONTAINER_SCRIPTS_DIR="$KERNEL_ROOT/scripts"
COMMON_SCRIPTS_DIR="$KERNEL_ROOT/../../../oss/scripts"
if [ ! -f "$COMMON_SCRIPTS_DIR/component_version.py" ]; then
  COMMON_SCRIPTS_DIR=/fboss/oss/scripts
fi

python3 "$COMMON_SCRIPTS_DIR/component_version.py" kernel --prepare \
  --output-dir "$OUT_DIR" --scripts-dir "$CONTAINER_SCRIPTS_DIR"

# Install kernel build dependencies
bash "$CONTAINER_SCRIPTS_DIR/setup_kernel_build_deps.sh"

# Build outside dist/ because the kernel version capture reads every RPM under it, so the rpmbuild tree with duplicate RPMS/ causes failure
BUILD_DIR="$CONTAINER_BUILD_ROOT/build-$KERNEL_VERSION"

mkdir -p "$BUILD_DIR/SOURCES"
cd "$BUILD_DIR"

# Download kernel source (spectool is part of rpmdevtools).
# spectool can exit 0 even when the CDN download 404s, so verify the tarball
# actually landed and fall back to the kernel.org git snapshot (converted to
# .tar.xz to match Source0 in kernel.spec) when the primary source is missing.
KERNEL_TARBALL="SOURCES/linux-$KERNEL_VERSION.tar.xz"
KERNEL_SOURCE_INFO="$KERNEL_TARBALL.download-url"
unset KERNEL_SOURCE_DOWNLOAD_URL
if [ ! -f "$BUILD_DIR/$KERNEL_TARBALL" ]; then
  rm -f "$KERNEL_SOURCE_INFO"
  spectool -g -C SOURCES "$CONTAINER_SPECS_DIR/kernel.spec" \
    --define "kernel_version $KERNEL_VERSION"

  if [ ! -f "$KERNEL_TARBALL" ]; then
    echo "Primary kernel source download failed; falling back to git.kernel.org snapshot..."
    snapshot_url="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/snapshot/linux-$KERNEL_VERSION.tar.gz"
    if ! curl -fL --retry 3 -o "SOURCES/linux-$KERNEL_VERSION.tar.gz" "$snapshot_url"; then
      rm -f "SOURCES/linux-$KERNEL_VERSION.tar.gz"
      echo "Fallback snapshot download failed for kernel $KERNEL_VERSION" >&2
      exit 1
    fi
    # Convert .tar.gz -> .tar.xz so rpmbuild/kernel.spec Source0 matches.
    gunzip -f "SOURCES/linux-$KERNEL_VERSION.tar.gz"
    xz -z "SOURCES/linux-$KERNEL_VERSION.tar"
    source_checksum="$(sha256sum "$KERNEL_TARBALL")"
    printf '%s %s\n' "${source_checksum%% *}" "$snapshot_url" >"$KERNEL_SOURCE_INFO"
  fi
fi

if [ -f "$KERNEL_SOURCE_INFO" ] && read -r source_sha256 source_url <"$KERNEL_SOURCE_INFO"; then
  source_checksum="$(sha256sum "$KERNEL_TARBALL")"
  if [ "${source_checksum%% *}" = "$source_sha256" ]; then
    export KERNEL_SOURCE_DOWNLOAD_URL="$source_url"
  fi
fi

# Ensure FBOSS config sources are present for rpmbuild
cp "$CONTAINER_CONFIGS_DIR/fboss-reference.config" "$BUILD_DIR/SOURCES/"

# Generate version-specific local overrides from YAML
python3 "$CONTAINER_SCRIPTS_DIR/generate_config_overrides.py" \
  "$CONTAINER_CONFIGS_DIR/fboss-local-overrides.yaml" \
  "$KERNEL_VERSION" \
  "$BUILD_DIR/SOURCES/fboss-local-overrides.config"

rpmbuild -ba "$CONTAINER_SPECS_DIR/kernel.spec" \
  --define "_topdir $BUILD_DIR" \
  --define "_sourcedir $BUILD_DIR/SOURCES" \
  --define "container_scripts_dir $CONTAINER_SCRIPTS_DIR" \
  --define "kernel_version $KERNEL_VERSION" || {
  rv=$?
  echo "$(date) Kernel build failed with rv=$rv" >&2
  exit 1
}
echo "$(date) Kernel build completed successfully"

# Copy RPMs to output directory
cp -r RPMS/* "$OUT_DIR"/ 2>/dev/null
cp -r SRPMS/* "$OUT_DIR"/ 2>/dev/null

python3 "$COMMON_SCRIPTS_DIR/component_version.py" kernel \
  --output-dir "$OUT_DIR" --scripts-dir "$CONTAINER_SCRIPTS_DIR"

# Create uncompressed tarball
# Compression will be handled by ImageBuilder on the host if needed
echo "Creating uncompressed tarball..."

# Create tarball with common transform rules
# shellcheck disable=SC2046
tar -cf $OUT_DIR/kernel-$KERNEL_VERSION.rpms.tar \
  --transform 's|.*/||' \
  --transform 's|^\(kernel-[^-]\+\)-.*\.\(x86_64\)\.rpm$|\1-\2.rpm|' \
  $(find RPMS -name "*.rpm")

tar -rf "$OUT_DIR/kernel-$KERNEL_VERSION.rpms.tar" \
  -C "$OUT_DIR" share/kernel-version.json

rm -rf "$BUILD_DIR"

echo 'Kernel RPM build complete!'
echo 'Output files:'
find "$OUT_DIR" \( -name '*.rpm' -o -name "*.tar" \) -type f
