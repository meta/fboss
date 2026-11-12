#!/usr/bin/env python3

# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Unit tests for device commands."""

import argparse
import json
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import MagicMock, Mock, patch

from distro_cli.cmds.device import (
    _download_with_cache,
    get_device_ip,
    getip_command,
    image_command,
    image_upstream_command,
    reprovision_command,
    setup_device_commands,
    ssh_command,
    update_command,
)
from distro_cli.lib.cli import CLI
from distro_cli.lib.device_update import DeviceUpdateError, DeviceUpdater
from distro_cli.lib.distro_infra import DISTRO_INFRA_CONTAINER
from distro_cli.lib.docker import container
from distro_cli.lib.exceptions import DistroInfraError
from distro_cli.lib.manifest import ImageManifest
from distro_cli.tests.test_helpers import waitfor


class TestDeviceCommands(unittest.TestCase):
    """Test device command group and subcommands"""

    IPXE_FILES = ("ipxev4.efi", "ipxev6.efi", "autoexec.ipxe")

    @classmethod
    def setUpClass(cls):
        """Set up test container before all tests"""
        try:
            result = subprocess.run(
                ["docker", "images", "-q", "fboss_distro_infra"],
                capture_output=True,
                text=True,
                check=True,
            )
            if not result.stdout.strip():
                raise unittest.SkipTest(
                    "fboss_distro_infra Docker image not found. "
                    "Please build it with: cd fboss-image/distro_infra && ./build.sh"
                )
        except (subprocess.CalledProcessError, FileNotFoundError):
            raise unittest.SkipTest("Docker not available or image not built")

        cwd = Path.cwd()
        cls.container_temp_dir = Path(
            tempfile.mkdtemp(prefix="distro_infra_test_", dir=cwd)
        )
        cls.container_persistent_dir = cls.container_temp_dir / "persistent"
        cls.container_persistent_dir.mkdir(parents=True, exist_ok=True)

        # Write interface name file (normally done by distro_infra.sh)
        interface_file = cls.container_persistent_dir / "interface_name.txt"
        interface_file.write_text("lo")

        # Clean up any existing container with the same name
        if container.container_is_running(DISTRO_INFRA_CONTAINER):
            container.stop_and_remove_container(DISTRO_INFRA_CONTAINER)

        # Start the fboss-distro-infra container in background
        volumes = {cls.container_persistent_dir: Path("/distro_infra/persistent")}

        exit_code = container.run_container(
            image="fboss_distro_infra",
            command=["/distro_infra/run_distro_infra.sh", "--intf", "lo", "--nodhcpv6"],
            volumes=volumes,
            ephemeral=False,
            detach=True,
            name=DISTRO_INFRA_CONTAINER,
            privileged=True,  # Required for network operations
        )

        if exit_code != 0:
            raise RuntimeError(f"Failed to start {DISTRO_INFRA_CONTAINER} container")

    @classmethod
    def tearDownClass(cls):
        """Clean up test container after all tests"""
        if container.container_is_running(DISTRO_INFRA_CONTAINER):
            container.stop_and_remove_container(DISTRO_INFRA_CONTAINER)

        shutil.rmtree(cls.container_temp_dir, ignore_errors=True)

    def setup_image_command_test(self):
        """Wait for container to create PXE boot infrastructure."""
        cache_dir = self.container_persistent_dir / "cache"

        waitfor(
            cache_dir.exists,
            lambda: self.fail("Timed out waiting for cache directory to be created"),
        )

        for filename in self.IPXE_FILES:
            cache_file = cache_dir / filename
            waitfor(
                cache_file.exists,
                lambda f=filename: self.fail(
                    f"Timed out waiting for {f} to be created"
                ),
            )

    def verify_image_command_common(self, mac):
        """Verify common PXE boot infrastructure created by image command"""
        dash_mac = mac.replace(":", "-")
        mac_dir = self.container_persistent_dir / dash_mac

        self.assertTrue(mac_dir.exists())
        self.assertTrue(mac_dir.is_dir())

        for ipxe_file in self.IPXE_FILES:
            ipxe_path = mac_dir / ipxe_file
            self.assertTrue(ipxe_path.exists())

        pxeboot_marker = mac_dir / "pxeboot_complete"
        self.assertTrue(pxeboot_marker.exists())

        ipxev6_serverip = mac_dir / "ipxev6.efi-serverip"
        if ipxev6_serverip.exists():
            content = ipxev6_serverip.read_text()
            self.assertIn("#!ipxe", content)
            self.assertIn("set server_ip", content)

        result = subprocess.run(
            [
                "docker",
                "exec",
                DISTRO_INFRA_CONTAINER,
                "cat",
                f"/distro_infra/dnsmasq_conf.d/{dash_mac}",
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn(mac, result.stdout)

        return mac_dir

    def setUp(self):
        """Set up test fixtures"""
        self.test_mac = "aa:bb:cc:dd:ee:ff"

        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            f.write('{"test": "manifest"}')
            self.manifest_path = Path(f.name)

        self.temp_dir = tempfile.mkdtemp()
        self.image_path = Path(self.temp_dir) / "test_image.tar"

        test_file = Path(self.temp_dir) / "test_file.txt"
        test_file.write_text("test content")

        with tarfile.open(self.image_path, "w") as tar:
            tar.add(test_file, arcname="test_file.txt")

    def tearDown(self):
        """Clean up test fixtures"""
        self.manifest_path.unlink()
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_device_commands_exist(self):
        """Test that device commands exist"""
        self.assertTrue(callable(setup_device_commands))
        self.assertTrue(callable(image_upstream_command))
        self.assertTrue(callable(image_command))
        self.assertTrue(callable(reprovision_command))
        self.assertTrue(callable(update_command))
        self.assertTrue(callable(getip_command))
        self.assertTrue(callable(ssh_command))

    def test_image_upstream(self):
        """Test image-upstream command"""
        args = argparse.Namespace(
            mac=self.test_mac,
            npu_sai="13.3",
            train="stable",
            kernel="v6.11",
            phy_sai="",
            bsps="oss",
            tag="",
        )
        # Mock the download and deploy functions to avoid actual network calls
        with (
            patch("distro_cli.cmds.device._download_with_cache") as mock_download,
            patch("distro_cli.cmds.device.deploy_image_to_device") as mock_deploy,
        ):
            mock_download.return_value = Path("/tmp/test_image.tar")
            # Call command - just verify it doesn't crash
            image_upstream_command(args)
            # Verify download was called
            mock_download.assert_called_once()
            # Verify deploy was called with the MAC and downloaded path
            mock_deploy.assert_called_once_with(
                self.test_mac, Path("/tmp/test_image.tar")
            )

    def test_image_command_with_tarball(self):
        """Test image command with tarball extraction"""
        self.setup_image_command_test()

        temp_dir = tempfile.mkdtemp()
        test_file = Path(temp_dir) / "test_file.txt"
        test_file.write_text("test content")
        tarball_path = Path(temp_dir) / "test_image.tar"
        with tarfile.open(tarball_path, "w") as tar:
            tar.add(test_file, arcname="test_file.txt")

        args = argparse.Namespace(mac=self.test_mac, image_path=str(tarball_path))
        image_command(args)

        mac_dir = self.verify_image_command_common(self.test_mac)

        extracted_file = mac_dir / "test_file.txt"
        self.assertTrue(extracted_file.exists())
        self.assertEqual(extracted_file.read_text(), "test content")

        shutil.rmtree(temp_dir, ignore_errors=True)

    def test_image_command_with_directory(self):
        """Test image command failure with directory (only tarballs supported)"""
        self.setup_image_command_test()

        temp_dir = tempfile.mkdtemp()
        dir_path = Path(temp_dir) / "test_image_dir"
        dir_path.mkdir()
        file1 = dir_path / "file1.txt"
        file2 = dir_path / "file2.txt"
        file1.write_text("content1")
        file2.write_text("content2")

        args = argparse.Namespace(mac=self.test_mac, image_path=str(dir_path))
        with self.assertRaises(SystemExit) as excinfo:
            image_command(args)
        self.assertEqual(excinfo.exception.code, 1)

        shutil.rmtree(temp_dir, ignore_errors=True)

    def test_image_command_with_single_file(self):
        """Test image command failure with non-tarball file"""
        self.setup_image_command_test()

        temp_dir = tempfile.mkdtemp()
        single_file_path = Path(temp_dir) / "single_file.bin"
        single_file_path.write_text("single file content")

        args = argparse.Namespace(mac=self.test_mac, image_path=str(single_file_path))
        with self.assertRaises(SystemExit) as excinfo:
            image_command(args)
        self.assertEqual(excinfo.exception.code, 1)

        shutil.rmtree(temp_dir, ignore_errors=True)

    def test_reprovision_stub(self):
        """Test reprovision command (stub)"""
        args = argparse.Namespace(mac=self.test_mac)
        # Call command - just verify it doesn't crash
        reprovision_command(args)

    @patch("distro_cli.cmds.device.container.exec_in_container")
    @patch("distro_cli.cmds.device.container.container_is_running")
    def test_get_device_ip_ipv4(self, mock_is_running, mock_exec):
        """Test get_device_ip returns IPv4 when available"""
        mock_is_running.return_value = True
        mock_exec.return_value = (
            0,
            '{"mac": "aa:bb:cc:dd:ee:ff", "ipv4": "192.168.1.100", "ipv6": "fe80::1"}',
            "",
        )

        ip = get_device_ip(self.test_mac)
        self.assertEqual(ip, "192.168.1.100")

    @patch("distro_cli.cmds.device.container.exec_in_container")
    @patch("distro_cli.cmds.device.container.container_is_running")
    def test_get_device_ip_ipv6_fallback(self, mock_is_running, mock_exec):
        """Test get_device_ip returns IPv6 when IPv4 not available"""
        mock_is_running.return_value = True
        mock_exec.return_value = (
            0,
            '{"mac": "aa:bb:cc:dd:ee:ff", "ipv6": "fe80::1"}',
            "",
        )

        ip = get_device_ip(self.test_mac)
        self.assertEqual(ip, "fe80::1%lo")

    @patch("distro_cli.cmds.device.container.exec_in_container")
    @patch("distro_cli.cmds.device.container.container_is_running")
    def test_get_device_ip_ipv6_global_unscoped(self, mock_is_running, mock_exec):
        """Test get_device_ip does not scope a global IPv6 address"""
        mock_is_running.return_value = True
        mock_exec.return_value = (
            0,
            '{"mac": "aa:bb:cc:dd:ee:ff", "ipv6": "2001:db8::1"}',
            "",
        )

        ip = get_device_ip(self.test_mac)
        self.assertEqual(ip, "2001:db8::1")

    @patch("distro_cli.cmds.device.container.exec_in_container")
    @patch("distro_cli.cmds.device.container.container_is_running")
    @patch("distro_cli.cmds.device.os.execvp")
    def test_ssh_command_calls_execvp_correctly(
        self, mock_execvp, mock_is_running, mock_exec
    ):
        """Test ssh command calls os.execvp with correct arguments"""
        mock_is_running.return_value = True
        mock_exec.return_value = (
            0,
            '{"mac": "aa:bb:cc:dd:ee:ff", "ipv4": "192.168.1.100"}',
            "",
        )

        args = argparse.Namespace(mac=self.test_mac, interface=None)
        ssh_command(args)

        mock_execvp.assert_called_once_with(
            "ssh",
            [
                "ssh",
                "-o",
                "StrictHostKeyChecking=no",
                "-o",
                "UserKnownHostsFile=/dev/null",
                "root@192.168.1.100",
            ],
        )


class TestDeviceCLIIntegration(unittest.TestCase):
    """Test device command CLI integration for image command"""

    def setUp(self):
        """Set up CLI for testing"""
        self.cli = CLI(description="Test CLI")
        setup_device_commands(self.cli)

    def test_image_command_argument_parsing(self):
        """Test that image command arguments are parsed correctly"""
        with tempfile.NamedTemporaryFile(suffix=".tar", delete=False) as f:
            temp_image = f.name

        try:
            args = self.cli.parser.parse_args(
                ["device", "aa:bb:cc:dd:ee:ff", "image", temp_image]
            )
            self.assertEqual(args.mac, "aa:bb:cc:dd:ee:ff")
            self.assertEqual(str(args.image_path), temp_image)
            self.assertTrue(callable(args.func))
            self.assertEqual(args.func, image_command)
        finally:
            Path(temp_image).unlink()


class TestDownloadWithCache(unittest.TestCase):
    """Test _download_with_cache() error cases."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = Path(tempfile.mkdtemp())
        self.cache_dir = self.temp_dir / "cache"

    def tearDown(self):
        """Clean up test directory."""
        if self.temp_dir.exists():
            shutil.rmtree(self.temp_dir)

    def test_download_http_error_404(self):
        """Test that HTTP 404 error raises DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "nonexistent.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                404,
                "Not Found",
                {},
                None,
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("HTTP error 404", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_http_error_500(self):
        """Test that HTTP 500 error raises DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "server_error.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                500,
                "Internal Server Error",
                {},
                None,
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("HTTP error 500", str(context.exception))

    def test_download_http_304_without_cached_file(self):
        """Test that HTTP 304 without cached file raises DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        # Create metadata file but no cached artifact
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        metadata_path = self.cache_dir / (filename + ".http_metadata.json")
        metadata_path.write_text(json.dumps({"etag": "test-etag"}))

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                304,
                "Not Modified",
                {},
                None,
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("HTTP 304 Not Modified", str(context.exception))
            self.assertIn("cached file not found", str(context.exception))

    def test_download_http_304_with_cached_file(self):
        """Test that HTTP 304 with cached file returns cached path."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        # Create metadata file and cached artifact
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        metadata_path = self.cache_dir / (filename + ".http_metadata.json")
        metadata_path.write_text(json.dumps({"etag": "test-etag"}))
        cached_file = self.cache_dir / filename
        cached_file.write_text("cached content")

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                304,
                "Not Modified",
                {},
                None,
            )

            result = _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertEqual(result, cached_file)
            self.assertTrue(result.exists())
            self.assertEqual(result.read_text(), "cached content")

    def test_download_network_error(self):
        """Test that network errors raise DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.URLError("Network unreachable")

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("Failed to download", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_with_etag_and_last_modified(self):
        """Test successful download saves ETag and Last-Modified headers."""
        url_prefix = "http://example.com/"
        filename = "test.tar"

        # Create cache directory
        self.cache_dir.mkdir(parents=True, exist_ok=True)

        # Mock response with headers - needs to support file-like interface
        mock_response = MagicMock()
        mock_response.headers.get = Mock(
            side_effect=lambda key: {
                "ETag": "test-etag-123",
                "Last-Modified": "Mon, 01 Jan 2024 00:00:00 GMT",
            }.get(key)
        )
        # Make read() return data once, then empty to signal EOF
        mock_response.read = Mock(side_effect=[b"test content", b""])

        with patch(
            "distro_cli.cmds.device.urllib.request.urlopen", return_value=mock_response
        ):
            result = _download_with_cache(self.cache_dir, url_prefix, filename)

            # Verify file was created
            self.assertTrue(result.exists())
            self.assertEqual(result.read_text(), "test content")

            # Verify metadata was saved
            metadata_path = self.cache_dir / (filename + ".http_metadata.json")
            self.assertTrue(metadata_path.exists())
            metadata = json.loads(metadata_path.read_text())
            self.assertEqual(metadata["etag"], "test-etag-123")
            self.assertEqual(metadata["last_modified"], "Mon, 01 Jan 2024 00:00:00 GMT")

    def test_download_uses_cached_metadata_for_conditional_request(self):
        """Test that cached metadata is used for conditional requests."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        # Create cached metadata
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        metadata_path = self.cache_dir / (filename + ".http_metadata.json")
        metadata_path.write_text(
            json.dumps(
                {
                    "etag": "cached-etag",
                    "last_modified": "Sun, 31 Dec 2023 00:00:00 GMT",
                }
            )
        )

        # Mock response - needs to support file-like interface for shutil.copyfileobj
        mock_response = MagicMock()
        mock_response.headers.get = Mock(return_value=None)
        # Make read() return data once, then empty to signal EOF
        mock_response.read = Mock(side_effect=[b"new content", b""])

        with (
            patch(
                "distro_cli.cmds.device.urllib.request.urlopen",
                return_value=mock_response,
            ),
            patch("distro_cli.cmds.device.urllib.request.Request") as mock_request,
        ):
            mock_request_instance = MagicMock()
            mock_request.return_value = mock_request_instance

            _download_with_cache(self.cache_dir, url_prefix, filename)

            # Verify Request was created with URL
            mock_request.assert_called_once_with(full_url)

            # Verify conditional headers were added
            mock_request_instance.add_header.assert_any_call(
                "If-None-Match", "cached-etag"
            )
            mock_request_instance.add_header.assert_any_call(
                "If-Modified-Since", "Sun, 31 Dec 2023 00:00:00 GMT"
            )

    def test_download_http_error_403(self):
        """Test that HTTP 403 Forbidden error raises DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "forbidden.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                403,
                "Forbidden",
                {},
                None,
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("HTTP error 403", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_http_error_503(self):
        """Test that HTTP 503 Service Unavailable error raises DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "unavailable.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_urlopen.side_effect = urllib.error.HTTPError(
                full_url,
                503,
                "Service Unavailable",
                {},
                None,
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("HTTP error 503", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_url_error_dns_failure(self):
        """Test that DNS resolution failures raise DistroInfraError."""
        url_prefix = "http://nonexistent.invalid/"
        filename = "test.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            # Simulate DNS failure
            mock_urlopen.side_effect = urllib.error.URLError(
                OSError("nodename nor servname provided, or not known")
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("Failed to download", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_url_error_connection_timeout(self):
        """Test that connection timeout errors raise DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            # Simulate connection timeout
            mock_urlopen.side_effect = urllib.error.URLError(
                TimeoutError("Connection timed out")
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("Failed to download", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_url_error_connection_refused(self):
        """Test that connection refused errors raise DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        with patch("urllib.request.urlopen") as mock_urlopen:
            # Simulate connection refused
            mock_urlopen.side_effect = urllib.error.URLError(
                ConnectionRefusedError("Connection refused")
            )

            with self.assertRaises(DistroInfraError) as context:
                _download_with_cache(self.cache_dir, url_prefix, filename)

            self.assertIn("Failed to download", str(context.exception))
            self.assertIn(full_url, str(context.exception))

    def test_download_generic_exception_during_write(self):
        """Test that generic exceptions during file write raise DistroInfraError."""
        url_prefix = "http://example.com/"
        filename = "test.tar"
        full_url = url_prefix + filename

        # Mock response
        mock_response = MagicMock()
        mock_response.headers.get = Mock(return_value=None)
        mock_response.read = Mock(return_value=b"test content")

        with patch("urllib.request.urlopen", return_value=mock_response):
            # Make cache_dir read-only to trigger write error
            self.cache_dir.mkdir(parents=True, exist_ok=True)

            # Patch the open function to raise an exception
            with patch("pathlib.Path.open", side_effect=OSError("Disk full")):
                with self.assertRaises(DistroInfraError) as context:
                    _download_with_cache(self.cache_dir, url_prefix, filename)

                self.assertIn("Failed to download", str(context.exception))
                self.assertIn(full_url, str(context.exception))


class TestDeviceUpdater(unittest.TestCase):
    """Unit tests for DeviceUpdater class"""

    def setUp(self):
        """Set up test fixtures"""
        self.test_data_dir = Path(__file__).parent / "data"
        self.update_manifest_path = self.test_data_dir / "update_manifest.json"

    def test_validate_non_updatable_component(self):
        """Test that non-updatable components raise error"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="kernel",
        )
        with self.assertRaises(DeviceUpdateError) as ctx:
            updater.validate()
        self.assertIn("is not updatable", str(ctx.exception))
        self.assertIn("Updatable components:", str(ctx.exception))

    def test_validate_component_not_in_manifest(self):
        """Test that component in COMPONENT_SERVICES but missing from manifest raises error"""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump(
                {
                    "distribution_formats": {"onie": "test.bin"},
                    "kernel": {"download": "https://example.com/kernel.tar"},
                },
                f,
            )
            temp_manifest_path = Path(f.name)
        self.addCleanup(temp_manifest_path.unlink)

        manifest = ImageManifest(temp_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
        )
        with self.assertRaises(DeviceUpdateError) as ctx:
            updater.validate()
        self.assertIn("not found in manifest", str(ctx.exception))

    def test_validate_component_with_no_services_allowed_for_other_dependencies(self):
        """Test that component with empty services list is allowed for other_dependencies"""
        # Create a manifest with other_dependencies
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump(
                {
                    "distribution_formats": {"onie": "test.bin"},
                    "kernel": {"download": "https://example.com/kernel.tar"},
                    "other_dependencies": [
                        {"download": "https://example.com/nano.rpm"}
                    ],
                },
                f,
            )
            temp_manifest_path = Path(f.name)
        self.addCleanup(temp_manifest_path.unlink)

        manifest = ImageManifest(temp_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="other_dependencies",
        )
        updater.validate()

    def test_validate_component_with_no_services_raises_error(self):
        """Test that component with empty services list raises error (except other_dependencies)"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-platform-stack",
        )
        # Patch COMPONENT_SERVICES to have empty list for platform-stack
        with patch(
            "distro_cli.lib.device_update.COMPONENT_SERVICES",
            {"fboss-platform-stack": [], "fboss-forwarding-stack": ["wedge_agent"]},
        ):
            with self.assertRaises(DeviceUpdateError) as ctx:
                updater.validate()
            self.assertIn("has no services defined", str(ctx.exception))

    def test_validate_component_without_download_or_execute(self):
        """Test that component without download or execute raises error"""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump(
                {
                    "distribution_formats": {"onie": "test.bin"},
                    "kernel": {"download": "https://example.com/kernel.tar"},
                    "fboss-forwarding-stack": {},
                },
                f,
            )
            temp_manifest_path = Path(f.name)
        self.addCleanup(temp_manifest_path.unlink)

        manifest = ImageManifest(temp_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
        )
        with self.assertRaises(DeviceUpdateError) as ctx:
            updater.validate()
        self.assertIn("neither 'download' nor 'execute'", str(ctx.exception))

    def test_validate_success_forwarding_stack(self):
        """Test successful validation for fboss-forwarding-stack"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
        )
        # Should not raise
        updater.validate()

    def test_validate_success_platform_stack(self):
        """Test successful validation for fboss-platform-stack"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-platform-stack",
        )
        # Should not raise
        updater.validate()

    def test_get_services_from_component_services(self):
        """Test that services are correctly read from COMPONENT_SERVICES dict"""
        manifest = ImageManifest(self.update_manifest_path)

        updater1 = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
        )
        self.assertEqual(
            updater1._get_services(),
            ["wedge_agent", "fsdb", "qsfp_service"],
        )

        updater2 = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-platform-stack",
        )
        self.assertEqual(
            updater2._get_services(),
            [
                "platform_manager",
                "sensor_service",
                "fan_service",
                "data_corral_service",
            ],
        )

    def test_get_services_for_other_dependencies(self):
        """Test that other_dependencies returns empty service list"""
        # Create a manifest with other_dependencies
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump(
                {
                    "distribution_formats": {"onie": "test.bin"},
                    "kernel": {"download": "https://example.com/kernel.tar"},
                    "other_dependencies": [
                        {"download": "https://example.com/nano.rpm"}
                    ],
                },
                f,
            )
            temp_manifest_path = Path(f.name)
        self.addCleanup(temp_manifest_path.unlink)

        manifest = ImageManifest(temp_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="other_dependencies",
        )
        self.assertEqual(updater._get_services(), [])

    def test_update_requires_device_ip(self):
        """Test that update() raises DeviceUpdateError when device_ip is not set"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
            device_ip=None,
        )
        with self.assertRaises(DeviceUpdateError) as ctx:
            updater.update()
        self.assertIn("Device IP not set", str(ctx.exception))

    def _scp_target(self, device_ip: str) -> str:
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
            device_ip=device_ip,
        )
        with patch("distro_cli.lib.device_update.subprocess.run") as mock_run:
            mock_run.return_value = Mock(returncode=0, stdout=b"", stderr=b"")
            updater._transfer_and_execute(Path("/tmp/artifact.tar"), ["wedge_agent"])
        scp_args = next(
            c.args[0] for c in mock_run.call_args_list if c.args[0][0] == "scp"
        )
        return scp_args[-1]

    def test_scp_target_ipv4(self):
        """Test scp target leaves an IPv4 address unbracketed"""
        self.assertRegex(
            self._scp_target("192.168.1.100"),
            r"^root@192\.168\.1\.100:/tmp/fboss-update-[0-9a-f]+/$",
        )

    def test_scp_target_ipv6_bracketed(self):
        """Test scp target brackets global and scoped link-local IPv6 addresses"""
        self.assertRegex(
            self._scp_target("2001:db8::1"),
            r"^root@\[2001:db8::1\]:/tmp/fboss-update-[0-9a-f]+/$",
        )
        self.assertRegex(
            self._scp_target("fe80::1%eth0"),
            r"^root@\[fe80::1%eth0\]:/tmp/fboss-update-[0-9a-f]+/$",
        )

    def test_acquire_artifacts_and_services(self):
        """Test services are available for platform-stack component"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-platform-stack",
        )

        # Validate first
        updater.validate()

        # Verify services come from COMPONENT_SERVICES dict
        services = updater._get_services()
        self.assertEqual(
            services,
            [
                "platform_manager",
                "sensor_service",
                "fan_service",
                "data_corral_service",
            ],
        )

    def test_acquire_artifacts_forwarding_stack(self):
        """Test services are available for forwarding-stack component"""
        manifest = ImageManifest(self.update_manifest_path)
        updater = DeviceUpdater(
            mac="aa:bb:cc:dd:ee:ff",
            manifest=manifest,
            component="fboss-forwarding-stack",
        )

        # Validate first
        updater.validate()

        # Verify services come from COMPONENT_SERVICES dict
        services = updater._get_services()
        self.assertEqual(
            services,
            ["wedge_agent", "fsdb", "qsfp_service"],
        )


if __name__ == "__main__":
    unittest.main()
