#!/usr/bin/env python3
# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""Unit tests for download utilities."""

import http.server
import json
import os
import shutil
import socketserver
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from distro_cli.lib.artifact import ArtifactStore
from distro_cli.lib.constants import MANIFOLD_CLI_VAR
from distro_cli.lib.download import download_artifact, HTTP_METADATA_FILENAME
from distro_cli.lib.exceptions import ArtifactError
from distro_cli.tests.test_helpers import enter_tempdir, override_artifact_store_dir


class TestDownloadArtifact(unittest.TestCase):
    """Test download_artifact function."""

    def setUp(self):
        """Set up test fixtures."""
        # Use sandbox-safe temporary directory
        self._tempdir_ctx = enter_tempdir("download_test_")
        self.temp_dir = self._tempdir_ctx.__enter__()
        self.manifest_dir = self.temp_dir / "manifest"
        self.manifest_dir.mkdir()

        # Override artifact store directory for testing
        self.artifact_store_dir = self.temp_dir / ".artifacts"
        self._artifact_store_ctx = override_artifact_store_dir(self.artifact_store_dir)
        self._artifact_store_ctx.__enter__()

    def tearDown(self):
        """Clean up test directory."""
        # Exit context managers in reverse order
        self._artifact_store_ctx.__exit__(None, None, None)
        self._tempdir_ctx.__exit__(None, None, None)

    def test_download_file_url(self):
        """Test downloading from file:// URL."""
        source_file = self.temp_dir / "source.txt"
        source_file.write_text("file content")

        url = f"file://{source_file}"
        cache_hit, data_files, meta_files = download_artifact(
            url, self.manifest_dir, [], []
        )

        self.assertFalse(cache_hit)
        self.assertEqual(len(data_files), 1)
        self.assertEqual(len(meta_files), 1)  # Now includes mtime metadata
        self.assertTrue(data_files[0].exists())
        self.assertEqual(data_files[0].read_text(), "file content")

        # Verify metadata contains mtime
        metadata = json.loads(meta_files[0].read_text())
        self.assertIn("mtime", metadata)

    def test_download_file_url_does_not_move_source_file(self):
        """Test that downloading file:// URL does not move the source file.

        The artifact store should only move a temporary copy created by
        download_artifact(), leaving the original manifest file in place.
        """
        source_file = self.temp_dir / "source.txt"
        source_file.write_text("file content")

        url = f"file://{source_file}"
        store = ArtifactStore()
        store_key = "test-file-url-does-not-move-source"

        def fetch_fn(cached_data, cached_meta):
            return download_artifact(url, self.manifest_dir, cached_data, cached_meta)

        data_files, meta_files = store.get(store_key, fetch_fn)

        # The original source file must remain at its original location.
        self.assertTrue(source_file.exists())
        # The stored data file should be a different path (a copied temp file).
        self.assertEqual(len(data_files), 1)
        self.assertNotEqual(data_files[0], source_file)
        self.assertTrue(data_files[0].exists())
        self.assertEqual(data_files[0].read_text(), "file content")

    def test_download_file_url_mtime_caching(self):
        """Test that file:// URLs use mtime for cache detection."""
        source_file = self.temp_dir / "source.txt"
        source_file.write_text("original content")

        url = f"file://{source_file}"

        # First download - should not be a cache hit
        cache_hit1, data_files1, meta_files1 = download_artifact(
            url, self.manifest_dir, [], []
        )
        self.assertFalse(cache_hit1)
        self.assertEqual(len(data_files1), 1)
        self.assertEqual(len(meta_files1), 1)

        # Second download without modifying file - should be a cache hit
        cache_hit2, data_files2, meta_files2 = download_artifact(
            url, self.manifest_dir, data_files1, meta_files1
        )
        self.assertTrue(cache_hit2)
        self.assertEqual(data_files2, data_files1)
        self.assertEqual(meta_files2, meta_files1)

        # Modify the source file
        time.sleep(0.01)  # Ensure mtime changes
        source_file.write_text("modified content")

        # Third download after modification - should NOT be a cache hit
        cache_hit3, data_files3, meta_files3 = download_artifact(
            url, self.manifest_dir, data_files2, meta_files2
        )
        self.assertFalse(cache_hit3)
        self.assertEqual(len(data_files3), 1)
        self.assertEqual(len(meta_files3), 1)

    def test_metadata_file_structure(self):
        """Test that metadata files are created with correct structure."""
        # Create a test metadata file
        meta_file = self.temp_dir / HTTP_METADATA_FILENAME
        meta_file.write_text(
            json.dumps(
                {"etag": "test-etag", "last_modified": "Mon, 01 Jan 2024 00:00:00 GMT"}
            )
        )

        # Verify structure
        metadata = json.loads(meta_file.read_text())
        self.assertIn("etag", metadata)
        self.assertIn("last_modified", metadata)
        self.assertEqual(metadata["etag"], "test-etag")
        self.assertEqual(metadata["last_modified"], "Mon, 01 Jan 2024 00:00:00 GMT")


