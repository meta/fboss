"""Daemon-free tests for the image-build isolation boundary."""

import os
import socket
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from distro_cli.builder import image_builder
from distro_cli.lib.docker.container import run_container
from distro_cli.lib.exceptions import BuildError


class ContainerIsolationTest(unittest.TestCase):
    @patch("distro_cli.lib.docker.container.subprocess.run")
    def test_only_kvm_device_and_mknod_capability(self, run):
        run.return_value = Mock(returncode=0)
        self.assertEqual(
            run_container(
                "image",
                ["build"],
                devices={Path("/dev/kvm"): Path("/dev/kvm")},
                cap_add=["MKNOD"],
                shm_size="10g",
            ),
            0,
        )
        args = run.call_args.args[0]
        index = args.index("--cap-add")
        self.assertEqual(args[index + 1], "MKNOD")
        self.assertLess(index, args.index("image"))
        self.assertEqual(args.count("--cap-add"), 1)
        self.assertEqual(args.count("--device"), 1)
        self.assertIn("/dev/kvm:/dev/kvm", args)
        self.assertIn("--shm-size", args)
        self.assertNotIn("--privileged", args)
        self.assertNotIn("-v", args)

    @patch("distro_cli.lib.docker.container.subprocess.run")
    def test_defaults_unchanged(self, run):
        run.return_value = Mock(returncode=7)
        self.assertEqual(run_container("image", ["build"]), 7)
        args = run.call_args.args[0]
        self.assertIn("--network=host", args)
        self.assertNotIn("--cap-add", args)
        self.assertNotIn("--device", args)
        self.assertNotIn("--shm-size", args)

    @patch("distro_cli.lib.docker.container.subprocess.run")
    def test_read_only_workspace_mount(self, run):
        run.return_value = Mock(returncode=0)
        run_container(
            "image",
            ["build"],
            read_only_volumes={Path("/workspace"): Path("/image_builder")},
        )
        self.assertIn("/workspace:/image_builder:ro", run.call_args.args[0])


class GuestProxyTest(unittest.TestCase):
    @patch("distro_cli.builder.image_builder.socket.getaddrinfo")
    def test_http_proxy_hostname_resolved_once_and_credentials_preserved(self, resolve):
        resolve.return_value = [
            (socket.AF_INET, socket.SOCK_STREAM, 6, "", ("192.0.2.10", 8080))
        ]
        result = image_builder.guest_proxy_environment(
            {
                "http_proxy": "http://user:p%40ss@proxy.example.com:8080/path?x=1",
                "https_proxy": "http://user:p%40ss@proxy.example.com:8080/path?x=1",
                "NO_PROXY": "localhost,.example.com",
                "UNRELATED": "ignored",
            }
        )
        expected = "http://user:p%40ss@192.0.2.10:8080/path?x=1"
        self.assertEqual(
            result,
            {
                "http_proxy": expected,
                "https_proxy": expected,
                "NO_PROXY": "localhost,.example.com",
            },
        )
        resolve.assert_called_once_with(
            "proxy.example.com", 8080, type=socket.SOCK_STREAM
        )

    @patch("distro_cli.builder.image_builder.socket.getaddrinfo")
    def test_ipv6_only_proxy_resolved_and_bracketed(self, resolve):
        resolve.return_value = [
            (socket.AF_INET6, socket.SOCK_STREAM, 6, "", ("2001:db8::10", 8080, 0, 0))
        ]
        self.assertEqual(
            image_builder.guest_proxy_environment(
                {"http_proxy": "http://user:p%40ss@proxy.example.com:8080"}
            ),
            {"http_proxy": "http://user:p%40ss@[2001:db8::10]:8080"},
        )
        resolve.assert_called_once_with(
            "proxy.example.com", 8080, type=socket.SOCK_STREAM
        )

    @patch("distro_cli.builder.image_builder.socket.getaddrinfo")
    def test_literal_addresses_empty_and_tls_proxies_unchanged(self, resolve):
        env = {
            "http_proxy": "http://192.0.2.10:8080",
            "HTTP_PROXY": "http://[2001:db8::1]:8080",
            "https_proxy": "https://proxy.example.com:8443",
            "HTTPS_PROXY": "",
            "no_proxy": "localhost",
        }
        self.assertEqual(image_builder.guest_proxy_environment(env), env)
        resolve.assert_not_called()

    @patch("distro_cli.builder.image_builder.socket.getaddrinfo")
    def test_proxy_resolution_failure_is_actionable_without_credentials(self, resolve):
        resolve.side_effect = OSError("resolver failed")
        with self.assertRaisesRegex(
            BuildError, r"http_proxy.*proxy\.example\.com"
        ) as error:
            image_builder.guest_proxy_environment(
                {"http_proxy": "http://user:secret@proxy.example.com:8080"}
            )
        self.assertNotIn("secret", str(error.exception))
        self.assertIsNone(error.exception.__cause__)

    def test_malformed_http_proxy_rejected_without_credentials(self):
        for value in ("http:///missing", "http://user:secret@proxy:invalid"):
            with self.subTest(value=value), self.assertRaises(BuildError) as error:
                image_builder.guest_proxy_environment({"http_proxy": value})
            self.assertNotIn("secret", str(error.exception))


