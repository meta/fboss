# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for LinkTestRunner.

Covers the mono vs multi_switch branching for binary selection and warmboot
file path, plus the service start ordering in _setup_coldboot_test which
must launch FSDB before QSFP before HW Agent."""

from unittest.mock import MagicMock, patch

import pytest
from fboss_test_runner.errors import TestRunnerUsageError as UsageError
from fboss_test_runner.runners.link_test_runner import LinkTestRunner
from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.tier_selection import TestCapability


@pytest.fixture
def link_runner():
    return LinkTestRunner()


def _make_args(**overrides):
    args = MagicMock()
    args.agent_run_mode = "multi_switch"
    args.num_npus = 1
    args.platform_mapping_override_path = None
    args.bsp_platform_mapping_override_path = None
    args.disable_fsdb = False
    args.fsdb_config = None
    args.qsfp_config = None
    args.config = None
    args.mgmt_if = "eth0"
    args.known_bad_tests_file = None
    args.list_tests = False
    args.tier = None
    args.list_tests_for_features = None
    args.without_transceivers = False
    args.sai_replayer_logging = None
    args.sai_replayer_sdk_log_level = None
    args.simulator = None
    args.coldboot_only = True
    args.num_warmboot_iterations = 1
    args.run_on_reference_board = False
    for k, v in overrides.items():
        setattr(args, k, v)
    return args


class TestTierCapabilities:
    @pytest.mark.parametrize(
        ("without_transceivers", "expected_capabilities"),
        [
            (False, (TestCapability.TRANSCEIVER,)),
            (True, ()),
        ],
    )
    def test_transceiver_capability_follows_cli_flag(
        self,
        link_runner,
        without_transceivers,
        expected_capabilities,
    ):
        with patch.object(
            link_runner,
            "args",
            new=_make_args(without_transceivers=without_transceivers),
        ):
            assert (
                link_runner._get_available_test_capabilities() == expected_capabilities
            )


class TestBinaryNameByMode:
    def test_mono_uses_mono_link_test_binary(self, link_runner):
        with patch.object(link_runner, "args", new=_make_args(agent_run_mode="mono")):
            assert link_runner._get_test_binary_name().endswith(
                "mono_link_test-sai_impl"
            )

    def test_multi_switch_uses_multi_link_test_binary(self, link_runner):
        with patch.object(
            link_runner, "args", new=_make_args(agent_run_mode="multi_switch")
        ):
            assert link_runner._get_test_binary_name().endswith(
                "multi_link_test-sai_impl"
            )


class TestWarmbootCheckFileByMode:
    """mono uses _0 suffix, multi_switch uses no suffix.
    Wrong path silently skips warmboot."""

    def test_mono_uses_switch_index_zero(self, link_runner):
        with patch.object(link_runner, "args", new=_make_args(agent_run_mode="mono")):
            assert link_runner._get_warmboot_check_file().endswith("_0")

    def test_multi_switch_uses_no_switch_index(self, link_runner):
        with patch.object(
            link_runner, "args", new=_make_args(agent_run_mode="multi_switch")
        ):
            assert not link_runner._get_warmboot_check_file().endswith("_0")


class TestRequiredConfigs:
    @pytest.mark.parametrize(
        ("agent_run_mode", "config_override", "qsfp_config_override", "error"),
        [
            ("multi_switch", None, "valid", "--config is required"),
            (
                "multi_switch",
                "missing",
                "valid",
                "--config path does not exist",
            ),
            ("multi_switch", "valid", None, "--qsfp-config is required"),
            (
                "multi_switch",
                "valid",
                "missing",
                "--qsfp-config path does not exist",
            ),
            ("mono", "missing", "valid", "--config path does not exist"),
            ("mono", None, None, "--qsfp-config is required"),
        ],
    )
    def test_run_rejects_invalid_configs_before_test_discovery(
        self,
        link_runner,
        tmp_path,
        agent_run_mode,
        config_override,
        qsfp_config_override,
        error,
    ):
        config = tmp_path / "agent.conf"
        config.write_text("")
        qsfp_config = tmp_path / "qsfp.conf"
        qsfp_config.write_text("")

        if config_override is None:
            config_path = None
        elif config_override == "missing":
            config_path = str(tmp_path / "missing-agent.conf")
        else:
            config_path = str(config)
        if qsfp_config_override is None:
            qsfp_config_path = None
        elif qsfp_config_override == "missing":
            qsfp_config_path = str(tmp_path / "missing-qsfp.conf")
        else:
            qsfp_config_path = str(qsfp_config)

        args = _make_args(
            agent_run_mode=agent_run_mode,
            config=config_path,
            qsfp_config=qsfp_config_path,
        )
        with (
            patch.object(link_runner, "_get_tests_to_run") as mock_get_tests,
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_fsdb_service"
            ) as mock_fsdb,
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_qsfp_service"
            ) as mock_qsfp,
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_hw_agent_service"
            ) as mock_hw_agent,
            pytest.raises(UsageError, match=error),
        ):
            link_runner.run_test(args)

        mock_get_tests.assert_not_called()
        mock_fsdb.assert_not_called()
        mock_qsfp.assert_not_called()
        mock_hw_agent.assert_not_called()

    def test_mono_does_not_require_agent_config(self, link_runner, tmp_path):
        qsfp_config = tmp_path / "qsfp.conf"
        qsfp_config.write_text("")
        args = _make_args(
            agent_run_mode="mono",
            config=None,
            qsfp_config=str(qsfp_config),
        )

        with patch.object(TestRunner, "run_test") as base_run:
            link_runner.run_test(args)

        base_run.assert_called_once_with(args)

    def test_list_tests_does_not_require_configs(self, link_runner, capsys):
        args = _make_args(list_tests=True, config=None, qsfp_config=None)
        with (
            patch(
                "fboss_test_runner.runners.test_runner.shutil.which",
                return_value="/opt/fboss/bin/sai_multi_link_test-sai_impl",
            ),
            patch.object(link_runner, "_initialize_test_lists"),
            patch.object(link_runner, "_get_tests_to_run", return_value=["LinkTest.A"]),
        ):
            link_runner.list_tests(args)

        assert "LinkTest.A" in capsys.readouterr().out


class TestSetupColdbootServiceOrder:
    """Production correctness: services must start FSDB → QSFP → HW Agent.
    Reordering breaks downstream subscriptions (QSFP needs FSDB up first).
    HW Agent only starts in multi_switch mode."""

    def test_multi_switch_starts_services_in_order(self, link_runner):
        call_order = []
        with (
            patch.object(
                link_runner,
                "args",
                new=_make_args(agent_run_mode="multi_switch"),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_fsdb_service",
                side_effect=lambda **_: call_order.append("fsdb"),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_qsfp_service",
                side_effect=lambda **_: call_order.append("qsfp"),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_hw_agent_service",
                side_effect=lambda **_: call_order.append("hw_agent"),
            ),
        ):
            link_runner._setup_coldboot_test()
        assert call_order == ["fsdb", "qsfp", "hw_agent"]

    def test_mono_does_not_start_hw_agent_service(self, link_runner):
        # mono mode keeps the HW switch in-process; no separate HW Agent service.
        call_order = []
        with (
            patch.object(link_runner, "args", new=_make_args(agent_run_mode="mono")),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_fsdb_service",
                side_effect=lambda **_: call_order.append("fsdb"),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_qsfp_service",
                side_effect=lambda **_: call_order.append("qsfp"),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_hw_agent_service",
            ) as mock_hw_agent,
        ):
            link_runner._setup_coldboot_test()
        assert call_order == ["fsdb", "qsfp"]
        mock_hw_agent.assert_not_called()

    def test_disable_fsdb_skips_fsdb_startup(self, link_runner):
        with (
            patch.object(
                link_runner,
                "args",
                new=_make_args(agent_run_mode="mono", disable_fsdb=True),
            ),
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_fsdb_service"
            ) as mock_fsdb,
            patch(
                "fboss_test_runner.runners.link_test_runner.setup_and_start_qsfp_service"
            ) as mock_qsfp,
        ):
            link_runner._setup_coldboot_test()
        mock_fsdb.assert_not_called()
        # Catches over-skipping: an early-return in _setup_coldboot_test that
        # accidentally drops qsfp too must fail this test, not pass silently.
        mock_qsfp.assert_called()
