#!/usr/bin/env python3
# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import subprocess
import time
from argparse import ArgumentParser

from fboss_test_runner.runners.test_runner import TestRunner
from fboss_test_runner.services.config_baseline import (
    AGENT_COLDBOOT_TIMEOUT_SEC,
    ConfigBaseline,
    find_fboss2_cli,
    wait_agent_responsive,
)
from fboss_test_runner.services.fboss_agent_utils import (
    cleanup_hw_agent_service,
    cleanup_sw_agent_service,
    cold_boot_agents,
    HW_AGENT_SERVICE_PROD,
    is_agent_running,
    setup_and_start_hw_agent_service,
    setup_and_start_sw_agent_service,
    SW_AGENT_SERVICE_PROD,
)
from npu_sdk_utils import NPU_HW_AGENT_BINARY

# fboss2 integration known-bad and unsupported tests are published as two
# separate materialized JSONs (mirroring sai_agent_test): one carrying the
# `known_bad_tests` section, the other the `unsupported_tests` section. The base
# runner reads each by its own top-level key.
FBOSS2_INTEGRATION_KNOWN_BAD_TESTS_FILE = "./share/fboss2_integration_known_bad_tests/fboss2_integration_known_bad_tests.materialized_JSON"
FBOSS2_INTEGRATION_UNSUPPORTED_TESTS_FILE = "./share/fboss2_integration_unsupported_tests/fboss2_integration_unsupported_tests.materialized_JSON"


