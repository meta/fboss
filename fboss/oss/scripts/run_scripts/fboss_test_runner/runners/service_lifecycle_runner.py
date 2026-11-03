#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

from __future__ import annotations

import os
import shutil
from argparse import ArgumentParser, Namespace
from pathlib import Path

from fboss_test_runner.constants import (
    GENERATED_CONFIG_ROOT,
    OPT_ARG_AGENT_CONFIG_FILE,
    OPT_ARG_BSP_PLATFORM_MAPPING_OVERRIDE_PATH,
    OPT_ARG_FSDB_CONFIG_FILE,
    OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH,
    OPT_ARG_QSFP_CONFIG_FILE,
    SUB_ARG_NUM_NPUS,
    SUB_CMD_SERVICES,
)
from fboss_test_runner.services import service_utils
from fboss_test_runner.services.config_baseline import (
    AGENT_COLDBOOT_TIMEOUT_SEC,
    find_fboss2_cli,
    wait_agent_responsive,
)
from fboss_test_runner.services.fboss_agent_utils import (
    _DEFAULT_OSS_HW_AGENT_SERVICE_BINARY,
    _DEFAULT_OSS_SW_AGENT_SERVICE_BINARY,
    cleanup_hw_agent_service,
    cleanup_sw_agent_service,
    cold_boot_agents,
    HW_AGENT_SERVICE_PROD,
    setup_and_start_hw_agent_service,
    setup_and_start_sw_agent_service,
    SW_AGENT_SERVICE_PROD,
)
from fboss_test_runner.services.fsdb_service_utils import (
    _DEFAULT_OSS_FSDB_SERVICE_BINARY,
    _FSDB_SERVICE_OSS,
    cleanup_fsdb_service,
    setup_and_start_fsdb_service,
)
from fboss_test_runner.services.qsfp_service_utils import (
    _DEFAULT_OSS_QSFP_SERVICE_BINARY,
    _QSFP_SERVICE_OSS,
    cleanup_qsfp_service,
    setup_and_start_qsfp_service,
)
from npu_sdk_utils import (
    materialize_agent_config,
    NPU_HW_AGENT_BINARY,
    NPU_SDK_METADATA_FILENAME,
)

_AGENT_CONFIG_PATH = "/etc/coop/agent.conf"
_SERVICE_AGENT = "agent"
_SERVICE_FSDB = "fsdb"
_SERVICE_QSFP = "qsfp"
_SERVICE_CHOICES = [
    _SERVICE_AGENT,
    _SERVICE_FSDB,
    _SERVICE_QSFP,
]


