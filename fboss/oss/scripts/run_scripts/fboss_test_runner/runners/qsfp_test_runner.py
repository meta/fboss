#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import subprocess
from argparse import ArgumentParser

from fboss_test_runner.constants import (
    OPT_ARG_BSP_PLATFORM_MAPPING_OVERRIDE_PATH,
    OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
)
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.tier_selection import TestCapability

QSFP_KNOWN_BAD_TESTS = (
    "./share/qsfp_known_bad_tests/fboss_qsfp_known_bad_tests.materialized_JSON"
)
QSFP_UNSUPPORTED_TESTS = (
    "./share/qsfp_unsupported_tests/fboss_qsfp_unsupported_tests.materialized_JSON"
)
QSFP_SERVICE_DIR = "/dev/shm/fboss/qsfp_service"  # noqa: S108
QSFP_WARMBOOT_CHECK_FILE = f"{QSFP_SERVICE_DIR}/can_warm_boot"
QSFP_TEST_METADATA = "./share/hw_sanity_tests/qsfp_test_metadata.json"
OPT_ARG_WITHOUT_TRANSCEIVERS = "--without-transceivers"


class QsfpTestRunner(TestRunner):
    def _get_tier_metadata_file(self) -> str:
        return QSFP_TEST_METADATA

    def _get_available_test_capabilities(self) -> tuple[TestCapability, ...]:
        return (
            (TestCapability.EXTERNAL_PHY,)
            if self.args.without_transceivers
            else (TestCapability.TRANSCEIVER,)
        )

    def add_subcommand_arguments(self, sub_parser: ArgumentParser) -> None:
        super().add_subcommand_arguments(sub_parser)
        self._add_service_arguments(sub_parser)
        sub_parser.add_argument(
            OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
            nargs="?",
            type=str,
            help="A file path to a platform mapping JSON file to be used.",
            default=None,
        )

        sub_parser.add_argument(
            OPT_ARG_BSP_PLATFORM_MAPPING_OVERRIDE_PATH,
            nargs="?",
            type=str,
            help="A file path to a BSP platform mapping JSON file to be used.",
            default=None,
        )
        sub_parser.add_argument(
            OPT_ARG_WITHOUT_TRANSCEIVERS,
            action="store_true",
            help=(
                "Select tier rules for platforms without pluggable transceivers "
                "and with external PHYs."
            ),
        )

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            QSFP_KNOWN_BAD_TESTS,
            self.KNOWN_BAD_TESTS_LABEL,
        )

    def _get_unsupported_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.unsupported_tests_file,
            QSFP_UNSUPPORTED_TESTS,
            self.UNSUPPORTED_TESTS_LABEL,
        )

    def _get_test_binary_name(self) -> str:
        return "qsfp_hw_test"

    def _get_warmboot_check_file(self) -> str:
        return QSFP_WARMBOOT_CHECK_FILE

    def _get_test_run_args(self, conf_file: str) -> list[str]:
        args = self.args
        arg_list = ["--qsfp-config", args.qsfp_config]
        if args.platform_mapping_override_path is not None:
            arg_list.extend(
                [
                    "--platform_mapping_override_path",
                    args.platform_mapping_override_path,
                ]
            )
        if args.bsp_platform_mapping_override_path is not None:
            arg_list.extend(
                [
                    "--bsp_platform_mapping_override_path",
                    args.bsp_platform_mapping_override_path,
                ]
            )
        return arg_list

    def _setup_coldboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        subprocess.Popen(
            # Clean up left over flags
            ["rm", "-rf", QSFP_SERVICE_DIR]
        )
