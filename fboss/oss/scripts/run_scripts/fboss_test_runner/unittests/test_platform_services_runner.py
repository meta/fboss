# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for PlatformServicesTestRunner.

The runner has two unique behaviors worth testing: (1) per-test-type binary
selection via a dict map with a fallback default, and (2) the multi-type
iteration in run_test that iterates TEST_TYPE_CHOICES when --type is omitted."""

import json
import os
from argparse import Namespace
from unittest.mock import patch

import pytest
from fboss_test_runner.result_types import GtestResult, GtestStatus, TestExecutionResult
from fboss_test_runner.runners.platform_services_test_runner import (
    PlatformServicesTestRunner,
)
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.tier_selection import TestTier


@pytest.fixture
def platform_runner():
    return PlatformServicesTestRunner()


def _make_args(**overrides):
    values = {
        "type": None,
        "config": None,
        "list_tests": False,
        "tier": None,
        "skip_known_bad_tests": None,
        "sai_logging": "WARN",
        "fboss_logging": "WARN",
        "test_run_timeout": 300,
        "run_on_reference_board": False,
        "results_json": None,
        **overrides,
    }
    return Namespace(**values)


class TestBinaryNameByType:
    """The binary name comes from a dict map keyed on args.type. Unknown
    types fall back to platform_hw_test."""

    @pytest.mark.parametrize(
        "test_type,expected",
        [
            ("platform_hw_test", "platform_hw_test"),
            ("data_corral_service_hw_test", "data_corral_service_hw_test"),
            ("fan_service_hw_test", "fan_service_hw_test"),
            ("fw_util_hw_test", "fw_util_hw_test"),
            ("sensor_service_hw_test", "sensor_service_hw_test"),
            ("weutil_hw_test", "weutil_hw_test"),
            ("platform_manager_hw_test", "platform_manager_hw_test"),
        ],
    )
    def test_known_types_map_to_their_binary(
        self, platform_runner, test_type, expected
    ):
        with patch.object(platform_runner, "args", new=_make_args(type=test_type)):
            assert platform_runner._get_test_binary_name() == expected

    def test_unknown_type_falls_back_to_platform_hw_test(self, platform_runner):
        with patch.object(
            platform_runner, "args", new=_make_args(type="not_a_real_type")
        ):
            assert platform_runner._get_test_binary_name() == "platform_hw_test"


def test_all_platform_tests_are_t0(platform_runner):
    assert platform_runner._get_uniform_test_tier() is TestTier.T0


class TestRunTestMultiTypeIteration:
    """When --type is omitted, run_test iterates over all 7 TEST_TYPE_CHOICES,
    using a per-type argument copy for each pass. This is the unique runner-level
    behavior (vs the base TestRunner's single-type loop)."""

    def test_iterates_all_test_types_when_type_none(self, platform_runner):
        seen_types = []
        # Spy on _get_tests_to_run to capture the type set at each iteration.
        runner_args = _make_args(list_tests=False)

        def capture_type():
            seen_types.append(platform_runner.args.type)
            return ["FakeTest.A"]

        with (
            patch.object(platform_runner, "args", new=runner_args),
            patch("shutil.which", return_value="/opt/fboss/bin/platform_hw_test"),
            patch.object(platform_runner, "_initialize_test_lists"),
            patch.object(
                platform_runner, "_get_tests_to_run", side_effect=capture_type
            ),
            patch.object(platform_runner, "_filter_tests", side_effect=lambda t: t),
            patch.object(
                platform_runner,
                "_prepare_config_for_run",
                side_effect=lambda: None,
            ),
            patch.object(platform_runner, "_run_tests", return_value=[]),
            patch.object(platform_runner, "_print_output_summary"),
        ):
            platform_runner.run_test(runner_args)

        # All 7 platform test types are iterated, in the order of TEST_TYPE_CHOICES.
        assert seen_types == list(PlatformServicesTestRunner.TEST_TYPE_CHOICES)
        assert runner_args.type is None

    def test_merges_results_and_returns_failure(self, platform_runner, tmp_path):
        output = tmp_path / "results.json"
        runner_args = _make_args(results_json=str(output))

        def run_one(_runner, args):
            status = (
                GtestStatus.FAILED
                if args.type == "fan_service_hw_test"
                else GtestStatus.OK
            )
            return TestExecutionResult(
                exit_code=int(status is GtestStatus.FAILED),
                results=[GtestResult(args.type, status, 1)],
            )

        with (
            patch.object(
                TestRunner, "_execute_test", autospec=True, side_effect=run_one
            ),
            patch.object(platform_runner, "_print_output_summary"),
        ):
            exit_code = platform_runner.run_test(runner_args)

        assert exit_code == 1
        assert runner_args.results_json == str(output)
        assert [
            record["test_name"] for record in json.loads(output.read_text())
        ] == list(PlatformServicesTestRunner.TEST_TYPE_CHOICES)

    @pytest.mark.parametrize("setup_first", [False, True])
    def test_setup_failure_takes_precedence_over_test_failure(
        self, platform_runner, setup_first
    ):
        runner_args = _make_args()
        test_failure = TestExecutionResult(
            exit_code=1,
            results=[GtestResult("PlatformTest.Failed", GtestStatus.FAILED, 1)],
        )
        setup_failure = TestExecutionResult(
            exit_code=os.EX_TEMPFAIL,
            setup_failure="service failed to start",
        )
        executions = (
            [setup_failure, test_failure]
            if setup_first
            else [test_failure, setup_failure]
        )

        with (
            patch.object(platform_runner, "TEST_TYPE_CHOICES", ["first", "second"]),
            patch.object(platform_runner, "_run_test_type", side_effect=executions),
            patch.object(platform_runner, "_print_output_summary"),
        ):
            exit_code = platform_runner.run_test(runner_args)

        assert exit_code == os.EX_TEMPFAIL

    def test_reports_completed_results_before_setup_error(self, platform_runner):
        runner_args = _make_args()
        completed_result = GtestResult("PlatformTest.Passed", GtestStatus.OK, 1)
        setup_error = RuntimeError("service failed to start")

        with (
            patch.object(
                platform_runner,
                "_run_test_type",
                side_effect=[
                    TestExecutionResult(exit_code=0, results=[completed_result]),
                    TestExecutionResult(
                        exit_code=os.EX_TEMPFAIL,
                        setup_failure=str(setup_error),
                        error=setup_error,
                    ),
                ],
            ) as run_test_type,
            patch.object(platform_runner, "_print_output_summary") as print_summary,
            pytest.raises(RuntimeError, match="service failed to start"),
        ):
            platform_runner.run_test(runner_args)

        assert run_test_type.call_count == 2
        print_summary.assert_called_once_with([completed_result])

    def test_reports_completed_results_before_execution_error(
        self, platform_runner, tmp_path
    ):
        output = tmp_path / "results.json"
        runner_args = _make_args(results_json=str(output))
        completed_result = GtestResult("PlatformTest.Passed", GtestStatus.OK, 1)

        with (
            patch.object(
                platform_runner,
                "_run_test_type",
                side_effect=[
                    TestExecutionResult(exit_code=0, results=[completed_result]),
                    RuntimeError("test runner crashed"),
                ],
            ) as run_test_type,
            patch.object(platform_runner, "_print_output_summary") as print_summary,
            pytest.raises(RuntimeError, match="test runner crashed"),
        ):
            platform_runner.run_test(runner_args)

        assert run_test_type.call_count == 2
        print_summary.assert_called_once_with([completed_result])
        assert json.loads(output.read_text()) == [
            {
                "test_name": "PlatformTest.Passed",
                "status": "PASSED",
                "duration_ms": 1,
            }
        ]


class TestListTestsMultiTypeIteration:
    def test_lists_each_test_type_without_running_tests(self, platform_runner):
        runner_args = _make_args(list_tests=True)

        with (
            patch.object(TestRunner, "list_tests", return_value=0) as list_tests,
            patch.object(TestRunner, "run_test") as run_test,
        ):
            assert platform_runner.list_tests(runner_args) == 0

        assert [call.args[0].type for call in list_tests.call_args_list] == list(
            PlatformServicesTestRunner.TEST_TYPE_CHOICES
        )
        run_test.assert_not_called()
