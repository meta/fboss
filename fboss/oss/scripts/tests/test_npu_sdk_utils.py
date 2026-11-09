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

import json
import pathlib
import stat
import tempfile
import unittest

import npu_sdk_utils


class NpuSdkMetadataTest(unittest.TestCase):
    def test_record_updates_one_binary_and_preserves_other_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = pathlib.Path(temp_dir) / "npu_sdk_metadata.json"
            npu_sdk_utils.record_binary_metadata(
                path=path,
                binary_name="sai_test-sai_impl",
                npu_sai_impl="SAI_BRCM_IMPL",
                npu_sai_sdk_selector="SAI_VERSION_13_3_0_0_ODP",
                asic_sdk_version="6.5.32",
                sai_sdk_version="13.3.0.0_odp",
                built_at="2026-09-13T01:00:00Z",
            )

            npu_sdk_utils.record_binary_metadata(
                path=path,
                binary_name="fboss_hw_agent-sai_impl",
                npu_sai_impl="SAI_BRCM_IMPL",
                npu_sai_sdk_selector="SAI_VERSION_11_7_0_0_ODP",
                asic_sdk_version="6.5.30-4",
                sai_sdk_version="11.7.0.0_odp",
                built_at="2026-09-13T02:00:00Z",
            )

            self.assertEqual(
                {
                    "fboss_hw_agent-sai_impl": {
                        "builtAt": "2026-09-13T02:00:00Z",
                        "npuSaiImpl": "SAI_BRCM_IMPL",
                        "npuSaiSdkSelector": "SAI_VERSION_11_7_0_0_ODP",
                        "sdkVersion": {
                            "asicSdk": "6.5.30-4",
                            "saiSdk": "11.7.0.0_odp",
                        },
                    },
                    "sai_test-sai_impl": {
                        "builtAt": "2026-09-13T01:00:00Z",
                        "npuSaiImpl": "SAI_BRCM_IMPL",
                        "npuSaiSdkSelector": "SAI_VERSION_13_3_0_0_ODP",
                        "sdkVersion": {
                            "asicSdk": "6.5.32",
                            "saiSdk": "13.3.0.0_odp",
                        },
                    },
                },
                json.loads(path.read_text()),
            )
            self.assertEqual(
                npu_sdk_utils.READ_ONLY_MODE,
                stat.S_IMODE(path.stat().st_mode),
            )

    def test_remove_preserves_other_binary_and_deletes_empty_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = pathlib.Path(temp_dir) / "npu_sdk_metadata.json"
            for binary_name in ("fboss_hw_agent-sai_impl", "sai_test-sai_impl"):
                npu_sdk_utils.record_binary_metadata(
                    path=path,
                    binary_name=binary_name,
                    npu_sai_impl="SAI_BRCM_IMPL",
                    npu_sai_sdk_selector="SAI_VERSION_11_7_0_0_ODP",
                    asic_sdk_version="6.5.30-4",
                    sai_sdk_version="11.7.0.0_odp",
                    built_at="2026-09-13T01:00:00Z",
                )

            npu_sdk_utils.remove_binary_metadata(path, "fboss_hw_agent-sai_impl")
            self.assertEqual(
                {"sai_test-sai_impl"},
                set(json.loads(path.read_text())),
            )

            npu_sdk_utils.remove_binary_metadata(path, "sai_test-sai_impl")
            self.assertFalse(path.exists())
