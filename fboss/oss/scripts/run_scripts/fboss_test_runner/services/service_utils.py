#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import os
import shutil
import subprocess

_DEFAULT_OSS_LOG_DIR = "/opt/fboss/logs/"


def systemctl_stop(service_name: str) -> int:
    return subprocess.run(f"systemctl stop {service_name}", shell=True).returncode


def systemctl_start(service_name: str) -> int:
    return subprocess.run(f"systemctl start {service_name}", shell=True).returncode


def systemctl_enable(unit_file_path: str) -> int:
    return subprocess.run(f"systemctl enable {unit_file_path}", shell=True).returncode


def systemctl_disable(service_name: str) -> int:
    return subprocess.run(f"systemctl disable {service_name}", shell=True).returncode


def systemctl_daemon_reload() -> int:
    return subprocess.run("systemctl daemon-reload", shell=True).returncode


def systemctl_is_active(service_name: str) -> bool:
    return (
        subprocess.run(
            f"systemctl is-active --quiet {service_name}", shell=True
        ).returncode
        == 0
    )


def pkill_service(service_name: str) -> int:
    return subprocess.run(f"pkill -f {service_name}", shell=True).returncode


def write_rsyslog_conf(service_name: str) -> None:
    rsyslog_conf_path = f"/etc/rsyslog.d/{service_name}.conf"
    rsyslog_content = (
        f'if $programname == "{service_name}" '
        f"then {_DEFAULT_OSS_LOG_DIR}/{service_name}.log\n"
        f"& stop\n"
    )
    with open(rsyslog_conf_path, "w") as f:
        f.write(rsyslog_content)
        f.flush()


def remove_rsyslog_conf(service_name: str) -> None:
    subprocess.run(f"rm -f /etc/rsyslog.d/{service_name}.conf", check=False, shell=True)


def restart_rsyslog() -> None:
    subprocess.run("systemctl restart rsyslog", shell=True)


def setup_cold_boot_marker(volatile_dir: str, marker_file: str) -> None:
    subprocess.run(f"mkdir -p {volatile_dir}", check=False, shell=True)
    subprocess.run(f"touch {volatile_dir}/{marker_file}", check=False, shell=True)


def start_service(
    service_name: str,
    unit_file_path: str,
    is_warm_boot: bool = False,
    cold_boot_volatile_dir: str | None = None,
    cold_boot_marker_file: str | None = None,
    sleep_after_start: int = 0,
) -> None:
    boot_mode = "WARMBOOT" if is_warm_boot else "COLDBOOT"
    print(f"Starting {service_name} with {boot_mode}")
    if not is_warm_boot and cold_boot_volatile_dir and cold_boot_marker_file:
        setup_cold_boot_marker(cold_boot_volatile_dir, cold_boot_marker_file)
    enable_and_start_service(service_name, unit_file_path, sleep_after_start)


def enable_and_start_service(
    service_name: str,
    unit_file_path: str,
    sleep_after_start: int = 0,
) -> None:
    systemctl_enable(unit_file_path)
    systemctl_daemon_reload()
    start_cmd = f"systemctl start {service_name}"
    if sleep_after_start > 0:
        start_cmd += f"; sleep {sleep_after_start}"
    subprocess.run(start_cmd, shell=True)


def cleanup_service(
    service_names_to_stop: list[str],
    service_name_to_disable: str,
    service_names_to_pkill: list[str] | None = None,
) -> None:
    for svc in service_names_to_stop:
        systemctl_stop(svc)
    systemctl_disable(service_name_to_disable)
    systemctl_daemon_reload()
    for svc in service_names_to_pkill or service_names_to_stop:
        pkill_service(svc)
    remove_rsyslog_conf(service_name_to_disable)
    restart_rsyslog()


def validate_path(path: str, label: str) -> None:
    if not os.path.exists(path):
        raise Exception(f"{label}: {path} does not exist")
    print(f"{label}: {path}")


def resolve_binary(binary: str, label: str) -> str:
    if os.path.isabs(binary):
        validate_path(binary, label)
        return binary
    binary_path = shutil.which(binary)
    if binary_path is None:
        raise Exception(f"{label}: {binary} does not exist")
    print(f"{label}: {binary_path}")
    return binary_path


def build_unit_file_content(
    description: str,
    exec_start_cmd: str,
    syslog_identifier: str,
    memory_max: str = "3.75G",
    tsan_options: str = "die_after_fork=0",
) -> str:
    library_path = os.environ.get("LD_LIBRARY_PATH") or "/opt/fboss/lib"
    environment = [
        f"Environment=TSAN_OPTIONS={tsan_options}",
        f"Environment=LD_LIBRARY_PATH={library_path}",
    ]
    # systemd does not inherit the launching shell's env, so vars the SDK needs
    # must be declared here. Silicon One resolves its resource tree as
    # $BASE_OUTPUT_DIR/res and aborts hw_agent init without it. Forwarded only
    # when set, so it is a no-op for SDKs that don't use it.
    if base_output_dir := os.environ.get("BASE_OUTPUT_DIR"):
        environment.append(f"Environment=BASE_OUTPUT_DIR={base_output_dir}")
    environment_lines = "\n".join(environment)

    return f"""
[Unit]
Description={description}

[Service]
LimitNOFILE=10000000
LimitCORE=32G
MemoryMax={memory_max}
MemorySwapMax=0

{environment_lines}
ExecStart={exec_start_cmd}
SyslogIdentifier={syslog_identifier}
Restart=no

[Install]
WantedBy=multi-user.target
"""


def write_unit_file(unit_file_path: str, unit_file_content: str) -> None:
    with open(unit_file_path, "w") as f:
        f.write(unit_file_content)
        f.flush()


def setup_service(
    service_name: str,
    unit_file_path: str,
    unit_file_content: str,
) -> None:
    write_unit_file(unit_file_path, unit_file_content)
    write_rsyslog_conf(service_name)
    subprocess.run("systemctl restart rsyslog; sleep 5", shell=True)
