#!/usr/bin/env python3
# Copyright 2004-present Facebook. All Rights Reserved.

import argparse
import glob
import os
import pathlib
import re
import shutil
import stat
import subprocess
import tempfile
from typing import ClassVar, TYPE_CHECKING

if TYPE_CHECKING or __package__:
    from . import component_version
    from .platform_configs_utils import (
        copy_forwarding_stack_configs,
        copy_platform_descriptors,
    )
else:
    import component_version
    from platform_configs_utils import (
        copy_forwarding_stack_configs,
        copy_platform_descriptors,
    )

OPT_ARG_SCRATCH_PATH = "--scratch-path"
OPT_ARG_COPY_ROOT_LIBS = "--copy-root-libs"


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        OPT_ARG_SCRATCH_PATH,
        type=str,
        required=True,
        help=(
            "use this path for build and install files e.g. "
            + OPT_ARG_SCRATCH_PATH
            + "=/opt/app"
        ),
    )
    parser.add_argument(
        "--compress", help="Compress the FBOSS Binaries", action="store_true"
    )
    parser.add_argument(
        OPT_ARG_COPY_ROOT_LIBS,
        help="Copy libs from /lib and /lib64. Default: False",
        action="store_true",
    )
    return parser.parse_args()


class PackageFboss:
    FBOSS_BINS = "fboss_bins-1"
    FBOSS_BIN_TAR = "fboss_bins.tar.zst"
    FBOSS = "fboss"

    BIN = "bin"
    LIB = "lib"
    DATA = "share"
    NPU_SDK_METADATA_FILENAME = "npu_sdk_metadata.json"

    # Suffix for getdeps output location
    INSTALLED = "installed"
    BUILD = "build"

    GETDEPS = "fboss/oss/scripts/run-getdeps.py"

    DEVTOOLS_LIBRARY_PATH = "/opt/rh/devtoolset-8/root/usr/lib64"

    # Project names and binaries we want packaged.
    # If unspecified, all binaries for a given project will be copied over.
    NAME_TO_BINARIES: ClassVar[dict] = {
        FBOSS: [],
    }

    def __init__(self):
        self.scratch_path = args.scratch_path
        self.git_path = os.path.join(
            self.scratch_path, "repos/github.com-facebook-fboss.git"
        )
        self.tmp_dir_name = tempfile.mkdtemp(
            prefix=PackageFboss.FBOSS_BINS, dir=args.scratch_path
        )
        os.makedirs(os.path.join(self.tmp_dir_name, PackageFboss.BIN))
        os.makedirs(os.path.join(self.tmp_dir_name, PackageFboss.LIB))
        os.makedirs(os.path.join(self.tmp_dir_name, PackageFboss.DATA))
        self.copy_root_libs = args.copy_root_libs
        self.dependencies = set()

    def _get_dir_for(self, name, location):
        # TODO: Getdeps' show-inst-dir has issues. This needs to be
        # investigated and fixed. Until then, use a workaround using
        # regex matching for the directory name.
        if self.scratch_path is not None:
            scratch_path = self.scratch_path
            scratch_path_installed_dir = os.path.join(scratch_path, location, name)
            return glob.glob(scratch_path_installed_dir + "*")

        get_install_dir_cmd = [PackageFboss.GETDEPS, "show-inst-dir", name]
        if location == PackageFboss.BUILD:
            get_install_dir_cmd = [PackageFboss.GETDEPS, "show-build-dir", name]
        return (
            subprocess.check_output(get_install_dir_cmd)
            .decode("utf-8")
            .strip()
            .split("\n")[-1]
        )

    def get_fboss_subdirectory(self, path_suffix: str) -> str:
        candidate = os.path.join(self.git_path, path_suffix)
        # Check if this directory exists. Under certain conditions, this
        # directory might be missing, such as cases where the build was done
        # from a local checkout of the code.
        if not os.path.isdir(candidate):
            # If the run_scripts_path does not exist, attempt to locate the run
            # scripts via a relative path based on location of package-fboss.py
            # fboss/oss/scripts/package-fboss.py four parents up should take us
            # to the root of the repository
            fboss_root_path = pathlib.Path(
                __file__
            ).parent.parent.parent.parent.resolve()
            candidate = os.path.join(fboss_root_path, path_suffix)
        if os.path.isdir(candidate):
            return candidate
        raise RuntimeError(f"Could not find directory for {path_suffix}")

    def _copy_run_scripts(self, tmp_dir_name):
        run_scripts_path = self.get_fboss_subdirectory("fboss/oss/scripts/run_scripts")

        # Test-only and cache dirs are never imported at runtime and must be
        # skipped at this top level: the ignore_patterns below only filters
        # entries *within* a copied directory, not the root passed to copytree.
        skip_dirs = {"unittests", "tests", "__pycache__"}
        src_files = os.listdir(run_scripts_path)
        script_pkg_path = os.path.join(tmp_dir_name, PackageFboss.BIN)
        for file_name in src_files:
            if file_name in skip_dirs:
                continue
            full_file_name = os.path.join(run_scripts_path, file_name)
            # run_test.py imports the fboss_test_runner package, so directories
            # (the package and its subpackages) must be copied too, not skipped.
            if os.path.isdir(full_file_name):
                shutil.copytree(
                    full_file_name,
                    os.path.join(script_pkg_path, file_name),
                    ignore=shutil.ignore_patterns("__pycache__", "unittests", "tests"),
                    dirs_exist_ok=True,
                )
                print(f"Copied directory {full_file_name} to {script_pkg_path}")
            else:
                shutil.copy(full_file_name, script_pkg_path)
                print(f"Copied {full_file_name} to {script_pkg_path}")

        npu_sdk_utils_path = os.path.join(
            os.path.dirname(run_scripts_path), "npu_sdk_utils.py"
        )
        shutil.copy(npu_sdk_utils_path, script_pkg_path)
        print(f"Copied {npu_sdk_utils_path} to {script_pkg_path}")

    def _copy_run_configs(self, tmp_dir_name):
        run_configs_path = self.get_fboss_subdirectory("fboss/oss/scripts/run_configs")

        src_files = os.listdir(run_configs_path)
        for file_name in src_files:
            full_file_name = os.path.join(run_configs_path, file_name)
            config_path = os.path.join(tmp_dir_name, PackageFboss.DATA)
            full_config_name = os.path.join(config_path, file_name)
            print(f"Copying {full_file_name} to {full_config_name}")
            shutil.copytree(full_file_name, full_config_name)

    def _copy_configs(self, tmp_dir_name):
        hw_test_configs_path = self.get_fboss_subdirectory("fboss/oss/hw_test_configs")
        qsfp_test_configs_path = self.get_fboss_subdirectory(
            "fboss/oss/qsfp_test_configs"
        )
        link_test_configs_path = self.get_fboss_subdirectory(
            "fboss/oss/link_test_configs"
        )
        hw_sanity_test_configs_path = self.get_fboss_subdirectory(
            "fboss/oss/hw_sanity_tests"
        )
        hw_benchmark_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/hw_benchmark_tests"
        )
        print(f"Copying {hw_test_configs_path} to {tmp_dir_name}")
        shutil.copytree(
            hw_test_configs_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "hw_test_configs"),
        )
        print(f"Copying {qsfp_test_configs_path} to {tmp_dir_name}")
        shutil.copytree(
            qsfp_test_configs_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "qsfp_test_configs"),
        )
        print(f"Copying {link_test_configs_path} to {tmp_dir_name}")
        shutil.copytree(
            link_test_configs_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "link_test_configs"),
        )
        print(f"Copying {hw_sanity_test_configs_path} to {tmp_dir_name}")
        shutil.copytree(
            hw_sanity_test_configs_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "hw_sanity_tests"),
        )
        print(f"Copying {hw_benchmark_tests_path} to {tmp_dir_name}")
        shutil.copytree(
            hw_benchmark_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "hw_benchmark_tests"),
        )

    def _copy_completion_scripts(self, tmp_dir_name):
        completion_src = os.path.join(
            self.get_fboss_subdirectory("fboss/oss/scripts"),
            "fboss2_completion.bash",
        )
        # get_fboss_subdirectory prefers the getdeps source clone, which in a
        # default (non --local) build is pinned to a stable commit that may
        # predate this file. The completion script ships alongside this script
        # in the same commit, so fall back to the copy next to package-fboss.py.
        if not os.path.isfile(completion_src):
            completion_src = os.path.join(
                os.path.dirname(os.path.abspath(__file__)),
                "fboss2_completion.bash",
            )
        # The completion script is a non-critical convenience artifact; never
        # fail packaging if it is absent from an older checkout.
        if not os.path.isfile(completion_src):
            print(f"Skipping fboss2 bash completion; {completion_src} not found")
            return
        completion_dst_dir = os.path.join(
            tmp_dir_name, PackageFboss.DATA, "bash-completion", "completions"
        )
        os.makedirs(completion_dst_dir, exist_ok=True)
        # bash-completion loads the file named after the command.
        completion_dst = os.path.join(completion_dst_dir, "fboss2")
        print(f"Copying {completion_src} to {completion_dst}")
        shutil.copy(completion_src, completion_dst)

    def _copy_production_features(self, tmp_dir_name):
        production_features_path = self.get_fboss_subdirectory(
            "fboss/oss/production_features"
        )
        print(f"Copying {production_features_path} to {tmp_dir_name}")
        shutil.copytree(
            production_features_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "production_features"),
        )

    def _copy_known_bad_tests(self, tmp_dir_name):
        hw_known_bad_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/hw_known_bad_tests"
        )
        qsfp_known_bad_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/qsfp_known_bad_tests"
        )
        link_known_bad_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/link_known_bad_tests"
        )
        fboss2_integration_known_bad_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/fboss2_integration_known_bad_tests"
        )
        print(f"Copying {hw_known_bad_tests_path} to {tmp_dir_name}")
        print(f"Copying {qsfp_known_bad_tests_path} to {tmp_dir_name}")
        print(f"Copying {link_known_bad_tests_path} to {tmp_dir_name}")
        print(f"Copying {fboss2_integration_known_bad_tests_path} to {tmp_dir_name}")
        shutil.copytree(
            hw_known_bad_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "hw_known_bad_tests"),
        )
        shutil.copytree(
            qsfp_known_bad_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "qsfp_known_bad_tests"),
        )
        shutil.copytree(
            link_known_bad_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "link_known_bad_tests"),
        )
        shutil.copytree(
            fboss2_integration_known_bad_tests_path,
            os.path.join(
                tmp_dir_name,
                PackageFboss.DATA,
                "fboss2_integration_known_bad_tests",
            ),
        )

    def _copy_unsupported_tests(self, tmp_dir_name):
        sai_hw_unsupported_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/sai_hw_unsupported_tests"
        )
        print(f"Copying {sai_hw_unsupported_tests_path} to {tmp_dir_name}")
        shutil.copytree(
            sai_hw_unsupported_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "sai_hw_unsupported_tests"),
        )

        qsfp_unsupported_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/qsfp_unsupported_tests"
        )
        print(f"Copying {qsfp_unsupported_tests_path} to {tmp_dir_name}")
        shutil.copytree(
            qsfp_unsupported_tests_path,
            os.path.join(tmp_dir_name, PackageFboss.DATA, "qsfp_unsupported_tests"),
        )

        fboss2_integration_unsupported_tests_path = self.get_fboss_subdirectory(
            "fboss/oss/fboss2_integration_unsupported_tests"
        )
        print(f"Copying {fboss2_integration_unsupported_tests_path} to {tmp_dir_name}")
        shutil.copytree(
            fboss2_integration_unsupported_tests_path,
            os.path.join(
                tmp_dir_name,
                PackageFboss.DATA,
                "fboss2_integration_unsupported_tests",
            ),
        )

    def _copy_binaries(self, tmp_dir_name):
        print("Copying binaries...")

        self._update_ld_library_path()
        build_records = {}

        for name, binaries in PackageFboss.NAME_TO_BINARIES.items():
            bin_pkg_path = os.path.join(tmp_dir_name, PackageFboss.BIN)
            lib_pkg_path = os.path.join(tmp_dir_name, PackageFboss.LIB)

            # find the project directory: {scratch}/build/{name}
            project_dirs = self._get_dir_for(name, PackageFboss.BUILD)
            for project_dir in project_dirs:
                effective_binaries = binaries if binaries else os.listdir(project_dir)
                copied_binaries = {}

                for binary in effective_binaries:
                    if (
                        name == PackageFboss.FBOSS
                        and binary == PackageFboss.NPU_SDK_METADATA_FILENAME
                    ):
                        continue
                    bin_abs_path = os.path.join(project_dir, binary)
                    if self._is_python_dir_executable(bin_abs_path):
                        dst = os.path.join(bin_pkg_path, binary)
                        shutil.copytree(
                            bin_abs_path, dst, symlinks=True, dirs_exist_ok=True
                        )
                        print(f"Copied directory executable {bin_abs_path} to {dst}")
                        continue

                    try:
                        shutil.copy(bin_abs_path, bin_pkg_path)
                        print(f"Copied {bin_abs_path} to {bin_pkg_path}")
                    except OSError:
                        print(f"Skipping binary {bin_abs_path}")
                        continue

                    build_records.pop(binary, None)
                    copied_binaries[pathlib.Path(bin_pkg_path) / binary] = (
                        f"bin/{binary}"
                    )

                    # retrieve dependencies using ldd
                    dependencies = self._get_dependency_paths(bin_abs_path)
                    dependencies |= self._sdk_sibling_libs(dependencies)
                    for lib_abs_path in dependencies:
                        try:
                            shutil.copy(lib_abs_path, lib_pkg_path)
                            print(f"Copied {lib_abs_path} to {lib_pkg_path}")
                        except OSError:
                            print(f"Skipping library {lib_abs_path}")

                if name == PackageFboss.FBOSS:
                    self._copy_npu_sdk_metadata(project_dir, tmp_dir_name)
                    build_records.update(
                        component_version.collect_binary_records(
                            copied_binaries,
                            pathlib.Path(project_dir)
                            / component_version.RECORD_DIRECTORY,
                        )
                    )

        component_version.write_declaration(
            pathlib.Path(tmp_dir_name) / component_version.DECLARATION_PATH,
            build_records,
            "package-fboss.py",
        )

        self._copy_run_scripts(tmp_dir_name)
        self._copy_run_configs(tmp_dir_name)
        self._copy_configs(tmp_dir_name)
        copy_platform_descriptors(
            pathlib.Path(self.get_fboss_subdirectory("fboss/configs/platforms")),
            pathlib.Path(tmp_dir_name),
        )
        copy_forwarding_stack_configs(
            pathlib.Path(self.get_fboss_subdirectory("fboss/configs/platforms")),
            pathlib.Path(tmp_dir_name),
        )
        self._copy_known_bad_tests(tmp_dir_name)
        self._copy_unsupported_tests(tmp_dir_name)
        self._copy_production_features(tmp_dir_name)
        self._copy_completion_scripts(tmp_dir_name)

    def _copy_npu_sdk_metadata(self, project_dir: str, tmp_dir_name: str) -> None:
        metadata_path = os.path.join(
            project_dir, PackageFboss.NPU_SDK_METADATA_FILENAME
        )
        if not os.path.isfile(metadata_path):
            return
        if stat.S_IMODE(os.stat(metadata_path).st_mode) != 0o444:
            raise RuntimeError(f"NPU SDK metadata must be read-only: {metadata_path}")

        destination = os.path.join(
            tmp_dir_name,
            PackageFboss.DATA,
            PackageFboss.NPU_SDK_METADATA_FILENAME,
        )
        shutil.copy2(metadata_path, destination)
        print(f"Copied {metadata_path} to {destination}")

    @staticmethod
    def _is_python_dir_executable(path: str) -> bool:
        return os.path.isdir(path) and os.path.isfile(os.path.join(path, "__main__.py"))

    def _update_ld_library_path(self) -> None:
        find_cmd = [
            "find",
            f"{self.scratch_path}/{PackageFboss.INSTALLED}",
            "-type",
            "d",
            "-regex",
            ".*/\\(lib\\|lib64\\)",
        ]
        try:
            output = subprocess.check_output(find_cmd).decode("utf-8").strip()
            lib_paths = ":".join(output.splitlines())
            if os.environ.get("LD_LIBRARY_PATH") is not None:
                os.environ["LD_LIBRARY_PATH"] = (
                    f"{lib_paths}:{os.environ['LD_LIBRARY_PATH']}"
                )
            else:
                os.environ["LD_LIBRARY_PATH"] = lib_paths
        except subprocess.CalledProcessError:
            print("Unable to update LD_LIBRARY_PATH, libs may be missing!")

    def _sdk_sibling_libs(self, dependencies: set[str]) -> set[str]:
        """
        Shared libraries shipped beside a dynamically linked libsai_impl.so, plus
        their own dependencies. An NPU SDK may dlopen these (for example
        per-device libraries), so ldd on the binaries reports neither.
        """
        siblings = set()
        for lib in dependencies:
            if os.path.basename(lib) != "libsai_impl.so":
                continue
            sdk_dir = os.path.dirname(lib)
            for name in os.listdir(sdk_dir):
                path = os.path.join(sdk_dir, name)
                # libfoo.so or libfoo.so.1.2, not libfoo.so.debug or *.so.bak.
                if (
                    re.fullmatch(r".+\.so(\.\d+)*", name)
                    and os.path.isfile(path)
                    and path not in self.dependencies
                ):
                    siblings.add(path)
        self.dependencies |= siblings
        for sibling in list(siblings):
            siblings |= self._get_dependency_paths(sibling)
        return siblings

    def _get_dependency_paths(self, path_to_binary: str) -> set[str]:
        """
        Get full paths to dependencies identified by ldd for a given binary.
        """

        dependencies = set()
        # All of glibc comes from the host: its libraries share GLIBC_PRIVATE
        # symbols, so bundling some beside the host's libc mixes versions.
        exclude_patterns = [
            "libc.so",
            "libm.so",
            "libresolv.so",
            "libdl.so",
            "libpthread.so",
            "librt.so",
            "libutil.so",
            "libnss_",
            "ld-linux",
            "libgcc_s.so",
            "libsystemd.so",
        ]

        try:
            output = subprocess.check_output(["file", path_to_binary])
            if b"executable" not in output and b"shared object" not in output:
                return dependencies

            output = subprocess.check_output(["ldd", path_to_binary]).decode("utf-8")
        except subprocess.CalledProcessError:
            print(f"Could not run ldd on path {path_to_binary}")
            return dependencies

        for line in output.splitlines():
            if not line.strip():
                continue

            # ldd output can take multiple forms:
            # /lib64/ld-linux-x86-64.so.2 (0x00007f4c1e7d2000)
            # libudev.so.1 => /lib64/libudev.so.1 (0x00007f4c1bc48000)
            parts = line.split("=>")
            if len(parts) > 1:
                lib_path = parts[1].split("(")[0].strip()
            else:
                lib_path = parts[0].split("(")[0].strip()

            if any(s in lib_path for s in exclude_patterns):
                continue

            not_present = lib_path not in self.dependencies
            root_and_copy = lib_path.startswith("/lib") and self.copy_root_libs
            not_root = not lib_path.startswith("/lib")
            if not_present and (root_and_copy or not_root):
                self.dependencies.add(lib_path)
                dependencies.add(lib_path)

        return dependencies

    def _compress_binaries(self):
        print("Compressing FBOSS Binaries...")
        tar_path = os.path.join(args.scratch_path, PackageFboss.FBOSS_BIN_TAR)
        subprocess.run(
            ["tar", "-cvf", tar_path, "--zstd", "-C", self.tmp_dir_name, "."],
            check=False,
        )
        print(f"Compressed to {tar_path}")

    def run(self, args):
        self._copy_binaries(self.tmp_dir_name)
        if args.compress:
            self._compress_binaries()


if __name__ == "__main__":
    args = parse_args()
    PackageFboss().run(args)
