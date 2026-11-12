#!/usr/bin/env python3
# @noautodeps
# Copyright Meta Platforms, Inc. and affiliates.

"""Unit tests for main.py argument parsing + cross-argument validation."""

import os
import sys
import unittest
from argparse import Namespace
from io import StringIO
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import fboss_test_runner.main as main_mod
from fboss_test_runner.errors import TestRunnerUsageError as UsageError
from fboss_test_runner.main import (
    _get_fboss_root,
    _parse_args,
    _runner_action,
    _setup_platform_descriptors,
    main,
)


class ParseArgsValidationTest(unittest.TestCase):
    def test_filter_and_filter_file_are_mutually_exclusive(self):
        with self.assertRaises(ValueError):
            _parse_args(["sai", "--filter", "X", "--filter_file", "f.conf"])

    def test_profile_requires_filter_file(self):
        with self.assertRaises(ValueError):
            _parse_args(["sai", "--profile", "p"])

    def test_filter_only_is_valid(self):
        args = _parse_args(["sai", "--filter", "X"])
        self.assertEqual(args.filter, "X")
        self.assertIsNone(args.filter_file)

    def test_profile_with_filter_file_is_valid(self):
        args = _parse_args(["sai", "--profile", "p", "--filter_file", "f.conf"])
        self.assertEqual(args.profile, "p")
        self.assertEqual(args.filter_file, "f.conf")

    def test_tier_is_parsed(self):
        args = _parse_args(["sai", "--tier", "0"])
        self.assertEqual(args.tier, "0")

    def test_invalid_tier_is_rejected(self):
        with self.assertRaises(SystemExit):
            _parse_args(["sai", "--tier", "3"])

    def test_profile_with_tier_is_valid(self):
        args = _parse_args(["sai", "--tier", "0", "--profile", "s"])
        self.assertEqual(args.tier, "0")
        self.assertEqual(args.profile, "s")

    def test_results_json_is_parsed(self):
        args = _parse_args(["sai", "--results-json", "/tmp/results.json"])
        self.assertEqual(args.results_json, "/tmp/results.json")

    def test_services_start_parses_required_configs(self):
        args = _parse_args(
            [
                "services",
                "start",
                "agent",
                "qsfp",
                "fsdb",
                "--agent-config",
                "/tmp/agent.conf",
                "--qsfp-config",
                "/tmp/qsfp.conf",
                "--num-npus",
                "2",
            ]
        )
        self.assertEqual(args.service_action, "start")
        self.assertEqual(args.services, ["agent", "qsfp", "fsdb"])
        self.assertEqual(args.agent_config, "/tmp/agent.conf")
        self.assertEqual(args.num_npus, 2)
        self.assertEqual(_runner_action(args).__name__, "start")

    def test_services_default_to_all(self):
        args = _parse_args(
            [
                "services",
                "start",
                "--agent-config",
                "/tmp/agent.conf",
                "--qsfp-config",
                "/tmp/qsfp.conf",
            ]
        )
        self.assertEqual(args.services, [])

    def test_services_start_parses_fsdb_without_other_configs(self):
        args = _parse_args(["services", "start", "fsdb"])
        self.assertIsNone(args.agent_config)
        self.assertIsNone(args.qsfp_config)

    def test_services_stop_does_not_require_configs(self):
        args = _parse_args(["services", "stop"])
        self.assertEqual(args.service_action, "stop")
        self.assertEqual(args.num_npus, 1)
        self.assertEqual(args.services, [])
        self.assertEqual(_runner_action(args).__name__, "stop")


class FbossRootTest(unittest.TestCase):
    def test_uses_sourced_package_root(self):
        with patch.dict(os.environ, {"FBOSS": "/tmp/fboss-package"}):
            self.assertEqual(_get_fboss_root(), "/tmp/fboss-package")

    def test_empty_sourced_root_uses_packaged_fallback(self):
        with patch.dict(os.environ, {"FBOSS": ""}):
            self.assertEqual(
                _get_fboss_root(),
                os.path.abspath(
                    os.path.join(os.path.dirname(main_mod.__file__), "..", "..")
                ),
            )


