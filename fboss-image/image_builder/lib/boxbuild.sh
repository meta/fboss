#!/bin/bash
# KVM-only KIWI helpers.

validate_container_kvm() {
  python3 - <<'PY'
import fcntl
import os
import stat

if not stat.S_ISCHR(os.stat('/dev/kvm').st_mode):
    raise RuntimeError('KVM isolation requires a character device')
with open('/dev/kvm', 'rb+', buffering=0) as kvm:
    if fcntl.ioctl(kvm.fileno(), 0xae00, 0) != 12:  # KVM_GET_API_VERSION
        raise RuntimeError('Unsupported KVM API')
    vm = fcntl.ioctl(kvm.fileno(), 0xae01, 0)  # KVM_CREATE_VM, no guest boot
    os.close(vm)
PY
}

prepare_box_proxy_profile() {
  local proxy_name
  BOX_PROXY_PROFILE="${DESCRIPTION_DIR}/boxroot/etc/boxproxy"
  mkdir -p "$(dirname "${BOX_PROXY_PROFILE}")" || return 1
  # An old file must never retain permissive permissions from a prior run.
  rm -f "${BOX_PROXY_PROFILE}" || return 1
  (
    umask 077
    : >"${BOX_PROXY_PROFILE}"
  ) || return 1
  for proxy_name in http_proxy https_proxy no_proxy HTTP_PROXY HTTPS_PROXY NO_PROXY; do
    if [[ -v ${proxy_name} ]]; then
      printf 'export %s=%q\n' "${proxy_name}" "${!proxy_name}" >>"${BOX_PROXY_PROFILE}" || return 1
    fi
  done
}

cleanup_box_proxy_profile() {
  rm -f "${BOX_PROXY_PROFILE:-}"
}

move_box_result() {
  local directory=$1 pattern=$2 destination=$3
  local -a matches=()
  local file
  while IFS= read -r -d '' file; do
    matches+=("${file}")
  done < <(find "${directory}" -maxdepth 1 -type f -name "${pattern}" -print0)
  if [[ ${#matches[@]} -ne 1 || ! -s ${matches[0]} ]]; then
    echo "ERROR: expected exactly one nonempty ${pattern} result in ${directory}" >&2
    return 1
  fi
  if [[ ${matches[0]} != "${destination}" ]]; then
    mv -- "${matches[0]}" "${destination}"
  fi
}

BOX_NAME=fedora

stage_box_caches() {
  # Download or update the box once into a persistent cache, then give each
  # concurrent build a private copy: the plugin extracts the kernel into its
  # box cache, and the copies are verified here, not per build. The lock
  # serializes runs sharing the persistent cache.
  local shared=$1 cache
  shift
  mkdir -p "${shared}" || return 1
  (
    flock 9 || exit 1
    KIWI_BOXED_CACHE_DIR="${shared}" python3 -s - "${BOX_NAME}" <<'PY' || exit 1
import sys

from kiwi_boxed_plugin.box_download import BoxDownload

BoxDownload(sys.argv[1]).fetch(update_check=True, snapshot=True)
PY
    for cache in "$@"; do
      mkdir -p "${cache}/${BOX_NAME}" || exit 1
      find "${shared}/${BOX_NAME}" -maxdepth 1 -type f \
        ! -name 'kernel.*' ! -name 'initrd.*' \
        -exec cp --reflink=auto -t "${cache}/${BOX_NAME}" -- {} + || exit 1
    done
  ) 9>"${shared}/.lock"
}

run_kiwi_boxbuild() (
  set -o pipefail
  local prefix=$1 build_target=$2 cache=$3
  shift 3
  # Two builds run at once; each guest gets half the CPUs. QEMU backs guest RAM
  # with /dev/shm, which the CLI sizes from FBOSS_BOX_MEMORY_GB.
  local cpus=$(($(nproc) / 2))
  if ((cpus < 4)); then
    cpus=4
  fi
  # SC2016: $0 belongs to awk, not shell expansion.
  # shellcheck disable=SC2016
  KIWI_BOXED_CACHE_DIR="${cache}" kiwi-ng-3 "$@" system boxbuild --box "${BOX_NAME}" \
    --kiwi-version 11.0.1 --virtiofs-sharing --no-update-check \
    --box-smp-cpus "${cpus}" --box-memory "${FBOSS_BOX_MEMORY_GB:-8}G" -- \
    --description "${DESCRIPTION_DIR}" --target-dir "${build_target}" |&
    stdbuf -oL tee -a "${LOG_FILE}" |
    stdbuf -oL awk -v prefix="${prefix}| " '{print prefix $0}' || return
  # Upstream ignores QEMU's exit status and defaults a missing result.code
  # to success. A fresh target and explicit guest success are both required.
  if [[ ! -f ${build_target}/result.code || $(cat "${build_target}/result.code") != 0 ]]; then
    echo 'ERROR: missing or unsuccessful guest result.code' >&2
    return 1
  fi
)
