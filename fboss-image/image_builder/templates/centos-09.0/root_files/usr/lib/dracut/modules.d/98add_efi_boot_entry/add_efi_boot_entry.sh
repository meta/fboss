#!/bin/bash
# Registers the installed system with the UEFI firmware. kiwi-dump 10.3 does
# not create a boot entry, so after a disk wipe the installed system is not
# bootable. The entry goes after any network boot entries so PXE re-installs
# keep priority.

type report_and_quit >/dev/null 2>&1 || . /lib/kiwi-dialog-lib.sh

EFIENTRY_LABEL=CentOS
EFIENTRY_LOADER='\EFI\centos\shimx64.efi'
EFIENTRY_ESP_MNT=/run/add_efi_boot_entry
EFIENTRY_EFIVARS=/sys/firmware/efi/efivars

efientry_fail() {
  echo "add_efi_boot_entry: ERROR: $*" >&2
  return 1
}

# Print the ESP partition on disk $1: the one whose filesystem label is EFI.
efientry_find_esp() {
  local part
  for part in /sys/class/block/"${1##*/}"/*/partition; do
    part=${part%/partition}
    if [ "$(blkid -p -s LABEL -o value "/dev/${part##*/}")" = "EFI" ]; then
      echo "/dev/${part##*/}"
      return 0
    fi
  done
  return 1
}

# True if ${EFIENTRY_LOADER} exists on the ESP $1.
efientry_loader_present() {
  local found=1
  mkdir -p "${EFIENTRY_ESP_MNT}"
  mount -o ro -t vfat "$1" "${EFIENTRY_ESP_MNT}" || return 1
  [ -f "${EFIENTRY_ESP_MNT}${EFIENTRY_LOADER//\\//}" ] && found=0
  umount "${EFIENTRY_ESP_MNT}" ||
    efientry_fail "cannot unmount $1 from ${EFIENTRY_ESP_MNT}" || return 1
  rmdir "${EFIENTRY_ESP_MNT}"
  return "${found}"
}

# Print the boot numbers of the entries labeled $1. efibootmgr prints
# "Boot0004* CentOS", newer versions followed by a tab and the device path.
efientry_bootnums() {
  efibootmgr | awk -v label="$1" '
    /^Boot[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][* ] / {
      name = substr($0, 11)
      sub(/\t.*/, "", name)
      if (name == label) print substr($0, 5, 4)
    }'
}

# Print BootOrder with entry $1 moved right after the last network boot entry
# (a device path through a MAC address), or to the front if there is none.
efientry_boot_order() {
  efibootmgr -v | awk -v new="$1" '
    /^BootOrder: / { n = split($2, order, ","); next }
    /^Boot[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][* ] / && /MAC\(/ {
      net[substr($0, 5, 4)] = 1
    }
    END {
      last = 0
      for (i = 1; i <= n; i++) if (order[i] in net) last = i
      out = (last == 0) ? new : ""
      for (i = 1; i <= n; i++) {
        if (order[i] == new) continue
        out = out (out == "" ? "" : ",") order[i]
        if (i == last) out = out "," new
      }
      print out
    }'
}

add_efi_boot_entry() {
  declare image_target=${image_target}
  local disk
  local esp
  local partno
  local bootnum
  local stale
  local new
  local order

  [ -n "${image_target}" ] || return 0
  if [ ! -d /sys/firmware/efi ]; then
    echo "add_efi_boot_entry: not booted via UEFI, no boot entry to add"
    return 0
  fi
  disk=$(readlink -f "${image_target}")
  [ -b "${disk}" ] || efientry_fail "${image_target} is not a block device" || return 1

  mountpoint -q "${EFIENTRY_EFIVARS}" ||
    mount -t efivarfs efivarfs "${EFIENTRY_EFIVARS}" ||
    efientry_fail "cannot mount efivarfs" || return 1

  # kiwi-dump writes the image with dd and does not reread the partition
  # table, so the kernel may still hold the pre-dump one.
  blockdev --rereadpt "${disk}" ||
    efientry_fail "cannot reread the partition table of ${disk}" || return 1
  udevadm settle

  esp=$(efientry_find_esp "${disk}") ||
    efientry_fail "no partition labeled EFI on ${disk}" || return 1
  read -r partno <"/sys/class/block/${esp##*/}/partition"
  efientry_loader_present "${esp}" ||
    efientry_fail "${EFIENTRY_LOADER} not found on ${esp}" || return 1

  # Create before deleting, so a failed create leaves the old entry usable.
  stale=" $(efientry_bootnums "${EFIENTRY_LABEL}" | tr '\n' ' ')"
  efibootmgr -q -c -d "${disk}" -p "${partno}" \
    -L "${EFIENTRY_LABEL}" -l "${EFIENTRY_LOADER}" ||
    efientry_fail "cannot create the ${EFIENTRY_LABEL} entry" || return 1

  new=""
  for bootnum in $(efientry_bootnums "${EFIENTRY_LABEL}"); do
    case "${stale}" in
    *" ${bootnum} "*) ;;
    *) new="${new}${new:+ }${bootnum}" ;;
    esac
  done
  case "${new}" in
  ????) ;;
  *) efientry_fail "expected one new ${EFIENTRY_LABEL} entry, found: ${new:-none}" || return 1 ;;
  esac

  for bootnum in ${stale}; do
    efibootmgr -q -b "${bootnum}" -B ||
      efientry_fail "cannot delete stale entry Boot${bootnum}" || return 1
  done
  bootnum=${new}
  order=$(efientry_boot_order "${bootnum}")
  efibootmgr -q -o "${order}" ||
    efientry_fail "cannot set BootOrder ${order}" || return 1

  echo "add_efi_boot_entry: Boot${bootnum} ${EFIENTRY_LABEL} -> ${esp} ${EFIENTRY_LOADER}, BootOrder ${order}"
}

if ! add_efi_boot_entry; then
  report_and_quit "Failed to add the UEFI boot entry for the installed system"
fi
