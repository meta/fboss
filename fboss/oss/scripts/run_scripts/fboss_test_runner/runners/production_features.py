#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

"""Production feature filtering shared by runners whose test binary supports
--list_production_feature."""

import json
import os
import re
import subprocess
import sys
from argparse import Action, ArgumentParser, Namespace
from collections.abc import Iterator
from pathlib import Path

from npu_sdk_utils import load_binary_sdk_version, NPU_SDK_METADATA_FILENAME

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
# "-N" packaging OSS revision (e.g. 26.2.4210-1).
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
        parser: ArgumentParser,  # noqa: ARG002
        namespace: Namespace,
        values: str,
        option_string: str | None = None,  # noqa: ARG002
    ) -> None:
        setattr(namespace, self.dest, values)
        namespace.list_tests = True


class ProductionFeatureFilterMixin:
    """Mix into a TestRunner to filter tests by the production features the
    target ASIC supports."""

    args: Namespace

    def _add_production_feature_arguments(self, sub_parser: ArgumentParser) -> None:
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

    def _get_production_feature_test_config_options(self) -> set[str]:
        """Test config options this run always satisfies."""
        return set()

    def _get_test_production_features(self, test: str) -> set[str] | None:
        """Features the test declares, or None if the binary printed none."""
        ret = subprocess.run(
            [
                self._get_test_binary_name(),
                f"--gtest_filter={test}",
                "--list_production_feature",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        for line in ret.stdout.split("\n"):
            if line.startswith(FEATURE_LIST_PREFIX):
                features = line[len(FEATURE_LIST_PREFIX) :]
                return set(features.split(",")) if features else set()
        return None

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
        test_config_options = self._get_production_feature_test_config_options()
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

    def _filter_tests_by_production_features(self, tests: list[str]) -> list[str]:
        args = self.args
        if args.list_tests_for_features:
            target_features = set(args.list_tests_for_features.split(","))
            matching_tests = []
            for test in tests:
                test_features = self._get_test_production_features(test)
                if test_features and test_features.issubset(target_features):
                    matching_tests.append(test)
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
            test_features = self._get_test_production_features(test)
            if test_features is None:
                continue
            if "HW_SWITCH" in test_features or test_features.issubset(
                production_features
            ):
                tests_to_run.append(test)
        return tests_to_run
