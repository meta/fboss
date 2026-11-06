#!/usr/bin/env python3
# Copyright 2004-present Facebook. All Rights Reserved.

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ARG_FBOSS_TARBALL = "fboss_tar"

# TODO: paulcruz74 - deduplicate this from docker-build.py
FBOSS_IMAGE_NAME = "fboss_image"
FBOSS_CONTAINER_NAME = "FBOSS_BUILD_CONTAINER"

TEST_PATH_REGEX = re.compile(".*tests?$")
HW_TEST_PATH_REGEX = re.compile(".*hw_tests?$")
INTEGRATION_TEST_PATH_REGEX = re.compile(".*integration_tests?$")
EXCLUDED_TEST_REGEXES = [
    re.compile(".*hal_tests?$"),
]
CONTAINER_WORKDIR = "/var/FBOSS/fboss"

# Defaults for --asan runs, applied unless already set in the environment.
# Boost is linked into both libfolly.so and each test binary; ODR level 1
# skips that same-size duplicate but still reports size mismatches.
ASAN_ENV_DEFAULTS = {
    "ASAN_OPTIONS": "detect_leaks=0:detect_odr_violation=1",
    "ASAN_SYMBOLIZER_PATH": "/usr/local/llvm/bin/llvm-symbolizer",
}
# The Python interpreter is not ASAN-instrumented, so Python tests that load
# ASAN-built extension modules need the runtime preloaded. CPython's own
# allocations read as leaks, so leak detection is always off for them.
ASAN_PYTHON_ENV = (
    'LD_PRELOAD="$(c++ -print-file-name=libclang_rt.asan.so)" '
    'ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"'
)

_asan = False


def _python_test_shell(test_path: str, lib_path: str) -> str:
    prefix = f"{ASAN_PYTHON_ENV} " if _asan else ""
    return f"{prefix}LD_LIBRARY_PATH={lib_path}:/usr/local/lib64 python3 {test_path}"


def _asan_docker_env_args() -> list[str]:
    if not _asan:
        return []
    return [arg for k in ASAN_ENV_DEFAULTS for arg in ("-e", f"{k}={os.environ[k]}")]


def _is_inside_container() -> bool:
    """Detect if running inside a Docker or Podman container."""
    return os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")


_use_sudo = None


def _needs_sudo() -> bool:
    """Check if sudo is needed to run docker/podman."""
    global _use_sudo
    if _use_sudo is None:
        cp = subprocess.run(
            ["docker", "info"],
            capture_output=True,
            check=False,
        )
        _use_sudo = cp.returncode != 0
    return _use_sudo


def _docker_cmd() -> list[str]:
    """Return the docker command prefix, with sudo if needed."""
    if _needs_sudo():
        return ["sudo", "docker"]
    return ["docker"]


# TODO: paulcruz74 - deduplicate this from docker-build.py
def get_docker_path():
    github_actions_path = os.path.dirname(__file__)
    scripts_path = Path(github_actions_path).parent.absolute()
    oss_dir_path = Path(scripts_path).parent.absolute()
    return os.path.join(oss_dir_path, "docker")


def get_repo_path():
    github_actions_path = os.path.dirname(__file__)
    scripts_path = Path(github_actions_path).parent.absolute()
    return Path(scripts_path).parent.parent.parent.absolute()


def use_stable_hashes():
    cwd = os.getcwd()
    os.chdir(get_repo_path())

    rm_cmd = [
        "rm",
        "-rf",
        "build/deps/github_hashes/",
    ]
    subprocess.run(rm_cmd, check=False)

    extract_cmd = [
        "tar",
        "xvzf",
        "fboss/oss/stable_commits/latest_stable_hashes.tar.gz",
    ]
    subprocess.run(extract_cmd, check=False)

    os.chdir(cwd)


def is_container_running() -> bool:
    cmd = [
        *_docker_cmd(),
        "ps",
        "--filter",
        f"name=^/{FBOSS_CONTAINER_NAME}$",
        "--format",
        "{{.Names}}",
    ]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return FBOSS_CONTAINER_NAME in cp.stdout


def does_image_exist() -> bool:
    cmd = [*_docker_cmd(), "images", "-q", FBOSS_IMAGE_NAME]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return len(cp.stdout.strip()) > 0


