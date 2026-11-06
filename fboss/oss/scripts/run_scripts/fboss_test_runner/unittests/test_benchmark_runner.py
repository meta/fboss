#!/usr/bin/env python3
# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps

"""Unit tests for the thin BenchmarkTestRunner adapter.

The runner only selects a suite from CLI args and delegates to
BenchmarkFramework. Generic execution is covered in test_benchmark_framework.py;
per-family behavior in test_sai_benchmark_suite.py / test_qsfp_benchmark_suite.py.
"""

from unittest.mock import Mock, patch

import pytest
from fboss_test_runner.errors import TestRunnerUsageError
from fboss_test_runner.frameworks.qsfp_benchmark_suite import QsfpBenchmarkSuite
from fboss_test_runner.frameworks.sai_benchmark_suite import SaiBenchmarkSuite
from fboss_test_runner.main import _build_parser
from fboss_test_runner.runners.benchmark_test_runner import BenchmarkTestRunner


@pytest.fixture
def runner():
    return BenchmarkTestRunner()


@pytest.fixture
def args():
    a = Mock()
    a.sai = True
    a.qsfp = False
    a.agent_run_mode = "mono"
    a.config = None
    a.tier = None
    a.run_on_reference_board = False
    return a


# ---- CLI: --sai / --qsfp are mutually exclusive; SAI is the default -------


def test_parser_accepts_sai():
    ns = _build_parser().parse_args(["benchmark", "--sai"])
    assert ns.sai and not ns.qsfp


def test_parser_accepts_qsfp():
    ns = _build_parser().parse_args(["benchmark", "--qsfp", "--qsfp-config", "/x"])
    assert ns.qsfp and not ns.sai


def test_parser_defaults_to_sai_when_neither_given():
    # Backward compatible: bare `benchmark` runs SAI.
    ns = _build_parser().parse_args(["benchmark"])
    assert not ns.sai and not ns.qsfp
    assert isinstance(BenchmarkTestRunner._select_suite(ns), SaiBenchmarkSuite)


def test_parser_rejects_both_families():
    with pytest.raises(SystemExit):
        _build_parser().parse_args(["benchmark", "--sai", "--qsfp"])


# ---- suite selection ------------------------------------------------------


def test_select_suite_sai(args):
    assert isinstance(BenchmarkTestRunner._select_suite(args), SaiBenchmarkSuite)


def test_select_suite_qsfp(args):
    args.sai = False
    args.qsfp = True
    assert isinstance(BenchmarkTestRunner._select_suite(args), QsfpBenchmarkSuite)


def test_run_test_qsfp_without_config_raises(runner, args):
    args.sai = False
    args.qsfp = True
    args.qsfp_config = None
    with pytest.raises(ValueError, match="--qsfp requires --qsfp-config"):
        runner.run_test(args)


def test_get_test_binary_name_sai(runner, args):
    runner.args = args
    assert runner._get_test_binary_name() == "sai_all_benchmarks-sai_impl"


def test_get_test_binary_name_qsfp(runner, args):
    args.qsfp = True
    runner.args = args
    assert runner._get_test_binary_name() == "qsfp_hw_test_benchmark"


def test_run_test_delegates_to_framework_with_sai_suite(runner, args):
    with patch(
        "fboss_test_runner.runners.benchmark_test_runner.BenchmarkFramework"
    ) as mock_fw:
        exit_code = runner.run_test(args)
    suite = mock_fw.call_args[0][0]
    assert isinstance(suite, SaiBenchmarkSuite)
    mock_fw.return_value.run.assert_called_once_with(args, None)
    mock_fw.return_value.list_tests.assert_not_called()
    assert exit_code == 0


def test_run_test_delegates_to_framework_with_qsfp_suite(runner, args):
    args.sai = False
    args.qsfp = True
    with patch(
        "fboss_test_runner.runners.benchmark_test_runner.BenchmarkFramework"
    ) as mock_fw:
        exit_code = runner.run_test(args)
    suite = mock_fw.call_args[0][0]
    assert isinstance(suite, QsfpBenchmarkSuite)
    mock_fw.return_value.run.assert_called_once_with(args, None)
    mock_fw.return_value.list_tests.assert_not_called()
    assert exit_code == 0


def test_list_tests_delegates_to_framework(runner, args):
    args.list_tests = True
    with patch(
        "fboss_test_runner.runners.benchmark_test_runner.BenchmarkFramework"
    ) as mock_fw:
        exit_code = runner.list_tests(args)
    suite = mock_fw.call_args[0][0]
    assert isinstance(suite, SaiBenchmarkSuite)
    mock_fw.return_value.list_tests.assert_called_once_with(args, None)
    mock_fw.return_value.run.assert_not_called()
    assert exit_code == 0


def test_tier_patterns_are_passed_to_framework(runner, args):
    args.tier = "2"
    with (
        patch.object(runner, "_get_test_patterns_for_tier", return_value=[".*Scale.*"]),
        patch(
            "fboss_test_runner.runners.benchmark_test_runner.BenchmarkFramework"
        ) as mock_fw,
    ):
        runner.list_tests(args)

    mock_fw.return_value.list_tests.assert_called_once_with(args, [".*Scale.*"])


def test_qsfp_benchmarks_reject_tier(runner, args):
    args.qsfp = True
    args.qsfp_config = "/tmp/qsfp.json"
    args.tier = "1"
    with pytest.raises(TestRunnerUsageError, match="not supported for QSFP benchmarks"):
        runner.list_tests(args)
