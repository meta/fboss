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

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fboss.oss.scripts import component_version


class ComponentVersionTest(unittest.TestCase):
    def test_missing_embedded_section_reports_binary(self) -> None:
        with mock.patch.object(component_version.subprocess, "run"):
            with self.assertRaisesRegex(
                ValueError, r"Missing \.fboss\.build section in /build/fsdb"
            ):
                component_version.read_embedded_build_info(Path("/build/fsdb"))

    def test_generate_accepts_build_version_and_overrides_environment(self) -> None:
        with (
            tempfile.TemporaryDirectory() as temporary,
            mock.patch.dict(
                component_version.os.environ, {"FBOSS_BUILD_VERSION": "env-version"}
            ),
            mock.patch.object(
                sys,
                "argv",
                [
                    "component_version.py",
                    "generate",
                    "--source-dir",
                    "/source",
                    "--source-revision",
                    "a" * 40,
                    "--build-version",
                    "release-42",
                    "--output-dir",
                    temporary,
                    "--target",
                    "fsdb",
                ],
            ),
        ):
            component_version.main()
            source = (Path(temporary) / "fsdb.cpp").read_text()
            self.assertIn("release-42", source)
            self.assertNotIn("env-version", source)

    def test_empty_build_version_defaults_to_dev(self) -> None:
        identity = component_version.get_source_build_info(
            Path("/source"), "a" * 40, build_version=""
        )
        self.assertEqual(
            identity, {"source_revision": "a" * 40, "build_version": "dev"}
        )

    def test_service_and_kernel_use_same_producer_environment(self) -> None:
        for dirty in ("true", "false", ""):
            with (
                self.subTest(dirty=dirty),
                tempfile.TemporaryDirectory() as temporary,
                mock.patch.dict(
                    component_version.os.environ,
                    {
                        "FBOSS_SOURCE_REVISION": "a" * 40,
                        "FBOSS_SOURCE_DIRTY": dirty,
                    },
                    clear=True,
                ),
                mock.patch.object(
                    component_version.subprocess, "check_output"
                ) as query,
                mock.patch.object(
                    component_version, "generate_build_info_sources"
                ) as generate,
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        "component_version.py",
                        "generate",
                        "--source-dir",
                        "/source",
                        "--output-dir",
                        temporary,
                        "--target",
                        "fsdb",
                    ],
                ),
            ):
                component_version.main()
                component_version.prepare_kernel_build(
                    Path(temporary),
                    Path("/source/kernel/scripts"),
                )
                declaration = json.loads(
                    (
                        Path(temporary) / component_version.KERNEL_BUILD_INFO_PATH
                    ).read_text()
                )
                expected: dict[str, object] = {"source_revision": "a" * 40}
                if dirty:
                    expected["source_dirty"] = dirty == "true"
                self.assertEqual(declaration["build"]["fboss"], expected)
                self.assertEqual(
                    generate.call_args.args[2], {**expected, "build_version": "dev"}
                )
                query.assert_not_called()

    def test_explicit_source_options_override_environment(self) -> None:
        with mock.patch.dict(
            component_version.os.environ,
            {"FBOSS_SOURCE_REVISION": "a" * 40, "FBOSS_SOURCE_DIRTY": "true"},
            clear=True,
        ):
            self.assertEqual(
                component_version.get_source_build_info_from_environment(
                    Path("/source"), "b" * 40, "false", "release-42"
                ),
                {
                    "source_revision": "b" * 40,
                    "source_dirty": False,
                    "build_version": "release-42",
                },
            )

    def test_invalid_dirty_environment_is_rejected_by_service_and_kernel(self) -> None:
        with (
            tempfile.TemporaryDirectory() as temporary,
            mock.patch.dict(
                component_version.os.environ, {"FBOSS_SOURCE_DIRTY": "invalid"}
            ),
            mock.patch.object(
                sys,
                "argv",
                [
                    "component_version.py",
                    "generate",
                    "--source-dir",
                    "/source",
                    "--output-dir",
                    temporary,
                    "--target",
                    "fsdb",
                ],
            ),
            mock.patch.object(sys, "stderr", new_callable=io.StringIO) as error,
        ):
            with self.assertRaises(SystemExit) as exit_error:
                component_version.main()
            self.assertEqual(exit_error.exception.code, 2)
            self.assertIn("FBOSS_SOURCE_DIRTY must be true or false", error.getvalue())
            with self.assertRaisesRegex(
                ValueError, "FBOSS_SOURCE_DIRTY must be true or false"
            ):
                component_version.prepare_kernel_build(Path(temporary), Path("/source"))

    def test_explicit_revision_does_not_use_packaging_checkout(self) -> None:
        with mock.patch.object(component_version.subprocess, "check_output") as query:
            identity = component_version.get_source_build_info(
                Path("/source"), "a" * 40
            )
        query.assert_not_called()
        self.assertEqual(
            identity, {"source_revision": "a" * 40, "build_version": "dev"}
        )

    def test_unknown_revision_is_not_reported_as_clean(self) -> None:
        with mock.patch.object(
            component_version.subprocess, "check_output", side_effect=OSError
        ):
            self.assertEqual(
                component_version.get_source_build_info(Path("/source")),
                {"build_version": "dev"},
            )

    def test_git_revision_and_scoped_dirty_state(self) -> None:
        with mock.patch.object(
            component_version.subprocess,
            "check_output",
            side_effect=["b" * 40 + "\n", " M fboss/agent/Main.cpp\n"],
        ) as query:
            identity = component_version.get_source_build_info(Path("/source"))
        self.assertEqual(
            identity,
            {"source_revision": "b" * 40, "source_dirty": True, "build_version": "dev"},
        )
        self.assertEqual(query.call_args.args[0][-2:], ["--", "."])

    def test_sapling_identity_uses_shared_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".hg").mkdir()
            scripts = root / "kernel/scripts"
            scripts.mkdir(parents=True)
            with (
                mock.patch.object(
                    component_version.subprocess,
                    "check_output",
                    side_effect=["b" * 40, "M kernel/config\n"],
                ) as query,
                mock.patch.dict(component_version.os.environ, {}, clear=True),
            ):
                env = component_version.get_source_build_environment(scripts)
            self.assertEqual(
                env, {"FBOSS_SOURCE_REVISION": "b" * 40, "FBOSS_SOURCE_DIRTY": "true"}
            )
            self.assertEqual(query.call_args_list[0].args[0][:2], ["sl", "log"])
            self.assertEqual(query.call_args_list[0].kwargs["cwd"], root)

    def test_empty_git_directory_does_not_hide_sapling_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".hg").mkdir()
            scripts = root / "fbcode/fboss/oss/scripts"
            scripts.mkdir(parents=True)
            (root / "fbcode/.git").mkdir()
            with mock.patch.object(
                component_version.subprocess,
                "check_output",
                side_effect=["b" * 40, ""],
            ) as query:
                identity = component_version.get_source_build_info(scripts)
            self.assertEqual(identity["source_revision"], "b" * 40)
            self.assertFalse(identity["source_dirty"])
            self.assertEqual(query.call_args_list[0].args[0][0], "sl")
            self.assertEqual(query.call_args_list[0].kwargs["cwd"], root)

    def test_environment_command_uses_explicit_producer_overrides(self) -> None:
        for dirty in (None, "true", "false"):
            expected = {
                "FBOSS_SOURCE_REVISION": "a" * 40,
                "FBOSS_BUILD_VERSION": "v0.1",
            }
            if dirty is not None:
                expected["FBOSS_SOURCE_DIRTY"] = dirty
            with (
                self.subTest(dirty=dirty),
                mock.patch.dict(component_version.os.environ, expected, clear=True),
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        "component_version.py",
                        "environment",
                        "--source-dir",
                        "/producer",
                    ],
                ),
                mock.patch.object(
                    component_version.subprocess, "check_output"
                ) as query,
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output,
            ):
                component_version.main()
                self.assertEqual(json.loads(output.getvalue()), expected)
                query.assert_not_called()

    def test_unknown_dirty_state_and_revision_stay_absent_in_environment(self) -> None:
        for results, expected in (
            (["b" * 40, OSError()], {"FBOSS_SOURCE_REVISION": "b" * 40}),
            ([OSError()], {}),
            (["unknown"], {}),
        ):
            with (
                self.subTest(results=results),
                mock.patch.dict(component_version.os.environ, {}, clear=True),
                mock.patch.object(
                    component_version.subprocess, "check_output", side_effect=results
                ),
            ):
                self.assertEqual(
                    component_version.get_source_build_environment(Path("/producer")),
                    expected,
                )

    def test_incremental_generation_preserves_unchanged_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            component_version.generate_build_info_sources(
                output, ["fsdb", "qsfp_service"], {"source_revision": "a" * 40}
            )
            fsdb = output / "fsdb.cpp"
            first = fsdb.read_text()
            timestamp = fsdb.stat().st_mtime_ns
            component_version.generate_build_info_sources(
                output, ["fsdb"], {"source_revision": "a" * 40}
            )
            self.assertEqual(fsdb.stat().st_mtime_ns, timestamp)
            component_version.generate_build_info_sources(
                output, ["qsfp_service"], {"source_revision": "b" * 40}
            )
            self.assertEqual(fsdb.read_text(), first)
            self.assertIn("b" * 40, (output / "qsfp_service.cpp").read_text())

    def test_rejects_invalid_source_and_target(self) -> None:
        with self.assertRaises(ValueError):
            component_version.get_source_build_info(Path("/source"), "oss-ci-12345")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                component_version.generate_build_info_sources(
                    Path(temporary), ["../fsdb"], {}
                )

    def test_packaging_detects_changed_binary_and_supports_legacy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "fsdb"
            binary.write_bytes(b"original executable")
            record = root / component_version.RECORD_DIRECTORY / "fsdb.json"
            component_version.write_if_changed(
                record,
                json.dumps(
                    {
                        "schema": 1,
                        "build_target": "fsdb",
                        "source_revision": "a" * 40,
                        "sha256": component_version.checksum(binary),
                    }
                ),
            )
            records = component_version.collect_binary_records({binary: "bin/fsdb"})
            self.assertEqual(records["fsdb"]["source_revision"], "a" * 40)
            binary.write_bytes(b"replaced executable")
            with self.assertRaisesRegex(ValueError, "Stale"):
                component_version.collect_binary_records({binary: "bin/fsdb"})
            record.unlink()
            self.assertEqual(
                component_version.collect_binary_records({binary: "bin/fsdb"}), {}
            )

    def test_declaration_preserves_multiple_service_revisions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "declaration.json"
            binaries = {
                "fsdb": {"source_revision": "a" * 40},
                "qsfp_service": {"source_revision": "b" * 40},
            }
            component_version.write_declaration(output, binaries, "package.py")
            self.assertEqual(json.loads(output.read_text())["binaries"], binaries)

    def test_component_build_uses_common_declaration_without_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "declaration.json"
            build = {"upstream": {"version": "6.11.1"}}
            component_version.write_declaration(
                output,
                {},
                "build_kernel.sh",
                build=build,
                versions={"kernel": "6.11.1-1.fboss.el9"},
            )
            self.assertEqual(
                json.loads(output.read_text()),
                {
                    "declaration_schema": 1,
                    "producer": "build_kernel.sh",
                    "build": build,
                    "versions": {"kernel": "6.11.1-1.fboss.el9"},
                },
            )

    def test_service_declaration_shape_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "declaration.json"
            binaries = {"fsdb": {"source_revision": "a" * 40}}
            component_version.write_declaration(output, binaries, "package.py")
            self.assertEqual(
                json.loads(output.read_text()),
                {
                    "declaration_schema": 1,
                    "producer": "package.py",
                    "binaries": binaries,
                },
            )
            absent = output.with_name("absent.json")
            component_version.write_declaration(absent, {}, "package.py")
            self.assertFalse(absent.exists())


if __name__ == "__main__":
    unittest.main()
