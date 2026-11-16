#!/usr/bin/env python3

# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Unit tests for host firmware boot-mode detection."""

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from distro_cli.lib import host_boot


class TestIsUefiBoot(unittest.TestCase):
    def test_efi_directory_present_is_uefi(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "efi").mkdir()
            self.assertTrue(host_boot.is_uefi_boot(root))
            self.assertFalse(host_boot.is_bios_boot(root))

    def test_efi_directory_absent_is_bios(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertFalse(host_boot.is_uefi_boot(root))
            self.assertTrue(host_boot.is_bios_boot(root))

    def test_missing_firmware_root_is_bios(self):
        # Fail closed: an unreadable firmware tree must not be read as UEFI.
        self.assertTrue(host_boot.is_bios_boot(Path("/nonexistent/firmware")))


class TestWarnBiosBootHost(unittest.TestCase):
    def test_uefi_host_returns_false_without_warning(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "efi").mkdir()
            with patch.object(host_boot, "FIRMWARE_ROOT", root):
                with self.assertNoLogs(host_boot.logger, level="WARNING"):
                    self.assertFalse(host_boot.warn_bios_boot_host())

    def test_bios_host_warns_and_returns_true(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.object(host_boot, "FIRMWARE_ROOT", root):
                with self.assertLogs(host_boot.logger, level="WARNING") as logs:
                    self.assertTrue(host_boot.warn_bios_boot_host())
        self.assertIn("caller's risk", "\n".join(logs.output))

    def test_bios_host_warns_even_with_ci_env_set(self):
        # FBOSS_META_ENV is set in ordinary devserver shells, so it must
        # not affect the gate.
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            with (
                patch.object(host_boot, "FIRMWARE_ROOT", root),
                patch.dict("os.environ", {"FBOSS_META_ENV": "1"}),
            ):
                with self.assertLogs(host_boot.logger, level="WARNING"):
                    self.assertTrue(host_boot.warn_bios_boot_host())


if __name__ == "__main__":
    unittest.main()
