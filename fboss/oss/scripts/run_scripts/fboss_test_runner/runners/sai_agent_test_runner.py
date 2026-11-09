#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import json
import os
import re
import subprocess
import sys
from argparse import Action, ArgumentParser, Namespace
from collections.abc import Iterator
from pathlib import Path

from fboss_test_runner.constants import (
    OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
    SAI_AGENT_TEST_KNOWN_BAD_TESTS,
    SAI_AGENT_UNSUPPORTED_TESTS,
    SUB_ARG_AGENT_RUN_MODE,
    SUB_ARG_AGENT_RUN_MODE_MONO,
    SUB_ARG_AGENT_RUN_MODE_MULTI,
    SUB_ARG_NUM_NPUS,
)
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.runners.utils import run_script
from fboss_test_runner.services.fboss_agent_utils import (
    agent_can_warm_boot_file_path,
    cleanup_hw_agent_service,
    setup_and_start_hw_agent_service,
)
from npu_sdk_utils import (
    load_binary_sdk_version,
    NPU_HW_AGENT_BINARY,
    NPU_SDK_METADATA_FILENAME,
)

OPT_ARG_PRODUCTION_FEATURES = "--production-features"
OPT_ARG_ENABLE_PRODUCTION_FEATURES = "--enable-production-features"
OPT_ARG_LIST_TESTS_FOR_FEATURE = "--list-tests-for-features"
OPT_ARG_TEST_CONFIG_OPTIONS = "--test-config-options"
ASIC_PRODUCTION_FEATURES = (
    "./share/production_features/asic_production_features.materialized_JSON"
)
FEATURE_LIST_PREFIX = "Feature List: "
# VendorSdk values in production_features.thrift
VENDOR_SDK_BCM_NATIVE = 0
VENDOR_SDK_BRCM_SAI = 1
VENDOR_SDK_LEABA = 2
VENDOR_SDK_NVDA = 3
# Version pattern and per-component weights, as in netcastle's
# teams/fboss/utils/sdk_version_normalizer.py. Leaba additionally accepts the
# "-N" packaging revision OSS records (e.g. 26.2.4210-1).
_SDK_VERSION_FORMATS = {
    VENDOR_SDK_BRCM_SAI: (
        re.compile(r"^(\d+)\.(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:_.*)?$"),
        (10**6, 10**4, 10**2, 1),
    ),
    VENDOR_SDK_LEABA: (
        re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:[-_].*)?$"),
        (10**7, 10**5, 1),
    ),
}
_NVDA_VERSION_WEIGHTS = (10**12, 10**9, 10**6, 10**3, 1)


def sdk_version_key(vendor_sdk: int, version: str) -> int:
    """Normalize an SDK version to an int comparable within one vendor SDK."""
    version = version.strip()
    if vendor_sdk == VENDOR_SDK_NVDA:
        # Every numeric component separated by "." or "_", e.g. 2511.36_0_20
        parts = [int(p) for p in re.split(r"[._]", version) if p.isdigit()]
        weights = _NVDA_VERSION_WEIGHTS
    else:
        if vendor_sdk not in _SDK_VERSION_FORMATS:
            raise ValueError(f"Unsupported VendorSdk: {vendor_sdk}")
        pattern, weights = _SDK_VERSION_FORMATS[vendor_sdk]
        match = pattern.match(version)
        parts = [int(g) if g else 0 for g in match.groups()] if match else []
    if not parts:
        raise ValueError(f"Unparseable SDK version: {version!r}")
    return sum(part * weight for part, weight in zip(parts, weights))


def _sai_feature_support_entries(
    asic: str, asic_production_features: dict
) -> Iterator[tuple[str, dict]]:
    """Yield (feature name, featureSupportSpecs entry) for each of the ASIC's
    features with a SAI vendor SDK support entry for this ASIC. An ASIC has a
    single SAI vendor, and BCM native entries never apply to SAI tests."""
    # featureSupportSpecs references features by id
    feature_id_to_name = dict(
        zip(
            asic_production_features.get("asicToFeatures", {}).get(asic, []),
            asic_production_features["asicToFeatureNames"][asic],
        )
    )
    for spec in asic_production_features.get("featureSupportSpecs", []):
        name = feature_id_to_name.get(spec["feature"])
        if name is None:
            continue
        for entry in spec["supportedFrom"]:
            if asic in entry["asics"] and entry["sdk"] != VENDOR_SDK_BCM_NATIVE:
                yield name, entry
                break


class _ListTestsForFeaturesAction(Action):
    def __call__(
        self,
        parser: ArgumentParser,
        namespace: Namespace,
        values: str,
        option_string: str | None = None,
    ) -> None:
        setattr(namespace, self.dest, values)
        namespace.list_tests = True


