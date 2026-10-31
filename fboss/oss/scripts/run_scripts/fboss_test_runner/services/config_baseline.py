# @noautodeps
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

"""Suite-level containment of config changes made by fboss2 integration tests.

One test suite committing a config the device cannot apply used to poison
every suite that ran after it: the hw_agent crash-loops on the bad config,
every later `config session commit` kills the sw_agent, and innocent tests
fail. The fboss2 CLI keeps the live config in a git repo (/etc/coop), so the
harness records the repo HEAD before a suite starts and rolls back to it after
the suite ends -- healing the agents if the suite left them dead.

Per-test isolation is intentionally not provided here; tests that need it
snapshot/restore in their own SetUp/TearDown.
"""

import os
import shutil
import subprocess
import time
from collections.abc import Callable

# The fboss2 CLI's git repo and the files it tracks there. Mirrors
# ConfigSession (fboss/cli/fboss2/session/ConfigSession.{h,cpp}): each config
# domain is a git-tracked file under the repo plus a stable daemon-facing
# symlink at the repo root pointing at it.
COOP_DIR = "/etc/coop"
# (git-relative tracked file, daemon-facing symlink at the repo root)
CONFIG_DOMAINS: tuple[tuple[str, str], ...] = (
    ("cli/agent.conf", "agent.conf"),
    ("bgpcpp/bgpcpp.conf", "bgpcpp.conf"),
)
METADATA_FILE = "cli/cli_metadata.json"
TRACKED_FILES: tuple[str, ...] = (*(f for f, _ in CONFIG_DOMAINS), METADATA_FILE)
GIT_AUTHOR = ("fboss_test_runner", "fboss-cli@localhost")

# Same defaults the CLI's own tests use for waiting on an agent to come back
# after a restart; a cold boot on some ASICs takes 90s+.
AGENT_READY_TIMEOUT_SEC = 120
AGENT_COLDBOOT_TIMEOUT_SEC = 300
AGENT_POLL_INTERVAL_SEC = 10