class Fboss2IntegrationTestRunner(TestRunner):
    """
    Runner for fboss2 integration tests.

    fboss2 integration tests are C++ gtest-based tests that run CLI commands and verify output.
    They test the CLI tool itself (fboss2-dev) on a running FBOSS instance.

    fboss2 integration tests are platform/SAI independent - they test the CLI binary which
    communicates with the agent via Thrift, regardless of the underlying
    hardware abstraction layer.

    Agent lifecycle: uses production service names (fboss_sw_agent, fboss_hw_agent@N)
    because fboss2-dev may restart agents during config commits. Detects whether the
    device has production multi-switch services running or needs service setup from scratch.
    Cold boots both agents once to establish a clean state, then reuses the running
    agents between tests and only cold boots again when an agent is no longer up.

    Config containment: before each gtest suite the /etc/coop git HEAD is
    recorded, and after the suite the config is rolled back to it (see
    services/config_baseline.py). One suite committing a config the hardware
    rejects can crash-loop the agents; without the rollback every later suite
    fails on the leftover config.
    """

    _AGENT_CONFIG_PATH = "/etc/coop/agent.conf"
    _CONFIG_SNAPSHOT_PATH = "/tmp/agent.conf.fboss2_test_snapshot"

    # Grace window we allow an agent that is still coming up (systemd
    # "activating", e.g. fboss2-dev bounced it during a config commit) to reach
    # "active" before we give up and cold boot. A healthy agent passes on the
    # first poll, and a dead agent ("failed"/"inactive") is cold booted
    # immediately without waiting, so this window only applies while an agent is
    # genuinely mid-restart.
    _AGENT_READY_GRACE_RETRIES: int = 30
    _AGENT_READY_GRACE_SLEEP_SEC: int = 1
    _SYSTEMD_ACTIVE: str = "active"
    _SYSTEMD_ACTIVATING: str = "activating"

    def __init__(self) -> None:
        super().__init__()
        # Whether fboss_sw_agent and fboss_hw_agent@N are already running
        self._is_prod_multi_switch: bool = False
        self._switch_indexes: list[int] = []
        self._test_config_source: str = self._AGENT_CONFIG_PATH
        # Tracks whether the one-time initial cold boot has run. The first cold
        # boot always happens (to load the test config and a clean state); after
        # that we health-gate per-test cold boots (or skip them entirely with
        # --skip-coldboot).
        self._initial_coldboot_done: bool = False
        # /etc/coop git sha recorded at suite start; None when capture failed.
        self._suite_baseline: str | None = None
        self._config_baseline: ConfigBaseline | None = None
        # systemd NRestarts per agent unit at suite start; a higher value at
        # suite end means systemd auto-restarted a crashed agent.
        self._suite_start_restarts: dict[str, int] = {}

    def add_subcommand_arguments(self, sub_parser: ArgumentParser) -> None:
        """Add CLI test-specific command line arguments"""
        super().add_subcommand_arguments(sub_parser)
        sub_parser.set_defaults(fruid_path=None, coldboot_only=True)
        sub_parser.add_argument(
            "--num-npus",
            type=int,
            choices=[1, 2],
            default=1,
            help="Number of NPUs (switch indexes). Default is 1.",
        )
        sub_parser.add_argument(
            "--skip-coldboot",
            action="store_true",
            default=False,
            help="Aggressively skip all per-test cold boots after the initial "
            "one, even if an agent is down (default behavior already skips the "
            "cold boot when the agents are still up).",
        )

    def _get_config_path(self) -> str:
        return self._AGENT_CONFIG_PATH

    def _get_known_bad_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.known_bad_tests_file,
            FBOSS2_INTEGRATION_KNOWN_BAD_TESTS_FILE,
            self.KNOWN_BAD_TESTS_LABEL,
        )

    def _get_unsupported_tests_file(self) -> str:
        return self._resolve_tests_file(
            self.args.unsupported_tests_file,
            FBOSS2_INTEGRATION_UNSUPPORTED_TESTS_FILE,
            self.UNSUPPORTED_TESTS_LABEL,
        )

    def _get_test_binary_name(self) -> str:
        return "fboss2_integration_test"

    def _get_npu_sdk_metadata_binary_name(self) -> str:
        # The integration test drives split agents and does not link the NPU
        # SDK; the hardware-agent process links it.
        return NPU_HW_AGENT_BINARY

    def _get_warmboot_check_file(self) -> str:
        return ""

    def _get_test_run_args(self, conf_file: str) -> list[str]:  # noqa: ARG002
        return []

    def _setup_run(self, conf_file: str) -> None:
        args = self.args
        self._switch_indexes = list(range(args.num_npus))
        self._is_prod_multi_switch = all(
            is_agent_running(
                self._switch_indexes,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
                sw_agent_service_name=SW_AGENT_SERVICE_PROD,
            )
        )
        self._test_config_source = conf_file

        if self._is_prod_multi_switch:
            print(
                "Production multi-switch detected — "
                f"{SW_AGENT_SERVICE_PROD} and "
                f"{HW_AGENT_SERVICE_PROD}N already running. "
                "Snapshotting agent config."
            )
            subprocess.run(
                ["cp", self._AGENT_CONFIG_PATH, self._CONFIG_SNAPSHOT_PATH], check=True
            )
        else:
            print("No running agents detected — setting up agent services.")

        if conf_file != self._AGENT_CONFIG_PATH:
            print(f"Copying test config {conf_file} to {self._AGENT_CONFIG_PATH}")
            subprocess.run(["cp", conf_file, self._AGENT_CONFIG_PATH], check=True)

        if not self._is_prod_multi_switch:
            setup_and_start_hw_agent_service(
                switch_indexes=self._switch_indexes,
                fboss_agent_config_path=self._AGENT_CONFIG_PATH,
                is_warm_boot=False,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
                hw_agent_for_testing=False,
            )
            setup_and_start_sw_agent_service(
                fboss_agent_config_path=self._AGENT_CONFIG_PATH,
                is_warm_boot=False,
                sw_agent_service_name=SW_AGENT_SERVICE_PROD,
            )

    def _setup_coldboot_test(self, sai_replayer_log_path: str | None = None) -> None:  # noqa: ARG002
        # The first cold boot always runs: it loads the test config and
        # establishes a clean state (in prod multi-switch the running agents
        # still hold the old config until rebooted).
        if self._initial_coldboot_done:
            # Aggressive override: skip all per-test cold boots after the first.
            # Default False matches argparse; also guard None when self.args
            # isn't set in unit tests that mock _agents_ready.
            if self.args is not None and getattr(self.args, "skip_coldboot", False):
                print("########## Skipping per-test cold boot (--skip-coldboot).")
                return
            # Default: reuse the already-running agents and only cold boot when
            # an agent is down (e.g. a prior test crashed/wedged it). Most fboss2
            # integration tests self-revert their config changes, so rebooting
            # between every test is largely wasted.
            if self._agents_ready():
                print("########## Agents still up, skipping per-test cold boot.")
                return

        if self._test_config_source != self._AGENT_CONFIG_PATH:
            subprocess.run(
                ["cp", self._test_config_source, self._AGENT_CONFIG_PATH], check=True
            )
        cold_boot_agents(
            self._switch_indexes,
            hw_agent_service_name=HW_AGENT_SERVICE_PROD,
            sw_agent_service_name=SW_AGENT_SERVICE_PROD,
        )
        self._initial_coldboot_done = True

    def _on_suite_start(self, suite: str) -> None:  # noqa: ARG002
        self._suite_start_restarts = self._unit_restart_counts()
        self._suite_baseline = self._get_config_baseline().capture()

    def _on_suite_end(self, suite: str) -> None:
        if not self._get_config_baseline().restore(self._suite_baseline):
            print(
                f"########## Failed to restore the config baseline after {suite}; "
                "subsequent suites may fail spuriously"
            )
        self._suite_baseline = None

    def _get_config_baseline(self) -> ConfigBaseline:
        if self._config_baseline is None:
            self._config_baseline = ConfigBaseline(
                units_healthy=self._agents_healthy_for_suite,
                agent_responsive=self._agent_responsive,
                cold_boot_agents=self._cold_boot_agents,
                fboss2_cli=find_fboss2_cli(),
            )
        return self._config_baseline

    def _agent_responsive(self) -> bool:
        # Generous timeout: this is also the wait after a cold boot.
        return wait_agent_responsive(
            find_fboss2_cli(), timeout_sec=AGENT_COLDBOOT_TIMEOUT_SEC
        )

    def _agents_healthy_for_suite(self) -> bool:
        """Agents are healthy enough to trust a soft (CLI) rollback.

        `systemctl is-active` alone is a snapshot, and the production units
        run Restart=always with no start limit: a crash-looping agent never
        reaches "failed", it reads "activating" for RestartSec and "active"
        for the few seconds it lives each cycle. So on top of the liveness
        check, require that systemd did not auto-restart any agent unit
        since the suite started. NRestarts counts only Restart=-triggered
        restarts, so the warmboot/coldboot restarts the CLI itself performs
        on commit do not register (they reset the counter, which is why a
        lower value is not treated as a crash). A crash during the suite
        means the device is not in a state worth preserving; the caller cold
        boots it from the baseline.
        """
        if not self._agents_ready():
            return False
        crashed = [
            f"{unit} (NRestarts {self._suite_start_restarts.get(unit)} -> {after})"
            for unit, after in self._unit_restart_counts().items()
            if after > self._suite_start_restarts.get(unit, after)
        ]
        if crashed:
            print(
                "########## Agent unit auto-restarted during this suite: "
                + ", ".join(crashed)
            )
            return False
        return True

    def _unit_restart_counts(self) -> dict[str, int]:
        """systemd's NRestarts for each agent unit. Units that cannot be
        queried are omitted (and so never count as crashed)."""
        counts: dict[str, int] = {}
        for unit in self._agent_services():
            try:
                result = subprocess.run(
                    ["systemctl", "show", "-p", "NRestarts", "--value", unit],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                counts[unit] = int(result.stdout.strip())
            except (OSError, ValueError) as e:
                print(f"Warning: failed to read NRestarts for {unit}: {e}")
        return counts

    def _cold_boot_agents(self) -> None:
        cold_boot_agents(
            self._switch_indexes,
            hw_agent_service_name=HW_AGENT_SERVICE_PROD,
            sw_agent_service_name=SW_AGENT_SERVICE_PROD,
        )

    def _agent_services(self) -> list[str]:
        """systemd unit names for the sw_agent and every hw_agent."""
        services = [SW_AGENT_SERVICE_PROD]
        services += [
            f"{HW_AGENT_SERVICE_PROD}{switch_index}"
            for switch_index in self._switch_indexes
        ]
        return services

    def _agents_ready(self) -> bool:
        """Return True only if the sw_agent and every hw_agent service is active.

        We poll only while an agent is still coming up (systemd "activating").
        A dead agent ("failed"/"inactive"/etc. -- e.g. a prior test crashed it)
        will not recover on its own, so we return immediately and let the caller
        cold boot rather than waste the grace window polling it; this is what
        makes recovery after a failed test fast.

        The sw_agent and each hw_agent are checked independently, so in
        multi-switch mode a crashed hw_agent is not masked by a healthy sw_agent.

        Note: this is a liveness (systemd active) check. The OSS agent utils do
        not expose a thrift run-state query, so unlike the netcastle path we
        cannot additionally require CONFIGURED here.
        """
        services = self._agent_services()
        for attempt in range(self._AGENT_READY_GRACE_RETRIES):
            states = self._service_states(services)
            if states == [self._SYSTEMD_ACTIVE] * len(services):
                return True
            if self._SYSTEMD_ACTIVATING not in states:
                # Nothing is coming up -> a cold boot is required now; do not
                # waste the grace window polling a dead agent.
                return False
            if attempt + 1 < self._AGENT_READY_GRACE_RETRIES:
                time.sleep(self._AGENT_READY_GRACE_SLEEP_SEC)
        return False

    def _service_states(self, services: list[str]) -> list[str]:
        """Return the `systemctl is-active` state of each service (one call).

        `systemctl is-active s1 s2 ...` prints one state per unit ("active",
        "activating", "failed", "inactive", ...). Returns an empty list on any
        error, which the caller treats as not-ready.
        """
        try:
            result = subprocess.run(
                ["systemctl", "is-active", *services],
                capture_output=True,
                text=True,
                check=False,
            )
            return [line.strip() for line in result.stdout.splitlines() if line.strip()]
        except Exception as e:
            print(f"Warning: failed to query agent service states: {e}")
            return []

    def _end_run(self) -> None:
        if self._is_prod_multi_switch:
            print("Restoring original agent config and restarting agents.")
            subprocess.run(
                ["cp", self._CONFIG_SNAPSHOT_PATH, self._AGENT_CONFIG_PATH], check=False
            )
            try:
                cold_boot_agents(
                    self._switch_indexes,
                    hw_agent_service_name=HW_AGENT_SERVICE_PROD,
                    sw_agent_service_name=SW_AGENT_SERVICE_PROD,
                )
            except Exception as e:
                # Broad catch: cold_boot_agents raises generic Exception;
                # cleanup must not prevent config restoration below.
                print(f"Warning: error restarting agents during cleanup: {e}")
            subprocess.run(["rm", "-f", self._CONFIG_SNAPSHOT_PATH], check=False)
        else:
            print("Cleaning up agent services.")
            cleanup_sw_agent_service(SW_AGENT_SERVICE_PROD)
            cleanup_hw_agent_service(
                self._switch_indexes,
                hw_agent_service_name=HW_AGENT_SERVICE_PROD,
            )
