#!/usr/bin/env python3
# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps

import json
import os
import pathlib
import tempfile
import unittest
from argparse import Namespace
from typing import cast
from unittest import mock

from fboss_test_runner.runners.test_runner import TestRunner
from npu_sdk_utils import main as npu_sdk_utils_main, materialize_agent_config


class NpuSdkConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary_directory.name)
        self.config_path = self.root / "agent.conf"
        self.metadata_path = self.root / "npu_sdk_metadata.json"
        self.output_directory = self.root / "output"
        self.output_directory.mkdir()

        self.config = {
            "defaultCommandLineArgs": {"foo": "bar"},
            "sw": {
                "sdkVersion": {"asicSdk": "sdk", "saiSdk": "sai"},
                "switchSettings": {"l2LearningMode": 0},
            },
        }
        self.metadata = {
            "sai_test-sai_impl": {
                "builtAt": "2026-09-13T01:23:45Z",
                "npuSaiImpl": "SAI_BRCM_IMPL",
                "npuSaiSdkSelector": "SAI_VERSION_11_7_0_0_ODP",
                "sdkVersion": {
                    "asicSdk": "6.5.30-4",
                    "saiSdk": "11.7.0.0_odp",
                },
            }
        }
        self.config_path.write_text(json.dumps(self.config))
        self.metadata_path.write_text(json.dumps(self.metadata))

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_materializes_sdk_version_without_modifying_source(self) -> None:
        output_path = materialize_agent_config(
            self.config_path,
            self.metadata_path,
            "sai_test-sai_impl",
            self.output_directory,
        )

        output = json.loads(output_path.read_text())
        self.assertEqual(
            {"asicSdk": "6.5.30-4", "saiSdk": "11.7.0.0_odp"},
            output["sw"]["sdkVersion"],
        )
        self.assertEqual(
            self.config["sw"]["switchSettings"], output["sw"]["switchSettings"]
        )
        self.assertEqual(self.config, json.loads(self.config_path.read_text()))

    def test_adds_sdk_version_when_config_omits_it(self) -> None:
        del self.config["sw"]["sdkVersion"]
        self.config_path.write_text(json.dumps(self.config))

        output_path = materialize_agent_config(
            self.config_path,
            self.metadata_path,
            "sai_test-sai_impl",
            self.output_directory,
        )

        output = json.loads(output_path.read_text())
        self.assertEqual(
            {"asicSdk": "6.5.30-4", "saiSdk": "11.7.0.0_odp"},
            output["sw"]["sdkVersion"],
        )

    def test_rejects_missing_binary_metadata(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing-sai-binary"):
            materialize_agent_config(
                self.config_path,
                self.metadata_path,
                "missing-sai-binary",
                self.output_directory,
            )

    def test_rejects_missing_metadata_file(self) -> None:
        self.metadata_path.unlink()

        with self.assertRaisesRegex(FileNotFoundError, "NPU SDK metadata not found"):
            materialize_agent_config(
                self.config_path,
                self.metadata_path,
                "sai_test-sai_impl",
                self.output_directory,
            )

    def test_rejects_incomplete_sdk_version(self) -> None:
        self.metadata["sai_test-sai_impl"]["sdkVersion"] = {"asicSdk": "6.5.30-4"}
        self.metadata_path.write_text(json.dumps(self.metadata))

        with self.assertRaisesRegex(ValueError, "invalid saiSdk"):
            materialize_agent_config(
                self.config_path,
                self.metadata_path,
                "sai_test-sai_impl",
                self.output_directory,
            )

    def test_command_line_materializes_sdk_version(self) -> None:
        npu_sdk_utils_main(
            [
                "materialize-agent-config",
                "--config-path",
                str(self.config_path),
                "--metadata-path",
                str(self.metadata_path),
                "--binary-name",
                "sai_test-sai_impl",
                "--output-directory",
                str(self.output_directory),
            ]
        )

        output = json.loads((self.output_directory / "agent.conf").read_text())
        self.assertEqual(
            {"asicSdk": "6.5.30-4", "saiSdk": "11.7.0.0_odp"},
            output["sw"]["sdkVersion"],
        )


class TestRunnerSdkConfigIntegrationTest(unittest.TestCase):
    def test_run_replaces_and_retains_materialized_config(self) -> None:
        class SdkConfigRunner(TestRunner):
            def __init__(self) -> None:
                super().__init__()
                self.config_seen_during_run: dict[str, object] | None = None
                self.config_path_seen_during_run: str | None = None
                self.assertions: tuple[list[str], str] | None = None

            def _get_test_binary_name(self) -> str:
                return "sai_test-sai_impl"

            def _get_npu_sdk_metadata_binary_name(self) -> str:
                return self._get_test_binary_name()

            def _get_warmboot_check_file(self) -> str:
                return ""

            def _get_test_run_args(self, conf_file: str) -> list[str]:
                return ["--config", conf_file]

            def _prepare_tests(self, args: Namespace) -> list[str]:
                self.args = args
                return ["Suite.Test"]

            def _run_tests(
                self, tests_to_run: list[str], conf_file: str, args: Namespace
            ) -> list:
                self.config_path_seen_during_run = conf_file
                self.config_seen_during_run = json.loads(
                    pathlib.Path(conf_file).read_text()
                )
                self.assertions = (tests_to_run, args.config)
                return []

            def _print_output_summary(self, results, results_json=None) -> None:
                pass

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = pathlib.Path(temporary_directory)
            share = root / "share"
            share.mkdir()
            config_path = root / "agent.conf"
            config_path.write_text(json.dumps({"sw": {}}))
            (share / "npu_sdk_metadata.json").write_text(
                json.dumps(
                    {
                        "sai_test-sai_impl": {
                            "sdkVersion": {
                                "asicSdk": "6.5.30-4",
                                "saiSdk": "11.7.0.0_odp",
                            }
                        }
                    }
                )
            )
            args = Namespace(
                command="sai",
                config=str(config_path),
                results_json=None,
                run_on_reference_board=False,
            )
            runner = SdkConfigRunner()
            generated_root = root / "generated_configs"
            prepared_path = generated_root / "sai" / config_path.name
            prepared_path.parent.mkdir(parents=True)
            prepared_path.write_text("stale config")

            with (
                mock.patch.dict(os.environ, {"FBOSS_DATA": str(share)}),
                mock.patch(
                    "fboss_test_runner.runners.test_runner.GENERATED_CONFIG_ROOT",
                    str(generated_root),
                ),
            ):
                exit_code = runner.run_test(args)

            self.assertEqual(0, exit_code)
            config_seen = cast(dict[str, object], runner.config_seen_during_run)
            sw_config = cast(dict[str, object], config_seen["sw"])
            self.assertEqual(
                {"asicSdk": "6.5.30-4", "saiSdk": "11.7.0.0_odp"},
                sw_config["sdkVersion"],
            )
            self.assertEqual(
                (["Suite.Test"], runner.config_path_seen_during_run),
                runner.assertions,
            )
            self.assertEqual(str(config_path), args.config)
            self.assertEqual(
                str(prepared_path), cast(str, runner.config_path_seen_during_run)
            )
            self.assertTrue(prepared_path.is_file())
