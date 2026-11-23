#!/usr/bin/env python3
# Copyright 2004-present Facebook. All Rights Reserved.

import argparse
import concurrent.futures
import glob
import os
import pathlib
import stat
import sys
import tarfile
import tempfile
from collections.abc import Mapping
from typing import TYPE_CHECKING

if TYPE_CHECKING or __package__:
    from . import component_version
    from .platform_configs_utils import (
        get_forwarding_stack_config_paths,
        get_platform_descriptor_paths,
    )
else:
    import component_version
    from platform_configs_utils import (
        get_forwarding_stack_config_paths,
        get_platform_descriptor_paths,
    )

SRC_DIR = pathlib.Path("/var/FBOSS/fboss")
OSS_DIR = SRC_DIR / "fboss/oss"
SCRIPTS_DIR = OSS_DIR / "scripts"
RUN_SCRIPTS_DIR = SRC_DIR / "fboss/oss/scripts/run_scripts"
RUN_CONFIGS_DIR = SRC_DIR / "fboss/oss/scripts/run_configs"
PLATFORM_CONFIGS_DIR = SRC_DIR / "fboss/configs/platforms"

BUILD_DIR = "--build-dir"
TARGET_NAMES = (
    "agent-benchmarks",
    "forwarding-stack",
    "platform-stack",
    "bgp",
    "openr",
)
NPU_SDK_METADATA_FILENAME = "npu_sdk_metadata.json"
NPU_SDK_METADATA_ARCHIVE_PATH = f"share/{NPU_SDK_METADATA_FILENAME}"
NPU_SDK_UTILS_SCRIPT = SCRIPTS_DIR / "npu_sdk_utils.py"


# Maps getdeps package name to library name when they differ.
LIB_NAME_OVERRIDES = {"fmt-python": "fmt", "sai_impl_staging": "sai_impl"}

# Global definitions describing what we package for each target.

COMMON_LIBS = ["gflags", "glog", "folly", "fmt-python", "wangle", "fizz", "mvfst"]

FORWARDING_BINARIES = [
    "diag_shell_client",
    "fboss2",
    "fboss2-dev",
    "fboss2-readonly",
    "fboss_hw_agent-sai_impl",
    "fboss_pai_diag_shell_client",
    "fboss_sw_agent",
    "fsdb",
    "qsfp_service",
    "wedge_agent-sai_impl",
    "wedge_qsfp_util",
]

FORWARDING_EXTRA = {
    OSS_DIR / "hw_sanity_tests": "share/hw_sanity_tests",
    RUN_SCRIPTS_DIR / "fboss_agent_utils.py": "bin/fboss_agent_utils.py",
    RUN_SCRIPTS_DIR / "qsfp_service_utils.py": "bin/qsfp_service_utils.py",
    RUN_SCRIPTS_DIR / "run_test.py": "bin/run_test.py",
    NPU_SDK_UTILS_SCRIPT: "bin/npu_sdk_utils.py",
    RUN_SCRIPTS_DIR / "setup.py": "bin/setup.py",
    RUN_SCRIPTS_DIR / "setup_fboss_env": "bin/setup_fboss_env",
    RUN_SCRIPTS_DIR / "topology.cdf": "bin/topology.cdf",
    # bash-completion loads the file named after the command; fboss2-dev is
    # registered by the same script.
    OSS_DIR
    / "scripts/fboss2_completion.bash": "share/bash-completion/completions/fboss2",
    RUN_CONFIGS_DIR / "default_configs": "share/default_configs",
    RUN_CONFIGS_DIR / "j3b": "share/j3b",
    RUN_CONFIGS_DIR / "janga800bic": "share/janga800bic",
    RUN_CONFIGS_DIR / "r3": "share/r3",
    RUN_CONFIGS_DIR / "tahan800bc": "share/tahan800bc",
    RUN_CONFIGS_DIR / "th": "share/th",
    RUN_CONFIGS_DIR / "th3": "share/th3",
    RUN_CONFIGS_DIR / "th4": "share/th4",
    RUN_CONFIGS_DIR / "th5": "share/th5",
}

# run-getdeps.py stages the SAI as installed/sai_impl_staging-<hash>/lib.
FORWARDING_LIBS = ["sai_impl_staging"]

# BGP and Open/R install rather than leaving binaries in the build tree
# (`install(TARGETS bgp_bin DESTINATION sbin)`), so their binaries come from
# the getdeps install tree instead of build/fboss.
BGP_BINARIES = ["bgp"]

