#!/bin/bash
set -e

echo "=== Running editbootinstall.sh ==="
echo "Working directory: $(pwd)"
echo "Script arguments: $*"

# Script arguments:
# $1 = disk image file (e.g., /distro_infra/output/FBOSS-Distro-Image.x86_64-1.0.raw)
# $2 = root device (e.g., /dev/loop17p3)

DISK_IMAGE="$1"
ROOT_DEV="$2"

if [ -z "$ROOT_DEV" ]; then
  echo "ERROR: Root device not provided as argument"
  echo "Script arguments: $*"
  exit 1
fi

echo "Disk image: $DISK_IMAGE"
echo "Root device: $ROOT_DEV"

# Get the filesystem UUID from the root device
ROOT_UUID=$(blkid -s UUID -o value "$ROOT_DEV" 2>/dev/null || true)

if [ -z "$ROOT_UUID" ]; then
  echo "ERROR: Cannot get filesystem UUID from device $ROOT_DEV"
  blkid "$ROOT_DEV"
  exit 1
fi

echo "Root filesystem UUID: $ROOT_UUID"

# Find the EFI partition device
# Root device is like /dev/loop17p3, EFI partition is likely p2
# Extract the base device and partition number
if [[ $ROOT_DEV =~ ^(.+)p([0-9]+)$ ]]; then
  BASE_DEV="${BASH_REMATCH[1]}"
  ROOT_PART_NUM="${BASH_REMATCH[2]}"
  EFI_DEV="${BASE_DEV}p2" # EFI partition is typically p2
  echo "Base device: $BASE_DEV"
  echo "Root partition number: $ROOT_PART_NUM"
  echo "EFI device: $EFI_DEV"
else
  echo "ERROR: Cannot parse root device: $ROOT_DEV"
  exit 1
fi

# Verify EFI device exists
if [ ! -b "$EFI_DEV" ]; then
  echo "ERROR: EFI device $EFI_DEV does not exist"
  ls -la /dev/loop* || true
  exit 1
fi

# Create temporary mount point
MOUNT_POINT=$(mktemp -d)
echo "Created temporary mount point: $MOUNT_POINT"

# Cleanup function
cleanup() {
  echo "Cleaning up..."
  if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT" || true
  fi
  rmdir "$MOUNT_POINT" || true
}

trap cleanup EXIT

# Mount the EFI partition
echo "Mounting EFI partition $EFI_DEV to $MOUNT_POINT..."
mount -t vfat "$EFI_DEV" "$MOUNT_POINT"

# Verify the EFI directory structure exists
if [ ! -d "$MOUNT_POINT/EFI/BOOT" ]; then
  echo "ERROR: EFI/BOOT directory not found in mounted partition"
  ls -la "$MOUNT_POINT" || true
  exit 1
fi

echo "EFI partition mounted successfully"

# $prefix is left on the ESP so save_env can write grubenv there; GRUB cannot
# write files on the btrfs /boot. config.sh puts grubenv and all modules there.
if [ ! -s "$MOUNT_POINT/EFI/centos/grubenv" ]; then
  echo "ERROR: EFI/centos/grubenv missing from the ESP"
  ls -la "$MOUNT_POINT/EFI/centos" || true
  exit 1
fi

# --root-dev-only limits the search to $root's disk (the ESP's), as RHEL's own
# stub does. grub2-common's %posttrans leaves a stub containing it alone;
# otherwise an update regenerates EFI/centos/grub.cfg and moves grubenv back.
# Rewrite EFI/BOOT/grub.cfg
BOOT_GRUB_CFG="$MOUNT_POINT/EFI/BOOT/grub.cfg"
echo "Rewriting $BOOT_GRUB_CFG..."
cat >"$BOOT_GRUB_CFG" <<EOF
set efipart=\$root
set prefix=(\$efipart)/efi/boot
insmod btrfs
set btrfs_relative_path="yes"
search --root-dev-only --fs-uuid --set=root $ROOT_UUID
set prefix=(\$efipart)/efi/centos
configfile (\$root)/boot/grub2/grub.cfg
EOF

echo "✓ Updated $BOOT_GRUB_CFG"
echo "Contents:"
cat "$BOOT_GRUB_CFG"
echo ""

# Rewrite EFI/centos/grub.cfg
if [ -d "$MOUNT_POINT/EFI/centos" ]; then
  CENTOS_GRUB_CFG="$MOUNT_POINT/EFI/centos/grub.cfg"
  echo "Rewriting $CENTOS_GRUB_CFG..."
  cat >"$CENTOS_GRUB_CFG" <<EOF
set efipart=\$root
set prefix=(\$efipart)/efi/centos
insmod btrfs
set btrfs_relative_path="yes"
search --root-dev-only --fs-uuid --set=root $ROOT_UUID
configfile (\$root)/boot/grub2/grub.cfg
EOF

  echo "✓ Updated $CENTOS_GRUB_CFG"
  echo "Contents:"
  cat "$CENTOS_GRUB_CFG"
  echo ""
fi

# Sync to ensure changes are written to disk
sync
sync

echo "=== editbootinstall.sh completed successfully ==="
exit 0