class ConfigBaseline:
    """Capture the /etc/coop git baseline for a suite and restore it after.

    Collaborators are injected so the runner (and the unit tests) decide how
    "is the agent healthy" and "cold boot the agents" are answered:

    - ``units_healthy()``: every agent systemd unit is active. This is what
      catches a crash-looping hw_agent while the sw_agent still answers thrift
      -- the thrift probe alone would report healthy and we would try a soft
      rollback that cannot reach hardware.
    - ``agent_responsive()``: the sw_agent serves thrift (switch CONFIGURED).
    - ``cold_boot_agents()``: cold boot the agents from the on-disk config.
    - ``fboss2_cli``: the fboss2 CLI binary used for the soft rollback path.
    """

    def __init__(
        self,
        units_healthy: Callable[[], bool],
        agent_responsive: Callable[[], bool],
        cold_boot_agents: Callable[[], None],
        fboss2_cli: str | None = None,
        repo_dir: str = COOP_DIR,
    ) -> None:
        self._units_healthy = units_healthy
        self._agent_responsive = agent_responsive
        self._cold_boot_agents = cold_boot_agents
        self._fboss2_cli = fboss2_cli or find_fboss2_cli()
        self._repo = repo_dir

    # ---- git plumbing --------------------------------------------------

    def _git(self, *args: str, check: bool = False) -> subprocess.CompletedProcess:
        # safe.directory: the harness runs as root while /etc/coop is owned by
        # the `coop` user; without it every git command refuses to run (the
        # CLI's Git class passes the same override).
        return subprocess.run(
            [
                "git",
                "-c",
                f"safe.directory={self._repo}",
                "-C",
                self._repo,
                "-c",
                f"user.name={GIT_AUTHOR[0]}",
                "-c",
                f"user.email={GIT_AUTHOR[1]}",
                *args,
            ],
            capture_output=True,
            text=True,
            check=check,
        )

    def _tracked_present(self) -> list[str]:
        return [f for f in TRACKED_FILES if os.path.exists(self._path(f))]

    def _path(self, rel: str) -> str:
        return os.path.join(self._repo, rel)

    def head(self) -> str | None:
        result = self._git("rev-parse", "HEAD")
        return result.stdout.strip() if result.returncode == 0 else None

    def _is_repo(self) -> bool:
        return self._git("rev-parse", "--git-dir").returncode == 0

    def _worktree_dirty(self) -> bool:
        """True when a tracked config file on disk differs from HEAD (e.g. the
        harness or an operator copied a config through the agent.conf symlink
        without committing). Exact comparison is right here: the CLI commits
        exactly the bytes it writes, so a clean tree means HEAD is what the
        daemons read."""
        result = self._git(
            "status", "--porcelain", "--untracked-files=no", "--", *TRACKED_FILES
        )
        return bool(result.stdout.strip())

    def _drifted(self, baseline: str) -> bool:
        """The config moved away from the baseline: a commit landed since, or
        the tracked files were edited behind git's back. Content is not
        compared textually -- a baseline captured from a raw config file and
        the CLI's re-serialization of the same config differ in layout while
        meaning the same thing, and only the CLI can compare them
        semantically (which its `config rollback` does)."""
        return self.head() != baseline or self._worktree_dirty()

    def _files_at_revision(self, revision: str) -> list[str]:
        result = self._git("ls-tree", "--name-only", revision, "--", *TRACKED_FILES)
        return [line for line in result.stdout.splitlines() if line]

    def _commit(self, message: str) -> bool:
        present = self._tracked_present()
        if not present:
            return False
        self._git("add", "--", *present)
        head = self.head()
        if head is not None:
            # Stage removals of tracked files that no longer exist on disk.
            for gone in set(self._files_at_revision(head)) - set(present):
                self._git("rm", "-q", "--cached", "--", gone)
        result = self._git("commit", "-q", "-m", message)
        if result.returncode != 0:
            print(f"Warning: git commit failed: {result.stderr.strip()}")
            return False
        return True

    def _initialize_repo(self) -> None:
        """Mirror ConfigSession::initializeGit(): create the repo and seed the
        tracked files from the live daemon configs, then make the initial
        commit. A device the CLI has never committed on has plain files at the
        symlink locations; those are the seeds."""
        os.makedirs(self._path("cli"), exist_ok=True)
        for tracked, live in CONFIG_DOMAINS:
            tracked_path = self._path(tracked)
            live_path = self._path(live)
            if os.path.exists(tracked_path):
                continue
            if os.path.isfile(live_path) and not os.path.islink(live_path):
                os.makedirs(os.path.dirname(tracked_path), exist_ok=True)
                shutil.copyfile(live_path, tracked_path)
            elif tracked == CONFIG_DOMAINS[0][0]:
                # The agent config always exists in the repo, even if empty.
                with open(tracked_path, "w") as f:
                    f.write("{}")
        if not os.path.exists(self._path(METADATA_FILE)):
            with open(self._path(METADATA_FILE), "w") as f:
                f.write("{}")
        if not self._is_repo():
            self._git("init", "-q", check=True)
            self._git("symbolic-ref", "HEAD", "refs/heads/main")
        if self.head() is None:
            self._commit("Initial commit")

    # ---- public API ----------------------------------------------------

    def capture(self) -> str | None:
        """Record the current config as the baseline and return its sha.

        Initializes the git repo if the CLI has never done so, and first
        commits any uncommitted drift in the tracked files (this harness and
        operators copy configs through the agent.conf symlink without
        committing), so the baseline is what the agents actually run. Returns
        None when no baseline could be taken; restore() is then a no-op.
        """
        try:
            if not self._is_repo() or self.head() is None:
                self._initialize_repo()
            head = self.head()
            if head is None:
                print("Warning: could not establish a config baseline commit")
                return None
            if self._worktree_dirty():
                print("Committing uncommitted config drift as the suite baseline")
                self._commit("fboss_test_runner: suite baseline snapshot")
                head = self.head()
            print(
                f"Suite config baseline: {head[:8] if head is not None else 'unknown'}"
            )
            return head
        except Exception as e:
            print(f"Warning: failed to capture config baseline: {e}")
            return None

    def restore(self, baseline: str | None) -> bool:
        """Restore the config to ``baseline`` and make sure the agents run it.

        Fast path when the config is unchanged and the agents are healthy.
        Soft path (`fboss2-dev --force config rollback <sha>`, applied by the CLI at
        the recorded action level) when the config drifted but the agents can
        serve thrift. Hard path (check out the tracked files at the baseline,
        commit, cold boot) when any agent unit is dead or the soft path failed.
        Returns False if the agents could not be brought back.
        """
        if not baseline:
            print("No config baseline captured; skipping suite restore")
            return True

        try:
            drifted = self._drifted(baseline)
        except Exception as e:
            print(f"Warning: failed to compare config against baseline: {e}")
            drifted = True

        healthy = self._units_healthy() and self._agent_responsive()
        if not drifted:
            if healthy:
                return True
            print(
                "########## Config matches baseline but agents are unhealthy; "
                "cold booting"
            )
            return self._restore_hard(baseline)

        print(
            f"########## Test suite left the config off its baseline "
            f"{baseline[:8]}; restoring"
        )
        if healthy and self._restore_soft(baseline):
            return True
        return self._restore_hard(baseline)

    def _restore_soft(self, baseline: str) -> bool:
        # A dirty tree is test debris; without --force the CLI refuses to
        # discard live config that differs from HEAD (it commits a snapshot).
        # Global flags must precede the subcommand.
        result = subprocess.run(
            [self._fboss2_cli, "--force", "config", "rollback", baseline],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            print(
                f"config rollback to baseline failed ({result.stderr.strip()}); "
                "falling back to hard restore"
            )
            return False
        if not self._agent_responsive():
            print("Agent not responsive after rollback; falling back to hard restore")
            return False
        return True

    def _restore_hard(self, baseline: str) -> bool:
        print(
            f"########## Hard-restoring config baseline {baseline[:8]} "
            "(check out tracked files + cold boot agents)"
        )
        try:
            at_baseline = set(self._files_at_revision(baseline))
            if at_baseline:
                self._git("checkout", baseline, "--", *sorted(at_baseline), check=True)
            for tracked in TRACKED_FILES:
                # Absent at the baseline (e.g. a BGP config a suite introduced):
                # remove it, as the CLI's own rollback would.
                if tracked not in at_baseline and os.path.exists(self._path(tracked)):
                    os.remove(self._path(tracked))
            for tracked, live in CONFIG_DOMAINS:
                tracked_path = self._path(tracked)
                live_path = self._path(live)
                if not os.path.exists(tracked_path):
                    if os.path.islink(live_path):
                        os.remove(live_path)
                    continue
                # The daemons read through a stable symlink; a device the CLI
                # has never committed on still has a plain file there.
                if not os.path.islink(live_path) or os.readlink(live_path) != tracked:
                    if os.path.lexists(live_path):
                        os.remove(live_path)
                    os.symlink(tracked, live_path)
            self._commit(f"fboss_test_runner: restore baseline {baseline[:8]}")
        except Exception as e:
            print(f"Warning: hard restore of config baseline failed: {e}")
            return False

        # A crash-looping unit may sit in "failed" with its start limit
        # exhausted; clear that so the restart is allowed to run.
        subprocess.run(["systemctl", "reset-failed"], check=False)
        try:
            self._cold_boot_agents()
        except Exception as e:
            print(f"Warning: cold boot after baseline restore failed: {e}")
        return self._agent_responsive()


def find_fboss2_cli() -> str:
    return shutil.which("fboss2-dev") or "/opt/fboss/bin/fboss2-dev"


def wait_agent_responsive(
    fboss2_cli: str,
    timeout_sec: int = AGENT_READY_TIMEOUT_SEC,
    poll_interval_sec: int = AGENT_POLL_INTERVAL_SEC,
) -> bool:
    """Poll a cheap thrift-backed CLI command until the sw_agent answers.

    Poll-first, so a responsive agent returns on the first iteration with no
    sleep. Two expected failure modes while an agent restarts -- "Connection
    refused" (not yet listening) and "switch is still initializing" (HW init in
    progress) -- both clear once the warmboot/coldboot finishes.
    """
    deadline = time.monotonic() + timeout_sec
    while True:
        result = subprocess.run(
            [fboss2_cli, "show", "product"], capture_output=True, text=True, check=False
        )
        if result.returncode == 0:
            return True
        if time.monotonic() >= deadline:
            return False
        print(f"Agent not ready yet, retrying in {poll_interval_sec}s...")
        time.sleep(poll_interval_sec)
