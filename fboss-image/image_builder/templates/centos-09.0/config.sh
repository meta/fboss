#!/bin/bash

echo "--- Executing $0 ---"

LOCAL_RPM_REPO_DIR="/usr/local/share/local_rpm_repo"
mkdir -p "$LOCAL_RPM_REPO_DIR"

# Modify the PRETTY_NAME entry in /usr/lib/os-release
sed -i 's/^PRETTY_NAME=.*/PRETTY_NAME="FBOSS Distro Image"/' /usr/lib/os-release
sed -i 's/^NAME=.*/NAME="FBOSS Distro Image"/' /usr/lib/os-release

# kiwi's oem-dump blocksize check reads the target's PHYSICAL block size, but
# kiwi's own docs (elements.rst, target_blocksize) define it as the LOGICAL
# size. Our NVMe might be 512e (512 logical / 4096 physical), so the stock check
# aborts the PXE install via report_and_quit -> reboot -f.
# Upstream: OSInside/kiwi#3022. Delete this once fixed upstream.
KIWI_DUMP=/usr/lib/dracut/modules.d/55kiwi-dump/kiwi-dump-image.sh
KIWI_BS_OLD='target_blocksize="/sys/block/${target_node}/queue/physical_block_size"'
KIWI_BS_NEW='target_blocksize="/sys/block/${target_node}/queue/logical_block_size"'
if grep -qF "$KIWI_BS_OLD" "$KIWI_DUMP"; then
  sed -i "s|$KIWI_BS_OLD|$KIWI_BS_NEW|" "$KIWI_DUMP"
  echo "Patched kiwi oem-dump blocksize check: physical -> logical"
else
  echo "kiwi oem-dump blocksize check not in expected form; leaving upstream behaviour"
fi

# The image is built for 4096-byte logical blocks. On a 512-byte disk the
# rewrite_512b_partition_tables installer module converts it in place after
# the dump, so let that one mismatch through; any other mismatch is still
# refused.
KIWI_BS_CMP_OLD='if [ ! "${target_blocksize}" = "${image_blocksize}" ];then'
KIWI_BS_CMP_NEW='if [ ! "${target_blocksize}" = "${image_blocksize}" ] && [ ! "${image_blocksize}:${target_blocksize}" = "4096:512" ];then'
if grep -qF "$KIWI_BS_CMP_OLD" "$KIWI_DUMP"; then
  KIWI_DUMP_TMP=$(mktemp)
  awk -v old="$KIWI_BS_CMP_OLD" -v new="$KIWI_BS_CMP_NEW" '
    i = index($0, old) { $0 = substr($0, 1, i - 1) new substr($0, i + length(old)) }
    { print }
  ' "$KIWI_DUMP" >"$KIWI_DUMP_TMP"
  if ! bash -n "$KIWI_DUMP_TMP"; then
    echo "ERROR: patched $KIWI_DUMP does not parse"
    exit 1
  fi
  cat "$KIWI_DUMP_TMP" >"$KIWI_DUMP"
  rm -f "$KIWI_DUMP_TMP"
  echo "Patched kiwi oem-dump blocksize check: allow 4096b image on 512b target"
else
  echo "ERROR: kiwi oem-dump blocksize comparison not in expected form; 512b installs would be refused"
  exit 1
fi