# Deps from the getdeps manifests that COMMON_LIBS does not already cover.
BGP_LIBS = [
    # bgp installs its own shared libraries (install(TARGETS ... DESTINATION lib))
    "bgp",
    "fb303",
    "fbthrift",
    "openr",
    "re2",
    "zstd",
]

OPENR_BINARIES = ["openr"]

OPENR_LIBS = ["openr", "fb303", "fbthrift", "re2"]

FORWARDING_TEST_BINARIES = [
    "fboss-platform-mapping-gen",
    "led_service_hw_test",
    "multi_switch_agent_hw_test",
    "qsfp_hw_test",
    "sai_agent_hw_test-sai_impl",
    "sai_agent_scale_test-sai_impl",
    "sai_invariant_agent_test-sai_impl",
    "sai_mono_link_test-sai_impl",
    "sai_multi_link_test-sai_impl",
    "sai_replayer-sai_impl",
    "sai_test-sai_impl",
]

FORWARDING_TEST_EXTRA = {
    OSS_DIR
    / "fboss2_integration_known_bad_tests": "share/fboss2_integration_known_bad_tests",
    OSS_DIR
    / "fboss2_integration_unsupported_tests": "share/fboss2_integration_unsupported_tests",
    OSS_DIR / "hw_known_bad_tests": "share/hw_known_bad_tests",
    OSS_DIR / "hw_test_configs": "share/hw_test_configs",
    OSS_DIR / "link_known_bad_tests": "share/link_known_bad_tests",
    OSS_DIR / "link_test_configs": "share/link_test_configs",
    OSS_DIR / "production_features": "share/production_features",
    OSS_DIR / "qsfp_known_bad_tests": "share/qsfp_known_bad_tests",
    OSS_DIR / "qsfp_test_configs": "share/qsfp_test_configs",
    OSS_DIR / "qsfp_unsupported_tests": "share/qsfp_unsupported_tests",
    OSS_DIR / "sai_hw_unsupported_tests": "share/sai_hw_unsupported_tests",
    RUN_SCRIPTS_DIR / "brcmsim.py": "bin/brcmsim.py",
}

AGENT_BENCHMARK_BINARIES = [
    "sai_all_benchmarks-sai_impl",
    "sai_multi_switch_all_benchmarks-sai_impl",
]

PLATFORM_BINARIES = [
    "data_corral_service",
    "fan_service",
    "fw_util",
    "led_service",
    "platform_manager",
    "rackmon",
    "sensor_service",
    "sensor_service_client",
    "rma-showtech",
    "weutil",
]

PLATFORM_EXTRA = {
    OSS_DIR
    / "hw_sanity_tests/bsp_sanity_tests.conf": "share/hw_sanity_tests/bsp_sanity_tests.conf"
}

PLATFORM_LIBS = []

PLATFORM_TEST_BINARIES = [
    "bsp_tests",
    "data_corral_service_hw_test",
    "fan_service_hw_test",
    "fboss-bspmapping-gen",
    "fixmyfboss",
    "fw_util_hw_test",
    "mac_address_check_test",
    "platform_config_lib_config_generator",
    "platform_hw_test",
    "platform_manager_hw_test",
    "rackmon_test",
    "sensor_service_hw_test",
    "sensor_service_utils_test",
    "weutil_crc16_ccitt_test",
    "weutil_fboss_eeprom_interface_test",
    "weutil_hw_test",
]

PLATFORM_TEST_EXTRA = {
    RUN_SCRIPTS_DIR / "run_test.py": "bin/run_test.py",
    RUN_SCRIPTS_DIR / "setup_fboss_env": "bin/setup_fboss_env",
}


def _find_getdeps_libs(
    build_dir: pathlib.Path, packages: list[str]
) -> dict[pathlib.Path, str]:
    """Find shared libraries for the given packages under the getdeps installed directory."""
    libs = {}
    for pkg in packages:
        pkg_dir = _find_installed_pkg_dir(build_dir, pkg)
        if pkg_dir is None:
            print(f"Warning: no .so libraries found for {pkg}")
            continue

        lib_name = LIB_NAME_OVERRIDES.get(pkg, pkg)
        matches = []
        for lib_dir in ("lib64", "lib"):
            pattern = str(pkg_dir / lib_dir / f"lib{lib_name}*.so*")
            matches.extend(glob.glob(pattern))
        if matches:
            for path in matches:
                libs[pathlib.Path(path)] = f"lib/{os.path.basename(path)}"
        else:
            print(f"Warning: no .so libraries found for {pkg}")
    return libs


