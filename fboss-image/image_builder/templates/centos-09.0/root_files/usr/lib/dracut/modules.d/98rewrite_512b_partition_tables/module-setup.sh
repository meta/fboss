#!/bin/bash

# called by dracut
check() {
  # Only included when asked for, via <installmedia> in config.xml, so it
  # lands in the kiwi installer initrd and never in the installed system's.
  return 255
}

# called by dracut
depends() {
  echo kiwi-dump
  return 0
}

# called by dracut
installkernel() {
  instmods loop
}

# called by dracut
install() {
  # shellcheck disable=SC2154
  declare moddir=${moddir}
  inst_multiple \
    awk blkid blockdev cat dd losetup mkdir od readlink sfdisk tr udevadm

  # Between kiwi-dump-image.sh (pre-mount 30), which writes the image, and
  # kiwi-dump-reboot (pre-mount 40), which kexecs into it.
  inst_hook pre-mount 35 "${moddir}/rewrite_512b_partition_tables.sh"
}
