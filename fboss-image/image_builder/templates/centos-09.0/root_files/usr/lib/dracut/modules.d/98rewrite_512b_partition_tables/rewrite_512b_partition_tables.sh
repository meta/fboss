#!/bin/bash
# Sourced by dracut in the kiwi installer initrd, after kiwi-dump-image.sh has
# written the 4096-byte-sector disk image to ${image_target}.
#
# On a 512-byte disk only the GPT is wrong: it sits at byte 4096 in 4K LBAs,
# where firmware and the kernel look for it at byte 512. Partition contents
# are byte addressed and need nothing; the ESP's 4K-sector FAT and the btrfs
# root both mount on 512-byte devices. Rewrite the GPT in 512-byte LBAs.

type report_and_quit >/dev/null 2>&1 || . /lib/kiwi-dialog-lib.sh

PT512B_WORK=/run/rewrite_512b_partition_tables

pt512b_fail() {
  echo "rewrite_512b_partition_tables: ERROR: $*" >&2
  return 1
}

# True if a GPT header signature sits at byte offset $2 of $1.
pt512b_gpt_at() {
  [ "$(dd if="$1" bs=4096 iflag=skip_bytes,count_bytes skip="$2" count=8 2>/dev/null |
    tr -d '\000')" = "EFI PART" ]
}

# Rewrite an `sfdisk -d` dump of 4K LBAs as an sfdisk script in 512-byte
# LBAs. Partition lines lose their device name, so numbering must already be
# contiguous from 1.
#
# Sample input, with $1 = /dev/loop3:
#   label: gpt
#   label-id: AE3BF39E-A13D-7E4C-BAE2-BA1D99E2C4C9
#   device: /dev/loop3
#   unit: sectors
#   first-lba: 256
#   last-lba: 524282
#   sector-size: 4096
#
#   /dev/loop3p1 : start=         256, size=         512, type=21686148-6449-6E6F-744E-656564454649, uuid=40BA4B40-CE01-EE44-B320-0E9EBCF5BAF3, name="p.legacy"
#   /dev/loop3p2 : start=         768, size=       16384, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, uuid=8B035C6E-A8F4-C84F-93F3-A76717595580, name="p.UEFI"
#   /dev/loop3p3 : start=       17152, size=      290043, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, uuid=71049751-4054-7340-8F0D-E69934E6F0AB, name="p.lxroot"
#
# which becomes label/label-id plus lines such as
#   start=2048, size=4096, type=21686148-6449-6E6F-744E-656564454649, uuid=40BA4B40-CE01-EE44-B320-0E9EBCF5BAF3, name="p.legacy"
pt512b_scale_table() {
  awk -v dev="$1" '
    function scale(line, key,    v) {
      if (!match(line, key "= *[0-9]+")) { bad = 1; return line }
      v = substr(line, RSTART, RLENGTH)
      sub(key "= *", "", v)
      return substr(line, 1, RSTART - 1) key "=" (v * 8) substr(line, RSTART + RLENGTH)
    }
    /^(label|label-id):/ { print; next }
    index($0, dev) == 1 {
      if (substr($1, length(dev) + 1) !~ /^p?[0-9]+$/) { bad = 1 }
      partno = substr($1, length(dev) + 1)
      sub(/^p/, "", partno)
      if (partno != ++n) { bad = 1 }
      line = substr($0, index($0, ":") + 1)
      sub(/^ +/, "", line)
      print scale(scale(line, "start"), "size")
    }
    END { exit bad || !n }
  '
}

rewrite_512b_partition_tables() {
  declare image_target=${image_target}
  local disk
  local lbs
  local alt_lba
  local loop
  local part
  local found_btrfs=0

  [ -n "${image_target}" ] || return 0
  disk=$(readlink -f "${image_target}")
  [ -b "${disk}" ] || return 0
  read -r lbs <"/sys/class/block/${disk##*/}/queue/logical_block_size"
  [ "${lbs}" = "512" ] || return 0
  if pt512b_gpt_at "${disk}" 512; then
    echo "rewrite_512b_partition_tables: ${disk} already has a 512-byte GPT, nothing to convert"
    return 0
  fi
  pt512b_gpt_at "${disk}" 4096 ||
    pt512b_fail "no 4K GPT found on ${disk} after image dump" || return 1
  if getargbool 0 rd.kiwi.install.retain_last ||
    getargbool 0 rd.kiwi.install.retain_past; then
    pt512b_fail "rd.kiwi.install.retain_* is not supported on 512-byte disks" || return 1
  fi

  echo "rewrite_512b_partition_tables: converting 4K image layout on 512-byte disk ${disk}"

  # The primary header records its backup's LBA at offset 32; the backup and
  # its 4 LBAs of entries end the image.
  alt_lba=$(od -An -tu8 --endian=little -j $((4096 + 32)) -N 8 "${disk}" | tr -d ' ')
  if [ $(((alt_lba + 1) * 4096)) -gt "$(blockdev --getsize64 "${disk}")" ]; then
    pt512b_fail "implausible backup GPT LBA ${alt_lba}" || return 1
  fi

  # A read-only 4K loop view of the disk lets sfdisk read the 4K table.
  mkdir -p "${PT512B_WORK}"
  loop=$(losetup -f --show -r -b 4096 "${disk}") ||
    pt512b_fail "cannot create 4K loop view of ${disk}" || return 1
  if ! sfdisk -d "${loop}" >"${PT512B_WORK}/table4k"; then
    losetup -d "${loop}"
    pt512b_fail "cannot read 4K partition table" || return 1
  fi
  losetup -d "${loop}"
  pt512b_scale_table "${loop}" \
    <"${PT512B_WORK}/table4k" >"${PT512B_WORK}/table512" ||
    pt512b_fail "cannot read a contiguous 4K partition table" || return 1

  # Clear the 4K primary GPT (4K LBAs 1-5, all before the first partition) and
  # its backup at the end of the image, so nothing finds a stale table.
  dd if=/dev/zero of="${disk}" bs=4096 seek=1 count=5 conv=notrunc status=none &&
    dd if=/dev/zero of="${disk}" bs=4096 seek=$((alt_lba - 4)) count=5 \
      conv=notrunc status=none ||
    pt512b_fail "cannot clear 4K GPT" || return 1

  sfdisk --quiet --wipe never --wipe-partitions never "${disk}" \
    <"${PT512B_WORK}/table512" ||
    pt512b_fail "cannot write 512-byte partition table" || return 1
  udevadm settle
  pt512b_gpt_at "${disk}" 512 ||
    pt512b_fail "512-byte GPT not found after write" || return 1

  for part in /sys/class/block/"${disk##*/}"/*/partition; do
    part=${part%/partition}
    [ "$(blkid -p -s TYPE -o value "/dev/${part##*/}")" = "btrfs" ] && found_btrfs=1
  done
  [ "${found_btrfs}" = "1" ] ||
    pt512b_fail "no btrfs partition visible after conversion" || return 1

  echo "rewrite_512b_partition_tables: ${disk} converted to 512-byte layout"
}

if ! rewrite_512b_partition_tables; then
  report_and_quit "Failed to adapt the installed image to the 512-byte-sector disk"
fi
