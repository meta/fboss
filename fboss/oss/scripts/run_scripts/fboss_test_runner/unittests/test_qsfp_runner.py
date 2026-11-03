# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for QsfpTestRunner."""

from unittest.mock import MagicMock, patch

import pytest
from fboss_test_runner.runners.qsfp_test_runner import QSFP_SERVICE_DIR, QsfpTestRunner
from fboss_test_runner.tier_selection import TestCapability


@pytest.fixture
def qsfp_runner():
    return QsfpTestRunner()


def _make_args(**overrides):
    args = MagicMock()
    args.qsfp_config = "/tmp/qsfp.conf"
    args.platform_mapping_override_path = None
    args.bsp_platform_mapping_override_path = None
    args.known_bad_tests_file = None
    args.without_transceivers = False
    for k, v in overrides.items():
        setattr(args, k, v)
    return args


class TestTierCapabilities:
    @pytest.mark.parametrize(
        ("without_transceivers", "expected_capabilities"),
        [
            (False, (TestCapability.TRANSCEIVER,)),
            (True, (TestCapability.EXTERNAL_PHY,)),
        ],
    )
    def test_capabilities_follow_cli_flag(
        self,
        qsfp_runner,
        without_transceivers,
        expected_capabilities,
    ):
        with patch.object(
            qsfp_runner,
            "args",
            new=_make_args(without_transceivers=without_transceivers),
        ):
            assert (
                qsfp_runner._get_available_test_capabilities() == expected_capabilities
            )


class TestRunArgsOverrides:
    def test_no_overrides_only_qsfp_config(self, qsfp_runner):
        with patch.object(qsfp_runner, "args", new=_make_args()):
            args_list = qsfp_runner._get_test_run_args("ignored.conf")
        assert args_list == ["--qsfp-config", "/tmp/qsfp.conf"]

    def test_platform_mapping_override_appended_when_set(self, qsfp_runner):
        with patch.object(
            qsfp_runner,
            "args",
            new=_make_args(platform_mapping_override_path="/tmp/pm.json"),
        ):
            args_list = qsfp_runner._get_test_run_args("ignored.conf")
        idx = args_list.index("--platform_mapping_override_path")
        assert args_list[idx + 1] == "/tmp/pm.json"
        assert "--bsp_platform_mapping_override_path" not in args_list

    def test_both_overrides_appended_when_both_set(self, qsfp_runner):
        with patch.object(
            qsfp_runner,
            "args",
            new=_make_args(
                platform_mapping_override_path="/tmp/pm.json",
                bsp_platform_mapping_override_path="/tmp/bsp.json",
            ),
        ):
            args_list = qsfp_runner._get_test_run_args("ignored.conf")
        pm_idx = args_list.index("--platform_mapping_override_path")
        bsp_idx = args_list.index("--bsp_platform_mapping_override_path")
        assert args_list[pm_idx + 1] == "/tmp/pm.json"
        assert args_list[bsp_idx + 1] == "/tmp/bsp.json"


class TestColdbootCleansQsfpServiceDir:
    def test_invokes_rm_rf_on_qsfp_service_dir(self, qsfp_runner):
        with patch("subprocess.Popen") as mock_popen:
            qsfp_runner._setup_coldboot_test()
        mock_popen.assert_called_once()
        cmd = mock_popen.call_args.args[0]
        assert cmd == ["rm", "-rf", QSFP_SERVICE_DIR]
