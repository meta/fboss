# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for TestRunner orchestration: _run_test, _run_tests, and related
end-to-end execution paths (gtest output post-processing, synthesize-OK
fallback for early-exit binaries, warmboot loop, timeout handling, config
backup, filter merging), plus the load_from_file helper consumed
by _get_tests_to_run via args.filter_file."""

import json
import os
import subprocess
import tempfile
from argparse import Namespace
from unittest.mock import MagicMock, patch

import pytest
from fboss_test_runner.errors import TestRunnerUsageError
from fboss_test_runner.result_types import GtestResult, GtestStatus, RunOutcome
from fboss_test_runner.runners.utils import load_from_file
from fboss_test_runner.tier_selection import TestCapability, TestTier


class TestLoadFromFile:
    """load_from_file is a module-level helper used by _get_tests_to_run
    (args.filter_file path) and BenchmarkTestRunner. Tests live here next to
    the consumer."""

    def test_basic(self):
        """Test loading entries from a file with comments and blank lines"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".conf") as f:
            f.write("# Comment line\n")
            f.write("entry1\n")
            f.write("\n")
            f.write("entry2\n")
            f.write("# Another comment\n")
            f.write("entry3\n")
            temp_file = f.name
        try:
            result = load_from_file(temp_file)
            assert result == ["entry1", "entry2", "entry3"]
        finally:
            os.unlink(temp_file)

    def test_nonexistent(self):
        """Test loading from a nonexistent file returns empty list"""
        result = load_from_file("/nonexistent/path.conf")
        assert result == []

    def test_empty(self):
        """Test loading from an empty file returns empty list"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".conf") as f:
            temp_file = f.name
        try:
            result = load_from_file(temp_file)
            assert result == []
        finally:
            os.unlink(temp_file)

    def test_with_profile(self):
        """Test loading with profile filters to matching tagged lines"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".conf") as f:
            f.write("entry_untagged\n")
            f.write("entry_tagged_p1 p1\n")
            f.write("entry_tagged_p2 p2\n")
            f.write("entry_tagged_t t\n")
            temp_file = f.name
        try:
            result = load_from_file(temp_file, profile="p1")
            assert result == ["entry_tagged_p1"]
        finally:
            os.unlink(temp_file)

    def test_no_profile_includes_untagged_and_t(self):
        """Test that without profile, untagged and t-tagged lines are included"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".conf") as f:
            f.write("entry_untagged\n")
            f.write("entry_tagged_p1 p1\n")
            f.write("entry_tagged_t t\n")
            temp_file = f.name
        try:
            result = load_from_file(temp_file)
            assert result == ["entry_untagged", "entry_tagged_t"]
        finally:
            os.unlink(temp_file)

    def test_comments_and_whitespace(self):
        """Test that comments and whitespace-only lines are skipped"""
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".conf") as f:
            f.write("# full comment\n")
            f.write("   \n")
            f.write("\n")
            f.write("  # indented comment\n")
            f.write("entry1\n")
            temp_file = f.name
        try:
            result = load_from_file(temp_file)
            assert result == ["entry1"]
        finally:
            os.unlink(temp_file)


class TestTierFilterComposition:
    def test_prepare_tests_applies_tier_filter_after_runner_filter(self, runner):
        args = Namespace(tier="0")
        with (
            patch("shutil.which", return_value="/opt/fboss/bin/test_binary"),
            patch.object(runner, "_initialize_test_lists"),
            patch.object(
                runner, "_get_tests_to_run", return_value=["Suite.B", "Suite.A"]
            ),
            patch.object(
                runner, "_filter_tests", return_value=["Suite.A"]
            ) as runner_filter,
            patch.object(
                runner, "_filter_tests_for_tier", return_value=["Suite.A"]
            ) as tier_filter,
        ):
            assert runner._prepare_tests(args) == ["Suite.A"]

        runner_filter.assert_called_once_with(["Suite.B", "Suite.A"])
        tier_filter.assert_called_once_with(["Suite.A"], TestTier.T0)

    def test_base_runner_rejects_tier_selection(self, runner):
        runner.args = Namespace(command="unsupported")
        with pytest.raises(TestRunnerUsageError, match="not supported for unsupported"):
            runner._filter_tests_for_tier([], TestTier.T0)

    @pytest.mark.parametrize(
        "requested_tier,expected",
        [
            (TestTier.T0, ["Suite.B", "Suite.A"]),
            (TestTier.T1, []),
        ],
    )
    def test_uniform_tier_filters_without_metadata(
        self, runner, requested_tier, expected
    ):
        tests = ["Suite.B", "Suite.A"]
        with (
            patch.object(runner, "_get_uniform_test_tier", return_value=TestTier.T0),
            patch.object(runner, "_get_tier_metadata_file") as metadata_file,
        ):
            assert runner._filter_tests_for_tier(tests, requested_tier) == expected

        metadata_file.assert_not_called()

    def test_metadata_filter_expands_patterns_and_intersects_candidates(
        self, runner, tmp_path
    ):
        metadata = tmp_path / "test_metadata.json"
        metadata.write_text(
            json.dumps(
                {
                    "rules": [
                        {
                            "testNamePattern": "EmptyTest.*",
                            "tier": "T0",
                            "requiredCapabilities": [],
                            "profiles": [],
                        },
                        {
                            "testNamePattern": "CapableTest.*",
                            "tier": "T0",
                            "requiredCapabilities": [TestCapability.TRANSCEIVER.value],
                            "profiles": [],
                        },
                        {
                            "testNamePattern": "OtherTier.*",
                            "tier": "T1",
                            "requiredCapabilities": [],
                            "profiles": [],
                        },
                    ]
                }
            )
        )
        candidates = [
            "Unrelated.Test",
            "CapableTest.Case",
            "EmptyTest.CheckInit",
        ]
        with (
            patch.object(runner, "_get_tier_metadata_file", return_value=str(metadata)),
            patch.object(
                runner,
                "_get_available_test_capabilities",
                return_value=(TestCapability.TRANSCEIVER,),
            ),
            patch.object(
                runner,
                "_list_tests_to_run",
                return_value=["EmptyTest.CheckInit", "CapableTest.Case"],
            ) as expand_patterns,
        ):
            selected = runner._filter_tests_for_tier(candidates, TestTier.T0)

        expand_patterns.assert_called_once_with("EmptyTest.*:CapableTest.*")
        assert selected == ["CapableTest.Case", "EmptyTest.CheckInit"]

    def test_metadata_filter_reports_missing_file(self, runner, tmp_path):
        missing = str(tmp_path / "missing.json")
        with (
            patch.object(runner, "_get_tier_metadata_file", return_value=missing),
            pytest.raises(TestRunnerUsageError, match="Unable to load test metadata"),
        ):
            runner._filter_tests_for_tier([], TestTier.T0)

    def test_metadata_filter_skips_gtest_when_no_patterns_match(self, runner, tmp_path):
        metadata = tmp_path / "test_metadata.json"
        metadata.write_text(
            json.dumps(
                {
                    "rules": [
                        {
                            "testNamePattern": "OtherTier.*",
                            "tier": "T1",
                            "requiredCapabilities": [],
                            "profiles": [],
                        }
                    ]
                }
            )
        )
        with (
            patch.object(runner, "_get_tier_metadata_file", return_value=str(metadata)),
            patch.object(runner, "_list_tests_to_run") as expand_patterns,
        ):
            assert runner._filter_tests_for_tier(["Suite.Test"], TestTier.T0) == []

        expand_patterns.assert_not_called()


class TestRunTestGtestFallback:
    """Tests for _run_test gtest output post-processing (prefix injection,
    synthesize-OK fallback for empty output)."""

    @patch("subprocess.check_output")
    def test_preserves_skipped_does_not_synthesize_ok(
        self, mock_check_output, runner, mock_args
    ):
        """End-to-end: a SKIPPED gtest result must not be rewritten as OK."""
        mock_check_output.return_value = b"[  SKIPPED ] HwFooTest.Bar (5 ms)\n"
        # _get_test_run_cmd reads args off the runner instance (self.args);
        # patch it on the runner for the duration of the call.
        with patch.object(runner, "args", new=mock_args):
            outcome = runner._run_test(
                conf_file="dummy.conf",
                test_prefix="cold_boot.",
                test_to_run="HwFooTest.Bar",
                setup_warmboot=False,
            )
        assert len(outcome.results) == 1
        result = outcome.results[0]
        assert result.status == GtestStatus.SKIPPED
        assert result.test_name == "cold_boot.HwFooTest.Bar"
        assert result.filter_name == "HwFooTest.Bar"
        # Critical: the fallback must NOT have rewritten this to OK.
        assert result.status != GtestStatus.OK

    @patch("subprocess.check_output")
    def test_synthesize_ok_when_no_gtest_line(
        self, mock_check_output, runner, mock_args
    ):
        """Fallback path still works: empty output (e.g. --setup-for-warmboot early
        exit) should still synthesize an OK result so the test isn't lost from
        the summary."""
        mock_check_output.return_value = b""
        with patch.object(runner, "args", new=mock_args, create=True):
            outcome = runner._run_test(
                conf_file="dummy.conf",
                test_prefix="warm_boot.",
                test_to_run="HwFooTest.Bar",
                setup_warmboot=True,
            )
        assert len(outcome.results) == 1
        result = outcome.results[0]
        assert result.status == GtestStatus.OK
        assert result.test_name == "warm_boot.HwFooTest.Bar"
        assert result.filter_name == "HwFooTest.Bar"


class TestRunTestTimeout:
    """Test that subprocess.TimeoutExpired is converted to a [ TIMEOUT ]
    result line so the test surfaces in the summary."""

    @patch("subprocess.check_output")
    def test_timeout_synthesizes_timeout_line(
        self, mock_check_output, runner, mock_args
    ):
        mock_check_output.side_effect = subprocess.TimeoutExpired(
            cmd=["dummy"], timeout=300
        )
        mock_args.test_run_timeout = 300
        with patch.object(runner, "args", new=mock_args, create=True):
            outcome = runner._run_test(
                conf_file="dummy.conf",
                test_prefix="cold_boot.",
                test_to_run="HwSlowTest.Slow",
                setup_warmboot=False,
            )
        assert len(outcome.results) == 1
        result = outcome.results[0]
        # Critical: timeout must produce a TIMEOUT result, NOT be silently
        # dropped or rewritten as OK.
        assert result.status == GtestStatus.TIMEOUT
        assert result.test_name == "cold_boot.HwSlowTest.Slow"
        assert result.filter_name == "HwSlowTest.Slow"
        # Duration reported is timeout_in_second * 1000.
        assert result.duration_ms == 300000


class TestRunTestsWarmboot:
    """Tests for _run_tests cold→warm orchestration."""

    def _make_args(self, mock_args, coldboot_only):
        mock_args.coldboot_only = coldboot_only
        mock_args.sai_replayer_logging = None
        mock_args.simulator = None
        mock_args.sai_logging = "WARN"
        mock_args.fboss_logging = "WARN"
        mock_args.test_run_timeout = 300
        mock_args.num_warmboot_iterations = 1
        return mock_args

    def _make_conf(self, tmp_path):
        # _run_tests early-returns when conf_file does not exist on disk.
        conf = tmp_path / "test.conf"
        conf.write_text("")
        return str(conf)

    def test_warmboot_runs_both_phases_when_check_file_present(
        self, runner, mock_args, tmp_path
    ):
        check_file = tmp_path / "can_warm_boot"
        check_file.write_text("")
        runner._get_warmboot_check_file = lambda: str(check_file)
        args = self._make_args(mock_args, coldboot_only=False)
        conf = self._make_conf(tmp_path)

        with (
            patch.object(
                runner,
                "_run_test",
                return_value=RunOutcome(
                    "[ OK ] x.HwT.t (1 ms)", [GtestResult("x.HwT.t", GtestStatus.OK, 1)]
                ),
            ) as mock_run,
            patch.object(runner, "_setup_coldboot_test"),
            patch.object(runner, "_setup_warmboot_test"),
            patch.object(runner, "_end_run"),
        ):
            outputs = runner._run_tests(["HwT.t"], conf, args)

        # 1 cold + 1 warm = 2 invocations, 2 outputs.
        assert mock_run.call_count == 2
        assert len(outputs) == 2
        prefixes = [call.args[1] for call in mock_run.call_args_list]
        assert prefixes == [runner.COLDBOOT_PREFIX, runner.WARMBOOT_PREFIX]

    def test_coldboot_only_skips_warmboot(self, runner, mock_args, tmp_path):
        # Even when the check file exists, --coldboot_only must skip warmboot.
        check_file = tmp_path / "can_warm_boot"
        check_file.write_text("")
        runner._get_warmboot_check_file = lambda: str(check_file)
        args = self._make_args(mock_args, coldboot_only=True)
        conf = self._make_conf(tmp_path)

        with (
            patch.object(
                runner,
                "_run_test",
                return_value=RunOutcome(
                    "[ OK ] x.HwT.t (1 ms)", [GtestResult("x.HwT.t", GtestStatus.OK, 1)]
                ),
            ) as mock_run,
            patch.object(runner, "_setup_coldboot_test"),
            patch.object(runner, "_setup_warmboot_test") as mock_warm_setup,
            patch.object(runner, "_end_run"),
        ):
            outputs = runner._run_tests(["HwT.t"], conf, args)

        assert mock_run.call_count == 1
        assert len(outputs) == 1
        mock_warm_setup.assert_not_called()

    def test_warmboot_skipped_when_check_file_missing(
        self, runner, mock_args, tmp_path
    ):
        # Regression for the "warmboot file path mismatch" failure mode:
        # if _get_warmboot_check_file points at a path that doesn't exist
        # (e.g. wrong switch_index for the run mode), the warmboot pass is
        # silently skipped — no warmboot result will appear in the summary.
        missing = tmp_path / "does_not_exist"
        runner._get_warmboot_check_file = lambda: str(missing)
        args = self._make_args(mock_args, coldboot_only=False)
        conf = self._make_conf(tmp_path)

        with (
            patch.object(
                runner,
                "_run_test",
                return_value=RunOutcome(
                    "[ OK ] x.HwT.t (1 ms)", [GtestResult("x.HwT.t", GtestStatus.OK, 1)]
                ),
            ) as mock_run,
            patch.object(runner, "_setup_coldboot_test"),
            patch.object(runner, "_setup_warmboot_test") as mock_warm_setup,
            patch.object(runner, "_end_run"),
        ):
            outputs = runner._run_tests(["HwT.t"], conf, args)

        # Warmboot was requested (coldboot_only=False) but check file is
        # missing → only the coldboot pass runs.
        assert mock_run.call_count == 1
        assert len(outputs) == 1
        mock_warm_setup.assert_not_called()


class TestSimulatorEnv:
    """_apply_simulator_env overlays simulator vars onto a per-instance env
    copy, so simulator runs never leak env across runner instances (the env
    used to live on a shared ClassVar)."""

    def test_xgs_overlay(self, runner):
        runner._apply_simulator_env("th4")
        assert runner.env_var["BCM_SIM_PATH"] == "1"
        assert runner.env_var["SOC_TARGET_SERVER"] == "127.0.0.1"
        assert runner.env_var["SOC_TARGET_PORT"] == "22222"

    def test_dnx_overlay(self, runner):
        runner._apply_simulator_env("j3")
        assert runner.env_var["ADAPTER_DEVID_0"] == "8860"
        assert runner.env_var["SOC_TARGET_SERVER"] == "localhost"
        assert runner.env_var["SAI_BOOT_FLAGS"] == "0x1020000"

    def test_no_simulator_leaves_env_unchanged(self, runner):
        before = dict(runner.env_var)
        runner._apply_simulator_env(None)
        assert runner.env_var == before

    def test_env_is_per_instance_no_leak(self, runner):
        # Applying simulator env to one runner must not leak onto a
        # separate instance.
        other = type(runner)()
        assert "SOC_TARGET_PORT" not in other.env_var
        runner._apply_simulator_env("th4")
        assert "SOC_TARGET_PORT" in runner.env_var
        assert "SOC_TARGET_PORT" not in other.env_var


class TestPrepareConfigForRun:
    """Test that _prepare_config_for_run creates a retained config and
    rewrites AUTOLOAD_BOARD_SETTINGS when --run-on-reference-board is set.
    Exercises _string_in_file + _replace_string_in_file transitively."""

    def test_reference_board_creates_modified_copy(self, runner, tmp_path):
        conf = tmp_path / "platform.materialized_JSON"
        conf.write_text("AUTOLOAD_BOARD_SETTINGS: 0\nother: value\n")

        generated_root = tmp_path / "generated_configs"
        modified_path = generated_root / "sai" / conf.name
        args = MagicMock()
        args.command = "sai"
        args.config = str(conf)
        args.run_on_reference_board = True
        with (
            patch.object(runner, "args", new=args, create=True),
            patch(
                "fboss_test_runner.runners.test_runner.GENERATED_CONFIG_ROOT",
                str(generated_root),
            ),
        ):
            returned = runner._prepare_config_for_run()

        assert returned == str(modified_path)
        assert modified_path.is_file()
        new_contents = modified_path.read_text()
        assert "AUTOLOAD_BOARD_SETTINGS: 1" in new_contents
        assert "AUTOLOAD_BOARD_SETTINGS: 0" not in new_contents
        # Original must be untouched.
        assert "AUTOLOAD_BOARD_SETTINGS: 0" in conf.read_text()

    def test_no_reference_board_returns_original_unmodified(self, runner, tmp_path):
        conf = tmp_path / "platform.materialized_JSON"
        conf.write_text("AUTOLOAD_BOARD_SETTINGS: 0\n")
        args = MagicMock()
        args.config = str(conf)
        args.run_on_reference_board = False
        with patch.object(runner, "args", new=args, create=True):
            returned = runner._prepare_config_for_run()
        assert returned == str(conf)
        # The original contents remain unchanged.
        assert "AUTOLOAD_BOARD_SETTINGS: 0" in conf.read_text()


class TestGetTestsToRunFilter:
    def _make_args(self, **overrides):
        args = MagicMock()
        args.filter = None
        args.filter_file = None
        args.profile = None
        for k, v in overrides.items():
            setattr(args, k, v)
        return args

    def test_filter_file_overrides_filter_and_joins_with_colon(self, runner):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False) as f:
            f.write("HwVlan* default\n")
            f.write("HwAcl* default\n")
            filter_file_path = f.name

        try:
            args = self._make_args(
                filter="ignored_when_filter_file_set",
                filter_file=filter_file_path,
                profile="default",
            )
            with (
                patch.object(runner, "args", new=args, create=True),
                patch.object(
                    runner, "_list_tests_to_run", return_value=["HwVlanTest.A"]
                ) as mock_list,
                patch.object(runner, "_is_known_bad_test", return_value=False),
                patch.object(runner, "_is_unsupported_test", return_value=False),
            ):
                runner._get_tests_to_run()

            first_filter = mock_list.call_args_list[0].args[0]
            assert ":" in first_filter
            assert "HwVlan*" in first_filter
            assert "HwAcl*" in first_filter
            assert "ignored_when_filter_file_set" not in first_filter
        finally:
            os.unlink(filter_file_path)

    def test_known_bad_tests_excluded_from_final_filter(self, runner):
        listed = ["HwVlanTest.Delete", "HwRouteTest.Add"]

        def fake_is_known_bad(test):
            return test == "HwVlanTest.Delete"

        args = self._make_args()
        with (
            patch.object(runner, "args", new=args, create=True),
            patch.object(
                runner, "_list_tests_to_run", side_effect=[listed, ["HwRouteTest.Add"]]
            ) as mock_list,
            patch.object(runner, "_is_known_bad_test", side_effect=fake_is_known_bad),
            patch.object(runner, "_is_unsupported_test", return_value=False),
        ):
            result = runner._get_tests_to_run()

        assert mock_list.call_args_list[0].args[0] == "*"
        second_filter = mock_list.call_args_list[1].args[0]
        assert "HwRouteTest.Add" in second_filter
        assert "HwVlanTest.Delete" not in second_filter
        assert result == ["HwRouteTest.Add"]


class TestResolveTestsFile:
    """Tests for the shared TestRunner._resolve_tests_file helper used by every
    runner: prefer a caller-supplied override when it exists, else fall back to
    the default file when it exists, else return "" (no filtering). Per-runner
    tests only assert their default file wires through; the resolution logic is
    exercised here once."""

    _EXISTS = "fboss_test_runner.runners.test_runner.os.path.exists"
    _DEFAULT = "./share/known_bad_tests/default.materialized_JSON"

    def test_override_used_when_present(self, runner):
        with patch(self._EXISTS, return_value=True):
            assert (
                runner._resolve_tests_file(
                    "/tmp/override.json", self._DEFAULT, "known_bad"
                )
                == "/tmp/override.json"
            )

    def test_override_missing_falls_back_to_default(self, runner):
        # Override set but absent -> use the (present) default file.
        with patch(self._EXISTS, side_effect=lambda p: p == self._DEFAULT):
            assert (
                runner._resolve_tests_file(
                    "/tmp/missing.json", self._DEFAULT, "known_bad"
                )
                == self._DEFAULT
            )

    def test_override_and_default_missing_returns_empty(self, runner):
        with patch(self._EXISTS, return_value=False):
            assert (
                runner._resolve_tests_file(
                    "/tmp/missing.json", self._DEFAULT, "known_bad"
                )
                == ""
            )

    def test_default_used_when_present(self, runner):
        with patch(self._EXISTS, return_value=True):
            assert (
                runner._resolve_tests_file(None, self._DEFAULT, "unsupported")
                == self._DEFAULT
            )

    def test_default_missing_returns_empty(self, runner):
        with patch(self._EXISTS, return_value=False):
            assert runner._resolve_tests_file(None, self._DEFAULT, "unsupported") == ""

    def test_no_default_returns_empty(self, runner):
        # A runner with no default file (default_file="") and no override.
        assert runner._resolve_tests_file(None, "", "unsupported") == ""


class TestGetTestRunCmdSwitchId:
    def test_switch_id_for_testing_is_included(self, runner, mock_args):
        mock_args.switch_id_for_testing = 1
        with patch.object(runner, "args", new=mock_args):
            cmd = runner._get_test_run_cmd("dummy.conf", "HwFooTest.Bar", [])
            assert "--switch_id_for_testing=1" in cmd

    def test_switch_id_for_testing_none_is_excluded(self, runner, mock_args):
        mock_args.switch_id_for_testing = None
        with patch.object(runner, "args", new=mock_args):
            cmd = runner._get_test_run_cmd("dummy.conf", "HwFooTest.Bar", [])
            assert not any("switch_id_for_testing" in item for item in cmd)


class TestAgentCrashDowngrade:
    """A unit crash detected after a test must fail that test even though the
    gtest binary itself reported OK (systemd Restart= resurrects a crashed
    production agent inside the test's own readiness wait, so gtest never
    sees it)."""

    _MOD = "fboss_test_runner.runners.test_runner"

    def _make_args(self, mock_args, **overrides):
        # _run_tests probes these with os.path / getattr; a bare Mock breaks them.
        mock_args.sai_replayer_logging = None
        mock_args.simulator = None
        for k, v in overrides.items():
            setattr(mock_args, k, v)
        return mock_args

    def _make_conf(self, tmp_path):
        conf = tmp_path / "test.conf"
        conf.write_text("")
        return str(conf)

    def _ok_outcome(self):
        return RunOutcome(
            "[ OK ] cold_boot.HwT.t (1 ms)",
            [GtestResult("cold_boot.HwT.t", GtestStatus.OK, 1)],
        )

    def _run(self, runner, mock_args, tmp_path, exits):
        args = self._make_args(mock_args, coldboot_only=True)
        conf = self._make_conf(tmp_path)
        with (
            patch.object(runner, "_run_test", return_value=self._ok_outcome()),
            patch.object(runner, "_setup_coldboot_test"),
            patch.object(runner, "_end_run"),
            patch(f"{self._MOD}.find_unclean_unit_exits", return_value=exits) as e,
            patch(f"{self._MOD}.list_core_dumps", return_value=set()) as c,
            patch(f"{self._MOD}.describe_core_dump"),
        ):
            all_results = runner._run_tests(["HwT.t"], conf, args)
        # the journal is read once per test; cores are listed before and after
        e.assert_called_once()
        assert c.call_count == 2
        return all_results

    def test_crash_reason_downgrades_ok_to_failed(self, runner, mock_args, tmp_path):
        all_results = self._run(
            runner,
            mock_args,
            tmp_path,
            exits=["fboss_hw_agent@0.service main process dumped core (status=6/ABRT)"],
        )
        assert [r.status for r in all_results] == [GtestStatus.FAILED]

    def test_no_crash_leaves_result_untouched(self, runner, mock_args, tmp_path):
        all_results = self._run(runner, mock_args, tmp_path, exits=[])
        assert [r.status for r in all_results] == [GtestStatus.OK]


class TestCrashDetection:
    """What `_run_test_guarded` treats as a crash: any unit the test left
    running (production agents for the CLI suite, qsfp_service and the
    platform services for the SAI suites, ...) dying uncleanly inside the
    test window, or any core dump from a process other than the test binary."""

    _MOD = "fboss_test_runner.runners.test_runner"

    _OLD_CORE = "/var/lib/systemd/coredump/core.fboss_sw_agent.0.x.9.9.zst"

    def _reason(self, runner, exits, cores, summaries=None):
        """Run one guarded test and return the crash reason handed to
        _apply_agent_crash, or None if it was not called. `summaries` maps a
        core path to what `describe_core_dump` says about it."""
        outcome = RunOutcome("", [GtestResult("cold_boot.HwT.t", GtestStatus.OK, 1)])
        listings = iter([{self._OLD_CORE}, {self._OLD_CORE, *cores}])
        summaries = summaries or {}
        with (
            patch(f"{self._MOD}.time.time", return_value=1.0),
            patch.object(runner, "_run_test", return_value=outcome),
            patch.object(runner, "_apply_agent_crash") as apply,
            patch(f"{self._MOD}.find_unclean_unit_exits", return_value=exits) as e,
            patch(
                f"{self._MOD}.list_core_dumps", side_effect=lambda: next(listings)
            ) as c,
            patch(
                f"{self._MOD}.describe_core_dump",
                side_effect=lambda core: summaries.get(core, f"core {core}"),
            ),
        ):
            assert (
                runner._run_test_guarded("c", "cold_boot.", "HwT.t", False) is outcome
            )
        # both probes look at the window that opened when the binary started
        e.assert_called_once_with(1.0)
        assert c.call_count == 2
        if not apply.called:
            return None
        apply.assert_called_once()
        assert apply.call_args[0][:3] == (outcome, "cold_boot.", "HwT.t")
        # the core paths ride along for the banner, never in the reason
        assert apply.call_args[0][4] == sorted(
            core for core in cores if "sai_test-sai_im" not in core
        )
        return apply.call_args[0][3]

    def test_unit_crash_is_reported(self, runner):
        reason = self._reason(
            runner,
            ["qsfp_service.service main process dumped core (status=6/ABRT)"],
            [],
        )
        assert reason == "qsfp_service.service main process dumped core (status=6/ABRT)"

    def test_foreign_cores_are_described_not_listed(self, runner):
        """The reason is what result databases index and classify failures
        on, and they cap it at a few hundred characters: a core path would
        crowd out the crash. Each core contributes its stack summary; the
        paths go to the banner."""
        hw = "/var/lib/systemd/coredump/core.fboss_hw_agent-.0.x.1.2.zst"
        qsfp = "/var/lib/systemd/coredump/core.qsfp_service.0.x.3.4.zst"
        reason = self._reason(
            runner,
            [],
            [hw, qsfp],
            {
                hw: "fboss_hw_agent@0.service SIGABRT in SaiStore::check < SaiSwitch::init",
                qsfp: "qsfp_service.service SIGSEGV in TransceiverManager::refresh",
            },
        )
        assert reason == (
            "fboss_hw_agent@0.service SIGABRT in SaiStore::check < SaiSwitch::init; "
            "qsfp_service.service SIGSEGV in TransceiverManager::refresh"
        )

    def test_own_core_is_ignored(self, runner):
        """The test binary dumping core already failed the test on its own;
        naming its core as a unit crash would be noise."""
        with patch.object(
            runner,
            "_get_test_binary_name",
            return_value="/opt/fboss/bin/sai_test-sai_impl",
        ):
            assert (
                self._reason(
                    runner,
                    [],
                    ["/var/lib/systemd/coredump/core.sai_test-sai_im.0.x.5.6.zst"],
                )
                is None
            )

    def test_core_summary_replaces_the_units_exit_line(self, runner):
        """PID 1's "main process dumped core" line and the core summary
        describe the same death; only the summary says why. A unit that
        died without leaving a core keeps its journal line."""
        hw = "/var/lib/systemd/coredump/core.fboss_hw_agent-.0.x.1.2.zst"
        reason = self._reason(
            runner,
            [
                "fboss_hw_agent@0.service main process dumped core (status=11/SEGV)",
                "fboss_sw_agent.service main process exited (status=1/FAILURE)",
            ],
            [hw],
            {hw: "fboss_hw_agent@0.service SIGSEGV in SaiPortManager::addPort"},
        )
        assert reason == (
            "fboss_sw_agent.service main process exited (status=1/FAILURE); "
            "fboss_hw_agent@0.service SIGSEGV in SaiPortManager::addPort"
        )

    def test_healthy_window_is_not_a_crash(self, runner):
        assert self._reason(runner, [], []) is None

    def test_apply_downgrades_ok_and_skipped_only(self, runner):
        outcome = RunOutcome(
            "gtest transcript line 1\ngtest transcript line 2\n",
            [
                GtestResult("cold_boot.HwT.ok", GtestStatus.OK, 1),
                GtestResult("cold_boot.HwT.skipped", GtestStatus.SKIPPED, 1),
                GtestResult("cold_boot.HwT.timeout", GtestStatus.TIMEOUT, 1),
            ],
        )
        runner._apply_agent_crash(
            outcome, "cold_boot.", "HwT.*", "x.service dumped core"
        )
        assert [r.status for r in outcome.results] == [
            GtestStatus.FAILED,
            GtestStatus.FAILED,
            GtestStatus.TIMEOUT,
        ]
        # the binary's transcript is kept (it is the only copy), the verdict
        # is appended after it
        assert outcome.console_output.startswith(
            "gtest transcript line 1\ngtest transcript line 2\n########## CRASH"
        )
        assert "x.service dumped core" in outcome.console_output
        assert outcome.console_output.endswith(
            "\n".join(r.as_log_line() for r in outcome.results)
        )
