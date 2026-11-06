#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

from __future__ import annotations

from argparse import Namespace
from unittest.mock import call, MagicMock, patch

import pytest
from fboss_test_runner.runners.service_lifecycle_runner import (
    _AGENT_CONFIG_PATH,
    ServiceLifecycleRunner,
)


def _start_args(**overrides) -> Namespace:
    values = {
        "agent_config": "/tmp/input-agent.conf",
        "qsfp_config": "/tmp/qsfp.conf",
        "fsdb_config": None,
        "platform_mapping_override_path": None,
        "bsp_platform_mapping_override_path": None,
        "num_npus": 2,
        "services": None,
    }
    values.update(overrides)
    return Namespace(**values)


class StartServicesTest:
    def test_fsdb_does_not_require_agent_or_qsfp_config(self):
        runner = ServiceLifecycleRunner()
        args = _start_args(
            agent_config=None,
            qsfp_config=None,
            services=["fsdb"],
        )

        with (
            patch.object(runner, "_prepare_agent_config") as prepare_config,
            patch.object(runner, "_validate_binaries"),
            patch(
                "fboss_test_runner.runners.service_lifecycle_runner.shutil.copyfile"
            ) as copyfile,
            patch(
                "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_fsdb_service"
            ) as setup_fsdb,
            patch(
                "fboss_test_runner.runners.service_lifecycle_runner.service_utils.systemctl_is_active",
                return_value=True,
            ),
        ):
            assert runner.start(args) == 0

        prepare_config.assert_not_called()
        copyfile.assert_not_called()
        setup_fsdb.assert_called_once()

    def test_managed_agent_requires_config(self):
        with pytest.raises(ValueError, match="--agent-config is required"):
            ServiceLifecycleRunner().start(
                _start_args(
                    agent_config=None,
                    qsfp_config=None,
                    services=["agent"],
                )
            )

    def test_managed_qsfp_requires_config(self):
        with pytest.raises(ValueError, match="--qsfp-config is required"):
            ServiceLifecycleRunner().start(
                _start_args(
                    agent_config=None,
                    qsfp_config=None,
                    services=["qsfp"],
                )
            )

    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.wait_agent_responsive",
        return_value=True,
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.service_utils.systemctl_is_active",
        return_value=True,
    )
    @patch("fboss_test_runner.runners.service_lifecycle_runner.cold_boot_agents")
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_sw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_hw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_qsfp_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_fsdb_service"
    )
    @patch.object(ServiceLifecycleRunner, "_prepare_agent_config")
    @patch("fboss_test_runner.runners.service_lifecycle_runner.shutil.copyfile")
    def test_starts_every_service_and_leaves_it_running(
        self,
        copyfile,
        prepare_config,
        setup_fsdb,
        setup_qsfp,
        setup_hw,
        setup_sw,
        cold_boot,
        is_active,
        wait_responsive,
    ):
        prepare_config.return_value = "/tmp/prepared-agent.conf"
        startup = MagicMock()
        startup.attach_mock(setup_fsdb, "fsdb")
        startup.attach_mock(setup_qsfp, "qsfp")
        startup.attach_mock(setup_hw, "hw")
        startup.attach_mock(setup_sw, "sw")
        startup.attach_mock(cold_boot, "cold_boot")

        with patch.object(
            ServiceLifecycleRunner, "_validate_binaries"
        ) as validate_binaries:
            assert ServiceLifecycleRunner().start(_start_args()) == 0

        validate_binaries.assert_called_once_with({"agent", "fsdb", "qsfp"})
        assert [invocation[0] for invocation in startup.mock_calls] == [
            "fsdb",
            "qsfp",
            "hw",
            "sw",
            "cold_boot",
        ]
        copyfile.assert_called_once_with("/tmp/prepared-agent.conf", _AGENT_CONFIG_PATH)
        setup_fsdb.assert_called_once_with(
            fsdb_service_config_path=None, is_warm_boot=False
        )
        setup_qsfp.assert_called_once()
        setup_hw.assert_called_once_with(
            switch_indexes=[0, 1],
            fboss_agent_config_path=_AGENT_CONFIG_PATH,
            platform_mapping_override_path=None,
            is_fsdb_disabled=False,
            is_warm_boot=False,
            hw_agent_service_name="fboss_hw_agent@",
            hw_agent_for_testing=False,
        )
        setup_sw.assert_called_once_with(
            fboss_agent_config_path=_AGENT_CONFIG_PATH,
            is_warm_boot=False,
            sw_agent_service_name="fboss_sw_agent",
        )
        cold_boot.assert_called_once_with(
            [0, 1],
            hw_agent_service_name="fboss_hw_agent@",
            sw_agent_service_name="fboss_sw_agent",
        )
        assert is_active.call_args_list == [
            call("fsdb_service_oss"),
            call("qsfp_service_oss"),
            call("fboss_sw_agent"),
            call("fboss_hw_agent@0"),
            call("fboss_hw_agent@1"),
        ]
        wait_responsive.assert_called_once()

    @patch.object(ServiceLifecycleRunner, "_stop_all")
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.wait_agent_responsive",
        return_value=False,
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.service_utils.systemctl_is_active",
        return_value=True,
    )
    @patch("fboss_test_runner.runners.service_lifecycle_runner.cold_boot_agents")
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_sw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_hw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_qsfp_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_fsdb_service"
    )
    @patch.object(ServiceLifecycleRunner, "_prepare_agent_config")
    @patch("fboss_test_runner.runners.service_lifecycle_runner.shutil.copyfile")
    def test_readiness_failure_rolls_back(
        self,
        _copyfile,
        prepare_config,
        _setup_fsdb,
        _setup_qsfp,
        _setup_hw,
        _setup_sw,
        _cold_boot,
        _is_active,
        _wait_responsive,
        stop_all,
    ):
        prepare_config.return_value = "/tmp/prepared-agent.conf"

        with (
            patch.object(ServiceLifecycleRunner, "_validate_binaries"),
            pytest.raises(RuntimeError, match="did not become responsive"),
        ):
            ServiceLifecycleRunner().start(_start_args())

        stop_all.assert_called_once_with(
            [0, 1], services={"agent", "qsfp", "fsdb"}, raise_on_error=False
        )

    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.wait_agent_responsive",
        return_value=True,
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.service_utils.systemctl_is_active",
        return_value=True,
    )
    @patch("fboss_test_runner.runners.service_lifecycle_runner.cold_boot_agents")
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_sw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_hw_agent_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_qsfp_service"
    )
    @patch(
        "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_fsdb_service"
    )
    @patch("fboss_test_runner.runners.service_lifecycle_runner.cleanup_fsdb_service")
    @patch.object(ServiceLifecycleRunner, "_prepare_agent_config")
    @patch("fboss_test_runner.runners.service_lifecycle_runner.shutil.copyfile")
    def test_starts_only_selected_agent(
        self,
        _copyfile,
        prepare_config,
        cleanup_fsdb,
        setup_fsdb,
        setup_qsfp,
        setup_hw,
        setup_sw,
        cold_boot,
        is_active,
        wait_responsive,
    ):
        prepare_config.return_value = "/tmp/prepared-agent.conf"

        with patch.object(ServiceLifecycleRunner, "_validate_binaries"):
            assert (
                ServiceLifecycleRunner().start(
                    _start_args(qsfp_config=None, services=["agent"])
                )
                == 0
            )

        cleanup_fsdb.assert_not_called()
        setup_fsdb.assert_not_called()
        setup_qsfp.assert_not_called()
        setup_hw.assert_called_once()
        assert setup_hw.call_args.kwargs["is_fsdb_disabled"] is True
        setup_sw.assert_called_once()
        cold_boot.assert_called_once()
        assert is_active.call_args_list == [
            call("fboss_sw_agent"),
            call("fboss_hw_agent@0"),
            call("fboss_hw_agent@1"),
        ]
        wait_responsive.assert_called_once()

    def test_missing_selected_binary_fails_before_start(self):
        with (
            patch(
                "fboss_test_runner.runners.service_lifecycle_runner.service_utils.resolve_binary",
                side_effect=Exception("missing FSDB binary"),
            ),
            patch(
                "fboss_test_runner.runners.service_lifecycle_runner.setup_and_start_fsdb_service"
            ) as setup_fsdb,
            pytest.raises(Exception, match="missing FSDB binary"),
        ):
            ServiceLifecycleRunner().start(
                _start_args(
                    agent_config=None,
                    qsfp_config=None,
                    services=["fsdb"],
                )
            )

        setup_fsdb.assert_not_called()


