#!/usr/bin/env bash
#
# Build script for FBOSS forwarding and platform stacks
# This script builds the specified stack binaries inside a container.
#
# Prerequisites (handled by build_entrypoint.py):
#   - SAI SDK extracted to /deps/npu_sai or /deps/phy_sai
#   - Kernel RPMs installed (from /deps/kernel tarball, optional)
#
# This script:
#   - Parses the requested stack type (forwarding or platform)
#   - For forwarding, enables SAI/SDK handling via need_sai=1
#   - Configures SAI environment variables (when need_sai=1)
#   - Builds FBOSS dependencies
#   - Builds the appropriate FBOSS CMake target
#   - Packages artifacts into tarballs
#
set -euxo pipefail

log() {
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

usage() {
  echo "Usage: $0 forwarding|platform [--num-jobs N]" >&2
  exit 1
}

if [ "$#" -ne 1 ] && [ "$#" -ne 3 ]; then
  usage
fi

stack_type="$1"

job_override=""
if [ "$#" -eq 3 ]; then
  if [ "$2" != "--num-jobs" ]; then
    usage
  fi
  job_override="$3"
  if [[ ! $job_override =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid --num-jobs value: $job_override (expected a positive integer)" >&2
    exit 1
  fi
fi

need_sai=0
stack_label=""
cmake_target=""
package_target=""
build_with_phy_sai="no"

case "$stack_type" in
forwarding)
  need_sai=1
  stack_label="forwarding"
  cmake_target="fboss_forwarding_stack"
  package_target="forwarding-stack"
  mem_per_core=16 # GB
  ;;
platform)
  stack_label="platform"
  cmake_target="fboss_platform_services"
  package_target="platform-stack"
  mem_per_core=7 # GB
  ;;
*)
  echo "Unsupported stack type: $stack_type (only 'forwarding' and 'platform' are supported)" >&2
  exit 1
  ;;
esac

if [ -n "$job_override" ]; then
  num_jobs="$job_override"
else
  gb_per_core=$(free -g | awk '/^Mem:/{print int($2 / '$mem_per_core')}')
  num_cores=$(nproc)
  if [ "$num_cores" -gt "$gb_per_core" ]; then
    num_jobs="$gb_per_core"
  else
    num_jobs="$num_cores"
  fi
fi
export num_jobs # Export so it's available in subshells
log "Using num_jobs=${num_jobs} for ${stack_type} stack"

BUILD_TYPE="${BUILD_TYPE:-MinSizeRel}"

# Setup FBOSS build environment compatibility:
#    build_entrypoint provides: /src (repo), /deps/*-extracted (dependencies)
#    FBOSS build expects: /var/FBOSS/fboss (repo)
if [ -d "/src" ]; then
  log "Setting up symlinks"

  # Link /var/FBOSS/fboss -> /src (the worktree root)
  mkdir -p /var/FBOSS
  rm -rf /var/FBOSS/fboss # Remove any stale symlink or directory
  ln -sf /src /var/FBOSS/fboss
  echo "  Created: /var/FBOSS/fboss -> /src"
fi

SAI_DIR=""
if [ "$need_sai" -eq 1 ]; then
  if [ -d "/deps/npu_sai-extracted" ]; then
    SAI_DIR="/deps/npu_sai-extracted"
  fi

  # Check if phy_sai build environment exists (will be sourced in subshell later)
  if [ -f "/deps/phy_sai-extracted/phy_sai_build.env" ]; then
    SAI_DIR="/deps/phy_sai-extracted"
    build_with_phy_sai="yes"
  fi
  echo "Found SAI at $SAI_DIR (installed by build_entrypoint.py)"
fi
export SAI_DIR

