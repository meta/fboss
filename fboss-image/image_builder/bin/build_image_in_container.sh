#!/bin/bash

# Script that builds a PXE and USB bootable ISO an image using kiwi-ng-3.

# For hardlinking component artifacts into the kiwi description tree we need
# both source and destination to live on the same filesystem. The kiwi
# description lives under /image_builder, so we expose the deps staging
# directory there and create a /deps symlink pointing into it. The
# /deps mount remains available for reading.
ln -sfn /image_builder/deps_staging /deps

# Get the full path to the workspace root directory where everything lives
WSROOT=$(cd "$(dirname "$0")/.." && pwd)

# Change directory full path to correct levels up from the script location so that we can include
# the functions.sh file
LOG_DIR="${WSROOT}/logs"

# Source common functions
# shellcheck disable=SC1091
source "${WSROOT}/lib/functions.sh"

# Save all arguments for later use
ORIGINAL_ARGS=("$0" "$@")

# Default values
DESCRIPTION_DIR="${WSROOT}/templates/centos-09.0"
TARGET_DIR="${WSROOT}/output"
BUILD_PXE=""
BUILD_ONIE=""
KIWI_DEBUG=""
AFTER_PKGS_INSTALL_FILE=""
AFTER_PKGS_EXECUTE_FILE=""

# User configurable variables
DEPS_DIR="" # Component artifacts directory

mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/build_image_in_container.log"

help() {
  echo "Usage: $0 [options]"
  echo ""
  echo "Options:"
  echo ""
  echo "  --deps <dir>                Directory containing component artifacts (organized by component name)"
  echo "  -a|--after-pkgs-install     JSON File (in templates/centos-09.0 directory) containing additional packages to install to the image"
  echo "  -e|--after-pkgs-execute     JSON File (in templates/centos-09.0 directory) containing list of commands to execute after packages are installed"
  echo "  -p|--build-pxe-usb          Build PXE and USB installers image (default: no)"
  echo "  -o|--build-onie             Build ONIE installer image (default: no)"
  echo ""
  echo "  -d|--debug                  Enable kiwi-ng debug"
  echo "  -h|--help                   Print this help message"
  echo ""
}

# Once this is finalized, it may be better to use a Dockerfile to build the image
update_docker() {
  dnf install -y \
    epel-release \
    kiwi \
    policycoreutils \
    python3-kiwi \
    dracut-kiwi-live \
    dracut-kiwi-overlay \
    dnf-plugin-versionlock \
    dracut-kiwi-oem-dump \
    kiwi-systemdeps-image-validation \
    syslinux \
    btrfs-progs \
    mtools \
    glibc-static

  # The python3-kiwi RPM installs for the system Python 3.9, but python3 may
  # resolve to a newer version (e.g. 3.12) via update-alternatives. The
  # kiwi-ng-3 shebang uses "python3 -s" which excludes /usr/local/lib paths.
  # Install kiwi to the system site-packages visible under -s.
  #
  # Pinned: this install shadows the RPM above, so it is the kiwi that actually
  # runs, and the container is rebuilt from scratch on every build -- unpinned,
  # each run silently picks up whatever PyPI serves that day. 11.0.1 is the last
  # version known to produce an image; a later one fails partitioning with
  # "sgdisk: Could not change partition 1's type code to EF02!". Bump
  # deliberately, after a green build, rather than by drift.
  KIWI_VERSION=11.0.1
  KIWI_SITE_PKG=$(python3 -s -c "import site; print(site.getsitepackages()[0])")
  python3 -m pip install "kiwi==${KIWI_VERSION}" --target "${KIWI_SITE_PKG}"
}