class ServiceLifecycleRunner:
    def add_subcommands(self, parser: ArgumentParser) -> None:
        actions = parser.add_subparsers(dest="service_action", required=True)

        start_parser = actions.add_parser(
            "start", help="start FBOSS services and leave them running"
        )
        start_parser.add_argument(
            "services",
            nargs="*",
            choices=_SERVICE_CHOICES,
            help="services to start (default: all)",
        )
        start_parser.add_argument(
            OPT_ARG_AGENT_CONFIG_FILE,
            help="required when agent is selected",
        )
        start_parser.add_argument(
            OPT_ARG_QSFP_CONFIG_FILE,
            help="required when qsfp is selected",
        )
        start_parser.add_argument(OPT_ARG_FSDB_CONFIG_FILE, default=None)
        start_parser.add_argument(OPT_ARG_PLATFORM_MAPPING_OVERRIDE_PATH, default=None)
        start_parser.add_argument(
            OPT_ARG_BSP_PLATFORM_MAPPING_OVERRIDE_PATH, default=None
        )
        self._add_num_npus_argument(start_parser)
        start_parser.set_defaults(runner_action=self.start)

        stop_parser = actions.add_parser(
            "stop", help="stop selected FBOSS services (default: all)"
        )
        stop_parser.add_argument(
            "services",
            nargs="*",
            choices=_SERVICE_CHOICES,
            help="services to stop (default: all)",
        )
        self._add_num_npus_argument(stop_parser)
        stop_parser.set_defaults(runner_action=self.stop)

    @staticmethod
    def _add_num_npus_argument(parser: ArgumentParser) -> None:
        parser.add_argument(
            SUB_ARG_NUM_NPUS,
            choices=[1, 2],
            default=1,
            type=int,
            help="Number of NPUs (switch indexes). Default is 1.",
        )

    @staticmethod
    def _switch_indexes(args: Namespace) -> list[int]:
        return list(range(args.num_npus))

    @staticmethod
    def _prepare_agent_config(config_path: str) -> str:
        source = Path(config_path)
        if not source.is_file():
            raise RuntimeError(f"Agent config does not exist: {source}")

        fboss_data = os.environ.get("FBOSS_DATA")
        if not fboss_data:
            raise RuntimeError("FBOSS_DATA is not set; cannot locate NPU SDK metadata")

        output_directory = Path(GENERATED_CONFIG_ROOT) / SUB_CMD_SERVICES
        output_directory.mkdir(parents=True, exist_ok=True)
        prepared_config = output_directory / source.name
        staging_config = output_directory / f".{source.name}.tmp"
        try:
            staging_config.unlink(missing_ok=True)
            shutil.copyfile(source, staging_config)
            materialize_agent_config(
                config_path=staging_config,
                metadata_path=Path(fboss_data) / NPU_SDK_METADATA_FILENAME,
                binary_name=NPU_HW_AGENT_BINARY,
                output_directory=output_directory,
            )
            os.replace(staging_config, prepared_config)
        except Exception:
            staging_config.unlink(missing_ok=True)
            raise
        print(f"Using generated service config {prepared_config}")
        return str(prepared_config)

    @staticmethod
    def _stop_all(
        switch_indexes: list[int],
        services: set[str],
        raise_on_error: bool = True,
    ) -> None:
        failures = []

        def cleanup_agents() -> None:
            cleanup_sw_agent_service(SW_AGENT_SERVICE_PROD)
            cleanup_hw_agent_service(
                switch_indexes,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
            )

        cleanup_steps = [
            (_SERVICE_AGENT, cleanup_agents),
            (_SERVICE_QSFP, cleanup_qsfp_service),
            (_SERVICE_FSDB, cleanup_fsdb_service),
        ]
        for service, cleanup in cleanup_steps:
            if service not in services:
                continue
            try:
                cleanup()
            except Exception as error:
                failures.append(f"{service}: {error}")
        if failures and raise_on_error:
            raise RuntimeError("Failed to stop FBOSS services: " + "; ".join(failures))

    @staticmethod
    def _validate_start_args(args: Namespace, services: set[str]) -> None:
        if _SERVICE_AGENT in services and not args.agent_config:
            raise ValueError("--agent-config is required when agent is selected")
        if _SERVICE_QSFP in services and not args.qsfp_config:
            raise ValueError("--qsfp-config is required when qsfp is selected")

    @staticmethod
    def _validate_binaries(services: set[str]) -> None:
        binaries = []
        if _SERVICE_AGENT in services:
            binaries.extend(
                [
                    (_DEFAULT_OSS_HW_AGENT_SERVICE_BINARY, "HW agent binary"),
                    (_DEFAULT_OSS_SW_AGENT_SERVICE_BINARY, "SW agent binary"),
                ]
            )
        if _SERVICE_QSFP in services:
            binaries.append((_DEFAULT_OSS_QSFP_SERVICE_BINARY, "QSFP binary"))
        if _SERVICE_FSDB in services:
            binaries.append((_DEFAULT_OSS_FSDB_SERVICE_BINARY, "FSDB binary"))
        for binary, label in binaries:
            service_utils.resolve_binary(binary, label)

    def _start_services(
        self,
        args: Namespace,
        switch_indexes: list[int],
        services: set[str],
    ) -> None:
        fsdb_enabled = _SERVICE_FSDB in services
        if _SERVICE_AGENT in services:
            prepared_config = self._prepare_agent_config(args.agent_config)
            shutil.copyfile(prepared_config, _AGENT_CONFIG_PATH)

        if fsdb_enabled:
            setup_and_start_fsdb_service(
                fsdb_service_config_path=args.fsdb_config,
                is_warm_boot=False,
            )
        if _SERVICE_QSFP in services:
            setup_and_start_qsfp_service(
                qsfp_service_config_path=args.qsfp_config,
                platform_mapping_override_path=args.platform_mapping_override_path,
                bsp_platform_mapping_override_path=(
                    args.bsp_platform_mapping_override_path
                ),
                is_fsdb_disabled=not fsdb_enabled,
                is_warm_boot=False,
            )
        if _SERVICE_AGENT in services:
            setup_and_start_hw_agent_service(
                switch_indexes=switch_indexes,
                fboss_agent_config_path=_AGENT_CONFIG_PATH,
                platform_mapping_override_path=args.platform_mapping_override_path,
                is_fsdb_disabled=not fsdb_enabled,
                is_warm_boot=False,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
                hw_agent_for_testing=False,
            )
            setup_and_start_sw_agent_service(
                fboss_agent_config_path=_AGENT_CONFIG_PATH,
                is_warm_boot=False,
                sw_agent_service_name=SW_AGENT_SERVICE_PROD,
            )
            cold_boot_agents(
                switch_indexes,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
                sw_agent_service_name=SW_AGENT_SERVICE_PROD,
            )

    @staticmethod
    def _verify_services(
        args: Namespace,
        switch_indexes: list[int],
        services: set[str],
    ) -> None:
        expected_services = []
        if _SERVICE_FSDB in services:
            expected_services.append(_FSDB_SERVICE_OSS)
        if _SERVICE_QSFP in services:
            expected_services.append(_QSFP_SERVICE_OSS)
        if _SERVICE_AGENT in services:
            expected_services.append(SW_AGENT_SERVICE_PROD)
            expected_services.extend(
                f"{HW_AGENT_SERVICE_PROD}{index}" for index in switch_indexes
            )
        inactive_services = [
            service
            for service in expected_services
            if not service_utils.systemctl_is_active(service)
        ]
        if inactive_services:
            raise RuntimeError(
                "FBOSS services are not active: " + ", ".join(inactive_services)
            )
        if _SERVICE_AGENT in services and not wait_agent_responsive(
            find_fboss2_cli(), timeout_sec=AGENT_COLDBOOT_TIMEOUT_SEC
        ):
            raise RuntimeError("FBOSS agent did not become responsive")

    def start(self, args: Namespace) -> int:
        switch_indexes = self._switch_indexes(args)
        selected_services = args.services or _SERVICE_CHOICES
        services = set(selected_services)
        self._validate_start_args(args, services)
        self._validate_binaries(services)
        print(
            "Starting a persistent FBOSS service environment. "
            "Run `run_test.py services stop "
            f"{' '.join(selected_services)} --num-npus {args.num_npus}` when done."
        )
        try:
            self._start_services(args, switch_indexes, services)
            self._verify_services(args, switch_indexes, services)
        except Exception:
            self._stop_all(
                switch_indexes,
                services=services,
                raise_on_error=False,
            )
            raise

        print(f"Requested FBOSS services({' '.join(selected_services)}) are running.")
        return 0

    def stop(self, args: Namespace) -> int:
        selected_services = args.services or _SERVICE_CHOICES
        self._stop_all(self._switch_indexes(args), services=set(selected_services))
        print(
            f"Selected FBOSS services({' '.join(selected_services)}) have been stopped."
        )
        return 0