# kiwi's first-boot filesystem resize sorts on the lsblk column "START", which
# doesn't exist with CentOS 9 Stream's util-linux 2.37.4. Sorting silently
# no-ops and falls back to kernel, which does not order consistently. Sometimes
# the EFI partition is selected for resizing instead of the btrfs root and the
# rootfs is never resized.
#
# Resolve by partition NUMBER from sysfs instead, with the upstream lookup as
# a fallback.
KIWI_PARTLIB=/usr/lib/dracut/modules.d/59kiwi-lib/kiwi-partitions-lib.sh
KIWI_GPNN_NEW=$(mktemp)
cat >"$KIWI_GPNN_NEW" <<'EOF'
function get_partition_node_name {
    local disk=$1
    local partid=$2
    local index=1
    local disk_node
    local part_sysfs
    local partnode
    local sysfs_seen=0
    udev_pending
    # BUGFIX: resolve by partition number, not by position in the listing
    disk_node=$(readlink -f "${disk}")
    disk_node=${disk_node##*/}
    for part_sysfs in /sys/block/"${disk_node}"/*/partition; do
        [ -r "${part_sysfs}" ] || continue
        sysfs_seen=1
        [ "$(cat "${part_sysfs}")" = "${partid}" ] || continue
        partnode=${part_sysfs%/partition}
        partnode=/dev/${partnode##*/}
        if [ -b "${partnode}" ];then
            echo "${partnode}"
            return 0
        fi
    done
    # Where sysfs described this disk its answer is authoritative, including
    # "no such partition"; falling through on a gap in the numbering would let
    # the positional lookup return a neighbouring partition.
    if [ "${sysfs_seen}" = "1" ];then
        return 1
    fi
    # Upstream positional lookup, for whole-disk devices sysfs does not
    # enumerate this way (device mapper, fake raid)
    for partnode in $(
        { lsblk -p -l -o NAME,TYPE,START -x START "${disk}" 2>/dev/null ||\
        lsblk -p -l -o NAME,TYPE "${disk}"; } |\
        grep -E "part|md$" | cut -f1 -d ' '
    );do
        if [ "${index}" = "${partid}" ];then
            echo "${partnode}"
            return 0
        fi
        index=$((index + 1))
    done
    return 1
}
EOF

if [ ! -f "$KIWI_PARTLIB" ]; then
  echo "WARNING: $KIWI_PARTLIB not found; leaving upstream get_partition_node_name"
elif grep -q 'BUGFIX: resolve by partition number' "$KIWI_PARTLIB"; then
  echo "kiwi get_partition_node_name already patched"
elif ! grep -q '^function get_partition_node_name {$' "$KIWI_PARTLIB"; then
  echo "WARNING: get_partition_node_name not in expected form; leaving upstream behaviour"
else
  KIWI_PARTLIB_TMP=$(mktemp)
  awk -v newfn="$KIWI_GPNN_NEW" '
    $0 == "function get_partition_node_name {" {
      while ((getline line < newfn) > 0) print line
      close(newfn)
      skip = 1
      next
    }
    skip && $0 == "}" { skip = 0; next }
    skip { next }
    { print }
  ' "$KIWI_PARTLIB" >"$KIWI_PARTLIB_TMP"

  # A botched splice breaks the initrd and the box does not boot, so validate
  # before it goes anywhere near the image.
  if ! bash -n "$KIWI_PARTLIB_TMP"; then
    echo "ERROR: patched $KIWI_PARTLIB does not parse"
    exit 1
  fi
  cat "$KIWI_PARTLIB_TMP" >"$KIWI_PARTLIB"
  rm -f "$KIWI_PARTLIB_TMP"
  echo "Patched kiwi get_partition_node_name: positional -> by partition number"
fi
rm -f "$KIWI_GPNN_NEW"

echo "Creating FBOSS log directories..."
mkdir -p /var/facebook/logs/fboss/sdk
mkdir -p /var/facebook/logs/fboss/archive
semanage fcontext -a -t var_log_t '/var/facebook/logs/fboss(/.*)?'
restorecon -Rv /var/facebook/logs/fboss

# Create the coop user/group and /etc/coop directory at image-build time so
# every image ships with consistent ownership, rather than relying on
# fboss_init.sh to set it up (with default root-owned permissions) at first
# boot. Must run before RPM/component installs below: some packages (e.g.
# bgpd) install files into /etc/coop but don't own the directory itself, so
# they won't touch its permissions once it already exists.
#
# Individual operators (all members of "switching") run `fboss2-dev config`
# commands that create files/subdirs directly under /etc/coop as themselves,
# with no privilege elevation. The setgid bit makes new entries inherit the
# switching group, but the *write* bit on newly created files/dirs still
# depends on each operator's ambient umask (e.g. a 022 umask yields
# non-group-writable 0755 dirs / 0644 files, locking other operators out). A
# default ACL forces group rwx on every new file/dir regardless of umask, and
# is itself inherited by new subdirectories, so it cascades indefinitely.
echo "Creating coop user/group and /etc/coop directory..."
# Pin gid 1001: without -g the gid floats with creation order (kiwi
# pre-created it at 1001 when netops listed it; alone, -r yields a
# system gid instead), and the netadmin container matches it by number.
groupadd -f -r -g 1001 switching
useradd -r -g switching -s /sbin/nologin -M -d /nonexistent coop
mkdir -p /etc/coop
chown -R coop:switching /etc/coop
chmod 2775 /etc/coop
setfacl -R -m g:switching:rwx /etc/coop
setfacl -R -d -m g:switching:rwx -m o::rx /etc/coop

