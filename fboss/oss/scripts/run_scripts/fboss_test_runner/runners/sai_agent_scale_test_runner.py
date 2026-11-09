#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

from argparse import ArgumentParser

from fboss_test_runner.constants import (
    OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
    SAI_AGENT_SCALE_KNOWN_BAD_TESTS,
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
from npu_sdk_utils import NPU_HW_AGENT_BINARY


class SaiAgentScaleTestRunner(TestRunner):
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
        return ""

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            SAI_AGENT_SCALE_KNOWN_BAD_TESTS,
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
            return "sai_agent_scale_test-sai_impl"

        return "multi_switch_agent_scale_test"

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
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return []
        return ["--enable_sai_log", args.sai_logging]

    def _get_warmboot_check_file(self) -> str:
        args = self.args
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            return agent_can_warm_boot_file_path(switch_index=None)
        return agent_can_warm_boot_file_path(switch_index=0)

    def _get_test_run_args(self, conf_file: str) -> list[str]:
        args = self.args
        args_list = ["--config", conf_file, "--mgmt-if", args.mgmt_if]
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

    def _filter_tests(self, tests: list[str]) -> list[str]:
        return tests