def build_docker_image(docker_dir_path: str):
    fd, log_path = tempfile.mkstemp(suffix="docker-build.log")
    print(
        f"Attempting to build docker image from {docker_dir_path}/Dockerfile. You can run `sudo tail -f {log_path}` in order to follow along."
    )
    with os.fdopen(fd, "w") as output:
        dockerfile_path = os.path.join(docker_dir_path, "Dockerfile")
        cmd = [
            *_docker_cmd(),
            "build",
            ".",
            "-t",
            FBOSS_IMAGE_NAME,
            "-f",
            dockerfile_path,
        ]
        cp = subprocess.run(
            cmd,
            check=False,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
    if cp.returncode != 0:
        err_msg = f"An error occurred while trying to build the FBOSS docker image: {cp.stderr}"
        print(err_msg, file=sys.stderr)
        sys.exit(1)


def unpack_tarball(tarball_path: str) -> str:
    output_dir = tempfile.mkdtemp()
    print(f"Unpacking tarball to {output_dir}")
    subprocess.run(["tar", "-xvf", tarball_path, "-C", output_dir], check=False)
    return output_dir


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(ARG_FBOSS_TARBALL)
    parser.add_argument(
        "--exclude",
        dest="excludes",
        action="append",
        default=[],
        help="Test binary name to exclude. Can be specified multiple times.",
    )
    parser.add_argument(
        "--asan",
        action="store_true",
        help="The tarball is from a run-getdeps.py --asan build.",
    )

    return parser.parse_args()


def cleanup(output_dir: str):
    shutil.rmtree(output_dir)


def find_tests(output_dir, excludes=None):
    tests = []
    bin_dir = os.path.join(output_dir, "bin")
    for f in os.listdir(bin_dir):
        file_path = os.path.join(bin_dir, f)
        if excludes and f in excludes:
            print(f"Excluding test: {f}")
            continue
        # Make sure to ignore hw/integration tests and excluded test patterns.
        if (
            is_test(file_path)
            and not is_e2e_test(file_path)
            and not is_excluded_test(file_path)
        ):
            tests.append(f)
    return tests


def is_python_dir_test(path: str) -> bool:
    return os.path.isdir(path) and os.path.isfile(os.path.join(path, "__main__.py"))


def is_test(path: str) -> bool:
    match = TEST_PATH_REGEX.match(path)
    if not match:
        return False

    if is_python_dir_test(path):
        return True

    return os.path.isfile(path) and os.access(path, os.X_OK)


def is_excluded_test(path):
    return any(regex.match(path) for regex in EXCLUDED_TEST_REGEXES)


def is_e2e_test(path: str) -> bool:
    return is_hw_test(path) or is_integration_test(path)


def is_hw_test(path: str) -> bool:
    match = HW_TEST_PATH_REGEX.match(path)
    if not match:
        return False

    return os.path.isfile(path) and os.access(path, os.X_OK)


def is_integration_test(path: str) -> bool:
    match = INTEGRATION_TEST_PATH_REGEX.match(path)
    if not match:
        return False

    return os.path.isfile(path) and os.access(path, os.X_OK)


def run_test(test: str, output_dir: str) -> bool:
    use_stable_hashes()
    cmd_args = [*_docker_cmd(), "run"]
    lib_path = os.path.join(output_dir, "lib")
    cmd_args.extend(["-e", f"LD_LIBRARY_PATH={lib_path}"])
    cmd_args.extend(_asan_docker_env_args())
    # Mount fboss repository in container
    cmd_args.extend(["-v", f"{get_repo_path()}:{CONTAINER_WORKDIR}:z"])
    cmd_args.extend(["-v", f"{output_dir}:{output_dir}:z"])
    # Add required capability for sudo permissions
    cmd_args.append("--cap-add=CAP_AUDIT_WRITE")
    # Use host network to ensure IPv6 localhost (::1) is available for tests
    cmd_args.append("--network=host")
    cmd_args.append(f"{FBOSS_IMAGE_NAME}:latest")
    test_path = os.path.join(output_dir, "bin", test)
    if is_python_dir_test(test_path):
        cmd_args.extend(["bash", "-c", _python_test_shell(test_path, lib_path)])
    else:
        cmd_args.append(test_path)
    cp = subprocess.run(cmd_args, check=False)
    return cp.returncode == 0


def run_test_local(test: str, output_dir: str) -> bool:
    use_stable_hashes()
    lib_path = os.path.join(output_dir, "lib")
    test_path = os.path.join(output_dir, "bin", test)
    env = os.environ.copy()
    if is_python_dir_test(test_path):
        cmd = ["bash", "-c", _python_test_shell(test_path, lib_path)]
    else:
        env["LD_LIBRARY_PATH"] = lib_path
        cmd = [test_path]
    cp = subprocess.run(cmd, env=env, check=False)
    return cp.returncode == 0


def run_test_exec(test: str, output_dir: str) -> bool:
    use_stable_hashes()
    cmd_args = [*_docker_cmd(), "exec"]
    lib_path = os.path.join(output_dir, "lib")
    test_path = os.path.join(output_dir, "bin", test)
    cmd_args.extend(["-e", f"LD_LIBRARY_PATH={lib_path}"])
    cmd_args.extend(_asan_docker_env_args())
    cmd_args.append(FBOSS_CONTAINER_NAME)
    if is_python_dir_test(test_path):
        cmd_args.extend(["bash", "-c", _python_test_shell(test_path, lib_path)])
    else:
        cmd_args.append(test_path)
    cp = subprocess.run(cmd_args, check=False)
    return cp.returncode == 0


def main():
    global _asan
    args = parse_args()
    _asan = args.asan
    if _asan:
        for name, value in ASAN_ENV_DEFAULTS.items():
            os.environ.setdefault(name, value)

    inside_container = _is_inside_container()
    container_running = False

    if inside_container:
        print("Running inside container, executing tests directly.")
    else:
        container_running = is_container_running()
        if container_running:
            print(f"Reusing running container '{FBOSS_CONTAINER_NAME}'.")
        elif does_image_exist():
            print(f"Image '{FBOSS_IMAGE_NAME}' already exists, skipping build.")
        else:
            docker_path = get_docker_path()
            build_docker_image(docker_path)

    output_dir = unpack_tarball(args.fboss_tar)

    # If reusing a running container, copy test artifacts in once
    if container_running:
        print(f"Copying test artifacts into container '{FBOSS_CONTAINER_NAME}'...")
        subprocess.run(
            [
                *_docker_cmd(),
                "cp",
                output_dir,
                f"{FBOSS_CONTAINER_NAME}:{output_dir}",
            ],
            check=False,
        )

    tests = find_tests(output_dir, args.excludes)

    if inside_container:
        run_fn = run_test_local
    elif container_running:
        run_fn = run_test_exec
    else:
        run_fn = run_test

    failed_tests = [test for test in tests if not run_fn(test, output_dir)]

    if failed_tests:
        print(f"The following tests failed: {', '.join(failed_tests)}")
        exit_code = 1
    else:
        print("All tests passed!")
        exit_code = 0

    cleanup(output_dir)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