# Remove shim's fallback bootloader from the install ISO's ESP: it registers an
# NVRAM boot entry for whichever ESP it runs from. The installed system's ESP
# keeps it, so this applies to the ISO only.
strip_shim_fallback_from_iso() {
  local iso="$1"
  local esp_start esp_offset img d csv boot_listing

  # Appended partition: GPT type GUID, or MBR type 0xef on a plain isohybrid.
  esp_start=$(sfdisk -d "${iso}" 2>/dev/null |
    awk -F'[=,]' '
      /C12A7328-F81F-11D2-BA4B-00A0C93EC93B/ || /type=[[:space:]]*ef[[:space:]]*$/ {
        gsub(/[^0-9]/, "", $2); if ($2 != "") { print $2; exit }
      }')

  if [ -z "${esp_start}" ]; then
    echo "ERROR: no EFI system partition found in ${iso}" >&2
    sfdisk -d "${iso}" >&2 || true
    return 1
  fi

  esp_offset=$((esp_start * 512))
  img="${iso}@@${esp_offset}"
  export MTOOLS_SKIP_CHECK=1

  dprint "Stripping shim fallback from ${iso##*/} (ESP at byte ${esp_offset})..."

  # mdel exits non-zero on an absent file, which is not a failure here.
  mdel -i "${img}" ::/EFI/BOOT/fbx64.efi 2>/dev/null || true
  mdel -i "${img}" ::/EFI/BOOT/fallback.efi 2>/dev/null || true
  # mdir -b lists one level, so walk ::/EFI's subdirectories to reach the CSVs.
  for d in $(mdir -b -i "${img}" ::/EFI 2>/dev/null); do
    for csv in $(mdir -b -i "${img}" "${d}" 2>/dev/null | grep -i '\.csv$'); do
      mdel -i "${img}" "${csv}" 2>/dev/null || true
    done
  done

  # A failed delete is indistinguishable from an absent file, so verify.
  if ! boot_listing=$(mdir -b -i "${img}" ::/EFI/BOOT 2>&1); then
    echo "ERROR: cannot read ESP in ${iso}: ${boot_listing}" >&2
    return 1
  fi

  # The loader is always present; its absence means the offset is wrong.
  if ! printf '%s\n' "${boot_listing}" | grep -qi 'BOOTX64\.EFI'; then
    echo "ERROR: no BOOTX64.EFI in ${iso} ESP; wrong offset or unreadable FAT" >&2
    printf '%s\n' "${boot_listing}" >&2
    return 1
  fi

  if printf '%s\n' "${boot_listing}" | grep -qi -e 'fbx64\.efi' -e 'fallback\.efi'; then
    echo "ERROR: shim fallback still present in ${iso}" >&2
    printf '%s\n' "${boot_listing}" >&2
    return 1
  fi

  # Inert on its own: only the fallback binary reads it.
  for d in $(mdir -b -i "${img}" ::/EFI 2>/dev/null); do
    if mdir -b -i "${img}" "${d}" 2>/dev/null | grep -qi '\.csv$'; then
      echo "WARNING: ${iso} still carries a BOOT*.CSV" >&2
      break
    fi
  done
}

build_zstd() {
  if [ ! -d ${TARGET_DIR}/zstd ]; then
    dprint "Building static zstd..."
    git clone https://github.com/facebook/zstd.git ${TARGET_DIR}/zstd
    pushd ${TARGET_DIR}/zstd >/dev/null
    git checkout release
    make -C programs zstd-frugal LDFLAGS="-static"
    strip programs/zstd-frugal
    popd >/dev/null
  fi
}

build_onie_installer() {
  build_zstd

  dprint "Creating ONIE installer..."
  out_bin=${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.install.bin
  rootfs=${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.tar.zst

  pushd ${TARGET_DIR}/onie >/dev/null

  mkdir -p onie_installer
  templates_dir="${WSROOT}/templates/onie"

  cp ${templates_dir}/distro-setup.sh.tmpl onie_installer/distro-setup.sh
  chmod a+x onie_installer/distro-setup.sh

  cp ${templates_dir}/default_platform.conf onie_installer/default_platform.conf
  chmod a+x onie_installer/default_platform.conf

  cp ${templates_dir}/install.sh.tmpl onie_installer/install.sh
  chmod a+x onie_installer/install.sh

  cp ${TARGET_DIR}/zstd/programs/zstd-frugal onie_installer/zstd
  chmod a+x onie_installer/zstd

  dprint "  Finding kernel and initrd..."
  kernel=$(
    set +o pipefail
    tar -tf $rootfs | awk -F / '/^boot\/vmlinuz/ {print $2; exit 0}' || exit 1
  )
  initrd=$(
    set +o pipefail
    tar -tf $rootfs | awk -F / '/^boot\/initramfs/ {print $2; exit 0}' || exit 1
  )

  sed -i -e "s/%%KERNEL_FILENAME%%/$kernel/g" \
    -e "s/%%INITRD_FILENAME%%/$initrd/g" \
    onie_installer/install.sh

  mv $rootfs onie_installer/rootfs.tar.zst

  dprint "  Packaging installer..."
  tar -cf installer.tar onie_installer

  sha1=$(cat installer.tar | sha1sum | awk '{print $1}')

  cp ${templates_dir}/sharch_body.sh ${out_bin}
  sed -i -e "s/%%IMAGE_SHA1%%/$sha1/" \
    -e "s/%%PAYLOAD_IMAGE_SIZE%%/$(stat -c %s installer.tar)/" \
    ${out_bin}

  cat installer.tar >>${out_bin}

  rm -rf installer.tar onie_installer

  popd >/dev/null
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case "$1" in

  --deps)
    DEPS_DIR=$2
    shift 2
    ;;

  -p | --build-pxe-usb)
    BUILD_PXE="yes"
    shift 1
    ;;

  -o | --build-onie)
    BUILD_ONIE="yes"
    shift 1
    ;;

  -d | --debug)
    KIWI_DEBUG=" --debug "
    shift 1
    ;;

  -a | --after-pkgs-input-file)
    AFTER_PKGS_INSTALL_FILE=$2
    shift 2
    ;;

  -e | --after-pkgs-execute-file)
    AFTER_PKGS_EXECUTE_FILE=$2
    shift 2
    ;;

  -h | --help)
    help
    exit 0
    ;;

  *)
    echo "Unrecognized command option: '${1}'"
    exit 1
    ;;
  esac
