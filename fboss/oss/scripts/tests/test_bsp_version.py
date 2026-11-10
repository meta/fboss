# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fboss.oss.scripts import component_version


class BspVersionTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        (self.source / ".git").mkdir(parents=True)
        (self.source / ".git/HEAD").write_text("ref: refs/heads/main\n")
        self.output = self.root / "output"
        self.output.mkdir()
        self.rpm = (
            self.output / "fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64-4.4.2-1.x86_64.rpm"
        )
        self.rpm.write_bytes(b"bsp rpm")
        self.revision = "b" * 40
        self.fb_revision = "a" * 40
        self.addCleanup(mock.patch.stopall)
        mock.patch.dict(
            component_version.os.environ,
            {
                "FBOSS_SOURCE_REVISION": self.fb_revision,
                "FBOSS_SOURCE_DIRTY": "false",
            },
            clear=True,
        ).start()
        mock.patch.object(
            component_version,
            "_query_rpm",
            return_value="fboss_bsp_kmods-6.11.1-1.fboss.el9.x86_64\n4.4.2-1\nx86_64",
        ).start()

    def capture(self, dirty: bool = False) -> dict:
        with (
            mock.patch.object(
                sys,
                "argv",
                [
                    "component_version.py",
                    "bsp",
                    "--output-dir",
                    str(self.output),
                    "--source-dir",
                    str(self.source),
                ],
            ),
            mock.patch.object(
                component_version.subprocess,
                "check_output",
                side_effect=[self.revision, " M kmods/Makefile" if dirty else ""],
            ),
        ):
            component_version.main()
        return json.loads((self.output / "share/bsp-version.json").read_text())

    def test_bsp_subcommand_records_rpm_and_distinct_source_identities(self) -> None:
        (self.output / "fboss_bsp_kmods-4.4.2-1.src.rpm").write_bytes(b"source rpm")
        declaration = self.capture(dirty=True)
        self.assertEqual(declaration["declaration_schema"], component_version.SCHEMA)
        self.assertEqual(declaration["producer"], "component_version.py")
        build = declaration["build"]
        self.assertEqual(build["source_revision"], self.revision)
        self.assertTrue(build["source_dirty"])
        self.assertEqual(
            build["fboss"],
            {"source_revision": self.fb_revision, "source_dirty": False},
        )
        self.assertNotIn("binaries", declaration)
        self.assertEqual(
            declaration["versions"],
            {"bsp": "4.4.2-1", "kernel": "6.11.1-1.fboss.el9"},
        )
        self.assertEqual(build["kernel_release"], "6.11.1-1.fboss.el9.x86_64")

        self.assertEqual(
            build["rpm_sha256"],
            {self.rpm.name: hashlib.sha256(b"bsp rpm").hexdigest()},
        )

    def test_build_version_is_supplied_without_a_default(self) -> None:
        self.assertNotIn("build_version", self.capture()["build"])
        with mock.patch.dict(
            component_version.os.environ, {"FBOSS_BUILD_VERSION": "bsp-42"}
        ):
            self.assertEqual(self.capture()["build"]["build_version"], "bsp-42")
        self.assertNotIn("environment", self.capture()["build"])

    def test_unavailable_source_identity_is_not_replaced_by_fboss_revision(
        self,
    ) -> None:
        with mock.patch.object(
            component_version.subprocess, "check_output", side_effect=OSError
        ):
            component_version.capture_bsp_build(self.output, self.source)
        build = json.loads((self.output / "share/bsp-version.json").read_text())[
            "build"
        ]
        self.assertNotIn("source_revision", build)
        self.assertNotIn("source_dirty", build)
        self.assertEqual(build["fboss"]["source_revision"], self.fb_revision)

    def test_missing_or_multiple_binary_rpms_are_rejected(self) -> None:
        self.rpm.unlink()
        with self.assertRaisesRegex(ValueError, "Expected one"):
            component_version.capture_bsp_build(self.output, self.source)
        self.rpm.write_bytes(b"bsp rpm")
        (self.output / "fboss_bsp_kmods-other.rpm").write_bytes(b"other")
        with self.assertRaisesRegex(ValueError, "Expected one"):
            component_version.capture_bsp_build(self.output, self.source)

    def test_rpm_without_target_kernel_is_rejected(self) -> None:
        with mock.patch.object(
            component_version,
            "_query_rpm",
            return_value="fboss_bsp_kmods\n4.4.2-1\nx86_64",
        ):
            with self.assertRaisesRegex(ValueError, "target kernel"):
                component_version.capture_bsp_build(self.output, self.source)


if __name__ == "__main__":
    unittest.main()
