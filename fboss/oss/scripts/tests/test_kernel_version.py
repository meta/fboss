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
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fboss.oss.scripts import component_version


class KernelVersionTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "kernel-6.11.1-1.fboss.el9.src.rpm"
        self.core = self.root / "kernel-core-6.11.1-1.fboss.el9.x86_64.rpm"
        self.source.write_bytes(b"source rpm")
        self.core.write_bytes(b"core rpm")
        self.config = b'CONFIG_CC_VERSION_TEXT="gcc 12.4.1"\nCONFIG_NET=y\n'
        self.inputs = {
            name: name.encode()
            for name in (
                "kernel.spec",
                "fboss-reference.config",
                "fboss-local-overrides.config",
                "prepare_config.sh",
                "merge_config.py",
            )
        }
        self.inputs["kernel.spec"] = (
            b"Source0: https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-%{version}.tar.xz\n"
        )
        self.image = bytearray(0x300)
        self.image[0x202:0x206] = b"HdrS"
        self.image[0x20E:0x210] = (0x80).to_bytes(2, "little")
        self.build_string = (
            b"6.11.1-1.fboss.el9.x86_64 (builder@host) #1 SMP build time"
        )
        self.image[0x280 : 0x280 + len(self.build_string)] = self.build_string
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        for name in ("prepare_config.sh", "merge_config.py"):
            (self.scripts / name).write_bytes(self.inputs.pop(name))
        self.source_tar = self.tarball()
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(
            component_version, "_read_rpm_member", side_effect=self.read_member
        ).start()
        mock.patch.object(
            component_version, "_query_rpm", side_effect=self.query
        ).start()
        mock.patch.dict(component_version.os.environ, {}, clear=True).start()

    def tarball(self, version: str = "1", extra: str = "") -> bytes:
        output = io.BytesIO()
        makefile = f"VERSION = 6\nPATCHLEVEL = 11\nSUBLEVEL = {version}\nEXTRAVERSION =\n# {extra}\n".encode()
        with tarfile.open(fileobj=output, mode="w:xz") as archive:
            entry = tarfile.TarInfo("linux-6.11.1/Makefile")
            entry.size = len(makefile)
            archive.addfile(entry, io.BytesIO(makefile))
        return output.getvalue()

    def read_member(self, rpm: Path, member: str) -> bytes:
        if rpm == self.source:
            if member == "linux-6.11.1.tar.xz":
                return self.source_tar
            return self.inputs[member]
        self.assertEqual(rpm, self.core)
        if member == "boot/vmlinuz-6.11.1-1.fboss.el9.x86_64":
            return bytes(self.image)
        self.assertEqual(member, "boot/config-6.11.1-1.fboss.el9.x86_64")
        return self.config

    def query(self, rpm: Path, format: str) -> str:
        if format == "%{SOURCERPM}":
            return self.source.name
        if rpm == self.source:
            return "6.11.1"
        if format == "%{VERSION}-%{RELEASE}":
            return "6.11.1-1.fboss.el9"
        return "6.11.1-1.fboss.el9.x86_64"

    def capture(self) -> dict:
        component_version.capture_kernel_build(self.root, self.scripts)
        return json.loads((self.root / "share/kernel-version.json").read_text())

    def test_packaged_source_and_config_evidence_have_independent_checksums(
        self,
    ) -> None:
        first = self.capture()["build"]
        self.source_tar = self.tarball(extra="different source at same version")
        changed_source = self.capture()["build"]
        self.assertNotEqual(first["upstream"], changed_source["upstream"])
        self.assertEqual(first["inputs"], changed_source["inputs"])
        self.inputs["fboss-local-overrides.config"] = b"CONFIG_NET=n"
        changed_input = self.capture()["build"]
        self.assertEqual(changed_source["upstream"], changed_input["upstream"])
        self.assertNotEqual(changed_source["inputs"], changed_input["inputs"])
        self.config = b"CONFIG_NET=n\n"
        changed_config = self.capture()["build"]
        self.assertNotEqual(
            changed_input["config_sha256"], changed_config["config_sha256"]
        )
        self.assertNotIn("compiler", changed_config)

    def test_checkout_revision_is_not_substituted_for_kernel_identity(self) -> None:
        with mock.patch.dict(
            component_version.os.environ,
            {"FBOSS_SOURCE_REVISION": "assembler", "FBOSS_SOURCE_DIRTY": "true"},
        ):
            declaration = self.capture()
        self.assertNotIn("binaries", declaration)
        self.assertEqual(declaration["versions"], {"kernel": "6.11.1-1.fboss.el9"})
        build = declaration["build"]
        self.assertNotIn("fboss", build)
        self.assertNotIn("source_revision", build["upstream"])
        self.assertNotIn("build_version", build)
        self.assertEqual(build["upstream"]["version"], "6.11.1")
        self.assertEqual(build["compiler"], "gcc 12.4.1")
        self.assertEqual(build["patches"], [])

    def test_build_version_is_optional_and_has_no_default(self) -> None:
        with mock.patch.dict(
            component_version.os.environ, {"FBOSS_BUILD_VERSION": "kernel-42"}
        ):
            self.assertEqual(self.capture()["build"]["build_version"], "kernel-42")
        with mock.patch.dict(component_version.os.environ, {"FBOSS_BUILD_VERSION": ""}):
            self.assertNotIn("build_version", self.capture()["build"])

    def test_producer_identity_is_separate_from_upstream_and_captured_before_build(
        self,
    ) -> None:
        revision = "a" * 40
        with mock.patch.dict(
            component_version.os.environ,
            {
                "FBOSS_SOURCE_REVISION": revision,
                "FBOSS_SOURCE_DIRTY": "false",
                "KBUILD_BUILD_TIMESTAMP": "original timestamp",
                "UNRELATED_SECRET": "not a build input",
            },
        ):
            component_version.prepare_kernel_build(self.root, self.scripts)
        with mock.patch.dict(
            component_version.os.environ, {"FBOSS_SOURCE_REVISION": "b" * 40}
        ):
            build = self.capture()["build"]
        self.assertEqual(
            build["fboss"], {"source_revision": revision, "source_dirty": False}
        )
        self.assertNotIn("source_revision", build["upstream"])
        self.assertNotIn("invocation", build)
        self.assertNotIn("environment_overrides", build)

    def test_unavailable_producer_identity_is_absent(self) -> None:
        component_version.write_declaration(
            self.root / component_version.KERNEL_BUILD_INFO_PATH,
            {},
            "build_kernel.sh",
            build={"fboss": {"source_revision": "a" * 40}},
        )
        with mock.patch.object(
            component_version,
            "get_source_build_info",
            return_value={"build_version": "dev"},
        ):
            component_version.prepare_kernel_build(self.root, self.scripts)
        self.assertNotIn("fboss", self.capture()["build"])

    def test_preparation_does_not_replace_metadata_for_existing_artifacts(self) -> None:
        record = self.root / "share/kernel-version.json"
        record.parent.mkdir()
        record.write_text('{"existing": true}\n')
        with mock.patch.dict(
            component_version.os.environ, {"FBOSS_SOURCE_REVISION": "a" * 40}
        ):
            component_version.prepare_kernel_build(self.root, self.scripts)
        self.assertEqual(json.loads(record.read_text()), {"existing": True})
        self.assertEqual(self.capture()["build"]["fboss"]["source_revision"], "a" * 40)
        self.assertFalse(
            (self.root / component_version.KERNEL_BUILD_INFO_PATH).exists()
        )

    def test_sapling_revision_and_unknown_dirty_state(self) -> None:
        (self.root / ".hg").mkdir()
        with mock.patch.object(
            component_version.subprocess,
            "check_output",
            side_effect=[
                "a" * 40,
                component_version.subprocess.CalledProcessError(1, "sl status"),
            ],
        ):
            component_version.prepare_kernel_build(self.root, self.scripts)
        self.assertEqual(
            self.capture()["build"]["fboss"], {"source_revision": "a" * 40}
        )

    def test_download_and_declared_source_urls_are_distinct(self) -> None:
        with mock.patch.dict(
            component_version.os.environ,
            {
                "KERNEL_SOURCE_DOWNLOAD_URL": "https://git.kernel.org/snapshot/linux-6.11.1.tar.gz",
            },
        ):
            archive = self.capture()["build"]["upstream"]["source_archive"]
        self.assertEqual(
            archive["declared_url"],
            "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.11.1.tar.xz",
        )
        self.assertEqual(
            archive["download_url"],
            "https://git.kernel.org/snapshot/linux-6.11.1.tar.gz",
        )
        self.assertNotIn(
            "download_url", self.capture()["build"]["upstream"]["source_archive"]
        )

    def test_snapshot_download_url_survives_cache_reuse(self) -> None:
        script = Path(__file__).with_name("build_kernel.sh")
        if not script.is_file():
            script = (
                Path(__file__).parents[3]
                / "github/fboss-image/kernel/scripts/build_kernel.sh"
            )
        contents = script.read_text()
        download = contents[
            contents.index("KERNEL_TARBALL=") : contents.index(
                "# Ensure FBOSS config sources"
            )
        ]
        tools = self.root / "tools"
        tools.mkdir()
        for name, body in {
            "spectool": "exit 0\n",
            "curl": 'while [ "$1" != "-o" ]; do shift; done\nprintf "snapshot\\n" | gzip > "$2"\n',
        }.items():
            tool = tools / name
            tool.write_text("#!/bin/bash\nset -eu\n" + body)
            tool.chmod(0o755)
        (self.root / "SOURCES").mkdir()
        env = {
            "PATH": str(tools) + os.pathsep + os.defpath,
            "BUILD_DIR": str(self.root),
            "KERNEL_VERSION": "6.11.1",
            "CONTAINER_SPECS_DIR": str(self.root),
            "KERNEL_SOURCE_DOWNLOAD_URL": "unrelated inherited URL",
        }

        def run_download() -> str:
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    "set -euo pipefail\n"
                    + download
                    + '\nprintf "\\nURL=%s\\n" "${KERNEL_SOURCE_DOWNLOAD_URL-}"',
                ],
                cwd=self.root,
                env=env,
                capture_output=True,
                text=True,
                check=True,
            )
            return result.stdout.split("URL=", 1)[1].strip()

        expected = "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/snapshot/linux-6.11.1.tar.gz"
        self.assertEqual(run_download(), expected)
        for tool in tools.iterdir():
            tool.write_text("#!/bin/bash\nexit 99\n")
        self.assertEqual(run_download(), expected)
        archive = self.root / "SOURCES/linux-6.11.1.tar.xz"
        archive.write_bytes(b"replacement source")
        self.assertEqual(run_download(), "")
        archive.with_name(archive.name + ".download-url").unlink()
        self.assertEqual(run_download(), "")

    def test_actual_kernel_build_string_and_config_location_are_read_from_core(
        self,
    ) -> None:
        build = self.capture()["build"]
        self.assertEqual(build["kernel_build_string"], self.build_string.decode())
        self.assertEqual(build["config_file"], "boot/config-6.11.1-1.fboss.el9.x86_64")
        self.assertEqual(build["kernel_release"], "6.11.1-1.fboss.el9.x86_64")
        self.image = bytearray(b"unsupported kernel image")
        self.assertNotIn("kernel_build_string", self.capture()["build"])

    def test_container_and_package_inventories_are_not_captured(self) -> None:
        with mock.patch.dict(
            component_version.os.environ,
            {
                "FBOSS_SOURCE_REVISION": "b" * 40,
                "FBOSS_BUILD_IMAGE_ID": "sha256:" + "a" * 64,
            },
        ):
            component_version.prepare_kernel_build(self.root, self.scripts)
        build = self.capture()["build"]
        self.assertNotIn("starting_image_id", build)
        self.assertNotIn("environment", build)
        self.assertNotIn("rpm_packages", build)
        self.assertNotIn("python_packages", build)

    def test_kernel_subcommand_prepares_then_captures(self) -> None:
        with (
            mock.patch.dict(
                component_version.os.environ, {"FBOSS_SOURCE_REVISION": "a" * 40}
            ),
            mock.patch.object(
                sys,
                "argv",
                [
                    "component_version.py",
                    "kernel",
                    "--prepare",
                    "--output-dir",
                    str(self.root),
                    "--scripts-dir",
                    str(self.scripts),
                ],
            ),
        ):
            component_version.main()
        with mock.patch.object(
            sys,
            "argv",
            [
                "component_version.py",
                "kernel",
                "--output-dir",
                str(self.root),
                "--scripts-dir",
                str(self.scripts),
            ],
        ):
            component_version.main()
        declaration = json.loads((self.root / "share/kernel-version.json").read_text())
        self.assertNotIn("invocation", declaration["build"])
        self.assertEqual(declaration["build"]["fboss"]["source_revision"], "a" * 40)
        self.assertEqual(declaration["versions"], {"kernel": "6.11.1-1.fboss.el9"})

    def test_checksums_are_of_the_actual_rpm_bytes(self) -> None:
        build = self.capture()["build"]
        self.assertEqual(
            build["rpm_sha256"][self.core.name], hashlib.sha256(b"core rpm").hexdigest()
        )
        self.assertEqual(
            build["source_rpm"]["sha256"], hashlib.sha256(b"source rpm").hexdigest()
        )
        self.core.write_bytes(b"different binary")
        self.assertNotEqual(build["rpm_sha256"], self.capture()["build"]["rpm_sha256"])

    def test_upstream_version_mismatch_is_rejected(self) -> None:
        self.source_tar = self.tarball(version="2")
        with self.assertRaisesRegex(ValueError, "source version"):
            self.capture()

    def test_binary_from_another_source_rpm_is_rejected(self) -> None:
        with mock.patch.object(
            component_version, "_query_rpm", return_value="other.src.rpm"
        ):
            with self.assertRaisesRegex(ValueError, "does not name"):
                self.capture()

    def test_missing_or_ambiguous_source_rpm_is_rejected(self) -> None:
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, "Expected one"):
            self.capture()
        self.source.write_bytes(b"source")
        (self.root / "kernel-other.src.rpm").write_bytes(b"other")
        with self.assertRaisesRegex(ValueError, "Expected one"):
            self.capture()

    def test_duplicate_rpm_names_are_rejected(self) -> None:
        directory = self.root / "nested"
        directory.mkdir()
        (directory / self.core.name).write_bytes(b"duplicate")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.capture()


if __name__ == "__main__":
    unittest.main()