class PlatformDescriptorSetupTest(unittest.TestCase):
    def test_warns_when_fboss_data_is_missing(self):
        with (
            patch.dict(os.environ, {}, clear=True),
            patch("builtins.print") as print_warning,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        print_warning.assert_called_once()
        self.assertIn("FBOSS_DATA", print_warning.call_args.args[0])
        self.assertIs(print_warning.call_args.kwargs["file"], sys.stderr)
        symlink.assert_not_called()

    def test_warns_when_packaged_descriptors_are_missing(self):
        source = "/opt/fboss/share/platform_descriptors"
        with (
            patch.dict(os.environ, {"FBOSS_DATA": "/opt/fboss/share"}),
            patch.object(main_mod.os.path, "isdir", return_value=False),
            patch("builtins.print") as print_warning,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        print_warning.assert_called_once()
        self.assertIn(source, print_warning.call_args.args[0])
        self.assertIs(print_warning.call_args.kwargs["file"], sys.stderr)
        symlink.assert_not_called()

    def test_links_packaged_descriptors(self):
        def isdir(path):
            return path == "/opt/fboss/share/platform_descriptors"

        with (
            patch.dict(os.environ, {"FBOSS_DATA": "/opt/fboss/share"}),
            patch.object(main_mod.os.path, "isdir", side_effect=isdir),
            patch.object(main_mod.os.path, "lexists", return_value=False),
            patch("builtins.print") as print_message,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        print_message.assert_called_once()
        self.assertIn("/tmp/platform_descriptors", print_message.call_args.args[0])
        self.assertIn(
            "/opt/fboss/share/platform_descriptors", print_message.call_args.args[0]
        )
        symlink.assert_called_once_with(
            "/opt/fboss/share/platform_descriptors",
            "/tmp/platform_descriptors",
            target_is_directory=True,
        )

    def test_resolves_relative_fboss_data_before_linking(self):
        relative_data = "relative/share"
        source = os.path.abspath(f"{relative_data}/platform_descriptors")
        with (
            patch.dict(os.environ, {"FBOSS_DATA": relative_data}),
            patch.object(main_mod.os.path, "isdir", return_value=True),
            patch.object(main_mod.os.path, "lexists", return_value=False),
            patch("builtins.print"),
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        symlink.assert_called_once_with(
            source,
            "/tmp/platform_descriptors",
            target_is_directory=True,
        )

    def test_replaces_existing_descriptor_directory(self):
        with (
            patch.dict(os.environ, {"FBOSS_DATA": "/opt/fboss/share"}),
            patch.object(main_mod.os.path, "isdir", return_value=True),
            patch.object(main_mod.os.path, "islink", return_value=False),
            patch.object(main_mod.os.path, "lexists", return_value=True),
            patch.object(main_mod.shutil, "rmtree") as rmtree,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        rmtree.assert_called_once_with("/tmp/platform_descriptors")
        symlink.assert_called_once_with(
            "/opt/fboss/share/platform_descriptors",
            "/tmp/platform_descriptors",
            target_is_directory=True,
        )

    def test_replaces_existing_descriptor_symlink(self):
        def isdir(path):
            return path in {
                "/opt/fboss/share/platform_descriptors",
                "/tmp/platform_descriptors",
            }

        with (
            patch.dict(os.environ, {"FBOSS_DATA": "/opt/fboss/share"}),
            patch.object(main_mod.os.path, "isdir", side_effect=isdir),
            patch.object(main_mod.os.path, "islink", return_value=True),
            patch.object(main_mod.os.path, "lexists", return_value=True),
            patch.object(main_mod.os, "unlink") as unlink,
            patch.object(main_mod.shutil, "rmtree") as rmtree,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        unlink.assert_called_once_with("/tmp/platform_descriptors")
        rmtree.assert_not_called()
        symlink.assert_called_once_with(
            "/opt/fboss/share/platform_descriptors",
            "/tmp/platform_descriptors",
            target_is_directory=True,
        )

    def test_replaces_conflicting_destination(self):
        def isdir(path):
            return path == "/opt/fboss/share/platform_descriptors"

        with (
            patch.dict(os.environ, {"FBOSS_DATA": "/opt/fboss/share"}),
            patch.object(main_mod.os.path, "isdir", side_effect=isdir),
            patch.object(main_mod.os.path, "islink", return_value=False),
            patch.object(main_mod.os.path, "lexists", return_value=True),
            patch.object(main_mod.os, "unlink") as unlink,
            patch.object(main_mod.os, "symlink") as symlink,
        ):
            _setup_platform_descriptors()

        unlink.assert_called_once_with("/tmp/platform_descriptors")
        symlink.assert_called_once_with(
            "/opt/fboss/share/platform_descriptors",
            "/tmp/platform_descriptors",
            target_is_directory=True,
        )


class LogBundleFlagTest(unittest.TestCase):
    def test_log_bundle_defaults_off(self):
        self.assertFalse(_parse_args(["sai"]).log_bundle)

    def test_log_bundle_enabled_when_passed_after_subcommand(self):
        self.assertTrue(_parse_args(["sai", "--log-bundle"]).log_bundle)

    def test_log_bundle_before_subcommand_is_rejected(self):
        # The flag lives on the subparsers, so it must follow the subcommand.
        with self.assertRaises(SystemExit):
            _parse_args(["--log-bundle", "sai"])


class RunnerActionTest(unittest.TestCase):
    def test_dispatches_list_tests(self):
        args = _parse_args(["sai", "--list_tests"])
        self.assertEqual(_runner_action(args).__name__, "list_tests")

    def test_dispatches_run_test(self):
        args = _parse_args(["sai"])
        self.assertEqual(_runner_action(args).__name__, "run_test")

    def test_feature_filter_implies_list_tests(self):
        args = _parse_args(
            ["sai_agent", "--list-tests-for-features", "DLB,ACL_COUNTER"]
        )
        self.assertTrue(args.list_tests)
        self.assertEqual(args.list_tests_for_features, "DLB,ACL_COUNTER")
        self.assertEqual(_runner_action(args).__name__, "list_tests")


class MainLogBundleGatingTest(unittest.TestCase):
    """main() wraps the run in a LogCapture bundle only when --log-bundle is set;
    otherwise it dispatches the runner directly. (LogCapture's own behavior is
    covered in test_log_capture.)"""

    def _run_main(self, *, log_bundle: bool) -> SimpleNamespace:
        dispatched = MagicMock(return_value=0)
        runner = SimpleNamespace(run_test=dispatched)
        with (
            patch.object(sys, "argv", ["run_test.py", "sai"]),
            patch.dict(
                os.environ,
                {"FBOSS_BIN": "/opt/fboss/bin", "FBOSS_LIB": "/opt/fboss/lib"},
            ),
            patch.object(main_mod.os, "chdir"),
            patch.object(main_mod, "LogCapture") as log_capture,
            patch.object(main_mod, "setup_fboss_env"),
            patch.object(main_mod, "_setup_platform_descriptors"),
            patch.object(
                main_mod,
                "_parse_args",
                return_value=Namespace(
                    runner=runner,
                    list_tests=False,
                    log_bundle=log_bundle,
                ),
            ),
        ):
            with self.assertRaisesRegex(SystemExit, "0"):
                main()
        return SimpleNamespace(log_capture=log_capture, dispatched=dispatched)

    def test_no_flag_dispatches_without_capture(self):
        m = self._run_main(log_bundle=False)
        m.log_capture.assert_not_called()
        m.dispatched.assert_called_once()

    def test_flag_wraps_run_in_log_capture(self):
        m = self._run_main(log_bundle=True)
        # main wraps the run in `with LogCapture(test_type):`.
        m.log_capture.assert_called_once_with("sai")
        m.log_capture.return_value.__enter__.assert_called_once()
        m.log_capture.return_value.__exit__.assert_called_once()
        m.dispatched.assert_called_once()

    def test_results_json_uses_runner_exit_code(self):
        dispatched = MagicMock(return_value=1)
        runner = SimpleNamespace(run_test=dispatched)
        with (
            patch.object(sys, "argv", ["run_test.py", "sai"]),
            patch.dict(
                os.environ,
                {"FBOSS_BIN": "/opt/fboss/bin", "FBOSS_LIB": "/opt/fboss/lib"},
            ),
            patch.object(main_mod.os, "chdir"),
            patch.object(main_mod, "setup_fboss_env"),
            patch.object(main_mod, "_setup_platform_descriptors"),
            patch.object(
                main_mod,
                "_parse_args",
                return_value=Namespace(
                    runner=runner,
                    list_tests=False,
                    log_bundle=False,
                    results_json="/tmp/results.json",
                ),
            ),
        ):
            with self.assertRaisesRegex(SystemExit, "1"):
                main()


class MainUsageErrorHandlingTest(unittest.TestCase):
    def test_usage_errors_exit_cleanly_with_and_without_log_bundle(self):
        for log_bundle in (False, True):
            with self.subTest(log_bundle=log_bundle):
                dispatched = MagicMock(
                    side_effect=UsageError("--config is required to run link tests")
                )
                stderr = StringIO()
                with (
                    patch.object(sys, "argv", ["run_test.py", "link"]),
                    patch.dict(
                        os.environ,
                        {
                            "FBOSS_BIN": "/opt/fboss/bin",
                            "FBOSS_LIB": "/opt/fboss/lib",
                        },
                    ),
                    patch.object(main_mod.os, "chdir"),
                    patch.object(main_mod, "LogCapture") as log_capture,
                    patch.object(main_mod, "setup_fboss_env"),
                    patch.object(main_mod, "_setup_platform_descriptors"),
                    patch.object(
                        main_mod,
                        "_parse_args",
                        return_value=Namespace(
                            runner_action=dispatched,
                            log_bundle=log_bundle,
                        ),
                    ),
                    patch.object(sys, "stderr", stderr),
                ):
                    log_capture.return_value.__exit__.return_value = False
                    with self.assertRaises(SystemExit) as raised:
                        main()

                self.assertEqual(raised.exception.code, 2)
                self.assertEqual(
                    stderr.getvalue(),
                    "Error: --config is required to run link tests\n",
                )
                dispatched.assert_called_once()


if __name__ == "__main__":
    unittest.main()