@patch.dict(os.environ, dict.fromkeys(image_builder.PROXY_VARIABLES, ""))
class ImageIsolationTest(unittest.TestCase):
    def setUp(self):
        boxes = tempfile.TemporaryDirectory()
        self.addCleanup(boxes.cleanup)
        self.boxes = Path(boxes.name)
        box_patch = patch.object(
            image_builder, "box_cache_dir", return_value=self.boxes
        )
        box_patch.start()
        self.addCleanup(box_patch.stop)

    def _builder(self, root: Path) -> image_builder.ImageBuilder:
        manifest_file = root / "manifest.json"
        manifest_file.write_text("{}")
        manifest = SimpleNamespace(
            manifest_dir=root,
            manifest_path=manifest_file,
            data={"distribution_formats": {"usb": str(root / "usb.iso")}},
        )
        with patch.object(
            image_builder, "get_abs_path", return_value=root / "workspace"
        ):
            builder = image_builder.ImageBuilder(manifest, output_dir=root / "out")
        builder.centos_template_dir.mkdir(parents=True)
        return builder

    def test_box_memory_scales_with_host_and_is_clamped(self):
        gib = 2**30
        for host_gb, expected in ((16, 8), (32, 8), (48, 12), (64, 16), (224, 16)):
            with (
                self.subTest(host_gb=host_gb),
                patch.object(
                    image_builder.os,
                    "sysconf",
                    side_effect={
                        "SC_PAGE_SIZE": 4096,
                        "SC_PHYS_PAGES": host_gb * gib // 4096,
                    }.__getitem__,
                ),
            ):
                self.assertEqual(image_builder.box_memory_gb(), expected)

    def test_missing_kvm(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(BuildError, "KVM"):
                image_builder.validate_kvm_device(Path(tmp) / "missing")

    def test_regular_file_is_not_kvm(self):
        with tempfile.NamedTemporaryFile() as tmp:
            with self.assertRaisesRegex(BuildError, "character"):
                image_builder.validate_kvm_device(Path(tmp.name))

    def test_missing_kvm_fails_before_any_build_work(self):
        with tempfile.TemporaryDirectory() as tmp:
            builder = self._builder(Path(tmp))
            with (
                patch.object(
                    image_builder,
                    "validate_kvm_device",
                    side_effect=BuildError("KVM isolation requires /dev/kvm"),
                ),
                patch.object(image_builder, "build_fboss_builder_image") as image,
                patch.object(image_builder, "run_container") as run,
            ):
                with self.assertRaisesRegex(BuildError, "KVM"):
                    builder._build_base_image()
            image.assert_not_called()
            run.assert_not_called()

    def test_recovered_staging_does_not_reuse_or_overwrite_artifacts(self):
        for recovery in ("unchanged", "replaced", "renamed"):
            with self.subTest(recovery=recovery), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                builder = self._builder(root)
                original = root / "kernel.tar"
                original.write_bytes(b"original kernel")
                staging = builder.image_builder_dir / "deps_staging"
                (staging / "kernel").mkdir(parents=True)
                os.link(original, staging / "kernel/kernel.tar")
                (staging / "bsps").mkdir()
                (staging / "bsps/old.tar").write_bytes(b"removed component")
                current = original
                if recovery != "unchanged":
                    (root / "new").mkdir()
                    name = "kernel.tar" if recovery == "replaced" else "kernel-new.tar"
                    current = root / "new" / name
                    current.write_bytes(b"new kernel")
                builder.component_artifacts = {"kernel": current}

                def verify_staging(
                    original=original, staging=staging, current=current, **kwargs
                ):
                    self.assertEqual(original.read_bytes(), b"original kernel")
                    self.assertEqual(
                        sorted(p.relative_to(staging) for p in staging.rglob("*.tar")),
                        [Path("kernel") / current.name],
                    )
                    self.assertEqual(
                        (staging / "kernel" / current.name).read_bytes(),
                        current.read_bytes(),
                    )
                    return 9

                with (
                    patch.object(image_builder, "validate_kvm_device"),
                    patch.object(image_builder, "build_fboss_builder_image"),
                    patch.object(
                        image_builder, "run_container", side_effect=verify_staging
                    ) as run,
                    patch.object(builder, "_source_revision", return_value="test"),
                ):
                    with self.assertRaisesRegex(BuildError, "exit code 9"):
                        builder._build_base_image()
                    run.assert_called_once()
                self.assertEqual(original.read_bytes(), b"original kernel")
                self.assertFalse(staging.exists())

    def test_build_isolated_and_staging_cleaned_on_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            builder = self._builder(root)
            artifact = root / "kernel.tar"
            artifact.write_text("kernel")
            builder.component_artifacts = {"kernel": artifact}

            def invoke(**kwargs):
                self.assertEqual(
                    kwargs["devices"], {Path("/dev/kvm"): Path("/dev/kvm")}
                )
                self.assertEqual(kwargs["cap_add"], ["MKNOD"])
                self.assertEqual(kwargs["env"]["http_proxy"], "http://192.0.2.10:8080")
                self.assertEqual(kwargs["env"]["FBOSS_BOX_MEMORY_GB"], "16")
                self.assertEqual(kwargs["shm_size"], "34g")
                self.assertNotIn("privileged", kwargs)
                self.assertEqual(
                    kwargs["read_only_volumes"],
                    {builder.image_builder_dir: Path("/image_builder")},
                )
                self.assertEqual(
                    kwargs["volumes"],
                    {
                        builder.output_dir: Path("/image_builder/output"),
                        builder.image_builder_dir / "logs": Path("/image_builder/logs"),
                        self.boxes: Path("/var/cache/kiwi-boxes"),
                    },
                )
                self.assertTrue(
                    (
                        builder.image_builder_dir / "deps_staging/kernel/kernel.tar"
                    ).exists()
                )
                return 9

            with (
                patch.object(image_builder, "validate_kvm_device"),
                patch.object(image_builder, "build_fboss_builder_image"),
                patch.object(image_builder, "run_container", side_effect=invoke),
                patch.object(image_builder, "box_memory_gb", return_value=16),
                patch.dict(os.environ, {"http_proxy": "http://proxy.example.com:8080"}),
                patch.object(
                    image_builder.socket,
                    "getaddrinfo",
                    return_value=[
                        (
                            socket.AF_INET,
                            socket.SOCK_STREAM,
                            6,
                            "",
                            ("192.0.2.10", 8080),
                        )
                    ],
                ),
                patch.object(builder, "_write_version_file"),
            ):
                with self.assertRaisesRegex(BuildError, "exit code 9"):
                    builder._build_base_image()
            self.assertFalse((builder.image_builder_dir / "deps_staging").exists())