# Function to perform complete build with given suffix, called in a subshell
# to isolate environments
perform_build() {
  local build_suffix="$1"
  local output_suffix="$2"
  local sai_env_file="$3"
  local -a npu_flags=()

  log "perform_build Command: $0, ARGS: $build_suffix $output_suffix $sai_env_file"

  # Source SAI build environment if provided
  if [ -n "$sai_env_file" ] && [ -f "$sai_env_file" ]; then
    # shellcheck disable=SC1090
    source "$sai_env_file"
    log "SAI environment loaded from $sai_env_file"
  fi

  # Determine SAI implementation name based on environment variables
  local sai_name=""
  if [ "$need_sai" -eq 1 ]; then
    log "Using SAI_SDK_VERSION=${SAI_SDK_VERSION:-N/A} for SAI_VERSION=${SAI_VERSION:-Unknown}"

    npu_flags+=("--npu-libsai-impl-path" "$SAI_DIR/lib")
    npu_flags+=("--npu-experiments-path" "$SAI_DIR/include")

    if [ -n "${BUILD_SAI_FAKE:-}" ]; then
      sai_name="sai-fake"
      # Default for run-getdeps.py is to assume Fake SAI. Must unset real-SAI arguments above
      npu_flags=()
    elif [ -n "${SAI_BRCM_IMPL:-}" ]; then
      sai_name="sai-bcm-${SAI_SDK_VERSION}"
      npu_flags+=("--npu-sai-impl" "SAI_BRCM_IMPL")
    elif [ -n "${SAI_BRCM_PAI_IMPL:-}" ]; then
      sai_name="sai-brcm-pai-${SAI_SDK_VERSION}"
      npu_flags+=("--phy-sai-impl" "SAI_BRCM_PAI_IMPL")
    elif [ -n "${SAI_TAJO_IMPL:-}" ]; then
      sai_name="sai-tajo-${SAI_SDK_VERSION}"
      npu_flags+=("--npu-sai-impl" "SAI_TAJO_IMPL")
    else
      sai_name="sai-unknown-${SAI_SDK_VERSION}"
    fi
    log "Using SAI implementation: $sai_name"

    if [ -n "${SAI_VERSION:-}" ]; then
      npu_flags+=("--npu-sai-version" "$SAI_VERSION")
    fi
    if [ -n "${SAI_SDK_VERSION:-}" ]; then
      npu_flags+=("--npu-sai-sdk-version" "$SAI_SDK_VERSION")
    fi
  fi

  scratch_root="/build"
  # Setup build directories based on scratch_root
  if [ "$need_sai" -eq 1 ]; then
    build_dir="${scratch_root}/forwarding-stack/${sai_name}${build_suffix}"
  else
    build_dir="${scratch_root}/platform-stack${build_suffix}"
  fi
  mkdir -p "$build_dir"

  common_root="${scratch_root}/common"

  # Metadata is only recorded by a POST_BUILD step, which doesn't rerun for
  # up-to-date binaries. Delete the recorded binaries so they relink and re-record
  # accurately for this build..
  local npu_sdk_metadata="${build_dir}/build/fboss/npu_sdk_metadata.json"
  if [ "$need_sai" -eq 1 ] && [ -z "$build_suffix" ] && [ -f "$npu_sdk_metadata" ]; then
    log "Removing binaries recorded in $npu_sdk_metadata"
    local recorded_binaries
    recorded_binaries=$(python3 -c \
      'import json, sys; print("\n".join(json.load(open(sys.argv[1]))))' \
      "$npu_sdk_metadata")
    local binary
    while IFS= read -r binary; do
      if [ -z "$binary" ]; then
        continue
      fi
      if [[ $binary == */* ]]; then
        echo "Unexpected path in $npu_sdk_metadata: $binary" >&2
        exit 1
      fi
      rm -f "${build_dir}/build/fboss/${binary}"
    done <<<"$recorded_binaries"
    rm -f "$npu_sdk_metadata"
  fi

  common_options='--allow-system-packages'
  common_options+=' --scratch-path '$build_dir
  common_options+=' --extra-cmake-defines {'
  common_options+='"CMAKE_BUILD_TYPE":"MinSizeRel"'
  common_options+=',"CMAKE_CXX_STANDARD":"20"'
  common_options+=',"RANGE_V3_TESTS":"OFF"'
  common_options+=',"RANGE_V3_PERF":"OFF"}'
  common_options+=' --src-dir .'
  common_options+=' fboss'

  # Share download / repo / extracted caches across different types of builds
  mkdir -p "${common_root}/downloads"
  if [ ! -L "${build_dir}/downloads" ]; then
    ln -s "${common_root}/downloads" "${build_dir}/downloads"
  fi
  mkdir -p "${common_root}/repos"
  if [ ! -L "${build_dir}/repos" ]; then
    ln -s "${common_root}/repos" "${build_dir}/repos"
  fi
  mkdir -p "${common_root}/extracted"
  if [ ! -L "${build_dir}/extracted" ]; then
    ln -s "${common_root}/extracted" "${build_dir}/extracted"
  fi

  # Navigate to FBOSS source root
  cd /var/FBOSS/fboss

  log "Building FBOSS ${stack_label} stack${build_suffix}"

  # Save the manifests because we must modify them
  tar -cf manifests_snapshot.tar build
  tar -xf fboss/oss/stable_commits/latest_stable_hashes.tar.gz
  chmod -R a+r build/fbcode_builder/manifests

  if [ "$stack_type" = "platform" ]; then
    # For a platform-only build we do not need the vendor SAI implementation.
    # Temporarily drop sai_impl from the fboss manifest so getdeps will not try
    # to fetch it. The open-source SAI headers (libsai) remain in the manifest.
    if grep -q '^[[:space:]]*sai_impl[[:space:]]*$' build/fbcode_builder/manifests/fboss; then
      tmp_manifest="$(mktemp)"
      sed '/^[[:space:]]*sai_impl[[:space:]]*$/d' \
        build/fbcode_builder/manifests/fboss >"$tmp_manifest"
      mv "$tmp_manifest" build/fbcode_builder/manifests/fboss
      log "Temporarily removed sai_impl from fboss manifest for platform-only build"
    fi
  fi

  # Install system dependencies
  log "Installing system dependencies..."
  time nice -n 10 ./fboss/oss/scripts/run-getdeps.py \
    "${npu_flags[@]}" \
    install-system-deps \
    --recursive \
    $common_options

  # Build dependencies
  log "Building FBOSS dependencies..."
  time nice -n 10 ./fboss/oss/scripts/run-getdeps.py \
    "${npu_flags[@]}" \
    build \
    --build-type $BUILD_TYPE \
    --only-deps \
    $common_options

  log "Get deps SUCCESS"

  # Build FBOSS stack
  log "Building FBOSS ${stack_label} stack..."

  time nice -n 10 ./fboss/oss/scripts/run-getdeps.py \
    "${npu_flags[@]}" \
    build \
    --num-jobs "${num_jobs}" \
    --build-type "${BUILD_TYPE}" \
    --no-deps \
    ${common_options} \
    --cmake-target "${cmake_target}"

  log "${cmake_target} Build SUCCESS"

  # Package the stack
  # Note: package.py creates both <target>.tar (production binaries)
  # and <target>-tests.tar (test binaries). We only ship the production tar.
  log "Packaging ${stack_label} stack..."
  python3 /var/FBOSS/fboss/fboss/oss/scripts/package.py \
    --build-dir "$build_dir" \
    "$package_target"

  # Copy production artifact to output directory
  # Tests are NOT included in production image
  log "Copying artifacts to output"
  OUT_DIR=/output
  mkdir -p "$OUT_DIR"
  mv "${package_target}.tar" "$OUT_DIR/${package_target}${output_suffix}.tar"

  log "FBOSS ${stack_label} stack build complete!"
  log "Production artifact:"
  ls -lh "$OUT_DIR/${package_target}${output_suffix}.tar"

  # Restore modified manifests if we took a snapshot earlier.
  if [ -f manifests_snapshot.tar ]; then
    tar -xf manifests_snapshot.tar
    rm manifests_snapshot.tar
  fi
}

# First build: standard build with current settings
if [ "$need_sai" -eq 1 ]; then
  (
    log "Building with npu_sai SAI settings"
    perform_build "" "" "$SAI_DIR/sai_build.env"
  )
  if [ "$build_with_phy_sai" = "yes" ]; then
    (
      log "Building with phy_sai settings"
      perform_build "-phy-sai" "-phy-sai" "$SAI_DIR/phy_sai_build.env"
    )
  fi
else
  (
    log "Building platform stack"
    perform_build "" "" ""
  )
fi