class SimpleHTTPServer:
    """Simple HTTP server for testing downloads."""

    def __init__(self, directory, port=0):
        """Initialize HTTP server.

        Args:
            directory: Directory to serve files from
            port: Port to listen on (default: 0 = let OS assign random port)
        """
        self.directory = directory
        self.port = port
        self.server = None
        self.server_thread = None
        self._ready = threading.Event()

    def start(self):
        """Start the HTTP server in a background thread."""
        # Enable SO_REUSEADDR to avoid "Address already in use" errors
        socketserver.TCPServer.allow_reuse_address = True

        self.server = socketserver.TCPServer(
            ("127.0.0.1", self.port),
            lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
                *args, directory=str(self.directory), **kwargs
            ),
        )

        # Get the actual port assigned by the OS (important when port=0)
        self.port = self.server.server_address[1]

        def serve_with_ready_signal():
            """Serve forever and signal when ready."""
            self._ready.set()
            self.server.serve_forever()

        self.server_thread = threading.Thread(
            target=serve_with_ready_signal, daemon=True
        )
        self.server_thread.start()

        # Wait for server to be ready (with timeout)
        if not self._ready.wait(timeout=180):  # 3 minutes max
            raise RuntimeError("HTTP server failed to start within 3 minutes")

    def stop(self):
        """Stop the HTTP server."""
        if self.server:
            self.server.shutdown()
            self.server.server_close()

    def get_url(self, filename):
        """Get URL for a file served by this server.

        Args:
            filename: Name of file to get URL for

        Returns:
            Full HTTP URL to the file
        """
        return f"http://127.0.0.1:{self.port}/{filename}"


class TestDownloadHTTP(unittest.TestCase):
    """Test download_artifact with HTTP server."""

    @classmethod
    def setUpClass(cls):
        """Start HTTP server for all tests."""
        cls.test_dir = Path(tempfile.mkdtemp())

        # Create a test file to serve
        cls.test_file = cls.test_dir / "test_artifact.tar.zst"
        cls.test_file.write_text("test artifact content from HTTP")

        # Start HTTP server (port=0 lets OS assign random available port)
        cls.http_server = SimpleHTTPServer(cls.test_dir)
        cls.http_server.start()

    @classmethod
    def tearDownClass(cls):
        """Stop HTTP server and cleanup."""
        cls.http_server.stop()
        if cls.test_dir.exists():
            shutil.rmtree(cls.test_dir)

    def setUp(self):
        """Set up test fixtures."""
        # Use sandbox-safe temporary directory
        self._tempdir_ctx = enter_tempdir("download_http_test_")
        self.temp_dir = self._tempdir_ctx.__enter__()
        self.manifest_dir = self.temp_dir / "manifest"
        self.manifest_dir.mkdir()

        # Override artifact store directory for testing
        self.artifact_store_dir = self.temp_dir / ".artifacts"
        self._artifact_store_ctx = override_artifact_store_dir(self.artifact_store_dir)
        self._artifact_store_ctx.__enter__()

    def tearDown(self):
        """Clean up test directory."""
        # Exit context managers in reverse order
        self._artifact_store_ctx.__exit__(None, None, None)
        self._tempdir_ctx.__exit__(None, None, None)

    def test_download_http_url(self):
        """Test downloading from HTTP URL."""
        url = self.http_server.get_url("test_artifact.tar.zst")

        cache_hit, data_files, meta_files = download_artifact(
            url, self.manifest_dir, [], []
        )

        self.assertFalse(cache_hit)
        self.assertEqual(len(data_files), 1)
        self.assertEqual(len(meta_files), 1)
        self.assertTrue(data_files[0].exists())
        self.assertEqual(data_files[0].read_text(), "test artifact content from HTTP")

        # Verify metadata file exists and has correct structure
        self.assertTrue(meta_files[0].exists())
        self.assertEqual(meta_files[0].name, HTTP_METADATA_FILENAME)

    def test_download_http_caching(self):
        """Test HTTP caching with ETag/Last-Modified."""
        url = self.http_server.get_url("test_artifact.tar.zst")

        # First download
        cache_hit1, data_files1, meta_files1 = download_artifact(
            url, self.manifest_dir, [], []
        )

        self.assertFalse(cache_hit1)
        self.assertEqual(len(data_files1), 1)
        self.assertEqual(len(meta_files1), 1)

        # Second download with cached files - should get 304 Not Modified
        cache_hit2, data_files2, meta_files2 = download_artifact(
            url, self.manifest_dir, data_files1, meta_files1
        )

        # Should be a cache hit (304 Not Modified)
        self.assertTrue(cache_hit2)
        self.assertEqual(data_files2, data_files1)
        self.assertEqual(meta_files2, meta_files1)


