#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

from argparse import ArgumentParser

from fboss_test_runner.constants import OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.runners.utils import run_script
from fboss_test_runner.services.fboss_agent_utils import agent_can_warm_boot_file_path

SAI_HW_KNOWN_BAD_TESTS = (
    "./share/hw_known_bad_tests/sai_known_bad_tests.materialized_JSON"
)
SAI_UNSUPPORTED_TESTS = (
    "./share/sai_hw_unsupported_tests/sai_hw_unsupported_tests.materialized_JSON"
)


class SaiTestRunner(TestRunner):
    def add_subcommand_arguments(self, sub_parser: ArgumentParser) -> None:
        super().add_subcommand_arguments(sub_parser)
        self._add_sai_arguments(sub_parser)
        sub_parser.add_argument(
            OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
            nargs="?",
            type=str,
            help="A file path to a platform mapping JSON file to be used.",
            default=None,
        )

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            SAI_HW_KNOWN_BAD_TESTS,
            self.KNOWN_BAD_TESTS_LABEL,
        )

    def _get_unsupported_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.unsupported_tests_file,
            SAI_UNSUPPORTED_TESTS,
            self.UNSUPPORTED_TESTS_LABEL,
        )

    def _get_test_binary_name(self) -> str:
        return "sai_test-sai_impl"

    def _get_npu_sdk_metadata_binary_name(self) -> str:
        return self._get_test_binary_name()

    def _get_sai_replayer_logging_flags(
        self, sai_replayer_log_path: str | None
    ) -> list[str]:
        if sai_replayer_log_path is None:
            return []
        flags = [
            "--enable-replayer",
            "--enable_get_attr_log",
            "--enable_packet_log",
            "--sai-log",
            sai_replayer_log_path,
        ]
        level = getattr(self.args, "sai_replayer_sdk_log_level", None)
        if level is not None:
            flags.extend(["--sai_replayer_sdk_log_level", level])
        return flags

    def _get_sai_logging_flags(self) -> list[str]:
        return ["--enable_sai_log", self.args.sai_logging]

    def _get_warmboot_check_file(self) -> str:
        return agent_can_warm_boot_file_path(switch_index=0)

    def _get_test_run_args(self, conf_file: str) -> list[str]:
        args = self.args
        args_list = ["--config", conf_file, "--mgmt-if", args.mgmt_if]
        if args.platform_mapping_override_path is not None:
            args_list.extend(
                [
                    "--platform_mapping_override_path",
                    args.platform_mapping_override_path,
                ]
            )
        return args_list

    def _setup_coldboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        if args.setup_for_coldboot:
            run_script(args.setup_for_coldboot)

    def _setup_warmboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        if args.setup_for_warmboot:
            run_script(args.setup_for_warmboot)
