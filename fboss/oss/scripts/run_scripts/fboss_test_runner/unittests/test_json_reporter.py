# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps

import json
import os

import pytest
from fboss_test_runner.reporters.json_reporter import JsonReporter
from fboss_test_runner.result_types import compute_exit_code, GtestResult, GtestStatus


@pytest.fixture
def isolated_cwd(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    return tmp_path


def test_writes_filterable_per_case_records(isolated_cwd):
    results = [
        GtestResult(
            "cold_boot.HwFooTest.Bar",
            GtestStatus.OK,
            1200,
            filter_name="HwFooTest.Bar",
        ),
        GtestResult(
            "warm_boot.HwFooTest.Bar",
            GtestStatus.FAILED,
            3400,
            filter_name="HwFooTest.Bar",
        ),
        GtestResult(
            "warm_boot.2.HwFooTest.Bar",
            GtestStatus.SKIPPED,
            100,
            filter_name="HwFooTest.Bar",
        ),
        GtestResult("HwBazTest.Qux", GtestStatus.TIMEOUT, 5000),
    ]

    output = isolated_cwd / "results.json"
    JsonReporter().write_gtest_results(results, str(output))

    records = json.loads(output.read_text())
    assert records == [
        {
            "test_name": "HwFooTest.Bar",
            "status": "PASSED",
            "duration_ms": 1200,
        },
        {
            "test_name": "HwFooTest.Bar",
            "status": "FAILED",
            "duration_ms": 3400,
        },
        {
            "test_name": "HwFooTest.Bar",
            "status": "SKIPPED",
            "duration_ms": 100,
        },
        {
            "test_name": "HwBazTest.Qux",
            "status": "TIMEOUT",
            "duration_ms": 5000,
        },
    ]


def test_summary_writes_results_to_requested_path(runner, tmp_path):
    output = tmp_path / "netcastle-results.json"

    runner._print_output_summary(
        [
            GtestResult(
                "cold_boot.HwFooTest.Bar",
                GtestStatus.OK,
                1200,
                filter_name="HwFooTest.Bar",
            )
        ],
        str(output),
    )

    assert json.loads(output.read_text()) == [
        {
            "test_name": "HwFooTest.Bar",
            "status": "PASSED",
            "duration_ms": 1200,
        }
    ]


def test_compute_exit_code():
    assert compute_exit_code([GtestResult("HwFooTest.Bar", GtestStatus.OK, 1)]) == 0
    assert (
        compute_exit_code([GtestResult("HwFooTest.Bar", GtestStatus.SKIPPED, 1)]) == 0
    )
    assert compute_exit_code([GtestResult("HwFooTest.Bar", GtestStatus.FAILED, 1)]) == 1
    assert (
        compute_exit_code([GtestResult("HwFooTest.Bar", GtestStatus.TIMEOUT, 1)]) == 1
    )


def test_run_level_failure_writes_setup_failure(
    runner, mock_args, tmp_path, monkeypatch
):
    output = tmp_path / "netcastle-results.json"
    mock_args.results_json = str(output)
    monkeypatch.setattr("shutil.which", lambda _binary: "/opt/fboss/bin/test_binary")
    monkeypatch.setattr(runner, "_initialize_test_lists", lambda _args: None)
    monkeypatch.setattr(runner, "_get_tests_to_run", lambda: ["HwFooTest.Bar"])
    monkeypatch.setattr(runner, "_filter_tests", lambda tests: tests)

    def fail_setup():
        raise RuntimeError("service failed to start")

    monkeypatch.setattr(runner, "_prepare_config_for_run", fail_setup)

    assert runner.run_test(mock_args) == os.EX_TEMPFAIL
    assert json.loads(output.read_text()) == [
        {"run_status": "SETUP_FAILED", "message": "service failed to start"}
    ]


def test_execution_failure_is_not_reported_as_setup_failure(
    runner, mock_args, tmp_path, monkeypatch
):
    output = tmp_path / "netcastle-results.json"
    mock_args.results_json = str(output)
    monkeypatch.setattr("shutil.which", lambda _binary: "/opt/fboss/bin/test_binary")
    monkeypatch.setattr(runner, "_initialize_test_lists", lambda _args: None)
    monkeypatch.setattr(runner, "_get_tests_to_run", lambda: ["HwFooTest.Bar"])
    monkeypatch.setattr(runner, "_filter_tests", lambda tests: tests)
    monkeypatch.setattr(runner, "_prepare_config_for_run", lambda: None)

    def fail_execution(_tests, _config, _args):
        raise RuntimeError("test runner crashed")

    monkeypatch.setattr(runner, "_run_tests", fail_execution)

    with pytest.raises(RuntimeError, match="test runner crashed"):
        runner.run_test(mock_args)
    assert not output.exists()


def test_missing_binary_is_a_temporary_failure(runner, mock_args, monkeypatch):
    monkeypatch.setattr("shutil.which", lambda _binary: None)

    assert runner.run_test(mock_args) == os.EX_TEMPFAIL