# Default to python 3.12, also simulate the Debian python-is-python3 package
update-alternatives --install /usr/bin/python python /usr/bin/python3.12 1
update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 1
# Fixup firewalld and its CLIs to definitively use the original system python
# like the rest of the system tools and services.
for fwbin in /usr/sbin/firewalld /usr/bin/firewall-cmd /usr/bin/firewall-offline-cmd; do
  [ -f "$fwbin" ] && sed -i '1s#bin/python3 #bin/python3.9 #' "$fwbin"
done

# All dnf invocations with an invalid RPM repo configured will fail. Create the
# metadata for the local_rpm_repo now to prevent that.
createrepo /usr/local/share/local_rpm_repo

# Process component artifacts and install RPMs
#
# Component artifacts are copied to /repos/<component_name>/
# Each component is processed in isolation to avoid conflicts

process_kernel() {
  local component_dir=$1
  local component_tmp=$2

  echo "  Processing kernel component..."

  # Discover kernel tarballs (nullglob is enabled in the caller loop)
  local tarballs=("$component_dir"/*.tar*)

  if [ ${#tarballs[@]} -eq 0 ]; then
    echo "  WARNING: no kernel tarballs in $component_dir; the image will have no kernel RPMs"
    return 0
  fi

  if [ ${#tarballs[@]} -gt 1 ]; then
    echo "ERROR: Found multiple kernel tarballs in $component_dir:"
    for tb in "${tarballs[@]}"; do
      echo "  - $(basename "$tb")"
    done
    echo "Only a single kernel tarball is supported"
    return 1
  fi

  local tarball="${tarballs[0]}"

  echo "  Extracting $(basename "$tarball") (excluding devel/header/source RPMs)..."
  tar -xf "$tarball" -C "$component_tmp" \
    --exclude='*-devel-*.rpm' \
    --exclude='*-headers-*.rpm' \
    --exclude='*.src.rpm'

  # Copy any unarchived RPMs that may already be in the component directory.
  # Test the glob via an array: under nullglob an `ls "$dir"/*.rpm` guard
  # succeeds when there are no matches, because ls falls back to listing the
  # working directory.
  local loose_rpms=("$component_dir"/*.rpm)
  if [ ${#loose_rpms[@]} -gt 0 ]; then
    cp "${loose_rpms[@]}" "$component_tmp/"
  fi

  local rpms=("$component_tmp"/*.rpm)
  if [ ${#rpms[@]} -gt 0 ]; then
    echo "  Installing kernel RPMs..."
    dnf install --disablerepo=* -y "${rpms[@]}"
  else
    echo "  WARNING: no kernel RPMs after extracting $(basename "$tarball")"
  fi

  return 0
}

process_npu_sai_tarball() {
  local component_dir=$1
  local component_name
  component_name=$(basename "$component_dir")

  # Discover SAI tarballs
  local tarballs=("$component_dir"/*.tar*)

  if [ ${#tarballs[@]} -eq 0 ]; then
    echo "  WARNING: no SAI tarballs in $component_dir; SAI kmods will not be installed"
    return 0
  fi

  if [ ${#tarballs[@]} -gt 1 ]; then
    echo "ERROR: Found multiple SAI tarballs in $component_dir:"
    for tb in "${tarballs[@]}"; do
      echo "  - $(basename "$tb")"
    done
    echo "Only a single SAI tarball is supported"
    return 1
  fi

  local tarball="${tarballs[0]}"

  # Two shapes are published. The SDK vendors ship a sai-runtime RPM inside a
  # tarball; the periodic kmod builds ship the .ko files directly, already
  # laid out under lib/modules/<kver>/extra/<vendor>/.
  #
  # The RPM name is matched with a wildcard so it can carry its version --
  # sai-runtime-14.2.0-1.xgs_6_5_34.x86_64.rpm -- which is what makes
  # `rpm -qi sai-runtime` on a switch report which SDK the image was built with.
  if tar tf "$tarball" | grep -qE '^(\./)?sai-runtime.*\.rpm$'; then
    echo "  Extracting sai-runtime RPM from $(basename "$tarball")..."
    tar -xf "$tarball" -C "$component_dir" --wildcards '*sai-runtime*.rpm'

    local rpm
    rpm=$(find "$component_dir" -name 'sai-runtime*.rpm' -print -quit)
    if [ -z "$rpm" ]; then
      echo "ERROR: sai-runtime RPM listed in $(basename "$tarball") but not extracted"
      return 1
    fi

    echo "  Installing $rpm..."
    if ! dnf install -y "$rpm"; then
      echo "ERROR: Failed to install $rpm"
      return 1
    fi
  elif tar tf "$tarball" | grep -qE '^(\./)?lib/modules/.*\.ko$'; then
    echo "  Installing SAI kmods from $(basename "$tarball")..."
    tar -xf "$tarball" -C / './lib' 2>/dev/null || tar -xf "$tarball" -C / 'lib'

    # The kmods land in a search path but are not in modules.dep until depmod
    # runs, so modprobe cannot find them at boot without this.
    local kver
    kver=$(tar tf "$tarball" | sed -nE 's|^(\./)?lib/modules/([^/]+)/.*|\2|p' | head -1)
    if [ -n "$kver" ]; then
      echo "  Running depmod for $kver..."
      depmod -a "$kver"
    else
      echo "  WARNING: could not determine kernel version; skipping depmod"
    fi
  else
    echo "  WARNING: $(basename "$tarball") has neither sai-runtime.rpm nor"
    echo "           lib/modules/**/*.ko; SAI kmods will not be installed"
  fi

  rm -f "$tarball"
  return 0

}

install_component_rpms() {
  local component_dir=$1
  local component_name
  component_name=$(basename "$component_dir")

  echo "  Installing RPMs for $component_name..."
  dnf install -y "$component_dir"/*.rpm

  return $?
}

echo "Processing component artifacts..."

shopt -s nullglob
for component_dir in /repos/*; do
  [ -d "$component_dir" ] || continue
  component_name=$(basename "$component_dir")

  handler_rc=0

  case "$component_name" in
  kernel)
    echo "Processing component: $component_name"

    # Create temporary directory for this component
    component_tmp=$(mktemp -d)

    process_kernel "$component_dir" "$component_tmp"
    handler_rc=$?

    # Clean up temp directory
    rm -rf "$component_tmp"
    ;;

  npu_sai | phy_sai)
    process_npu_sai_tarball "$component_dir"
    handler_rc=$?
    ;;

  other_dependencies)
    install_component_rpms "$component_dir"
    handler_rc=$?
    ;;

  bsps)
    echo "Processing component: $component_name"
    for bsp in $component_dir/*.tar*; do
      echo "Processing BSP: $bsp"
      tar -C /usr/local/share/local_rpm_repo -xf "$bsp"
    done
    createrepo /usr/local/share/local_rpm_repo
    ;;

  fboss-forwarding-stack | fboss-platform-stack)
    echo "Processing component: $component_name"
    tarballs=("$component_dir"/*.tar*)
    if [ ${#tarballs[@]} -eq 0 ]; then
      echo "  WARNING: no $component_name tarball in $component_dir; those binaries will be absent from /opt/fboss"
    else
      # A component may carry more than one tarball: manifests that list the
      # forwarding stack per service (agent, fsdb, qsfp, fboss2) stage them all
      # into this one directory. Extract every one -- they unpack into disjoint
      # paths under /opt/fboss.
      mkdir -p /opt/fboss
      for tarball in "${tarballs[@]}"; do
        echo "  Extracting $(basename "$tarball")..."
        tar -C /opt/fboss -xf "$tarball"
      done
    fi
    ;;

  *)
    # Anything under /repos was put there because a manifest declared it, so
    # a component with no handler is a manifest/handler mismatch rather than
    # something to ignore. Failing here turns a silently incomplete image
    # into a build error: an unhandled component previously logged this line
    # and exited 0, and the image shipped without it.
    echo "ERROR: no handler defined for component: $component_name"
    handler_rc=1
    ;;
  esac

  # Exit if handler failed
  if [ $handler_rc -ne 0 ]; then
    echo "ERROR: Failed to process $component_name"
    exit 1
  fi
done
shopt -u nullglob

# Define paths
#
# Detect the installed kernel version from the boot directory
# shellcheck disable=SC2012
VMLINUZ_PATH=$(ls /boot/vmlinuz-* 2>/dev/null | head -n 1)
if [ -z "$VMLINUZ_PATH" ]; then
  echo "Error: No vmlinuz found in /boot"
  exit 1
fi

KERNEL_VERSION=$(basename "$VMLINUZ_PATH" | sed 's/^vmlinuz-//')
# Detect the initrd path from the boot directory
# shellcheck disable=SC2012
INITRD_PATH=$(ls /boot/initramfs-* 2>/dev/null | head -n 1)
if [ -z "$INITRD_PATH" ]; then
  echo "Error: No initramfs found in /boot"
  exit 1
fi

echo "Detected kernel version: ${KERNEL_VERSION}"
echo "Vmlinuz path: ${VMLINUZ_PATH}"
echo "Initrd path: ${INITRD_PATH}"

# Manually run dracut to create the initrd
#    --force is needed to overwrite any existing file
#    --kver specifies the kernel version to build for
echo "Running dracut manually..."
dracut --force --kver "${KERNEL_VERSION}" "${INITRD_PATH}"

# Run kernel-install for grub config
#
# This wipes ALL interfering variables set by kiwi-ng
# and runs kernel-install in a "sterile" environment.
env -i \
  PATH="/usr/bin:/usr/sbin:/bin:/sbin" \
  kernel-install add "${KERNEL_VERSION}" "${VMLINUZ_PATH}" --initrd-file "${INITRD_PATH}"
echo "Custom kernel ${KERNEL_VERSION} install complete."

# Use system GRUB 2.06 from packages
#
# The grub2-efi-x64 package already provides grubx64.efi with all necessary modules
# We just need to make sure the btrfs module is accessible on the EFI partition
echo "Using system GRUB 2.06 from grub2-efi-x64 package..."

# Clean up any old GRUB module directories from any previous builds
# (The grub2-efi-x64 package creates /boot/grub2/ and /boot/efi/EFI/ but not the module directories)
echo "Cleaning up old GRUB module directories..."
rm -rf /boot/grub/x86_64-efi /boot/grub2/x86_64-efi
echo "Old GRUB module directories cleaned"

# Copy GRUB modules to multiple locations
# EFI partition structure: (hd0,gpt2)/efi/centos/x86_64-efi/ and (hd0,gpt2)/efi/boot/x86_64-efi/
# During build, EFI partition is mounted at /boot/efi/, so:
#   (hd0,gpt2)/efi/centos/x86_64-efi/ -> /boot/efi/efi/centos/x86_64-efi/
#   (hd0,gpt2)/efi/boot/x86_64-efi/ -> /boot/efi/efi/boot/x86_64-efi/
#
# Essential modules needed for early boot (to find and mount btrfs root) dependency tree:
#   btrfs -> zstd, lzopio, extcmd, raid6rec, gzio
#   lzopio -> crypto
#   raid6rec -> diskfilter
#   gzio -> gcry_crc, crypto
ESSENTIAL_MODULES="btrfs.mod zstd.mod lzopio.mod extcmd.mod raid6rec.mod gzio.mod crypto.mod diskfilter.mod gcry_crc.mod"

# Copy essential modules to EFI partition locations
for efi_dir in /boot/efi/efi/centos/x86_64-efi /boot/efi/efi/boot/x86_64-efi; do
  mkdir -p "$efi_dir"
  for mod in $ESSENTIAL_MODULES; do
    cp /usr/lib/grub/x86_64-efi/$mod "$efi_dir/" 2>/dev/null || echo "Warning: $mod not found"
  done
  echo "Copied essential GRUB modules to $efi_dir (EFI partition)"
done

# Copy ALL modules to root partition (plenty of space there)
mkdir -p /boot/grub2/x86_64-efi
cp -r /usr/lib/grub/x86_64-efi/* /boot/grub2/x86_64-efi/
echo "Copied all GRUB modules to /boot/grub2/x86_64-efi/ (root partition)"

# Enable systemd services
echo "Enabling FBOSS systemd services..."
# Ships in sai-runtime.rpm (npu_sai component). The vendor spec is meant to
# enable it from %post, but not every SDK drop does -- 14.2.0 carries no
# scriptlets at all -- so enable it here as well. systemctl enable is
# idempotent, so this is harmless when the RPM does self-enable.
# The unit runs Before=sysinit.target, putting the BDE kmods and /dev nodes in
# place before platform_manager and the agents. Absent for manifests with an
# empty npu_sai (e.g. kernel_only.json), hence the guard.
systemctl enable sai-device-nodes.service ||
  echo "WARNING: sai-device-nodes.service not present; SAI kmods will not be loaded at boot"
systemctl enable fboss_init.service
systemctl enable local_rpm_repo.service
systemctl enable platform_manager.service
systemctl enable data_corral_service.service
systemctl enable fan_service.service
systemctl enable sensor_service.service
systemctl enable fsdb.service
systemctl enable qsfp_service.service
systemctl enable led_service.service
systemctl enable fboss_sw_agent.service
systemctl enable fboss_hw_agents.target
# Normally enabled by systemd preset; enabled explicitly so FBOSS log rotation
# does not depend on preset behaviour in the image build.
systemctl enable logrotate.timer

# firewalld's default public zone allows only ssh/dhcpv6-client/cockpit
# and rejects everything else. Enable the "fboss" service (thrift ports) and
# the fboss-forward policy (transit) for the default zone here so the ports are
# reachable off-box and transit traffic is not rejected.
echo "Configuring firewalld for FBOSS..."
if ! firewall-offline-cmd --zone=public --add-service=fboss; then
  echo "config.sh: FATAL: could not add the fboss service to the public firewalldzone" >&2
  exit 1
fi

# Fix NetworkManager connection profile permissions
# NM ignores profiles that are world-readable
chmod 600 /etc/NetworkManager/system-connections/eth0.nmconnection

# Done! Cleanup and install additional packages
echo "Cleaning up /repos directory..."
rm -rf /repos

JQ_INSTALLED=false
# Ensure jq is installed (needed to parse JSON)
if ! rpm -q jq >/dev/null 2>&1; then
  dnf install -y jq
  JQ_INSTALLED=true
fi

# Install additional packages from after_pkgs input file user "may" have passed in
if [ -f /var/tmp/after_pkgs_install_file.json ]; then

  echo "Processing after_pkgs_install JSON file..."
  EXTRA_PACKAGES=$(jq -r '.packages[]' /var/tmp/after_pkgs_install_file.json)

  if [ -n "$EXTRA_PACKAGES" ]; then
    mapfile -t PACKAGE_ARRAY <<<"$EXTRA_PACKAGES"
    echo "Installing packages: ${PACKAGE_ARRAY[*]}"
    dnf install -y --setopt=install_weak_deps=False "${PACKAGE_ARRAY[@]}"

    # Clean up, don't leave traces of the user's JSON file in the image
    rm -f /var/tmp/after_pkgs_install_file.json
  else
    echo "No extra packages specified in after_pkgs_install JSON file"
  fi
else
  echo "No after_pkgs_install JSON file"
fi

# Execute additional commands from after_pkgs_execute_file.json user "may" have passed in
if [ -f /var/tmp/after_pkgs_execute_file.json ]; then

  echo "Processing after_pkgs_execute JSON file..."
  EXTRA_COMMANDS=$(jq -r '.execute[]' /var/tmp/after_pkgs_execute_file.json)

  if [ -n "$EXTRA_COMMANDS" ]; then
    # Get the number of commands in the execute array
    NUM_COMMANDS=$(jq -r '.execute | length' /var/tmp/after_pkgs_execute_file.json)

    for ((i = 0; i < NUM_COMMANDS; i++)); do
      # Extract the command array for this index
      CMD_ARRAY=$(jq -r ".execute[$i] | @sh" /var/tmp/after_pkgs_execute_file.json)

      echo "Executing command $((i + 1))/$NUM_COMMANDS: $CMD_ARRAY"

      # Execute the command using eval to properly handle the shell-quoted array
      eval "$CMD_ARRAY"

      if [ $? -ne 0 ]; then
        echo "Warning: Command $((i + 1)) failed with exit code != 0"
      fi
    done

    # Clean up, don't leave traces of the user's JSON file in the image
    rm -f /var/tmp/after_pkgs_execute_file.json
  else
    echo "No commands specified in after_pkgs_execute JSON file"
  fi
fi

# Clean up, remove jq if we installed it
if [ "$JQ_INSTALLED" = true ]; then
  dnf remove -y jq
fi

# Make all yum repos optional. The network is not available in all deployments or early in boot. The CentOS default is
# to fail out if any RPM repo is not available, but that can cause platform_manager to fail to install the BSP RPM and
# thus fail booting.
sed -i 's/skip_if_unavailable=False/skip_if_unavailable=True/' /etc/dnf/dnf.conf

exit 0
