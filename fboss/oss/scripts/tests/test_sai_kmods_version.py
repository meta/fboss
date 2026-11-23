# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fboss.oss.scripts import component_version


class SaiKmodsVersionTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "sdk"
        self.source.mkdir()
        self.gpl = self.source / "xgs-sdk-6.5.34-gpl-modules"
        self.sdklt = self.source / "hsdk-6.5.34-gpl"
        for directory in (self.gpl, self.sdklt):
            directory.mkdir()
            (directory / "module.c").write_text("int module_version = 1;\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git(
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.com",
            "commit",
            "-qm",
            "vendor source",
        )
        self.revision = self.git("rev-parse", "HEAD").strip()
        self.modules = self.root / "modules"
        self.modules.mkdir()
        self.output = self.root / "output"
        self.output.mkdir()
        self.module = self.modules / "linux-bcm-knet.ko"
        self.module.write_bytes(b"built module")
        self.rpm = self.output / "sai-runtime-14.2.0-1.xgs_6_5_34.x86_64.rpm"
        self.rpm.write_bytes(b"binary rpm")
        self.release = "6.11.1-1.fboss.el9.x86_64"
        self.fboss_revision = "a" * 40
        self.addCleanup(mock.patch.stopall)
        mock.patch.dict(
            component_version.os.environ,
            {
                "FBOSS_SOURCE_REVISION": self.fboss_revision,
                "FBOSS_SOURCE_DIRTY": "false",
            },
            clear=True,
        ).start()

    def git(self, *args: str) -> str:
        return subprocess.check_output(
            ["git", "-C", str(self.source), *args], text=True
        )

    def prepare(self) -> dict:
        component_version.prepare_sai_kmods_build(self.modules, [self.gpl, self.sdklt])
        return json.loads(
            (self.modules / component_version.SAI_KMODS_BUILD_INFO_PATH).read_text()
        )

    def capture(self, version: str = "", kernel_rpms_dir: Path | None = None) -> dict:
        def modinfo(command: list[str], **kwargs: object) -> str:
            return {
                "version": version,
                "vermagic": self.release + " SMP mod_unload modversions",
                "srcversion": "533BB7E5866E52F63B9ACCB",
            }[command[2]]

        with (
            mock.patch.object(
                component_version, "_query_rpm", return_value="14.2.0\nx86_64"
            ),
            mock.patch.object(
                component_version, "_read_rpm_member", return_value=b"built module"
            ),
            mock.patch.object(
                component_version.subprocess, "check_output", side_effect=modinfo
            ),
        ):
            component_version.capture_sai_kmods_build(
                self.output, self.modules, kernel_rpms_dir
            )
        return json.loads(
            (self.output / component_version.SAI_KMODS_DECLARATION_PATH).read_text()
        )

    def test_vendor_checkout_and_fboss_tooling_remain_distinct(self) -> None:
        self.prepare()
        declaration = self.capture()
        self.assertEqual(
            declaration["versions"],
            {"native_sdk": "6.5.34", "sai": "14.2.0", "kernel": "6.11.1-1.fboss.el9"},
        )
        self.assertEqual(
            declaration["build"]["sources"][self.gpl.name],
            {"source_revision": self.revision, "source_dirty": False},
        )
        self.assertEqual(
            declaration["build"]["fboss"]["source_revision"], self.fboss_revision
        )
        self.assertNotIn("binaries", declaration)
        self.assertNotIn("sai_api", declaration["versions"])

    def test_git_file_for_an_sdk_worktree_is_supported(self) -> None:
        worktree = self.root / "worktree"
        self.git("worktree", "add", "--quiet", "--detach", str(worktree))
        component_version.prepare_sai_kmods_build(
            self.modules, [worktree / self.gpl.name]
        )
        declaration = self.capture()
        self.assertEqual(
            declaration["build"]["sources"][self.gpl.name]["source_revision"],
            self.revision,
        )

    def test_identity_is_captured_before_compatibility_edits(self) -> None:
        self.prepare()
        (self.gpl / "module.c").write_text("int module_version = 2;\n")
        self.assertFalse(
            self.capture()["build"]["sources"][self.gpl.name]["source_dirty"]
        )
        self.assertTrue(
            self.prepare()["build"]["sources"][self.gpl.name]["source_dirty"]
        )

    def test_untracked_download_is_not_given_its_bootstrap_revision(self) -> None:
        extracted = self.source / "dnx-sdk-6.5.34-gpl-modules"
        extracted.mkdir()
        (extracted / "module.c").write_text("int downloaded_source;\n")
        with self.assertLogs(component_version.logger, level="WARNING"):
            component_version.prepare_sai_kmods_build(self.modules, [extracted])
        self.assertEqual(self.capture()["build"]["sources"][extracted.name], {})

    def test_unknown_identity_does_not_invent_revision_or_dirty_state(self) -> None:
        with (
            mock.patch.object(
                component_version,
                "get_source_build_info_from_environment",
                return_value={"build_version": "dev"},
            ),
            mock.patch.object(
                component_version.subprocess, "check_output", side_effect=OSError
            ),
            self.assertLogs(component_version.logger, level="WARNING"),
        ):
            context = self.prepare()
        self.assertNotIn("fboss", context["build"])
        self.assertEqual(
            context["build"]["sources"], {self.gpl.name: {}, self.sdklt.name: {}}
        )
        self.assertNotIn("build_version", context["build"])

    def test_packaging_environment_does_not_replace_producer_identity(self) -> None:
        with mock.patch.dict(
            component_version.os.environ, {"FBOSS_BUILD_VERSION": "producer-42"}
        ):
            self.prepare()
        with mock.patch.dict(
            component_version.os.environ,
            {"FBOSS_SOURCE_REVISION": "c" * 40, "FBOSS_BUILD_VERSION": "assembler-99"},
        ):
            build = self.capture()["build"]
        self.assertEqual(build["fboss"]["source_revision"], self.fboss_revision)
        self.assertEqual(build["build_version"], "producer-42")
        self.assertNotIn("environment", build)

    def test_module_version_is_optional_and_checksums_describe_payloads(self) -> None:
        self.prepare()
        declaration = self.capture()
        self.assertNotIn("version", declaration["build"]["modules"][self.module.name])
        self.assertEqual(
            declaration["build"]["modules"][self.module.name]["sha256"],
            hashlib.sha256(b"built module").hexdigest(),
        )
        self.assertEqual(
            declaration["build"]["rpm_sha256"][self.rpm.name],
            hashlib.sha256(b"binary rpm").hexdigest(),
        )
        self.prepare()
        self.assertEqual(
            self.capture(version="vendor-module-1")["build"]["modules"][
                self.module.name
            ]["version"],
            "vendor-module-1",
        )

    def test_mismatched_rpm_module_bytes_are_rejected(self) -> None:
        self.prepare()
        with (
            mock.patch.object(
                component_version, "_query_rpm", return_value="14.2.0\nx86_64"
            ),
            mock.patch.object(
                component_version, "_read_rpm_member", return_value=b"different module"
            ),
        ):
            with self.assertRaisesRegex(ValueError, "payload does not match"):
                component_version.capture_sai_kmods_build(self.output, self.modules)

    def test_kernel_build_inputs_are_distinct_from_output_rpms(self) -> None:
        kernel = self.root / "kernel-inputs"
        kernel.mkdir()
        (kernel / "kernel-devel-x86_64.rpm").write_bytes(b"kernel build headers")
        (kernel / "kernel-headers-x86_64.rpm").write_bytes(b"kernel userspace headers")
        (kernel / "kernel-core-x86_64.rpm").write_bytes(b"kernel runtime")
        self.prepare()
        build = self.capture(kernel_rpms_dir=kernel)["build"]
        self.assertEqual(
            build["kernel_rpm_sha256"],
            {
                "kernel-devel-x86_64.rpm": hashlib.sha256(
                    b"kernel build headers"
                ).hexdigest(),
                "kernel-headers-x86_64.rpm": hashlib.sha256(
                    b"kernel userspace headers"
                ).hexdigest(),
            },
        )
        self.assertEqual(set(build["rpm_sha256"]), {self.rpm.name})
        self.prepare()
        self.assertNotIn("kernel_rpm_sha256", self.capture()["build"])

    def test_modules_for_different_kernels_are_rejected(self) -> None:
        self.prepare()
        other = self.modules / "other.ko"
        other.write_bytes(b"built module")

        def modinfo(command: list[str], **kwargs: object) -> str:
            return (
                ("6.12.1-other" if command[-1] == str(other) else self.release)
                if command[2] == "vermagic"
                else ""
            )

        with (
            mock.patch.object(
                component_version, "_query_rpm", return_value="14.2.0\nx86_64"
            ),
            mock.patch.object(
                component_version, "_read_rpm_member", return_value=b"built module"
            ),
            mock.patch.object(
                component_version.subprocess, "check_output", side_effect=modinfo
            ),
        ):
            with self.assertRaisesRegex(ValueError, "one target kernel"):
                component_version.capture_sai_kmods_build(self.output, self.modules)

    def test_missing_context_and_missing_modules_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "Missing prepared"):
            component_version.capture_sai_kmods_build(self.output, self.modules)
        self.prepare()
        self.module.unlink()
        with self.assertRaisesRegex(ValueError, "one target kernel"):
            self.capture()

    def test_source_versions_must_agree(self) -> None:
        other = self.source / "hsdk-6.5.32-gpl"
        other.mkdir()
        with self.assertLogs(component_version.logger, level="WARNING"):
            with self.assertRaisesRegex(ValueError, "disagree on SDK"):
                component_version.prepare_sai_kmods_build(
                    self.modules, [self.gpl, other]
                )

    def test_source_rpm_is_not_used_as_binary_evidence(self) -> None:
        (self.output / "sai-runtime-14.2.0-1.xgs_6_5_34.src.rpm").write_bytes(
            b"source rpm"
        )
        self.prepare()
        self.assertEqual(set(self.capture()["build"]["rpm_sha256"]), {self.rpm.name})

    def test_subcommand_uses_existing_schema_and_consumes_prepared_identity(
        self,
    ) -> None:
        with mock.patch.object(
            sys,
            "argv",
            [
                "component_version.py",
                "sai-kmods",
                "--prepare",
                "--output-dir",
                str(self.modules),
                "--source-dir",
                str(self.gpl),
            ],
        ):
            component_version.main()
        declaration = self.capture()
        self.assertEqual(declaration["declaration_schema"], component_version.SCHEMA)
        self.assertEqual(declaration["producer"], "component_version.py")
        self.assertFalse(
            (self.modules / component_version.SAI_KMODS_BUILD_INFO_PATH).exists()
        )

    def test_modinfo_invocation_is_bounded(self) -> None:
        self.prepare()
        calls: list[dict[str, object]] = []

        def modinfo(command: list[str], **kwargs: object) -> str:
            calls.append(kwargs)
            return {
                "version": "",
                "vermagic": self.release + " SMP mod_unload modversions",
                "srcversion": "533BB7E5866E52F63B9ACCB",
            }[command[2]]

        with (
            mock.patch.object(
                component_version, "_query_rpm", return_value="14.2.0\nx86_64"
            ),
            mock.patch.object(
                component_version, "_read_rpm_member", return_value=b"built module"
            ),
            mock.patch.object(
                component_version.subprocess, "check_output", side_effect=modinfo
            ),
        ):
            component_version.capture_sai_kmods_build(self.output, self.modules)
        self.assertTrue(calls)
        for kwargs in calls:
            self.assertIn("timeout", kwargs)


if __name__ == "__main__":
    unittest.main()