done

# Log everything for posterity ;-)
true >"${LOG_FILE}" # Truncate log file
export LOG_FILE

dprint "Script launch cmdline: ${ORIGINAL_ARGS[*]}"
dprint " ... logging all output to: ${LOG_FILE}"
dprint " ... output directory: ${TARGET_DIR}"

# Perform some sanity checks of user input when an explicit --deps is provided
if [ -n "${DEPS_DIR}" ] && [ ! -d "${DEPS_DIR}" ]; then
  dprint "ERROR: deps directory: ${DEPS_DIR} not accessible, exiting..."
  exit 1
fi

# Update the docker image
dprint "Updating docker image..."
update_docker |& tee -a "${LOG_FILE}"

# Create the output directory (in case it doesn't exist)
mkdir -p "${TARGET_DIR}"
chmod 777 "${TARGET_DIR}"

rm -f "${DESCRIPTION_DIR}/root.tar.gz" # Remove any existing tar file

# Start from an empty overlay. The overlay is populated below by copying
# root_files/ over it, and a copy can only add files: anything deleted from
# root_files/ would otherwise survive here from an earlier build and still ship
# in the image.
rm -rf "${DESCRIPTION_DIR}/root"

# Hardlink component artifacts to root/repos for processing in config.sh. When
# no explicit --deps is provided, use /deps (which resolves into the
# /image_builder filesystem) so that cp -la does not cross mount boundaries.
mkdir -p "${DESCRIPTION_DIR}/root/repos"

EFFECTIVE_DEPS_DIR="${DEPS_DIR:-/deps}"

