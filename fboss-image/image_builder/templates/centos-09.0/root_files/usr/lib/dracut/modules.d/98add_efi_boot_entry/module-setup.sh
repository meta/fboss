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
  instmods efivarfs vfat nls_cp437 nls_iso8859_1
}

# called by dracut
install() {
  # shellcheck disable=SC2154
  declare moddir=${moddir}
  inst_multiple \
    awk blkid blockdev cat efibootmgr mkdir mount mountpoint readlink rmdir \
    tr udevadm umount

  # Before kiwi-dump-reboot (pre-mount 40), whose kexec bypasses the firmware.
  inst_hook pre-mount 36 "${moddir}/add_efi_boot_entry.sh"
}
