# Copyright Meta Platforms, Inc. and affiliates.
# @noautodeps
"""Tests for fboss_agent_utils HW agent systemd service lifecycle."""

import os
import sys
from unittest.mock import ANY, MagicMock, patch

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import fboss_test_runner.services.fboss_agent_utils as fboss_agent_utils
import fboss_test_runner.services.service_utils as service_utils
from fboss_test_runner.services.fboss_agent_utils import (
    agent_can_warm_boot_file_path,
    cleanup_hw_agent_service,
    setup_and_start_hw_agent_service,
    warm_boot_hw_agent,
)


class TestAgentCanWarmBootFilePath:
    def test_none_returns_base_path(self):
        path = agent_can_warm_boot_file_path(switch_index=None)
        assert path == fboss_agent_utils.FBOSS_AGENT_WB_FLAG_FILE
        assert not path.endswith("_0")

    def test_index_appends_underscore_suffix(self):
        assert agent_can_warm_boot_file_path(switch_index=0).endswith("_0")
        assert agent_can_warm_boot_file_path(switch_index=3).endswith("_3")


class TestServiceUnitEnvironment:
    def test_empty_library_path_uses_packaged_default(self, monkeypatch):
        monkeypatch.setenv("LD_LIBRARY_PATH", "")

        unit = service_utils.build_unit_file_content("test", "true", "test")

        assert "Environment=LD_LIBRARY_PATH=/opt/fboss/lib" in unit

    def test_base_output_dir_forwarded_into_unit(self, monkeypatch):
        # systemd does not inherit the shell env; the SDK-locating BASE_OUTPUT_DIR
        # must be declared in the unit or SDK resource resolution fails (hw_agent
        # aborts on e.g. g202_slices_mappings.json). Assert on whole lines: a
        # directive merged with its neighbour is a unit systemd would reject.
        sdk_dir = "/opt/fboss/cisco/mklib.G202X.dc-25.11.4210.7"
        monkeypatch.setenv("BASE_OUTPUT_DIR", sdk_dir)

        unit = service_utils.build_unit_file_content("test", "true", "test")

        lines = unit.splitlines()
        assert f"Environment=BASE_OUTPUT_DIR={sdk_dir}" in lines
        assert "ExecStart=true" in lines

    def test_base_output_dir_absent_when_unset(self, monkeypatch):
        # No regression for SDKs/platforms that don't use BASE_OUTPUT_DIR:
        # the line must be omitted entirely when the var is unset.
        monkeypatch.delenv("BASE_OUTPUT_DIR", raising=False)

        unit = service_utils.build_unit_file_content("test", "true", "test")

        assert "BASE_OUTPUT_DIR" not in unit
        assert "ExecStart=true" in unit.splitlines()


class TestCleanupHwAgentService:
    def test_stops_all_three_service_variants_and_pkills(self):
        with patch.object(service_utils.subprocess, "run") as mock_run:
            cleanup_hw_agent_service([0])

        joined = "\n".join(c.args[0] for c in mock_run.call_args_list)
        assert "systemctl stop fboss_hw_agent@0" in joined
        assert "systemctl stop fboss_hw_agent_for_testing_0" in joined
        assert "systemctl stop fboss_hw_agent_oss@0" in joined
        assert "systemctl disable fboss_hw_agent_oss@0" in joined
        assert "systemctl daemon-reload" in joined
        assert "pkill -f fboss_hw_agent@0" in joined
        assert "pkill -f fboss_hw_agent_for_testing_0" in joined
        assert "pkill -f fboss_hw_agent_oss@0" in joined

    def test_disables_requested_service_name(self):
        with patch.object(service_utils.subprocess, "run") as mock_run:
            cleanup_hw_agent_service(
                [0],
                hw_agent_service_name=fboss_agent_utils.HW_AGENT_SERVICE_PROD,
            )

        disable_commands = [
            invocation.args[0]
            for invocation in mock_run.call_args_list
            if invocation.args[0].startswith("systemctl disable")
        ]
        assert disable_commands == ["systemctl disable fboss_hw_agent@0"]