class StopServicesTest:
    def test_stops_every_service_in_reverse_startup_order(self, monkeypatch):
        calls = []
        cleanup_hw = MagicMock(side_effect=lambda *_args, **_kwargs: calls.append("hw"))
        module = "fboss_test_runner.runners.service_lifecycle_runner"
        monkeypatch.setattr(
            f"{module}.cleanup_sw_agent_service", lambda *_: calls.append("sw")
        )
        monkeypatch.setattr(f"{module}.cleanup_hw_agent_service", cleanup_hw)
        monkeypatch.setattr(
            f"{module}.cleanup_qsfp_service", lambda: calls.append("qsfp")
        )
        monkeypatch.setattr(
            f"{module}.cleanup_fsdb_service", lambda: calls.append("fsdb")
        )

        assert ServiceLifecycleRunner().stop(Namespace(num_npus=2, services=None)) == 0
        assert calls == ["sw", "hw", "qsfp", "fsdb"]
        cleanup_hw.assert_called_once_with(
            [0, 1], hw_agent_service_name="fboss_hw_agent@"
        )

    def test_stop_only_selected_services(self, monkeypatch):
        calls = []
        module = "fboss_test_runner.runners.service_lifecycle_runner"
        monkeypatch.setattr(
            f"{module}.cleanup_sw_agent_service", lambda *_: calls.append("sw")
        )
        monkeypatch.setattr(
            f"{module}.cleanup_hw_agent_service", lambda *_: calls.append("hw")
        )
        monkeypatch.setattr(
            f"{module}.cleanup_qsfp_service", lambda: calls.append("qsfp")
        )
        monkeypatch.setattr(
            f"{module}.cleanup_fsdb_service", lambda: calls.append("fsdb")
        )
        assert (
            ServiceLifecycleRunner().stop(Namespace(num_npus=2, services=["qsfp"])) == 0
        )
        assert calls == ["qsfp"]
