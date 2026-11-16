# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Host firmware boot-mode detection for the distro image build.

The kiwi build runs in a privileged container with the host device tree
visible, and a BIOS GRUB install during the build can overwrite the host
BIOS GRUB core image with a prefix pointing into the kiwi build tree
(T297984315). A host that boots via BIOS GRUB then fails on its next
reboot. A host that boots via UEFI never consults that core image.

Detection is fail-closed: anything that is not positively UEFI is
treated as BIOS, including a missing or unreadable firmware tree.
"""

import logging
from pathlib import Path

logger = logging.getLogger(__name__)

FIRMWARE_ROOT = Path("/sys/firmware")
EFI_DIR_NAME = "efi"


def is_uefi_boot(firmware_root: Path | None = None) -> bool:
    """True when the running host booted via UEFI.

    The root is resolved at call time (not bound as a default argument)
    so tests and callers can redirect it via the module-level
    ``FIRMWARE_ROOT``.
    """
    root = FIRMWARE_ROOT if firmware_root is None else firmware_root
    return (root / EFI_DIR_NAME).is_dir()


def is_bios_boot(firmware_root: Path | None = None) -> bool:
    """True unless the host positively booted via UEFI (fail closed)."""
    return not is_uefi_boot(firmware_root)


def warn_bios_boot_host(container_isolated: bool = False) -> bool:
    """Warn on a BIOS-booted host; the caller accepts the cost and continues.

    This deliberately does not refuse: the caller proceeds knowing a
    BIOS GRUB install during the build can overwrite the host BIOS
    GRUB core image (T297984315), and that the repair script after
    the build is what mitigates it. If the build is killed before
    the repair script runs, or the repair fails, the host can still
    be bricked on its next reboot. A UEFI-booted host never consults
    that core image and needs no warning.

    Returns:
        True when the host boots via BIOS GRUB (the caller is
        proceeding at its own risk); False on UEFI hosts.
    """
    if not is_bios_boot():
        return False
    if container_isolated:
        logger.warning(
            "Host boots via BIOS GRUB; proceeding at the caller's risk. "
            "The build container is isolated from host block devices, so "
            "the overwrite should not happen, but if isolation regresses, "
            "the repair script after the build is the only mitigation "
            "(T297984315)."
        )
    else:
        logger.warning(
            "Host boots via BIOS GRUB; proceeding at the caller's risk "
            "(T297984315). A BIOS GRUB install during the build can "
            "overwrite the host core image with a prefix pointing into "
            "the kiwi build tree, and the host will fail on its next "
            "reboot unless the repair script after the build verifies "
            "and, if needed, repairs the boot sector. To check this "
            "host, run: sudo dd if=/dev/vda bs=512 count=2048 "
            "2>/dev/null | strings | grep -E 'image-root|/data/users' "
            "(confirm the boot disk with lsblk first). If that prints "
            "a match, repair before rebooting with: sudo grub2-install "
            "--target=i386-pc /dev/vda"
        )
    return True
