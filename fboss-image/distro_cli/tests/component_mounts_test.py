#!/usr/bin/env python3

# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

"""
Unit tests for ComponentBuilder manifest-declared extra mounts.
"""

import unittest
from pathlib import Path
from unittest.mock import patch

from distro_cli.builder.component import ComponentBuilder
from distro_cli.lib.artifact import ArtifactStore
from distro_cli.lib.exceptions import ComponentError
from distro_cli.tests.test_helpers import enter_tempdir, override_artifact_store_dir


def _execute_entry(mounts=None):
    """An execute-mode component entry, optionally with extra mounts."""
    entry = {"execute": ["build.sh"], "artifact": "*.rpm"}
    if mounts is not None:
        entry["mounts"] = mounts
    return entry


class TestComponentMounts(unittest.TestCase):
    """Test the generic 'mounts' field of execute-mode components."""

    def setUp(self):
        """Sandbox temp dir, artifact store, manifest dir and build script."""
        self._tempdir_ctx = enter_tempdir("component_mounts_test_")
        self.temp_dir = self._tempdir_ctx.__enter__()
        self._artifact_store_ctx = override_artifact_store_dir(self.temp_dir)
        self._artifact_store_ctx.__enter__()

        self.manifest_dir = self.temp_dir / "manifest"
        self.manifest_dir.mkdir()
        self.script = self.manifest_dir / "build.sh"
        self.script.touch()
        self.store = ArtifactStore()

    def tearDown(self):
        """Exit context managers in reverse order."""
        self._artifact_store_ctx.__exit__(None, None, None)
        self._tempdir_ctx.__exit__(None, None, None)

    def _builder(self, component_data):
        return ComponentBuilder(
            component_name="other_dependencies[0]",
            component_data=component_data,
            manifest_dir=self.manifest_dir,
            store=self.store,
            artifact_pattern="*.rpm",
        )

    def _run_execute(self, component_data):
        """Run _execute_build with container + artifact lookup mocked.

        Returns the (volumes, read_only_volumes) dicts the mocked container
        entry point received.
        """
        builder = self._builder(component_data)
        fake_artifact = self.temp_dir / "out.rpm"
        fake_artifact.touch()
        with (
            patch(
                "distro_cli.builder.component.get_abs_path",
                side_effect=lambda p: self.temp_dir / p,
            ),
            patch(
                "distro_cli.builder.component.execute_build_in_container"
            ) as mock_execute,
            patch(
                "distro_cli.builder.component.find_artifact_in_dir",
                return_value=fake_artifact,
            ),
        ):
            result = builder._execute_build(
                component_data["execute"], component_data["artifact"]
            )
        self.assertEqual(result, fake_artifact)
        kwargs = mock_execute.call_args.kwargs
        return kwargs["volumes"], kwargs["read_only_volumes"]

    def test_mounts_add_extra_volumes(self):
        """Relative host paths resolve from the manifest dir into volumes."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        volumes, read_only = self._run_execute(
            _execute_entry(mounts=[{"host": "../extra", "container": "/extra-ro"}])
        )
        self.assertIn(extra.resolve(), volumes)
        self.assertEqual(volumes[extra.resolve()], Path("/extra-ro"))
        self.assertIsNone(read_only)
        # Standard mounts are untouched.
        self.assertIn(Path("/src"), list(volumes.values()))

    def test_absolute_host_path(self):
        """Absolute host paths are used as-is."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        volumes, _ = self._run_execute(
            _execute_entry(mounts=[{"host": str(extra), "container": "/extra-ro"}])
        )
        self.assertEqual(volumes[extra], Path("/extra-ro"))

    def test_ro_mode_mounts_read_only(self):
        """mode=ro routes the mount to read_only_volumes."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        volumes, read_only = self._run_execute(
            _execute_entry(
                mounts=[{"host": "../extra", "container": "/extra-ro", "mode": "ro"}]
            )
        )
        self.assertNotIn(extra.resolve(), volumes)
        self.assertEqual(read_only[extra.resolve()], Path("/extra-ro"))

    def test_bad_mode_raises(self):
        """Only rw and ro modes are accepted."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        with self.assertRaises(ComponentError) as ctx:
            self._run_execute(
                _execute_entry(
                    mounts=[
                        {
                            "host": "../extra",
                            "container": "/extra-ro",
                            "mode": "sometimes",
                        }
                    ]
                )
            )
        self.assertIn("mode", str(ctx.exception))

    def test_missing_host_path_raises(self):
        """A mount whose host path does not exist fails with context."""
        with self.assertRaises(ComponentError) as ctx:
            self._run_execute(
                _execute_entry(mounts=[{"host": "../nope", "container": "/extra-ro"}])
            )
        self.assertIn("not found", str(ctx.exception))

    def test_relative_container_path_raises(self):
        """Container mount points must be absolute."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        with self.assertRaises(ComponentError) as ctx:
            self._run_execute(
                _execute_entry(mounts=[{"host": "../extra", "container": "extra-ro"}])
            )
        self.assertIn("absolute", str(ctx.exception))

    def test_standard_mount_collision_raises(self):
        """Mounts may not shadow the standard /src, /build, /output mounts."""
        extra = self.temp_dir / "extra"
        extra.mkdir()
        with self.assertRaises(ComponentError) as ctx:
            self._run_execute(
                _execute_entry(mounts=[{"host": "../extra", "container": "/src"}])
            )
        self.assertIn("collides", str(ctx.exception))

    def test_malformed_mount_raises(self):
        """Non-dict entries and missing keys fail loudly."""
        for bad in (
            ["../extra"],
            [{"host": "../extra"}],
            [{"container": "/extra-ro"}],
        ):
            with self.subTest(mounts=bad):
                with self.assertRaises(ComponentError):
                    self._run_execute(_execute_entry(mounts=bad))

    def test_no_mounts_key_is_noop(self):
        """Components without 'mounts' build exactly the standard volumes."""
        volumes, read_only = self._run_execute(_execute_entry())
        self.assertEqual(
            sorted(str(v) for v in volumes.values()),
            ["/build", "/fboss/oss/scripts", "/output", "/src", "/tools"],
        )
        self.assertIsNone(read_only)
