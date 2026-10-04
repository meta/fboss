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

# pyre-strict

import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from fboss.oss.scripts import package


class PackageTest(unittest.TestCase):
    @mock.patch.object(package, "get_forwarding_stack_config_paths", return_value={})
    @mock.patch.object(package, "get_platform_descriptor_paths", return_value={})
    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_forwarding_package_includes_npu_sdk_utils(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
        _mock_get_platform_descriptor_paths: mock.MagicMock,
        _mock_get_forwarding_stack_config_paths: mock.MagicMock,
    ) -> None:
        production_files, _ = package._build_target(
            "forwarding-stack", pathlib.Path("/build")
        )

        self.assertEqual(
            "bin/npu_sdk_utils.py",
            production_files[package.NPU_SDK_UTILS_SCRIPT],
        )

    @mock.patch.object(package, "get_forwarding_stack_config_paths", return_value={})
    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_forwarding_package_includes_platform_descriptors(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
        _mock_get_forwarding_stack_config_paths: mock.MagicMock,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            descriptor_dir = (
                platforms_dir
                / "accton"
                / "wedge800bact"
                / "platform_mapping"
                / "generated"
            )
            descriptor_dir.mkdir(parents=True)
            descriptor_path = descriptor_dir / "platform_descriptor.json"
            descriptor_path.write_text("{}")
            (descriptor_dir / "platform_mapping.json").write_text("{}")

            with mock.patch.object(package, "PLATFORM_CONFIGS_DIR", platforms_dir):
                production_files, _ = package._build_target(
                    "forwarding-stack", pathlib.Path("/build")
                )

            self.assertEqual(
                "share/platform_descriptors/platforms/accton/wedge800bact/"
                "platform_mapping/generated/platform_descriptor.json",
                production_files[descriptor_path],
            )

            tar_path = pathlib.Path(temp_dir) / "forwarding-stack.tar"
            package.write_tar(
                str(tar_path),
                {descriptor_path: production_files[descriptor_path]},
            )
            with tarfile.open(tar_path) as archive:
                self.assertIn(
                    "share/platform_descriptors/platforms/accton/wedge800bact/"
                    "platform_mapping/generated/platform_descriptor.json",
                    archive.getnames(),
                )

    @mock.patch.object(package, "get_platform_descriptor_paths", return_value={})
    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_forwarding_test_package_includes_generated_platform_configs(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
        _mock_get_platform_descriptor_paths: mock.MagicMock,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            config_path = (
                platforms_dir
                / "accton/wedge800bact/forwarding_stack/generated/agent/hw_test.conf"
            )
            config_path.parent.mkdir(parents=True)
            config_path.write_text("{}")

            with mock.patch.object(package, "PLATFORM_CONFIGS_DIR", platforms_dir):
                production_files, test_files = package._build_target(
                    "forwarding-stack", pathlib.Path("/build")
                )

            archive_path = (
                "share/configs/platforms/accton/wedge800bact/"
                "forwarding_stack/generated/agent/hw_test.conf"
            )
            self.assertNotIn(config_path, production_files)
            self.assertEqual(archive_path, test_files[config_path])

    def test_agent_benchmark_package_includes_generated_platform_configs(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            config_path = (
                platforms_dir
                / "celestica/montblanc/forwarding_stack/generated/agent/hw_test.conf"
            )
            config_path.parent.mkdir(parents=True)
            config_path.write_text("{}")

            with mock.patch.object(package, "PLATFORM_CONFIGS_DIR", platforms_dir):
                production_files, _ = package._build_target(
                    "agent-benchmarks", pathlib.Path("/build")
                )

            self.assertEqual(
                "share/configs/platforms/celestica/montblanc/"
                "forwarding_stack/generated/agent/hw_test.conf",
                production_files[config_path],
            )

    @mock.patch.object(package, "get_forwarding_stack_config_paths", return_value={})
    @mock.patch.object(package, "get_platform_descriptor_paths", return_value={})
    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_forwarding_packages_include_read_only_npu_sdk_metadata(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
        _mock_get_platform_descriptor_paths: mock.MagicMock,
        _mock_get_forwarding_stack_config_paths: mock.MagicMock,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            metadata_path = (
                build_dir / "build" / "fboss" / package.NPU_SDK_METADATA_FILENAME
            )
            metadata_path.parent.mkdir(parents=True)
            metadata_path.write_text("{}\n")
            metadata_path.chmod(0o444)

            production_files, test_files = package._build_target(
                "forwarding-stack", build_dir
            )

            self.assertEqual(
                package.NPU_SDK_METADATA_ARCHIVE_PATH,
                production_files[metadata_path],
            )
            self.assertEqual(
                package.NPU_SDK_METADATA_ARCHIVE_PATH,
                test_files[metadata_path],
            )

            tar_path = build_dir / "metadata.tar"
            package.write_tar(
                str(tar_path),
                {metadata_path: package.NPU_SDK_METADATA_ARCHIVE_PATH},
            )
            with tarfile.open(tar_path) as archive:
                self.assertEqual(
                    0o444,
                    archive.getmember(package.NPU_SDK_METADATA_ARCHIVE_PATH).mode,
                )

    @mock.patch.object(package, "get_forwarding_stack_config_paths", return_value={})
    @mock.patch.object(package, "get_platform_descriptor_paths", return_value={})
    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_forwarding_package_rejects_writable_npu_sdk_metadata(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
        _mock_get_platform_descriptor_paths: mock.MagicMock,
        _mock_get_forwarding_stack_config_paths: mock.MagicMock,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            metadata_path = (
                build_dir / "build" / "fboss" / package.NPU_SDK_METADATA_FILENAME
            )
            metadata_path.parent.mkdir(parents=True)
            metadata_path.write_text("{}\n")

            with self.assertRaisesRegex(RuntimeError, "must be read-only"):
                package._build_target("forwarding-stack", build_dir)

    def test_finds_unsuffixed_project_dir(self) -> None:
        """getdeps installs first-party projects without a build-config hash."""
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            (build_dir / "installed" / "bgp" / "sbin").mkdir(parents=True)

            self.assertEqual(
                package._find_installed_project_dir(build_dir, "bgp"),
                build_dir / "installed" / "bgp",
            )

    def test_libs_come_from_the_variant_tree(self) -> None:
        """libfolly.so ships from folly-python; installed/folly holds no .so."""
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            installed = build_dir / "installed"
            (installed / "folly").mkdir(parents=True)
            (installed / "folly-python" / "lib").mkdir(parents=True)
            (installed / "folly-python" / "lib" / "libfolly.so.0.58.0").touch()

            libs = package._find_getdeps_libs(build_dir, ["folly"])
            self.assertEqual(list(libs.values()), ["lib/libfolly.so.0.58.0"])

    def test_falls_back_to_the_hashed_dir(self) -> None:
        """Third-party projects only ever appear with the hash suffix."""
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            hashed = build_dir / "installed" / "boost-uWVzwhvt2_BurxQtUxbB8EM2b8"
            hashed.mkdir(parents=True)

            self.assertEqual(
                package._find_installed_pkg_dir(build_dir, "boost"), hashed
            )

    def test_returns_none_when_absent(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            (build_dir / "installed").mkdir()

            self.assertIsNone(package._find_installed_project_dir(build_dir, "bgp"))

    @mock.patch.object(package, "_find_getdeps_libs", return_value={})
    def test_platform_package_requires_all_binaries(
        self,
        _mock_find_getdeps_libs: mock.MagicMock,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = pathlib.Path(temp_dir)
            bin_dir = build_dir / "build" / "fboss"
            bin_dir.mkdir(parents=True)

            for binary in package.PLATFORM_BINARIES:
                if binary != "led_service":
                    (bin_dir / binary).touch()

            with self.assertRaisesRegex(RuntimeError, "led_service"):
                package._build_target("platform-stack", build_dir)
