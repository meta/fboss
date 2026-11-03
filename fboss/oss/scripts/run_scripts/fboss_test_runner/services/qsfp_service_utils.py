#!/usr/bin/env python3
# @noautodeps
# Copyright Meta Platforms, Inc. and affiliates.


from fboss_test_runner.services import service_utils

_DEFAULT_OSS_QSFP_SERVICE_BINARY = "qsfp_service"
_PLATFORM_MAPPING_OVERRIDE_PATH_ARG = "--platform_mapping_override_path"
_BSP_PLATFORM_MAPPING_OVERRIDE_PATH_ARG = "--bsp_platform_mapping_override_path"
_DEFAULT_QSFP_SERVICE_VOLATIRE_DIR = "/dev/shm/fboss/qsfp_service"
_QSFP_SERVICE_COLD_BOOT_FILE = "cold_boot_once_qsfp_service"

# These two qsfp services are running in prod and testing environment.
_QSFP_SERVICE_PROD = "qsfp_service"
_QSFP_SERVICE_FOR_TESTING = "qsfp_service_for_testing"
# Introduce an oss version of qsfp_service to run in oss environment.
_QSFP_SERVICE_OSS = "qsfp_service_oss"
_QSFP_SERVICE_UNIT_FILE_PATH = f"/run/systemd/system/{_QSFP_SERVICE_OSS}.service"


def _setup_qsfp_service(
    qsfp_service_config_path: str | None = None,
    platform_mapping_override_path: str | None = None,
    bsp_platform_mapping_override_path: str | None = None,
    is_fsdb_disabled: bool = False,
) -> None:
    print(f"Setting up {_QSFP_SERVICE_OSS}")

    qsfp_service_bin_path = service_utils.resolve_binary(
        _DEFAULT_OSS_QSFP_SERVICE_BINARY, "qsfp_service binary"
    )

    if not qsfp_service_config_path:
        raise Exception(f"qsfp_service config path: {qsfp_service_config_path} is None")
    service_utils.validate_path(qsfp_service_config_path, "qsfp_service config path")

    cleanup_qsfp_service()

    extra_args = ""
    if platform_mapping_override_path:
        service_utils.validate_path(
            platform_mapping_override_path, "platform_mapping override path"
        )
        extra_args += (
            f"{_PLATFORM_MAPPING_OVERRIDE_PATH_ARG} {platform_mapping_override_path}"
        )
    if bsp_platform_mapping_override_path:
        service_utils.validate_path(
            bsp_platform_mapping_override_path, "bsp_platform_mapping override path"
        )
        extra_args += f" {_BSP_PLATFORM_MAPPING_OVERRIDE_PATH_ARG} {bsp_platform_mapping_override_path}"

    if not is_fsdb_disabled:
        extra_args += " --fsdb_client_ssl_preferred=false --publish_stats_to_fsdb=true --publish_state_to_fsdb=true "

    qsfp_service_cmd = (
        f"{qsfp_service_bin_path} --qsfp-config {qsfp_service_config_path} {extra_args}"
    )

    service_utils.setup_service(
        service_name=_QSFP_SERVICE_OSS,
        unit_file_path=_QSFP_SERVICE_UNIT_FILE_PATH,
        unit_file_content=service_utils.build_unit_file_content(
            description="QSFP Service For OSS",
            exec_start_cmd=qsfp_service_cmd,
            syslog_identifier=_QSFP_SERVICE_OSS,
            tsan_options='"die_after_fork=0 halt_on_error=1"',
        ),
    )


def setup_and_start_qsfp_service(
    qsfp_service_config_path: str | None = None,
    platform_mapping_override_path: str | None = None,
    bsp_platform_mapping_override_path: str | None = None,
    is_fsdb_disabled: bool = False,
    is_warm_boot: bool = False,
) -> None:
    _setup_qsfp_service(
        qsfp_service_config_path,
        platform_mapping_override_path,
        bsp_platform_mapping_override_path,
        is_fsdb_disabled,
    )
    service_utils.start_service(
        service_name=_QSFP_SERVICE_OSS,
        unit_file_path=_QSFP_SERVICE_UNIT_FILE_PATH,
        is_warm_boot=is_warm_boot,
        cold_boot_volatile_dir=_DEFAULT_QSFP_SERVICE_VOLATIRE_DIR,
        cold_boot_marker_file=_QSFP_SERVICE_COLD_BOOT_FILE,
        sleep_after_start=10,
    )


def cleanup_qsfp_service() -> None:
    """
    Make sure no qsfp_service(PROD) or qsfp_service_for_testing(TESTING) is running and also disable
    qsfp_service_oss(OSS) if it is enabled.
    """
    print(f"Cleaning up {_QSFP_SERVICE_OSS}")

    service_utils.cleanup_service(
        service_names_to_stop=[
            _QSFP_SERVICE_PROD,
            _QSFP_SERVICE_FOR_TESTING,
            _QSFP_SERVICE_OSS,
        ],
        service_name_to_disable=_QSFP_SERVICE_OSS,
    )
