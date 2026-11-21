"""Daemon-free image-time source adaptation and checksum tests."""

import hashlib
import runpy
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from distro_cli.lib.docker.image import _compute_dependency_checksum

NETWORK = (
    "network = [f'user,model=virtio,"
    "hostfwd=tcp::{Defaults.box_ssh_port_forwarded_to_host}-:22']\n"
)
SOCKETS = (
    "import os\n"
    "index = 0\n"
    "daemon = ['--socket-path=/tmp/vhostqemu_{0}'.format(index),]\n"
    "chardev = ['socket,id=char{0},path=/tmp/vhostqemu_{0}'.format(index),]\n"
)


class KiwiConfigurationTest(unittest.TestCase):
    def setUp(self):
        resource = Path(__file__).with_name("configure_kiwi_boxed.py")
        source = Path(__file__).parents[4] / "oss/docker/configure_kiwi_boxed.py"
        self.configuration = runpy.run_path(
            str(resource if resource.exists() else source)
        )

    def test_rewrite_is_narrow_and_rejects_changed_options(self):
        rewrite = self.configuration["rewrite_options"]
        options = "options = ['--sandbox', 'namespace', '--other']\n"
        source = options + NETWORK + SOCKETS
        result = rewrite(source)
        self.assertIn("'--sandbox', 'chroot'", result)
        self.assertIn("'--inode-file-handles=never'", result)
        self.assertIn("'--other'", result)
        self.assertIn("f'user,model=virtio']", result)
        self.assertNotIn("hostfwd", result)
        with self.assertRaisesRegex(RuntimeError, "Unexpected"):
            rewrite(result)
        with self.assertRaisesRegex(RuntimeError, "Unexpected"):
            rewrite(source + source)
        with self.assertRaisesRegex(RuntimeError, "Unexpected"):
            rewrite(options)
        with self.assertRaisesRegex(RuntimeError, "Unexpected"):
            rewrite(options + NETWORK)

    def test_virtiofsd_sockets_follow_each_build_cache(self):
        rewritten = self.configuration["rewrite_options"](
            "options = ['--sandbox', 'namespace',]\n" + NETWORK + SOCKETS
        )
        self.assertNotIn("/tmp/vhostqemu", rewritten)
        for cache in ("/out/.box-1/pxe", "/out/.box-1/onie"):
            with (
                self.subTest(cache=cache),
                patch.dict("os.environ", {"KIWI_BOXED_CACHE_DIR": cache}),
            ):
                namespace = {}
                exec(compile(rewritten, "defaults.py", "exec"), namespace)
                self.assertEqual(
                    namespace["daemon"], [f"--socket-path={cache}/vhostqemu_0"]
                )
                self.assertEqual(
                    namespace["chardev"], [f"socket,id=char0,path={cache}/vhostqemu_0"]
                )

    def test_source_hash_and_version_fail_closed(self):
        configure = self.configuration["configure"]
        with tempfile.TemporaryDirectory() as tmp:
            defaults = Path(tmp) / "defaults.py"
            source = "options = ['--sandbox', 'namespace',]\n" + NETWORK + SOCKETS
            defaults.write_text(source)
            distribution = SimpleNamespace(locate_file=lambda _: defaults)
            with (
                patch(
                    "importlib.metadata.version",
                    side_effect={
                        "kiwi-boxed-plugin": "0.2.58",
                        "kiwi": "10.3.10",
                    }.__getitem__,
                ),
                patch("importlib.metadata.distribution", return_value=distribution),
            ):
                with self.assertRaisesRegex(
                    RuntimeError, "Unreviewed boxed plugin source"
                ):
                    configure()
                self.assertEqual(defaults.read_text(), source)
                with patch.dict(
                    configure.__globals__,
                    DEFAULTS_SHA256=hashlib.sha256(source.encode()).hexdigest(),
                ):
                    configure()
                self.assertIn("'chroot'", defaults.read_text())
            with patch("importlib.metadata.version", return_value="0.2.59"):
                with self.assertRaisesRegex(
                    RuntimeError, "Unreviewed kiwi-boxed-plugin version"
                ):
                    configure()

    def test_rejects_incompatible_kiwi_launcher(self):
        with (
            patch(
                "importlib.metadata.version",
                side_effect={
                    "kiwi-boxed-plugin": "0.2.58",
                    "kiwi": "11.0.1",
                }.__getitem__,
            ),
            patch("importlib.metadata.distribution") as distribution,
        ):
            with self.assertRaisesRegex(
                RuntimeError, "Unreviewed KIWI launcher version"
            ):
                self.configuration["configure"]()
            distribution.assert_not_called()

    def test_checksum_invalidates_new_copy_input_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            docker = root / "fboss/oss/docker"
            docker.mkdir(parents=True)
            (root / "build").mkdir()
            dockerfile = docker / "Dockerfile"
            dockerfile.write_text("FROM base\n")
            configure = docker / "configure_kiwi_boxed.py"
            configure.write_text("original")
            original = _compute_dependency_checksum(root)
            (docker / "unrelated.txt").write_text("not a new COPY input")
            self.assertEqual(_compute_dependency_checksum(root), original)
            configure.write_text("updated")
            changed = _compute_dependency_checksum(root)
            self.assertNotEqual(changed, original)
            dockerfile.write_text("FROM updated\n")
            self.assertNotEqual(_compute_dependency_checksum(root), changed)