class SaiAgentTestRunner(TestRunner):
    def add_subcommand_arguments(self, sub_parser: ArgumentParser) -> None:
        super().add_subcommand_arguments(sub_parser)
        self._add_sai_arguments(sub_parser)
        sub_parser.add_argument(
            OPT_ARG_PRODUCTION_FEATURES,
            type=str,
            help="production features json file",
            default=ASIC_PRODUCTION_FEATURES,
        )
        sub_parser.add_argument(
            OPT_ARG_ENABLE_PRODUCTION_FEATURES,
            type=str,
            metavar="ASIC",
            help="Enable filtering by production features for the specified ASIC",
            default=None,
        )
        sub_parser.add_argument(
            OPT_ARG_LIST_TESTS_FOR_FEATURE,
            type=str,
            action=_ListTestsForFeaturesAction,
            help="Return tests whose production feature tags are all contained "
            "in the supplied comma-separated list e.g. DLB,ACL_COUNTER,SINGLE_ACL_TABLE",
            default=None,
        )
        sub_parser.add_argument(
            OPT_ARG_TEST_CONFIG_OPTIONS,
            type=str,
            help="Comma-separated test config options (e.g. rbb) that satisfy the "
            "requiredTestConfigOptions of production feature SDK support entries. "
            "The agent run mode is always included.",
            default=None,
        )
        sub_parser.add_argument(
            OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
            nargs="?",
            type=str,
            help="A file path to a platform mapping JSON file to be used.",
            default=None,
        )
        sub_parser.add_argument(
            SUB_ARG_AGENT_RUN_MODE,
            choices=[
                SUB_ARG_AGENT_RUN_MODE_MONO,
                SUB_ARG_AGENT_RUN_MODE_MULTI,
            ],
            nargs="?",
            default=SUB_ARG_AGENT_RUN_MODE_MULTI,
            help="Specify agent run mode. Default is multi_switch mode.",
        )
        sub_parser.add_argument(
            SUB_ARG_NUM_NPUS,
            choices=[1, 2],
            default=1,
            type=int,
            help="Specify number of npus to run in multi switch mode. Default is 1.",
        )

    def _get_config_path(self) -> str:
        # TOOO Not available in OSS
        return ""

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            SAI_AGENT_TEST_KNOWN_BAD_TESTS,
            self.KNOWN_BAD_TESTS_LABEL,
        )

    def _get_unsupported_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.unsupported_tests_file,
            SAI_AGENT_UNSUPPORTED_TESTS,
            self.UNSUPPORTED_TESTS_LABEL,
        )

    def _get_test_binary_name(self) -> str:
        args = self.args
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MONO:
            return "sai_agent_hw_test-sai_impl"

        # Default to multi_switch mode
        return "multi_switch_agent_hw_test"

    def _get_npu_sdk_metadata_binary_name(self) -> str:
        # In multi-switch mode the test binary drives the software agent and
        # does not link the NPU SDK; the separate hardware agent does. The
        # monolithic test binary links the SDK itself.
        if self.args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return NPU_HW_AGENT_BINARY
        return self._get_test_binary_name()

    def _get_sai_replayer_logging_flags(
        self, sai_replayer_log_path: str | None
    ) -> list[str]:
        args = self.args
        if sai_replayer_log_path is None:
            return []
        # Multi switch mode is using hw agent as a service, so the sai replayer logging needs to
        # be enabled in the systemd unit file instead of the test binary flags
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return []
        flags = [
            "--enable-replayer",
            "--enable_get_attr_log",
            "--enable_packet_log",
            "--sai-log",
            sai_replayer_log_path,
        ]
        level = getattr(args, "sai_replayer_sdk_log_level", None)
        if level is not None:
            flags.extend(["--sai_replayer_sdk_log_level", level])
        return flags

    def _get_sai_logging_flags(self) -> list[str]:
        args = self.args
        # Multi switch mode is using hw agent as a service, so the sai replayer logging needs to
        # be enabled in the systemd unit file instead of the test binary flags
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return []
        return ["--enable_sai_log", args.sai_logging]

    def _get_warmboot_check_file(self) -> str:
        args = self.args
        # If it's multi_switch mode, we need to check the warmboot file for SW switch which doesn't
        # have any switch_index
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return agent_can_warm_boot_file_path(switch_index=None)
        # If it's mono mode, we need to check the warmboot file for hw switch which has switch_index
        # as 0
        return agent_can_warm_boot_file_path(switch_index=0)

    def _get_test_run_args(self, conf_file: str) -> list[str]:
        args = self.args
        args_list = [
            "--config",
            conf_file,
            "--mgmt-if",
            args.mgmt_if,
            "--undefok=num_npus_for_testing",
            "--num_npus_for_testing",
            str(args.num_npus),
        ]
        # Multi switch mode is using hw agent as a service, so the override platform mapping needs
        # to be specified in the systemd unit file instead of the test binary flags
        if (
            args.platform_mapping_override_path is not None
            and args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MONO
        ):
            args_list.extend(
                [
                    "--platform_mapping_override_path",
                    args.platform_mapping_override_path,
                ]
            )
        return args_list

    def _setup_run(self, conf_file: str) -> None:
        pass

    def _setup_coldboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        if args.setup_for_coldboot:
            run_script(args.setup_for_coldboot)
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            setup_and_start_hw_agent_service(
                switch_indexes=list(range(args.num_npus)),
                fboss_agent_config_path=args.config,
                platform_mapping_override_path=args.platform_mapping_override_path,
                sai_replayer_log_path=sai_replayer_log_path,
                sai_replayer_sdk_log_level=getattr(
                    args, "sai_replayer_sdk_log_level", None
                ),
                is_warm_boot=False,
            )

    def _setup_warmboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        if args.setup_for_warmboot:
            run_script(args.setup_for_warmboot)
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            setup_and_start_hw_agent_service(
                switch_indexes=list(range(args.num_npus)),
                fboss_agent_config_path=args.config,
                platform_mapping_override_path=args.platform_mapping_override_path,
                sai_replayer_log_path=sai_replayer_log_path,
                sai_replayer_sdk_log_level=getattr(
                    args, "sai_replayer_sdk_log_level", None
                ),
                is_warm_boot=True,
            )

    def _end_run(self) -> None:
        args = self.args
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            cleanup_hw_agent_service(list(range(args.num_npus)))

    def _get_sdk_gated_production_features(
        self, asic: str, asic_production_features: dict
    ) -> set[str]:
        """The ASIC's production features, minus those whose featureSupportSpecs
        entry for this ASIC needs a newer vendor SDK than the one built in, or
        test config options this run lacks. Mirrors netcastle's production
        feature SDK gating in ColdWarmbootTestFramework."""
        args = self.args
        supported = set(asic_production_features["asicToFeatureNames"][asic])
        try:
            # saiSdk is the vendor SDK version minSdkVersion is expressed in
            version = load_binary_sdk_version(
                Path(os.environ["FBOSS_DATA"]) / NPU_SDK_METADATA_FILENAME,
                self._get_npu_sdk_metadata_binary_name(),
            )["saiSdk"]
        except (KeyError, OSError, ValueError) as e:
            # e.g. fake SAI builds record no NPU SDK metadata
            print(f"Skipping production feature SDK gating: {e!r}")
            return supported
        test_config_options = {args.agent_run_mode}
        if args.test_config_options:
            test_config_options.update(args.test_config_options.split(","))

        gated_out: dict[str, str] = {}
        try:
            for name, entry in _sai_feature_support_entries(
                asic, asic_production_features
            ):
                vendor_sdk, min_sdk = entry["sdk"], entry["minSdkVersion"]
                missing_options = (
                    set(entry.get("requiredTestConfigOptions", []))
                    - test_config_options
                )
                if sdk_version_key(vendor_sdk, version) < sdk_version_key(
                    vendor_sdk, min_sdk
                ):
                    gated_out[name] = f"needs SDK >= {min_sdk}"
                elif missing_options:
                    gated_out[name] = f"needs {sorted(missing_options)}"
        except ValueError as e:
            # e.g. saiSdk "unknown" for an SDK missing from sdk_versions.py
            print(f"Skipping production feature SDK gating: {e!r}")
            return supported
        if gated_out:
            print(f"Gating out production features on SDK {version}: {gated_out}")
        return supported - gated_out.keys()

    def _filter_tests(self, tests: list[str]) -> list[str]:
        args = self.args
        if args.list_tests_for_features:
            target_features = set(args.list_tests_for_features.split(","))
            matching_tests = []
            for test in tests:
                cmd = [
                    self._get_test_binary_name(),
                    f"--gtest_filter={test}",
                    "--list_production_feature",
                ]
                ret = subprocess.run(
                    cmd,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                for line in ret.stdout.split("\n"):
                    if not line.startswith(FEATURE_LIST_PREFIX):
                        continue
                    test_feature_str = line[len(FEATURE_LIST_PREFIX) :]
                    test_features = (
                        set(test_feature_str.split(",")) if test_feature_str else set()
                    )
                    if test_features and test_features.issubset(target_features):
                        matching_tests.append(test)
                        break
            return matching_tests

        if not args.enable_production_features:
            return tests

        asic = str(args.enable_production_features)
        with open(args.production_features) as f:
            asic_production_features = json.load(f)
        asic_to_feature_names = asic_production_features["asicToFeatureNames"]

        # Check if the specified asic exists in the production features file
        if asic not in asic_to_feature_names:
            print(f"Error: ASIC '{asic}' not found in production features file.")
            sys.exit(1)

        production_features = self._get_sdk_gated_production_features(
            asic, asic_production_features
        )
        tests_to_run = []
        for test in tests:
            cmd = [
                self._get_test_binary_name(),
                f"--gtest_filter={test}",
                "--list_production_feature",
            ]
            ret = subprocess.run(
                cmd,
                check=False,
                capture_output=True,
                text=True,
            )
            for line in ret.stdout.split("\n"):
                if not line.startswith(FEATURE_LIST_PREFIX):
                    continue
                test_feature_str = line[len(FEATURE_LIST_PREFIX) :]
                test_features = (
                    set(test_feature_str.split(",")) if test_feature_str else set()
                )
                if "HW_SWITCH" in test_features:
                    tests_to_run += (test,)
                    break
                if test_features.issubset(production_features):
                    tests_to_run += (test,)
                    break
        return tests_to_run
