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

import pathlib
import tempfile
import unittest

from fboss.oss.scripts.platform_configs_utils import (
    copy_forwarding_stack_configs,
    copy_platform_descriptors,
    get_forwarding_stack_config_paths,
    get_platform_descriptor_paths,
)


class PlatformConfigsUtilsTest(unittest.TestCase):
    def _create_descriptor_dir(
        self, platforms_dir: pathlib.Path, vendor: str, platform: str
    ) -> pathlib.Path:
        generated_dir = (
            platforms_dir / vendor / platform / "platform_mapping" / "generated"
        )
        generated_dir.mkdir(parents=True)
        for filename in (
            "platform_descriptor.json",
            "platform_mapping.json",
            "port_id_to_port_assignment.json",
            "raw_platform_mapping.json",
        ):
            (generated_dir / filename).write_text(filename)
        (generated_dir / "not_packaged.txt").write_text("not packaged")
        return generated_dir

    def test_get_platform_descriptor_paths_preserves_source_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            accton_dir = self._create_descriptor_dir(
                platforms_dir, "accton", "wedge800bact"
            )
            nexthop_dir = self._create_descriptor_dir(
                platforms_dir, "nexthop", "m4062nhp"
            )

            paths = get_platform_descriptor_paths(platforms_dir)

            self.assertEqual(
                "share/platform_descriptors/platforms/accton/wedge800bact/"
                "platform_mapping/generated/platform_descriptor.json",
                paths[accton_dir / "platform_descriptor.json"],
            )
            self.assertEqual(
                "share/platform_descriptors/platforms/nexthop/m4062nhp/"
                "platform_mapping/generated/platform_mapping.json",
                paths[nexthop_dir / "platform_mapping.json"],
            )
            self.assertEqual(8, len(paths))
            self.assertNotIn(accton_dir / "not_packaged.txt", paths)

    def test_copy_platform_descriptors_uses_packaged_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            platforms_dir = root / "platforms"
            self._create_descriptor_dir(platforms_dir, "accton", "wedge800bact")
            package_root = root / "package"

            copy_platform_descriptors(platforms_dir, package_root)

            destination = (
                package_root
                / "share/platform_descriptors/platforms/accton/wedge800bact/"
                "platform_mapping/generated"
            )
            self.assertEqual(
                {
                    "platform_descriptor.json",
                    "platform_mapping.json",
                    "port_id_to_port_assignment.json",
                    "raw_platform_mapping.json",
                },
                {path.name for path in destination.iterdir()},
            )

    def test_platform_descriptor_requires_sibling_mapping(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            generated_dir = (
                platforms_dir
                / "accton"
                / "wedge800bact"
                / "platform_mapping"
                / "generated"
            )
            generated_dir.mkdir(parents=True)
            (generated_dir / "platform_descriptor.json").write_text("{}")

            with self.assertRaisesRegex(RuntimeError, "platform_mapping.json"):
                get_platform_descriptor_paths(platforms_dir)

    def _create_forwarding_stack_generated_dir(
        self, platforms_dir: pathlib.Path, vendor: str, platform: str
    ) -> pathlib.Path:
        generated_dir = (
            platforms_dir / vendor / platform / "forwarding_stack" / "generated"
        )
        agent_dir = generated_dir / "agent"
        agent_dir.mkdir(parents=True)
        (agent_dir / "default.conf").write_text("default")
        (agent_dir / "hw_test.conf").write_text("hw_test")
        return generated_dir

    def test_get_forwarding_stack_config_paths_preserves_source_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            generated_dir = self._create_forwarding_stack_generated_dir(
                platforms_dir, "accton", "wedge800bact"
            )
            platform_without_generated = (
                platforms_dir / "nexthop" / "m4062nhp" / "forwarding_stack"
            )
            platform_without_generated.mkdir(parents=True)
            (platform_without_generated / "source.conf").write_text("source")

            paths = get_forwarding_stack_config_paths(platforms_dir)

            self.assertEqual(
                "share/configs/platforms/accton/wedge800bact/"
                "forwarding_stack/generated/agent/hw_test.conf",
                paths[generated_dir / "agent/hw_test.conf"],
            )
            self.assertEqual(2, len(paths))
            self.assertNotIn(platform_without_generated / "source.conf", paths)

    def test_copy_forwarding_stack_configs_uses_packaged_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            platforms_dir = root / "platforms"
            self._create_forwarding_stack_generated_dir(
                platforms_dir, "accton", "wedge800bact"
            )
            package_root = root / "package"

            copy_forwarding_stack_configs(platforms_dir, package_root)

            destination = (
                package_root / "share/configs/platforms/accton/wedge800bact/"
                "forwarding_stack/generated/agent"
            )
            self.assertEqual(
                {"default.conf", "hw_test.conf"},
                {path.name for path in destination.iterdir()},
            )

    def test_forwarding_stack_configs_require_generated_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            platforms_dir = pathlib.Path(temp_dir) / "platforms"
            platforms_dir.mkdir()

            with self.assertRaisesRegex(
                RuntimeError, "No generated forwarding stack configs"
            ):
                get_forwarding_stack_config_paths(platforms_dir)
