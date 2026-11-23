#!/usr/bin/env python3

# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""
Unit tests for ImageBuilder class
"""

import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from distro_cli.builder.image_builder import ImageBuilder
from distro_cli.lib.exceptions import BuildError, ComponentError
from distro_cli.lib.manifest import ImageManifest
from distro_cli.tests.test_helpers import enter_tempdir, override_artifact_store_dir


class TestImageBuilder(unittest.TestCase):
    """Test ImageBuilder class"""

    def setUp(self):
        """Use the test manifest"""
        self.test_dir = Path(__file__).parent / "data"
        self.manifest_path = self.test_dir / "dev_image.json"
        self.manifest = ImageManifest(self.manifest_path)

        # Use sandbox-safe temporary directory
        self._tempdir_ctx = enter_tempdir("image_builder_test_")
        self.temp_dir = self._tempdir_ctx.__enter__()
        self.manifest.data["distribution_formats"] = {
            kind: str(self.temp_dir / Path(destination).name)
            for kind, destination in self.manifest.data["distribution_formats"].items()
        }

        # Override artifact store directory for testing
        self._artifact_store_ctx = override_artifact_store_dir(self.temp_dir)
        self._artifact_store_ctx.__enter__()

        self.builder = ImageBuilder(self.manifest)
        self.builder.image_builder_dir = self.temp_dir / "workspace"
        self.builder.centos_template_dir = (
            self.builder.image_builder_dir / "templates/centos-09.0"
        )
        self.builder.centos_template_dir.mkdir(parents=True)
        self.builder.after_pkgs_install_file = (
            self.builder.centos_template_dir / "after_pkgs_install_file.json"
        )
        self.builder.after_pkgs_execute_file = (
            self.builder.centos_template_dir / "after_pkgs_execute_file.json"
        )
        kvm_patch = patch("distro_cli.builder.image_builder.validate_kvm_device")
        kvm_patch.start()
        self.addCleanup(kvm_patch.stop)
        box_patch = patch(
            "distro_cli.builder.image_builder.box_cache_dir",
            return_value=self.temp_dir / "kiwi-boxes",
        )
        box_patch.start()
        self.addCleanup(box_patch.stop)

        # The base-image build refuses BIOS-booted hosts (T297984315).
        # Pin UEFI here so these tests are hermetic on any host firmware;
        # the gate itself is covered by host_boot_test.py and the two
        # tests at the end of this file.
        self._boot_patcher = patch(
            "distro_cli.lib.host_boot.is_bios_boot", return_value=False
        )
        self._boot_patcher.start()

        # Create a temporary directory for kernel artifacts
        self.kernel_artifacts_dir = Path(tempfile.mkdtemp(prefix="kernel-artifacts-"))

    def tearDown(self):
        """Clean up test fixtures"""
        self._boot_patcher.stop()

        # Clean up kernel artifacts directory
        if hasattr(self, "kernel_artifacts_dir") and self.kernel_artifacts_dir.exists():
            shutil.rmtree(self.kernel_artifacts_dir)

        # Exit context managers in reverse order
        self._artifact_store_ctx.__exit__(None, None, None)
        self._tempdir_ctx.__exit__(None, None, None)

    def _create_outputs(self):
        for ext in ("iso", "tar", "bin"):
            (
                self.builder.image_builder_dir
                / "output"
                / f"FBOSS-Distro-Image.x86_64-1.0.install.{ext}"
            ).write_bytes(b"fixture")

    def test_builder_initialization(self):
        """Test that builder initializes correctly"""
        self.assertIsNotNone(self.builder)
        self.assertEqual(self.builder.manifest, self.manifest)

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_build_all(
        self, mock_component_build, mock_build_image, mock_run_container
    ):
        """Test build_all method with mocked component builds"""
        # Create a real kernel tarball in temp directory
        kernel_artifact = self.kernel_artifacts_dir / "kernel.tar.zst"
        kernel_artifact.touch()
        mock_component_build.return_value = kernel_artifact

        # Mock successful container execution for base image build
        mock_run_container.return_value = 0

        # Mock _build_base_image and _move_distro_file to avoid file operations
        with (
            patch.object(self.builder, "_build_base_image") as mock_build_base,
            patch.object(self.builder, "_move_distro_file"),
        ):
            self.builder.build_all()

        # Verify _build_base_image was called (we mock it to avoid file writes)
        mock_build_base.assert_called_once()

        # Verify component build was called for each component element in manifest
        # dev_image.json has: kernel (1), other_dependencies (2 elements), fboss-platform-stack (1),
        # bsps (2 elements), npu_sai (1), fboss-forwarding-stack (1),
        # phy_sai (1), image_build_hooks (1) = 10 total
        self.assertEqual(mock_component_build.call_count, 10)

    @patch("distro_cli.builder.image_builder.ImageBuilder._compress_artifact")
    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_build_components(self, mock_component_build, mock_compress):
        """Test build_components method with mocked component builds"""
        # Mock component builds to return fake artifact paths
        mock_component_build.return_value = Path("/fake/artifact.tar")

        # Mock compression to return compressed path
        mock_compress.side_effect = lambda path, _: Path(
            str(path).replace(".tar", ".tar.zst")
        )

        # Request 'sai' and 'fboss-platform-stack' which both have execute commands
        # Note: 'sai' depends on 'kernel', so kernel will be built first
        # 'fboss-forwarding-stack' depends on 'sai', so sai will be built first
        components = ["npu_sai", "fboss-platform-stack"]
        self.builder.build_components(components)

        # Verify component build was called for:
        # 1. kernel (dependency of npu_sai, downloaded artifact)
        # 2. npu_sai (requested, execute build)
        # 3. fboss-platform-stack (requested, execute build)
        self.assertEqual(mock_component_build.call_count, 3)

        # Only artifacts produced by execute builds are compressed; downloaded
        # artifacts are used as-is. In this manifest, kernel is a download, so
        # we expect compression for npu_sai and fboss-platform-stack only.
        self.assertEqual(mock_compress.call_count, 2)

        # Verify artifacts were stored
        self.assertIn("kernel", self.builder.component_artifacts)  # Built as dependency
        self.assertIn("npu_sai", self.builder.component_artifacts)
        self.assertIn("fboss-platform-stack", self.builder.component_artifacts)

    @patch("distro_cli.builder.image_builder.ImageBuilder._compress_artifact")
    @patch("distro_cli.builder.image_builder.ComponentBuilder")
    def test_component_builder_receives_correct_salt_in_component_mode(
        self, mock_component_builder_class, mock_compress
    ):
        """Test that ComponentBuilder receives 'compressed' salt in component mode."""
        # Mock the ComponentBuilder instance and its build method
        mock_instance = mock_component_builder_class.return_value
        mock_instance.build.return_value = Path("/fake/artifact.tar")

        # Mock compression to return compressed path
        mock_compress.side_effect = lambda path, _: Path(
            str(path).replace(".tar", ".tar.zst")
        )

        # Build components (which enables compression)
        self.builder.build_components(["kernel"])

        # Verify ComponentBuilder was instantiated with artifact_key_salt='compressed'
        mock_component_builder_class.assert_called()
        call_kwargs = mock_component_builder_class.call_args.kwargs
        self.assertEqual(
            call_kwargs.get("artifact_key_salt"),
            "compressed",
            "ComponentBuilder should receive 'compressed' salt in component mode",
        )

    @patch("distro_cli.builder.image_builder.ComponentBuilder")
    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    def test_component_builder_receives_correct_salt_in_full_build_mode(
        self, mock_build_image, mock_run_container, mock_component_builder_class
    ):
        """Test that ComponentBuilder receives 'uncompressed' salt in full build mode."""
        # Mock the ComponentBuilder instance and its build method
        mock_instance = mock_component_builder_class.return_value
        mock_instance.build.return_value = Path("/fake/artifact.tar")

        # Mock successful container execution
        mock_run_container.return_value = 0

        # Mock _build_base_image and _move_distro_file to avoid file operations
        with (
            patch.object(self.builder, "_build_base_image"),
            patch.object(self.builder, "_move_distro_file"),
        ):
            # Build all (which keeps artifacts uncompressed)
            self.builder.build_all()

        # Verify ComponentBuilder was instantiated with artifact_key_salt='uncompressed'
        # Check the first call (should be for kernel component)
        mock_component_builder_class.assert_called()
        call_kwargs = mock_component_builder_class.call_args.kwargs
        self.assertEqual(
            call_kwargs.get("artifact_key_salt"),
            "uncompressed",
            "ComponentBuilder should receive 'uncompressed' salt in full build mode",
        )

    def test_build_component_not_found(self):
        """Test building a component that doesn't exist in manifest"""
        with self.assertRaises(ComponentError) as cm:
            self.builder.build_components(["nonexistent_component"])

        # Verify error message
        self.assertIn("nonexistent_component", str(cm.exception))
        self.assertIn("not found", str(cm.exception))

    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_build_component_execution_failure(self, mock_component_build):
        """Test handling of component build failure"""
        # Mock failed component build
        mock_component_build.side_effect = BuildError(
            "npu_sai build failed with exit code 1"
        )

        with self.assertRaises(BuildError) as cm:
            self.builder.build_components(["npu_sai"])

        # Verify error message contains component name and exit code
        self.assertIn("npu_sai", str(cm.exception))
        self.assertIn("exit code 1", str(cm.exception))

        # Component build should have been called once (for npu_sai)
        mock_component_build.assert_called_once()

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_kernel_rpms_extracted_and_mounted(
        self, mock_component_build, mock_build_image, mock_run_container
    ):
        """Test that component artifacts are staged under deps_staging and passed via --deps"""
        # Create a real kernel tarball in temp directory
        kernel_artifact = self.kernel_artifacts_dir / "kernel-6.11.1.rpms.tar.zst"
        kernel_artifact.touch()
        mock_component_build.return_value = kernel_artifact

        # Capture and verify staging during run_container call
        captured_deps_dir = None

        def capture_and_verify(*_, **__):
            nonlocal captured_deps_dir

            # On the host, artifacts should be staged under
            # image_builder_dir/deps_staging/<component_name>/
            deps_dir = self.builder.image_builder_dir / "deps_staging"
            kernel_dir = deps_dir / "kernel"
            assert kernel_dir.exists(), f"kernel directory not found in {deps_dir}"
            kernel_files = list(kernel_dir.glob("kernel-*.rpms.tar.zst"))
            assert len(kernel_files) == 1, "Kernel artifact not staged correctly"

            captured_deps_dir = deps_dir
            self._create_outputs()
            return 0

        mock_run_container.side_effect = capture_and_verify

        # Mock the _move_distro_file method to avoid file operations
        with patch.object(self.builder, "_move_distro_file"):
            self.builder.build_all()

        # Verify run_container was called
        mock_run_container.assert_called_once()

        # Verify deps directory was staged
        self.assertIsNotNone(captured_deps_dir, "Deps directory not staged")

        # Verify the build command invokes the image builder script
        command = mock_run_container.call_args.kwargs["command"]
        self.assertIn("/image_builder/bin/build_image_in_container.sh", command)

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_no_kernel_rpms_when_kernel_not_built(
        self, mock_component_build, mock_build_image, mock_run_container
    ):
        """Test that deps directory is passed even when components produce no artifacts"""
        mock_component_build.return_value = None

        # Capture and verify during run_container call while deps_staging still exists
        deps_checked = False

        def capture_and_verify(*_, **__):
            nonlocal deps_checked
            deps_dir = self.builder.image_builder_dir / "deps_staging"
            assert deps_dir.exists(), "deps_staging directory should exist during build"
            deps_checked = True
            self._create_outputs()
            return 0

        mock_run_container.side_effect = capture_and_verify

        with patch.object(self.builder, "_move_distro_file"):
            self.builder.build_all()

        mock_run_container.assert_called_once()
        self.assertTrue(
            deps_checked, "deps_staging directory was not checked during build"
        )

        command = mock_run_container.call_args.kwargs["command"]
        self.assertIn("/image_builder/bin/build_image_in_container.sh", command)

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    @patch("distro_cli.builder.component.ComponentBuilder.build")
    def test_kernel_artifact_staging(
        self, mock_component_build, mock_build_image, mock_run_container
    ):
        """Test that kernel artifacts are properly staged in tmpdir"""
        # Create a kernel tarball
        kernel_artifact = self.kernel_artifacts_dir / "kernel-6.11.1.rpms.tar.zst"
        kernel_artifact.touch()
        mock_component_build.return_value = kernel_artifact

        # Capture and verify during run_container call
        def capture_and_verify(*_, **__):
            deps_dir = self.builder.image_builder_dir / "deps_staging"
            kernel_staged = deps_dir / "kernel" / "kernel-6.11.1.rpms.tar.zst"
            assert kernel_staged.exists(), "Kernel artifact not staged in deps_staging"
            self._create_outputs()
            return 0

        mock_run_container.side_effect = capture_and_verify

        # Mock the _move_distro_file method to avoid file operations
        with patch.object(self.builder, "_move_distro_file"):
            self.builder.build_all()

        # Verify run_container was called
        mock_run_container.assert_called_once()

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    def test_base_image_build_warns_on_bios_host_and_continues(
        self, mock_build_image, mock_run_container
    ):
        """A BIOS-booted host warns; the caller accepts the cost and continues."""
        from distro_cli.lib import host_boot

        mock_run_container.return_value = 0
        with (
            patch.object(host_boot, "warn_bios_boot_host", return_value=True),
            patch.object(self.builder, "_stage_component_artifacts"),
            patch.object(self.builder, "_write_build_provenance"),
            patch.object(self.builder, "_write_version_file"),
            patch.object(self.builder, "_move_distro_file"),
        ):
            self.builder._build_base_image()

        mock_build_image.assert_called_once()
        mock_run_container.assert_called_once()

    @patch("distro_cli.builder.image_builder.run_container")
    @patch("distro_cli.builder.image_builder.build_fboss_builder_image")
    def test_base_image_build_gate_runs_before_container(
        self, mock_build_image, mock_run_container
    ):
        """The boot-mode gate runs before the builder image is built."""
        from distro_cli.lib import host_boot

        mock_run_container.return_value = 0
        order = []

        def record_gate():
            order.append("gate")

        def record_build_image():
            order.append("build_image")

        mock_build_image.side_effect = record_build_image

        with (
            patch.object(host_boot, "warn_bios_boot_host", record_gate),
            patch.object(self.builder, "_stage_component_artifacts"),
            patch.object(self.builder, "_write_build_provenance"),
            patch.object(self.builder, "_write_version_file"),
            patch.object(self.builder, "_move_distro_file"),
        ):
            self.builder._build_base_image()

        self.assertEqual(order, ["gate", "build_image"])


if __name__ == "__main__":
    unittest.main()
