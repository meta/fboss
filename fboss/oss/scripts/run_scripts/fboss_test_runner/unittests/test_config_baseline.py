#!/usr/bin/env python3
# @noautodeps
# Copyright Meta Platforms, Inc. and affiliates.

"""Unit tests for services/config_baseline.py.

Git operations run for real against a temporary repo standing in for
/etc/coop; only the fboss2 CLI and systemctl invocations are intercepted.
"""

import os
import subprocess
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import tempfile

from fboss_test_runner.services import config_baseline
from fboss_test_runner.services.config_baseline import ConfigBaseline

FAKE_CLI = "/nonexistent/fboss2-dev"
_REAL_RUN = subprocess.run


def _read(path: str) -> str:
    with open(path) as f:
        return f.read()


class _Harness:
    """A temp /etc/coop plus recorders for the CLI / systemctl calls."""

    def __init__(self, tmp: str, seed_agent_conf: str | None = '{"sw": 1}') -> None:
        self.repo = tmp
        if seed_agent_conf is not None:
            with open(os.path.join(tmp, "agent.conf"), "w") as f:
                f.write(seed_agent_conf)
        self.units_healthy = MagicMock(return_value=True)
        self.agent_responsive = MagicMock(return_value=True)
        self.cold_boot = MagicMock()
        self.cli_calls: list[list[str]] = []
        self.systemctl_calls: list[list[str]] = []
        self.cli_rc = 0
        # What the fake `config rollback` does to the repo when invoked.
        self.cli_side_effect = None
        self.baseline = ConfigBaseline(
            units_healthy=self.units_healthy,
            agent_responsive=self.agent_responsive,
            cold_boot_agents=self.cold_boot,
            fboss2_cli=FAKE_CLI,
            repo_dir=tmp,
        )

    def fake_run(self, cmd, *args, **kwargs):
        if cmd[0] == FAKE_CLI:
            self.cli_calls.append(cmd)
            if self.cli_side_effect:
                self.cli_side_effect()
            return subprocess.CompletedProcess(cmd, self.cli_rc, "", "boom")
        if cmd[0] == "systemctl":
            self.systemctl_calls.append(cmd)
            return subprocess.CompletedProcess(cmd, 0, "", "")
        return _REAL_RUN(cmd, *args, **kwargs)

    def git(self, *args: str) -> str:
        return _REAL_RUN(
            ["git", "-C", self.repo, *args], capture_output=True, text=True, check=True
        ).stdout.strip()

    def write_tracked_agent_conf(self, content: str, commit: bool = True) -> None:
        """Simulate a test's `config session commit`: write cli/agent.conf,
        point the agent.conf symlink at it, and commit."""
        path = os.path.join(self.repo, "cli", "agent.conf")
        with open(path, "w") as f:
            f.write(content)
        live = os.path.join(self.repo, "agent.conf")
        if os.path.lexists(live):
            os.remove(live)
        os.symlink("cli/agent.conf", live)
        if commit:
            self.git("add", "cli/agent.conf")
            self.git(
                "-c",
                "user.name=t",
                "-c",
                "user.email=t@t",
                "commit",
                "-q",
                "-m",
                "test commit",
            )


