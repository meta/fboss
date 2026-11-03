# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import os

from fboss_test_runner.services import service_utils

_DEFAULT_OSS_FSDB_SERVICE_BINARY = "fsdb"
# TODO: Add file.
_DEFAULT_OSS_FSDB_SERVICE_CONFIG_PATH = "link_test_configs/fsdb.conf"
_DEFAULT_FSDB_SERVICE_VOLATILE_DIR = "/tmp/fboss/fsdb_service"
_FSDB_SERVICE_COLD_BOOT_FILE = "cold_boot_once_fsdb_service"

# These two fsdb services are running in prod and testing environment.
_FSDB_SERVICE_PROD = "fsdb.service"
_FSDB_SERVICE_FOR_TESTING = "fsdb_service_for_testing"
# Introduce an oss version of fsdb_service to run in oss environment.
_FSDB_SERVICE_OSS = "fsdb_service_oss"

_FSDB_SERVICE_UNIT_FILE_PATH = f"/run/systemd/system/{_FSDB_SERVICE_OSS}.service"


def _setup_fsdb_service(fsdb_service_config_path: str | None = None) -> None:
    print(f"Setting up {_FSDB_SERVICE_OSS}")

    fsdb_service_bin_path = service_utils.resolve_binary(
        _DEFAULT_OSS_FSDB_SERVICE_BINARY, "fsdb_service binary"
    )

    if not fsdb_service_config_path:
        fsdb_service_config_path = os.path.join(
            os.environ.get("FBOSS_DATA") or "/opt/fboss/share",
            _DEFAULT_OSS_FSDB_SERVICE_CONFIG_PATH,
        )
    else:
        fsdb_service_config_path = os.path.abspath(fsdb_service_config_path)
    service_utils.validate_path(fsdb_service_config_path, "fsdb_service config path")

    cleanup_fsdb_service()

    fsdb_arg_for_config = f"--fsdb_config={fsdb_service_config_path}"
    fsdb_args_for_testing = "--checkOperOwnership=false  --ssl_policy=disabled"
    fsdb_args_for_prod_features = "--useIdPathsForSubs=false --serveHeartbeats"

    fsdb_service_cmd = f"{fsdb_service_bin_path} {fsdb_arg_for_config} {fsdb_args_for_testing} {fsdb_args_for_prod_features}"

    service_utils.setup_service(
        service_name=_FSDB_SERVICE_OSS,
        unit_file_path=_FSDB_SERVICE_UNIT_FILE_PATH,
        unit_file_content=service_utils.build_unit_file_content(
            description="FSDB Service For Testing",
            exec_start_cmd=fsdb_service_cmd,
            syslog_identifier=_FSDB_SERVICE_OSS,
            memory_max="2.4G",
        ),
    )


def setup_and_start_fsdb_service(
    fsdb_service_config_path: str | None = None, is_warm_boot: bool = False
) -> None:
    _setup_fsdb_service(fsdb_service_config_path)
    service_utils.start_service(
        service_name=_FSDB_SERVICE_OSS,
        unit_file_path=_FSDB_SERVICE_UNIT_FILE_PATH,
        is_warm_boot=is_warm_boot,
        cold_boot_volatile_dir=_DEFAULT_FSDB_SERVICE_VOLATILE_DIR,
        cold_boot_marker_file=_FSDB_SERVICE_COLD_BOOT_FILE,
        sleep_after_start=10,
    )


def cleanup_fsdb_service() -> None:
    """
    Make sure no fsdb_service service/binary is running and also disable
    fsdb_service_for_testing
    """
    print(f"Cleaning up {_FSDB_SERVICE_OSS}")

    service_utils.cleanup_service(
        service_names_to_stop=[
            _FSDB_SERVICE_PROD,
            _FSDB_SERVICE_FOR_TESTING,
            _FSDB_SERVICE_OSS,
        ],
        service_name_to_disable=_FSDB_SERVICE_OSS,
    )
