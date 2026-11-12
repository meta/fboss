#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import os
from argparse import ArgumentParser, Namespace

from fboss_test_runner.constants import (
    OPT_ARG_BSP_PLATFORM_MAPPING_OVERRIDE_PATH,
    OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
    SUB_ARG_AGENT_RUN_MODE,
    SUB_ARG_AGENT_RUN_MODE_MONO,
    SUB_ARG_AGENT_RUN_MODE_MULTI,
    SUB_ARG_NUM_NPUS,
)
from fboss_test_runner.errors import TestRunnerUsageError
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.services.fboss_agent_utils import (
    agent_can_warm_boot_file_path,
    cleanup_hw_agent_service,
    setup_and_start_hw_agent_service,
)
from fboss_test_runner.services.fsdb_service_utils import (
    cleanup_fsdb_service,
    setup_and_start_fsdb_service,
)
from fboss_test_runner.services.qsfp_service_utils import (
    cleanup_qsfp_service,
    setup_and_start_qsfp_service,
)
from fboss_test_runner.tier_selection import TestCapability

LINK_KNOWN_BAD_TESTS = (
    "./share/link_known_bad_tests/fboss_link_known_bad_tests.materialized_JSON"
)
LINK_TEST_METADATA = "./share/hw_sanity_tests/link_test_metadata.json"
OPT_ARG_WITHOUT_TRANSCEIVERS = "--without-transceivers"


class LinkTestRunner(TestRunner):
    def _get_tier_metadata_file(self) -> str:
        return LINK_TEST_METADATA

    def _get_available_test_capabilities(self) -> tuple[TestCapability, ...]:
        return () if self.args.without_transceivers else (TestCapability.TRANSCEIVER,)

    def run_test(self, args: Namespace) -> int:
        self.args = args
        self._validate_run_args()
        return super().run_test(args)

    def add_subcommand_arguments(self, sub_parser: ArgumentParser) -> None:
        super().add_subcommand_arguments(sub_parser)
        self._add_sai_arguments(sub_parser)
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
        sub_parser.add_argument(
            OPT_ARG_WITHOUT_TRANSCEIVERS,
            action="store_true",
            help="Exclude tier tests that require platform transceiver support.",
        )

    def _get_config_path(self) -> str:
        return ""

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            LINK_KNOWN_BAD_TESTS,
            self.KNOWN_BAD_TESTS_LABEL,
        )

    def _get_unsupported_tests_file(self) -> str:
        return ""

    def _validate_run_args(self) -> None:
        args = self.args
        conf_file = args.config if args.config is not None else self._get_config_path()
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI and not conf_file:
            raise TestRunnerUsageError("--config is required to run link tests")
        if conf_file and not os.path.exists(conf_file):
            raise TestRunnerUsageError(f"--config path does not exist: {conf_file}")

        qsfp_config = args.qsfp_config
        if not qsfp_config:
            raise TestRunnerUsageError("--qsfp-config is required to run link tests")
        if not os.path.exists(qsfp_config):
            raise TestRunnerUsageError(
                f"--qsfp-config path does not exist: {qsfp_config}"
            )

    def _get_test_binary_name(self) -> str:
        args = self.args
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MONO:
            return "sai_mono_link_test-sai_impl"

        # Default to multi_switch mode
        return "sai_multi_link_test-sai_impl"

    def _get_npu_sdk_metadata_binary_name(self) -> str:
        return self._get_test_binary_name()

    def _get_sai_replayer_logging_flags(
        self, sai_replayer_log_path: str | None
    ) -> list[str]:
        return []

    def _get_sai_logging_flags(self) -> list[str]:
        return ["--enable_sai_log", self.args.sai_logging]

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
        arg_list = ["--config", conf_file, "--mgmt-if", args.mgmt_if]
        if args.platform_mapping_override_path is not None:
            arg_list.extend(
                [
                    "--platform_mapping_override_path",
                    args.platform_mapping_override_path,
                ]
            )

        arg_list.extend(["--fsdb_client_ssl_preferred=false"])

        return arg_list

    def _setup_coldboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        # Start FSDB service if not disabled
        if not args.disable_fsdb:
            setup_and_start_fsdb_service(
                fsdb_service_config_path=args.fsdb_config,
                is_warm_boot=False,
            )

        setup_and_start_qsfp_service(
            qsfp_service_config_path=args.qsfp_config,
            platform_mapping_override_path=args.platform_mapping_override_path,
            bsp_platform_mapping_override_path=args.bsp_platform_mapping_override_path,
            is_fsdb_disabled=args.disable_fsdb,
            is_warm_boot=False,
        )
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            setup_and_start_hw_agent_service(
                switch_indexes=list(range(args.num_npus)),
                fboss_agent_config_path=args.config,
                platform_mapping_override_path=args.platform_mapping_override_path,
                sai_replayer_log_path=sai_replayer_log_path,
                sai_replayer_sdk_log_level=getattr(
                    args, "sai_replayer_sdk_log_level", None
                ),
                is_fsdb_disabled=args.disable_fsdb,
                is_warm_boot=False,
            )

    def _setup_warmboot_test(self, sai_replayer_log_path: str | None = None) -> None:
        args = self.args
        # Start FSDB service if not disabled
        if not args.disable_fsdb:
            setup_and_start_fsdb_service(
                fsdb_service_config_path=args.fsdb_config,
                is_warm_boot=True,
            )
        setup_and_start_qsfp_service(
            qsfp_service_config_path=args.qsfp_config,
            platform_mapping_override_path=args.platform_mapping_override_path,
            bsp_platform_mapping_override_path=args.bsp_platform_mapping_override_path,
            is_fsdb_disabled=args.disable_fsdb,
            is_warm_boot=True,
        )
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            setup_and_start_hw_agent_service(
                switch_indexes=list(range(args.num_npus)),
                fboss_agent_config_path=args.config,
                platform_mapping_override_path=args.platform_mapping_override_path,
                sai_replayer_log_path=sai_replayer_log_path,
                sai_replayer_sdk_log_level=getattr(
                    args, "sai_replayer_sdk_log_level", None
                ),
                is_fsdb_disabled=args.disable_fsdb,
                is_warm_boot=True,
            )

    def _end_run(self) -> None:
        args = self.args
        cleanup_qsfp_service()
        if not args.disable_fsdb:
            cleanup_fsdb_service()
        if args.agent_run_mode == SUB_ARG_AGENT_RUN_MODE_MULTI:
            cleanup_hw_agent_service(list(range(args.num_npus)))

    def _filter_tests(self, tests: list[str]) -> list[str]:
        return tests