class ManifoldDownloadTest(unittest.TestCase):
    """Test the manifold: scheme in download_artifact."""

    def setUp(self):
        """Set up test fixtures."""
        self._tempdir_ctx = enter_tempdir("manifold_download_test_")
        self.temp_dir = self._tempdir_ctx.__enter__()

        self._artifact_store_ctx = override_artifact_store_dir(
            self.temp_dir / ".artifacts"
        )
        self._artifact_store_ctx.__enter__()

    def tearDown(self):
        """Clean up test directory."""
        self._artifact_store_ctx.__exit__(None, None, None)
        self._tempdir_ctx.__exit__(None, None, None)

    def test_fetches_blob_via_cli(self):
        """A manifold: URL shells out to the CLI and returns the fetched file."""
        fake_cli = self.temp_dir / "fake_manifold"
        fake_cli.write_text(
            "#!/bin/bash\nprintf 'payload' > \"$3\"\n", encoding="utf-8"
        )
        fake_cli.chmod(0o755)

        with mock.patch.dict(os.environ, {MANIFOLD_CLI_VAR: str(fake_cli)}):
            cache_hit, data_files, meta_files = download_artifact(
                "manifold:bucket/tree/led/fboss_bins.tar.zst"
            )

        self.assertFalse(cache_hit)
        self.assertEqual(len(data_files), 1)
        self.assertEqual(data_files[0].name, "fboss_bins.tar.zst")
        self.assertEqual(data_files[0].read_text(), "payload")
        # Manifold exposes no ETag/mtime, so nothing is cached between runs.
        self.assertEqual(meta_files, [])

    def test_raises_when_cli_missing(self):
        """A missing CLI names the override variable rather than failing opaquely."""
        with mock.patch.dict(os.environ, {MANIFOLD_CLI_VAR: "/nonexistent/manifold"}):
            with self.assertRaises(ArtifactError) as ctx:
                download_artifact("manifold:bucket/tree/led/fboss_bins.tar.zst")

        self.assertIn(MANIFOLD_CLI_VAR, str(ctx.exception))

    def test_raises_when_fetch_fails(self):
        """A non-zero CLI exit surfaces the CLI's stderr."""
        fake_cli = self.temp_dir / "failing_manifold"
        fake_cli.write_text(
            "#!/bin/bash\necho 'Path not found' >&2\nexit 1\n", encoding="utf-8"
        )
        fake_cli.chmod(0o755)

        with mock.patch.dict(os.environ, {MANIFOLD_CLI_VAR: str(fake_cli)}):
            with self.assertRaises(ArtifactError) as ctx:
                download_artifact("manifold:bucket/tree/nope.tar")

        self.assertIn("Path not found", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
