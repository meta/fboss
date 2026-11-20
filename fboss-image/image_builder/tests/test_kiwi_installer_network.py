#!/usr/bin/env python3

import configparser
import importlib.resources
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


class KiwiInstallerNetworkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tempdir.cleanup)
        cls.module_dir = Path(cls.tempdir.name)
        source_dir = os.environ.get("KIWI_NETWORK_MODULE_DIR")
        if source_dir:
            source = Path(source_dir)
        else:
            source = importlib.resources.files("fboss_image.image_builder").joinpath(
                "network_module"
            )
        for filename in ["module-setup.sh", "NetworkManager.conf"]:
            (cls.module_dir / filename).write_text(
                source.joinpath(filename).read_text()
            )

    def run_module(self, body, **environment):
        return subprocess.run(
            ["bash", "-c", 'source "$moddir/module-setup.sh"\n' + body],
            env={**os.environ, "moddir": str(self.module_dir), **environment},
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_only_kiwi_dump_installers_include_the_module(self):
        for modules in [
            "",
            "base network-manager",
            "kiwi-dump-reboot",
            "kiwi-dump-extra",
        ]:
            with self.subTest(modules=modules):
                self.assertEqual(
                    self.run_module("check", add_dracutmodules=modules).returncode, 1
                )
        self.assertEqual(
            self.run_module(
                "check", add_dracutmodules="kiwi-dump kiwi-dump-reboot"
            ).returncode,
            0,
        )

    def test_depends_on_network_manager(self):
        result = self.run_module("depends")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "network-manager")

    def test_installs_the_policy_in_the_initrd(self):
        with tempfile.TemporaryDirectory() as initrd:
            result = self.run_module(
                """
inst_simple() {
    mkdir -p "$(dirname "$initdir$2")"
    cp "$1" "$initdir$2"
}
install
""",
                initdir=initrd,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            installed = (
                Path(initrd) / "usr/lib/NetworkManager/conf.d/90-kiwi-installer.conf"
            )
            self.assertEqual(
                installed.read_text(),
                (self.module_dir / "NetworkManager.conf").read_text(),
            )

    def test_policy_is_generic_and_initrd_only(self):
        policy = configparser.ConfigParser()
        policy.read(self.module_dir / "NetworkManager.conf")
        self.assertEqual(set(policy.sections()), {".config", "device-kiwi-installer"})
        self.assertEqual(dict(policy[".config"]), {"enable": "env:initrd"})
        self.assertEqual(
            dict(policy["device-kiwi-installer"]),
            {"match-device": "*", "keep-configuration": "no"},
        )


if __name__ == "__main__":
    unittest.main()
