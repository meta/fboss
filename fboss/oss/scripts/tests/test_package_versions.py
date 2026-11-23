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
import tarfile
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest import mock

from fboss.oss.scripts import component_version, package, package_fboss


class PackageVersionsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.binary = self.build / "fsdb"
        self.binary.write_bytes(b"service payload")
        self.record = {
            "schema": 1,
            "build_target": "fsdb",
            "source_revision": "a" * 40,
            "sha256": hashlib.sha256(b"service payload").hexdigest(),
        }
        component_version.write_if_changed(
            self.build / component_version.RECORD_DIRECTORY / "fsdb.json",
            json.dumps(self.record),
        )

    def test_named_archive_contains_payload_and_declaration(self) -> None:
        archive_path = self.root / "forwarding-stack.tar"
        package.write_tar(str(archive_path), {self.binary: "bin/fsdb"})
        with tarfile.open(archive_path) as archive:
            member = archive.extractfile(component_version.DECLARATION_PATH)
            self.assertIsNotNone(member)
            assert member is not None
            declaration = json.load(member)
            self.assertEqual(declaration["binaries"], {"fsdb": self.record})
            self.assertEqual(
                archive.getmember("bin/fsdb").size, len(b"service payload")
            )

    def test_ci_packager_replaces_binary_and_its_build_record(self) -> None:
        destination = self._package_replacement(with_metadata=True)
        declaration = json.loads(
            (destination / component_version.DECLARATION_PATH).read_text()
        )
        self.assertEqual(
            declaration["binaries"],
            {
                "fsdb": {
                    **self.record,
                    "source_revision": "b" * 40,
                    "sha256": hashlib.sha256(b"new service payload").hexdigest(),
                }
            },
        )

    def test_ci_packager_omits_metadata_for_legacy_replacement(self) -> None:
        destination = self._package_replacement(with_metadata=False)
        self.assertFalse((destination / component_version.DECLARATION_PATH).exists())

    def _package_replacement(self, with_metadata: bool) -> Path:
        second_build = self.root / "second_build"
        second_build.mkdir()
        (second_build / "fsdb").write_bytes(b"new service payload")
        second_record = {
            **self.record,
            "source_revision": "b" * 40,
            "sha256": hashlib.sha256(b"new service payload").hexdigest(),
        }
        if with_metadata:
            component_version.write_if_changed(
                second_build / component_version.RECORD_DIRECTORY / "fsdb.json",
                json.dumps(second_record),
            )
        packager = package_fboss.PackageFboss.__new__(package_fboss.PackageFboss)
        destination = self.root / "package"
        (destination / "bin").mkdir(parents=True)
        (destination / "lib").mkdir()
        skipped = [
            "_update_ld_library_path",
            "_copy_npu_sdk_metadata",
            "_copy_run_scripts",
            "_copy_run_configs",
            "_copy_configs",
            "_copy_known_bad_tests",
            "_copy_unsupported_tests",
            "_copy_production_features",
            "_copy_completion_scripts",
        ]
        with (
            mock.patch.object(
                packager,
                "_get_dir_for",
                return_value=[str(self.build), str(second_build)],
            ),
            mock.patch.object(packager, "_get_dependency_paths", return_value=set()),
            mock.patch.object(packager, "_sdk_sibling_libs", return_value=set()),
            mock.patch.object(
                packager, "get_fboss_subdirectory", return_value=str(self.root)
            ),
            mock.patch.object(package_fboss, "copy_platform_descriptors"),
            mock.patch.object(package_fboss, "copy_forwarding_stack_configs"),
        ):
            with ExitStack() as stack:
                for method in skipped:
                    stack.enter_context(mock.patch.object(packager, method))
                packager._copy_binaries(str(destination))
        self.assertEqual(
            (destination / "bin" / "fsdb").read_bytes(), b"new service payload"
        )
        self.assertFalse((destination / "bin" / "fsdb.json").exists())
        return destination


if __name__ == "__main__":
    unittest.main()
