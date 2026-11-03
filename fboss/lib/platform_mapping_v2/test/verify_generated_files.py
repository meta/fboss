#!/usr/bin/env python3
# pyre-strict

import filecmp
import os
import sys
import unittest
from typing import ClassVar

from fboss.lib.platform_mapping_v2.gen import (
    generate_platform_mappings_from_vendor_data,
    get_platform_descriptor_data,
    get_platform_mapping_output_dir,
    OSS_MULTI_NPU_SUPPORTED_PLATFORMS,
)
from fboss.lib.platform_mapping_v2.read_files_utils import (
    discover_platform_mapping_inputs,
    PlatformMappingInputs,
)


class TestVerifyPlatformMappingGeneratedFiles(unittest.TestCase):
    """
    GitHub Actions test that verifies a PR includes all generated files caused by code changes. This ensures that
    if a vendor changes any platform mapping generation code, we can guarantee it doesn't affect any platform mapping
    other than those we expect before merging the changes.

    This test will be run in OSS for all open-sourced platforms.
    """

    # Shared with facebook internal gen.py via the registry in gen.py, so a platform can
    # never be registered for verification without also being registered for
    # generation.
    _OSS_MULTI_NPU_SUPPORTED_PLATFORMS: ClassVar[dict[bool, list[str]]] = (
        OSS_MULTI_NPU_SUPPORTED_PLATFORMS
    )
    _OSS_INPUT_DIR: str = "fboss/configs/platforms"
    _TMP_GENERATED_DIR: str = "/tmp/generated_platform_mappings/"

    def _clear_tmp_generated_mappings(self) -> None:
        """
        Clears out any files in the /tmp/generated_platform_mappings directory.
        """
        if os.path.exists(self._TMP_GENERATED_DIR):
            for root, dirs, files in os.walk(self._TMP_GENERATED_DIR, topdown=False):
                for filename in files:
                    file_path = os.path.join(root, filename)
                    try:
                        os.unlink(file_path)
                    except OSError as e:
                        print(
                            f"Failed to delete {file_path}. Reason: {e}",
                            file=sys.stderr,
                        )
                for dirname in dirs:
                    dir_path = os.path.join(root, dirname)
                    try:
                        os.rmdir(dir_path)
                    except OSError as e:
                        print(
                            f"Failed to delete {dir_path}. Reason: {e}", file=sys.stderr
                        )

    def _generate_all_oss_platform_mappings_in_tmp(self) -> None:
        vendor_data_map = discover_platform_mapping_inputs(self._OSS_INPUT_DIR)
        for is_multi_npu, platforms in self._OSS_MULTI_NPU_SUPPORTED_PLATFORMS.items():
            for platform in platforms:
                generate_platform_mappings_from_vendor_data(
                    vendor_data_map, self._TMP_GENERATED_DIR, platform, is_multi_npu
                )

    def _get_relative_files(self, directory: str) -> dict[str, str]:
        relative_files = {}
        for root, _, filenames in os.walk(directory):
            for filename in filenames:
                if not filename.endswith(".json"):
                    continue
                path = os.path.join(root, filename)
                relative_files[os.path.relpath(path, directory)] = path
        return relative_files

    def _get_colocated_generated_files(
        self, vendor_data_map: PlatformMappingInputs
    ) -> dict[str, str]:
        generated_files = {}
        for platform_name in sorted(vendor_data_map):
            platform_input = vendor_data_map[platform_name]
            generated_dir = os.path.join(platform_input.input_dir, "generated")
            platform_descriptor_data = get_platform_descriptor_data(
                vendor_data_map, platform_name
            )
            aggregate_dir = get_platform_mapping_output_dir(
                vendor_data_map,
                platform_name,
                self._TMP_GENERATED_DIR,
                platform_descriptor_data,
            )
            relative_dir = os.path.relpath(aggregate_dir, self._TMP_GENERATED_DIR)
            for filename, path in self._get_relative_files(generated_dir).items():
                if (
                    filename == "raw_platform_mapping.json"
                    and platform_name != platform_input.base_platform
                ):
                    base_raw_path = os.path.join(
                        vendor_data_map[platform_input.base_platform].input_dir,
                        "generated",
                        filename,
                    )
                    if os.path.exists(base_raw_path) and filecmp.cmp(
                        path, base_raw_path, shallow=False
                    ):
                        continue

                relative_path = os.path.join(relative_dir, filename)
                generated_files[relative_path] = path
        return generated_files

    def test_generated_files_match(self) -> None:
        """
        This test:
        1. Runs platform mapping generation on all open-sourced platforms, specifying output directory to /tmp/generated_platform_mappings.
        2. Compares /tmp generated files and colocated fbcode generated files (including changes from current PR).
        3. Verifies all generated files match, raises AssertionError if not.
        """
        self._clear_tmp_generated_mappings()
        print(
            f"Cleared any existing files in {self._TMP_GENERATED_DIR}", file=sys.stderr
        )

        self._generate_all_oss_platform_mappings_in_tmp()
        print(
            f"Generated all platform mappings in {self._TMP_GENERATED_DIR}",
            file=sys.stderr,
        )

        self.assertTrue(
            os.path.exists(self._TMP_GENERATED_DIR),
            f"Tmp generated directory {self._TMP_GENERATED_DIR} not found",
        )

        vendor_data_map = discover_platform_mapping_inputs(self._OSS_INPUT_DIR)
        ref_files = self._get_colocated_generated_files(vendor_data_map)
        gen_files = self._get_relative_files(self._TMP_GENERATED_DIR)

        self.assertEqual(
            sorted(ref_files),
            sorted(gen_files),
            "Fbcode and tmp generated files don't match",
        )

        for filename, ref_path in ref_files.items():
            print(f"Verifying file {filename}", file=sys.stderr)
            self.assertTrue(
                filecmp.cmp(ref_path, gen_files[filename], shallow=False),
                f"File contents don't match for {filename}",
            )


def run_tests() -> None:
    # Provided for add_fb_python_executable callable
    suite = unittest.TestLoader().loadTestsFromTestCase(
        TestVerifyPlatformMappingGeneratedFiles
    )
    result = unittest.TextTestRunner().run(suite)

    if not result.wasSuccessful():
        raise Exception("Test failures.")