class TestSetupHwAgentServicePreconditions:
    def test_raises_when_binary_missing(self, tmp_path):
        config_path = tmp_path / "agent.conf"
        config_path.write_text("")
        with pytest.raises(Exception, match="HW Agent Service binary"):
            setup_and_start_hw_agent_service(
                switch_indexes=[0],
                fboss_agent_config_path=str(config_path),
                hw_agent_service_bin_path=str(tmp_path / "does_not_exist"),
            )

    def test_raises_when_config_missing(self, tmp_path):
        with pytest.raises(Exception, match="Agent config path"):
            setup_and_start_hw_agent_service(
                switch_indexes=[0],
                fboss_agent_config_path=str(tmp_path / "missing.conf"),
                hw_agent_service_bin_path=sys.executable,
            )


class TestSetupAndStartDispatch:
    def test_setup_disables_the_service_name_it_will_replace(self, tmp_path):
        config_path = tmp_path / "agent.conf"
        config_path.write_text("")
        with (
            patch.object(fboss_agent_utils, "cleanup_hw_agent_service") as cleanup,
            patch.object(service_utils, "write_unit_file"),
            patch.object(service_utils, "write_rsyslog_conf"),
            patch.object(fboss_agent_utils.subprocess, "run"),
            patch.object(fboss_agent_utils, "cold_boot_hw_agent", return_value=[0]),
        ):
            setup_and_start_hw_agent_service(
                switch_indexes=[0],
                fboss_agent_config_path=str(config_path),
                hw_agent_service_bin_path=sys.executable,
                hw_agent_service_name=fboss_agent_utils.HW_AGENT_SERVICE_PROD,
            )

        cleanup.assert_called_once_with(
            [0], hw_agent_service_name=fboss_agent_utils.HW_AGENT_SERVICE_PROD
        )

    # _setup_hw_agent_service writes systemd unit files to /tmp and rsyslog
    # configs to /etc/rsyslog.d, so dispatch-only tests stub it out entirely.
    def test_warm_boot_dispatch(self):
        with (
            patch.object(fboss_agent_utils, "_setup_hw_agent_service"),
            patch.object(
                fboss_agent_utils, "warm_boot_hw_agent", return_value=[0]
            ) as mock_warm,
            patch.object(
                fboss_agent_utils, "cold_boot_hw_agent", return_value=[0]
            ) as mock_cold,
        ):
            setup_and_start_hw_agent_service(
                switch_indexes=[0],
                fboss_agent_config_path="ignored.conf",
                hw_agent_service_bin_path=sys.executable,
                is_warm_boot=True,
            )
        mock_warm.assert_called_once_with([0], service_name=ANY)
        mock_cold.assert_not_called()

    def test_cold_boot_dispatch_and_failure_raises(self):
        with (
            patch.object(fboss_agent_utils, "_setup_hw_agent_service"),
            patch.object(fboss_agent_utils, "warm_boot_hw_agent", return_value=[0]),
            patch.object(
                fboss_agent_utils, "cold_boot_hw_agent", return_value=[1]
            ) as mock_cold,
            pytest.raises(Exception, match="cold-boot"),
        ):
            setup_and_start_hw_agent_service(
                switch_indexes=[0],
                fboss_agent_config_path="ignored.conf",
                hw_agent_service_bin_path=sys.executable,
                is_warm_boot=False,
            )
        mock_cold.assert_called_once_with([0], service_name=ANY)


class TestWarmBootMissingFileBehavior:
    def test_warmboot_proceeds_to_start_when_warmboot_file_missing(self):
        def fake_run(cmd, **_):
            rc = 1 if cmd.startswith("stat ") else 0
            return MagicMock(returncode=rc)

        all_commands = []

        def tracking_fake(cmd, **kw):
            all_commands.append(cmd)
            return fake_run(cmd, **kw)

        with (
            patch.object(
                fboss_agent_utils.subprocess, "run", side_effect=tracking_fake
            ),
            patch.object(service_utils.subprocess, "run", side_effect=tracking_fake),
        ):
            return_codes = warm_boot_hw_agent(switch_indexes=[0])

        assert any("systemctl start fboss_hw_agent_oss@0" in c for c in all_commands)
        assert return_codes == [0]