def write_tar(filename: str, contents: Mapping[pathlib.Path, str]) -> None:
    if not contents:
        return

    print(f"Creating {filename}...")
    records = component_version.collect_binary_records(contents)
    with tarfile.open(filename, "w") as tar:
        for src, dest in contents.items():
            if not os.path.exists(src):
                print(f"Warning: {src} does not exist")
                continue
            tar.add(src, dest)

        if records:
            with tempfile.TemporaryDirectory() as temporary:
                declaration = pathlib.Path(temporary) / "fboss-component-version.json"
                component_version.write_declaration(declaration, records, "package.py")
                tar.add(declaration, component_version.DECLARATION_PATH)

        # Add lib64 -> lib symlink so binaries with RPATH/RUNPATH pointing to
        # lib64/ can find libraries packaged under lib/ (e.g. fboss2-dev).
        tarinfo = tarfile.TarInfo(name="lib64")
        tarinfo.type = tarfile.SYMTYPE
        tarinfo.linkname = "lib"
        tar.addfile(tarinfo)


def _find_installed_pkg_dir(build_dir: pathlib.Path, pkg: str) -> pathlib.Path | None:
    """Most recent getdeps install directory matching a package, or None.

    Used to locate shared libraries, which do not necessarily live in the
    package's own tree: libfolly.so ships from installed/folly-python, while
    installed/folly holds no .so at all. The suffixed glob is what finds them,
    so it must not be narrowed to an exact name match.
    """
    pkg_dirs = sorted(
        (build_dir / "installed").glob(f"{pkg}-*"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not pkg_dirs:
        return None
    if len(pkg_dirs) > 1:
        print(
            f"Multiple directories found for {pkg}, using most recent: {pkg_dirs[0].name}"
        )
    return pkg_dirs[0]


def _find_installed_project_dir(
    build_dir: pathlib.Path, project: str
) -> pathlib.Path | None:
    """A project's own getdeps install tree, or None.

    Distinct from _find_installed_pkg_dir: a project's binaries are only ever in
    its own tree, and getdeps names that tree for first-party projects without a
    build-config hash -- installed/bgp, not installed/bgp-<hash>.
    """
    exact = build_dir / "installed" / project
    return exact if exact.is_dir() else None


def _find_installed_bin_dirs(
    build_dir: pathlib.Path, project: str
) -> list[pathlib.Path]:
    """Every binary directory in a project's install tree.

    Both are searched rather than just the first: a project may install some
    binaries to bin/ and others to sbin/, and picking one would drop the rest
    with only a warning.
    """
    pkg_dir = _find_installed_project_dir(build_dir, project)
    if pkg_dir is None:
        raise RuntimeError(f"No install tree for {project} under {build_dir}/installed")

    bin_dirs = [pkg_dir / d for d in ("sbin", "bin") if (pkg_dir / d).is_dir()]
    if not bin_dirs:
        raise RuntimeError(f"No bin/ or sbin/ under {pkg_dir}")
    return bin_dirs


def _resolve_binaries(
    bin_dirs: list[pathlib.Path], names: list[str], required: bool = False
) -> dict:
    """Map each binary to bin/<name>, searching every candidate directory.

    With required=True a binary that cannot be found raises. Targets whose
    binary list is a single entry would otherwise publish a tarball holding
    only libraries, and exit zero doing it.
    """
    resolved = {}
    missing = []
    for name in names:
        for bin_dir in bin_dirs:
            candidate = bin_dir / name
            if candidate.is_file():
                resolved[candidate] = f"bin/{name}"
                break
        else:
            missing.append(name)
            # Fall back to the first candidate so write_tar reports the missing
            # path, as it did before this searched more than one directory.
            resolved[bin_dirs[0] / name] = f"bin/{name}"

    if required and missing:
        searched = ", ".join(str(d) for d in bin_dirs)
        raise RuntimeError(f"Binaries not found in {searched}: {', '.join(missing)}")
    return resolved


def _build_target(target: str, build_dir: pathlib.Path):
    """Return mappings for a given target and build_dir

    The global lists/dicts above describe what we package. Here we only wire
    them up to the build-dir specific directory.
    """

    bins = []
    extras = {}
    libs = []
    bin_dirs = None
    require_bins = False
    test_bins = []
    test_extras = {}

    if target == "forwarding-stack":
        bins = FORWARDING_BINARIES
        extras = {
            **FORWARDING_EXTRA,
            **get_platform_descriptor_paths(PLATFORM_CONFIGS_DIR),
        }
        libs = FORWARDING_LIBS + COMMON_LIBS
        test_bins = FORWARDING_TEST_BINARIES
        test_extras = {
            **FORWARDING_TEST_EXTRA,
            **get_forwarding_stack_config_paths(PLATFORM_CONFIGS_DIR),
        }
    elif target == "platform-stack":
        bins = PLATFORM_BINARIES
        extras = PLATFORM_EXTRA
        libs = PLATFORM_LIBS + COMMON_LIBS
        test_bins = PLATFORM_TEST_BINARIES
        test_extras = PLATFORM_TEST_EXTRA
    elif target == "agent-benchmarks":
        bins = AGENT_BENCHMARK_BINARIES
        extras = {
            OSS_DIR / "hw_benchmark_tests": "share/hw_benchmark_tests",
            **get_forwarding_stack_config_paths(PLATFORM_CONFIGS_DIR),
        }
    elif target == "bgp":
        bins = BGP_BINARIES
        libs = BGP_LIBS + COMMON_LIBS
        # bgp and openr install their binaries; fboss leaves them in the build tree.
        bin_dirs = _find_installed_bin_dirs(build_dir, "bgp")
        require_bins = True
    elif target == "openr":
        bins = OPENR_BINARIES
        libs = OPENR_LIBS + COMMON_LIBS
        bin_dirs = _find_installed_bin_dirs(build_dir, "openr")
        require_bins = True

    if bin_dirs is None:
        bin_dirs = [build_dir / "build" / "fboss"]

    prod_files = _resolve_binaries(bin_dirs, bins, required=require_bins)
    prod_files.update(extras)
    prod_files.update(_find_getdeps_libs(build_dir, libs))

    # Include libunwind from llvm locally installed in the build container because CentOS does not have an equivalent
    # version and it is not installed by getdeps to be handled in _find_getdeps_libs().
    for ext in ("so", "so.1", "so.1.0"):
        prod_files[
            pathlib.Path(
                f"/usr/local/llvm/lib/x86_64-unknown-linux-gnu/libunwind.{ext}"
            )
        ] = f"lib/libunwind.{ext}"

    test_files = _resolve_binaries(bin_dirs, test_bins)
    test_files.update(test_extras)

    if target in ("agent-benchmarks", "forwarding-stack"):
        metadata_path = build_dir / "build" / "fboss" / NPU_SDK_METADATA_FILENAME
        if metadata_path.is_file():
            if stat.S_IMODE(metadata_path.stat().st_mode) != 0o444:
                raise RuntimeError(
                    f"NPU SDK metadata must be read-only: {metadata_path}"
                )
            prod_files[metadata_path] = NPU_SDK_METADATA_ARCHIVE_PATH
            if target == "forwarding-stack":
                test_files[metadata_path] = NPU_SDK_METADATA_ARCHIVE_PATH

    return (prod_files, test_files)


def package_fboss(target_name: str, target: list) -> None:
    with concurrent.futures.ProcessPoolExecutor() as executor:
        futures = [executor.submit(write_tar, f"{target_name}.tar", target[0])]
        if len(target) == 2:
            futures.append(
                executor.submit(write_tar, f"{target_name}-tests.tar", target[1])
            )
        for future in futures:
            future.result()


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description=(
            "Package FBOSS forwarding/platform stacks from a getdeps build tree."
        )
    )
    parser.add_argument(
        BUILD_DIR,
        dest="build_dir",
        type=pathlib.Path,
        required=True,
        help=(
            "Root scratch path used by getdeps. "
            "The fboss build tree is expected under SCRATCH_PATH/build/fboss."
        ),
    )
    parser.add_argument(
        "target", choices=list(TARGET_NAMES), help="Packaging target to create."
    )
    return parser.parse_args(argv)


def main(argv=None) -> None:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    target_mappings = _build_target(args.target, args.build_dir)
    # pyrefly: ignore [bad-argument-type]
    package_fboss(args.target, target_mappings)


if __name__ == "__main__":
    main()
