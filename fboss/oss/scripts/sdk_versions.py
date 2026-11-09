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

from __future__ import annotations

from dataclasses import dataclass


NPU_ASIC_SDK_VERSION = "NPU_ASIC_SDK_VERSION"
NPU_SAI_SDK_VERSION = "NPU_SAI_SDK_VERSION"
UNKNOWN_SDK_VERSION = "unknown"


@dataclass(frozen=True)
class SdkVersion:
    asic_sdk: str
    sai_sdk: str


# Keep these config-facing versions aligned with
# neteng/fboss/coop/inputs/agent_sdk_version.mcconf and the buildable SDKs in
# fboss/build/sdk.bzl. Preserve packaging revision suffixes specified by
# agent_sdk_version.mcconf; they are part of the version written to the agent
# config. The native and SAI versions cannot be derived from one another,
# particularly for Broadcom.
SDK_VERSIONS = {
    # Broadcom XGS
    "SAI_VERSION_8_2_0_0_ODP": SdkVersion("6.5.26-1", "8.2.0.0_odp"),
    "SAI_VERSION_10_2_0_0_ODP": SdkVersion("6.5.29", "10.2.0.0_odp"),
    "SAI_VERSION_11_7_0_0_ODP": SdkVersion("6.5.30-4", "11.7.0.0_odp"),
    "SAI_VERSION_12_2_0_0_ODP": SdkVersion("6.5.31", "12.2.0.0_odp"),
    "SAI_VERSION_13_3_0_0_ODP": SdkVersion("6.5.32", "13.3.0.0_odp"),
    "SAI_VERSION_14_0_EA_ODP": SdkVersion("6.5.34", "14.0_ea_odp"),
    "SAI_VERSION_14_2_0_0_ODP": SdkVersion("6.5.34", "14.2.0.0_odp"),
    "SAI_VERSION_15_4_EA_ODP": SdkVersion("6.5.35-2", "15.4_ea_odp"),
    "SAI_VERSION_15_4_0_0_ODP": SdkVersion("6.5.35", "15.4.0.0_odp"),
    # Broadcom DNX
    "SAI_VERSION_11_7_0_0_DNX_ODP": SdkVersion("6.5.30-5", "11.7.0.0_dnx_odp"),
    "SAI_VERSION_12_2_0_0_DNX_ODP": SdkVersion("6.5.31-2", "12.2.0.0_dnx_odp"),
    "SAI_VERSION_13_3_0_0_DNX_ODP": SdkVersion("6.5.32", "13.3.0.0_dnx_odp"),
    "SAI_VERSION_14_0_EA_DNX_ODP": SdkVersion("6.5.34", "14.0_ea_dnx_odp"),
    "SAI_VERSION_14_2_0_0_DNX_ODP": SdkVersion("6.5.34", "14.2.0.0_dnx_odp"),
    "SAI_VERSION_15_0_EA_DNX_ODP": SdkVersion("6.5.35", "15.0_ea_dnx_odp"),
    "SAI_VERSION_16_0_EA_DNX_ODP": SdkVersion("6.5.36", "16.0_ea_dnx_odp"),
    # Cisco
    "TAJO_SDK_VERSION_1_42_8": SdkVersion("1.42.8-3", "1.42.8-3"),
    "TAJO_SDK_VERSION_24_8_3001": SdkVersion("24.8.3001-3", "24.8.3001-3"),
    "TAJO_SDK_VERSION_25_5_4210": SdkVersion("25.5.4210-3", "25.5.4210-3"),
    "TAJO_SDK_VERSION_25_11_4210": SdkVersion("25.11.4210-2", "25.11.4210-2"),
    "TAJO_SDK_VERSION_26_2_4210": SdkVersion("26.2.4210-1", "26.2.4210-1"),
    "TAJO_SDK_VERSION_26_5_4210": SdkVersion("26.5.4210", "26.5.4210"),
    "TAJO_SDK_VERSION_26_5_5210": SdkVersion("26.5.5210", "26.5.5210"),
    "TAJO_SDK_VERSION_26_7_5211": SdkVersion("26.7.5211", "26.7.5211"),
    # Nvidia
    "CHENAB_SAI_SDK_VERSION_2511_36_0_20": SdkVersion("4.8.4062", "2511.36"),
    "CHENAB_SAI_SDK_VERSION_2605_37_0_20": SdkVersion("4.10.1", "2605.37"),
    "CHENAB_SAI_SDK_VERSION_2605_38_251_4": SdkVersion("4.10.2", "2605.38"),
}


def get_sdk_version_env_vars(sdk_selector: str) -> dict[str, str]:
    sdk_version = SDK_VERSIONS.get(sdk_selector)
    if sdk_version is None:
        return {
            NPU_ASIC_SDK_VERSION: UNKNOWN_SDK_VERSION,
            NPU_SAI_SDK_VERSION: UNKNOWN_SDK_VERSION,
        }
    return {
        NPU_ASIC_SDK_VERSION: sdk_version.asic_sdk,
        NPU_SAI_SDK_VERSION: sdk_version.sai_sdk,
    }