class TestCapture(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.h = _Harness(self.tmpdir.name)
        self.run_patch = patch.object(
            config_baseline.subprocess, "run", side_effect=self.h.fake_run
        )
        self.run_patch.start()

    def tearDown(self):
        self.run_patch.stop()
        self.tmpdir.cleanup()

    def test_initializes_repo_from_plain_config(self):
        sha = self.h.baseline.capture()
        self.assertIsNotNone(sha)
        self.assertEqual(self.h.git("rev-parse", "HEAD"), sha)
        # Seeded from the plain /etc/coop/agent.conf, like ConfigSession does.
        self.assertEqual(self.h.git("show", "HEAD:cli/agent.conf"), '{"sw": 1}')
        self.assertEqual(self.h.git("show", "HEAD:cli/cli_metadata.json"), "{}")
        # The live plain file is left alone until the CLI converts it.
        self.assertFalse(os.path.islink(os.path.join(self.h.repo, "agent.conf")))

    def test_reuses_existing_repo_head(self):
        first = self.h.baseline.capture()
        second = self.h.baseline.capture()
        self.assertEqual(first, second)

    def test_commits_uncommitted_drift(self):
        first = self.h.baseline.capture()
        # e.g. the runner cp'd a config through the symlink without committing
        self.h.write_tracked_agent_conf('{"sw": 2}', commit=False)
        second = self.h.baseline.capture()
        self.assertNotEqual(first, second)
        self.assertEqual(self.h.git("show", "HEAD:cli/agent.conf"), '{"sw": 2}')
        self.assertIn("suite baseline snapshot", self.h.git("log", "-1", "--format=%s"))

    def test_returns_none_when_git_unavailable(self):
        with patch.object(
            config_baseline.subprocess, "run", side_effect=OSError("no git")
        ):
            self.assertIsNone(self.h.baseline.capture())


class TestRestore(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.h = _Harness(self.tmpdir.name)
        self.run_patch = patch.object(
            config_baseline.subprocess, "run", side_effect=self.h.fake_run
        )
        self.run_patch.start()
        self.sha = self.h.baseline.capture()

    def tearDown(self):
        self.run_patch.stop()
        self.tmpdir.cleanup()

    def test_noop_without_baseline(self):
        self.assertTrue(self.h.baseline.restore(None))
        self.h.units_healthy.assert_not_called()
        self.h.cold_boot.assert_not_called()

    def test_fast_path_when_unchanged_and_healthy(self):
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertEqual(self.h.cli_calls, [])
        self.h.cold_boot.assert_not_called()
        self.assertEqual(self.h.git("rev-parse", "HEAD"), self.sha)

    def test_soft_rollback_when_drifted_and_healthy(self):
        self.h.write_tracked_agent_conf('{"sw": "poison"}')
        # The CLI's rollback re-serializes the config; its output need not be
        # byte-identical to the baseline blob, so success is judged by the
        # rollback's exit code and the agent answering, not by content.
        self.h.cli_side_effect = lambda: self.h.write_tracked_agent_conf(
            '{\n  "sw": 1\n}'
        )
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertEqual(
            self.h.cli_calls, [[FAKE_CLI, "--force", "config", "rollback", self.sha]]
        )
        self.h.cold_boot.assert_not_called()

    def test_uncommitted_edit_counts_as_drift(self):
        # Written behind git's back (harness cp through the symlink), HEAD
        # unchanged: still drifted, still rolled back.
        self.h.write_tracked_agent_conf('{"sw": "edited"}', commit=False)
        self.h.cli_side_effect = lambda: self.h.write_tracked_agent_conf(
            '{"sw": 1}', commit=False
        )
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertEqual(len(self.h.cli_calls), 1)

    def test_hard_restore_when_units_unhealthy(self):
        self.h.write_tracked_agent_conf('{"sw": "poison"}')
        # sw_agent answers thrift but the hw_agent unit is failed: the thrift
        # probe alone would say healthy; the unit probe must win.
        self.h.units_healthy.return_value = False
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertEqual(self.h.cli_calls, [])  # no soft rollback attempted
        self.assertEqual(
            _read(os.path.join(self.h.repo, "cli", "agent.conf")), '{"sw": 1}'
        )
        self.assertIn("restore baseline", self.h.git("log", "-1", "--format=%s"))
        self.assertEqual(self.h.systemctl_calls, [["systemctl", "reset-failed"]])
        self.h.cold_boot.assert_called_once()
        # thrift probe short-circuited by the unit probe, then used post-coldboot
        self.h.agent_responsive.assert_called_once()

    def test_hard_restore_repairs_daemon_symlink(self):
        self.h.write_tracked_agent_conf('{"sw": "poison"}')
        live = os.path.join(self.h.repo, "agent.conf")
        os.remove(live)
        with open(live, "w") as f:
            f.write("stale plain file")
        self.h.units_healthy.return_value = False
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertTrue(os.path.islink(live))
        self.assertEqual(os.readlink(live), "cli/agent.conf")
        self.assertEqual(_read(live), '{"sw": 1}')

    def test_soft_rollback_failure_falls_back_to_hard(self):
        self.h.write_tracked_agent_conf('{"sw": "poison"}')
        self.h.cli_rc = 1
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.assertEqual(len(self.h.cli_calls), 1)
        self.h.cold_boot.assert_called_once()
        self.assertEqual(
            _read(os.path.join(self.h.repo, "cli", "agent.conf")), '{"sw": 1}'
        )

    def test_unchanged_config_but_dead_agents_cold_boots(self):
        self.h.units_healthy.return_value = False
        self.assertTrue(self.h.baseline.restore(self.sha))
        self.h.cold_boot.assert_called_once()
        self.assertEqual(self.h.cli_calls, [])

    def test_reports_failure_when_agents_never_return(self):
        self.h.units_healthy.return_value = False
        self.h.agent_responsive.return_value = False
        self.assertFalse(self.h.baseline.restore(self.sha))


class TestWaitAgentResponsive(unittest.TestCase):
    @patch("fboss_test_runner.services.config_baseline.time.sleep")
    @patch("fboss_test_runner.services.config_baseline.subprocess.run")
    def test_returns_immediately_when_ready(self, mock_run, mock_sleep):
        mock_run.return_value = subprocess.CompletedProcess([], 0, "", "")
        self.assertTrue(config_baseline.wait_agent_responsive(FAKE_CLI, timeout_sec=30))
        mock_sleep.assert_not_called()
        self.assertEqual(mock_run.call_args[0][0], [FAKE_CLI, "show", "product"])

    @patch("fboss_test_runner.services.config_baseline.time.sleep")
    @patch("fboss_test_runner.services.config_baseline.time.monotonic")
    @patch("fboss_test_runner.services.config_baseline.subprocess.run")
    def test_polls_then_gives_up(self, mock_run, mock_monotonic, mock_sleep):
        mock_run.return_value = subprocess.CompletedProcess([], 1, "", "refused")
        mock_monotonic.side_effect = [0, 5, 15, 25, 40]
        self.assertFalse(
            config_baseline.wait_agent_responsive(
                FAKE_CLI, timeout_sec=30, poll_interval_sec=10
            )
        )
        self.assertGreaterEqual(mock_sleep.call_count, 1)


if __name__ == "__main__":
    unittest.main()