if [ -d "${EFFECTIVE_DEPS_DIR}" ] && [ -n "$(ls -A "${EFFECTIVE_DEPS_DIR}" 2>/dev/null)" ]; then
  dprint "Hardlinking component artifacts from ${EFFECTIVE_DEPS_DIR} to ${DESCRIPTION_DIR}/root/repos..."
  cp -al "${EFFECTIVE_DEPS_DIR}"/* "${DESCRIPTION_DIR}/root/repos/" 2>/dev/null || {
    dprint "Hardlinks not supported, falling back to copy..."
    cp -a "${EFFECTIVE_DEPS_DIR}"/* "${DESCRIPTION_DIR}/root/repos/"
  }
fi

dprint "Copying /etc/resolv.conf to ${DESCRIPTION_DIR}/root/etc/resolv.conf..."
# Pass /etc/resolv.conf to the chrooted environment
mkdir -p "${DESCRIPTION_DIR}/root/etc"
cp /etc/resolv.conf "${DESCRIPTION_DIR}/root/etc/"

# Copy rootfs template files to overlay
dprint "Copying rootfs files to overlay..."
cp -R ${DESCRIPTION_DIR}/root_files/* ${DESCRIPTION_DIR}/root/

# Written after the root_files copy so an overlay file cannot shadow it.
write_build_info() {
  local rel bytes size repos

  echo "FBOSS distro image"
  echo "Built on: $(date -u)"
  echo "Built by: $(whoami)@$(hostname)"

  # The manifest and the source revision are only knowable outside the
  # container; the CLI drops them here before starting the build.
  if [ -f "${WSROOT}/build-provenance" ]; then
    cat "${WSROOT}/build-provenance"
  else
    echo "Manifest: unknown (no build-provenance from the CLI)"
  fi

  echo ""
  echo "Components:"

  # Read the overlay's copy rather than the staging directory: it is what
  # actually ships, it is a real directory rather than the /deps symlink, and
  # it is the exact tree config.sh consumes as /repos.
  local repos="${DESCRIPTION_DIR}/root/repos"

  if [ ! -d "$repos" ]; then
    echo "  (none: $repos does not exist)"
    return
  fi

  # Two levels down is <component>/<artifact>, the layout config.sh consumes.
  find "$repos" -mindepth 2 -maxdepth 2 -type f -printf '%P\t%s\n' |
    sort |
    while IFS=$'\t' read -r rel bytes; do
      size=$(numfmt --to=iec "$bytes" 2>/dev/null || echo "${bytes}B")
      echo "  ${rel}  ${size}  sha256:$(sha256sum "${repos}/${rel}" | cut -d' ' -f1)"
    done

  if [ -z "$(find "$repos" -mindepth 2 -maxdepth 2 -type f -print -quit)" ]; then
    echo "  (none: no artifacts were staged)"
  fi
}

dprint "Recording image provenance in /etc/build-info..."
write_build_info >"${DESCRIPTION_DIR}/root/etc/build-info"
tee -a "${LOG_FILE}" <"${DESCRIPTION_DIR}/root/etc/build-info"

# The version file the CLI wrote before starting this build. Installed after the
# root_files copy for the same reason build-info is: an overlay file must not be
# able to shadow the record of what this image is. Copied rather than generated
# here because only the CLI can read the manifest and the staged artifacts.
if [ -f "${WSROOT}/fboss-distro-version.json" ]; then
  dprint "Installing /etc/fboss-distro-version.json..."
  cp "${WSROOT}/fboss-distro-version.json" \
    "${DESCRIPTION_DIR}/root/etc/fboss-distro-version.json"
else
  # Not fatal: an image without it still boots, and the build log says why.
  dprint "WARNING: no version file from the CLI; image will not carry one"
fi

# Remove any existing after_pkgs files from previous runs
rm -f ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_install_file.json
rm -f ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_execute_file.json

# Copy the after_pkgs input file in the rootfs so that we can access it the chrooted environment
# The file, if provided by the user, should already copied into the centos-09.0 directory as part
# of the image build process. Copy to fixed location in rootfs where config.sh checks for it.
# These will be deleted once they are processed.
if [[ -n ${AFTER_PKGS_INSTALL_FILE} && -f "${DESCRIPTION_DIR}/${AFTER_PKGS_INSTALL_FILE}" ]]; then
  mkdir -p ${DESCRIPTION_DIR}/root/var/tmp
  dprint "Copying ${DESCRIPTION_DIR}/${AFTER_PKGS_INSTALL_FILE} to ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_install_file.json"
  cp ${DESCRIPTION_DIR}/${AFTER_PKGS_INSTALL_FILE} ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_install_file.json
fi

if [[ -n ${AFTER_PKGS_EXECUTE_FILE} && -f "${DESCRIPTION_DIR}/${AFTER_PKGS_EXECUTE_FILE}" ]]; then
  mkdir -p ${DESCRIPTION_DIR}/root/var/tmp
  dprint "Copying ${DESCRIPTION_DIR}/${AFTER_PKGS_EXECUTE_FILE} to ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_execute_file.json"
  cp ${DESCRIPTION_DIR}/${AFTER_PKGS_EXECUTE_FILE} ${DESCRIPTION_DIR}/root/var/tmp/after_pkgs_execute_file.json
fi

# Generate the images
PXE_RC=0
ONIE_RC=0

if [ -n "${BUILD_PXE}" ]; then
  dprint "Generating PXE and USB installer, this will take few minutes..."
  rm -rf ${TARGET_DIR}/btrfs
  (
    set -e -o pipefail
    kiwi-ng-3 \
      --shared-cache-dir=/var/cache/kiwi-btrfs \
      --profile FBOSS \
      --type oem \
      ${KIWI_DEBUG} system build \
      --description ${DESCRIPTION_DIR} \
      --target-dir ${TARGET_DIR}/btrfs |&
      stdbuf -oL tee -a ${LOG_FILE} | stdbuf -oL awk '{print "PXE/USB Installer| " $0}'
    INSTALL_ISO=${TARGET_DIR}/btrfs/FBOSS-Distro-Image.x86_64-1.0.install.iso
    if [ -f "${INSTALL_ISO}" ]; then
      strip_shim_fallback_from_iso "${INSTALL_ISO}"
    fi
    mv ${TARGET_DIR}/btrfs/FBOSS-Distro-Image.x86_64-1.0.install.* ${TARGET_DIR}
  ) &
  PXE_PID=$!
fi

if [ -n "${BUILD_ONIE}" ]; then
  dprint "Generating ONIE installer, this will take few minutes..."
  rm -rf ${TARGET_DIR}/onie
  (
    set -e -o pipefail
    kiwi-ng-3 \
      --shared-cache-dir=/var/cache/kiwi-onie \
      --profile FBOSS \
      --type tbz \
      ${KIWI_DEBUG} system build \
      --description ${DESCRIPTION_DIR} \
      --target-dir ${TARGET_DIR}/onie |&
      stdbuf -oL tee -a ${LOG_FILE} | stdbuf -oL awk '{print "ONIE installer| " $0}'

    # Repack the rootfs so really long filenames are not truncated under Busybox
    dprint "Repacking rootfs with zstd..."
    mkdir ${TARGET_DIR}/onie/rootfs
    pushd ${TARGET_DIR}/onie/rootfs >/dev/null
    xzcat --threads=0 ${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.tar.xz | tar -x
    rm ${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.tar.xz
    tar --format=gnu -cf ${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.tar *
    zstd --threads=0 -19 ${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.tar
    popd >/dev/null

    build_onie_installer | awk '{print "ONIE installer| " $0}'
    mv ${TARGET_DIR}/onie/FBOSS-Distro-Image.x86_64-1.0.install.* ${TARGET_DIR}
  ) &
  ONIE_PID=$!
fi

if [ -n "${BUILD_PXE}" ]; then
  wait ${PXE_PID}
  PXE_RC=$?
fi

if [ -n "${BUILD_ONIE}" ]; then
  wait ${ONIE_PID}
  ONIE_RC=$?
fi

if [ ${PXE_RC} -ne 0 ]; then
  dprint "ERROR: PXE/USB image build failed"
fi
if [ ${ONIE_RC} -ne 0 ]; then
  dprint "ERROR: ONIE installer build failed"
fi

# The partx wrapper is installed by CI only, and its container is discarded
# once this script returns, so surface its trace while we still can.
if [ -f /tmp/partx_wrapper.log ]; then
  dprint "partx wrapper trace:"
  cat /tmp/partx_wrapper.log
fi

rm -rf ${TARGET_DIR}/btrfs ${TARGET_DIR}/onie

RC=$((PXE_RC + ONIE_RC))
dprint "Image generation completed with exit code ${RC}"
exit "${RC}"
