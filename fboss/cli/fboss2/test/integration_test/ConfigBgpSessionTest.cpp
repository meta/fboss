// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for BGP config *session* management, mirroring the agent
 * config session tests (e.g. ConfigSessionClearTest) but for the BGP domain.
 *
 * BGP global edits stage ~/.fboss2/bgp_config.json (no agent.conf), and
 * `config session commit` promotes it to /etc/coop/bgpcpp/bgpcpp.conf and
 * restarts bgpd. These tests exercise the session lifecycle around that:
 *   - clear (including a BGP-only session)
 *   - diff (a staged BGP change must be visible)
 *   - commit (bgpd is restarted)
 *   - rollback (the bgpd system config is restored)
 *
 * Per-attribute global config behavior lives in ConfigBgpGlobalTest.
 *
 * Requirements:
 *   - The fboss2-dev binary under test (config subcommand tree).
 *   - HOME is set (the session file lives under $HOME/.fboss2).
 *   - bgpd installed; commit/rollback tests skip if bgpd is not active.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

#include "fboss/cli/fboss2/test/integration_test/ConfigBgpTestBase.h"

namespace fs = std::filesystem;

using namespace facebook::fboss;
using ::testing::HasSubstr;
using ::testing::Not;

class ConfigBgpSessionTest : public ConfigBgpTestBase {};

// Staging a BGP global change creates a session file; `config session clear`
// removes it (a BGP-only session has no agent.conf to key off of).
TEST_F(ConfigBgpSessionTest, CreateAndClearBgpSession) {
  runBgpGlobal("graceful-restart-time", "55");
  ASSERT_TRUE(fs::exists(bgpSessionPath()))
      << "expected a staged BGP session after a global change";

  auto result = runCli({"config", "session", "clear"});
  ASSERT_EQ(result.exitCode, 0) << "stderr=" << result.stderr;

  EXPECT_FALSE(fs::exists(bgpSessionPath()))
      << "config session clear must remove the staged BGP config";
}

TEST_F(ConfigBgpSessionTest, ClearNonExistentBgpSession) {
  // Start fully clean: no agent session, no BGP session, no metadata.
  discardSession();
  clearBgpSession();

  auto result = runCli({"config", "session", "clear"});
  ASSERT_EQ(result.exitCode, 0) << "stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("No config session exists"));
}

// `config session diff` must surface a staged BGP global change.
TEST_F(ConfigBgpSessionTest, DiffShowsStagedBgpChange) {
  runBgpGlobal("graceful-restart-time", "77");

  auto result = runCli({"config", "session", "diff"});
  ASSERT_EQ(result.exitCode, 0) << "stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, Not(HasSubstr("No config session exists")));
  EXPECT_THAT(result.stdout, HasSubstr("graceful_restart_convergence_seconds"));
}

// Committing a staged BGP change restarts the bgpd daemon.
TEST_F(ConfigBgpSessionTest, CommitRestartsBgpDaemon) {
  if (bgpDaemonActiveState() != "active") {
    GTEST_SKIP() << "bgpd is not active on this DUT; skipping commit/restart "
                    "verification";
  }
  discardSession();
  const std::string pidBefore = bgpDaemonMainPid();

  runBgpGlobal("graceful-restart-time", "123");
  ASSERT_FALSE(commitAndGetSha().empty());

  EXPECT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return to active after commit; state="
      << bgpDaemonActiveState();

  const std::string pidAfter = bgpDaemonMainPid();
  EXPECT_NE(pidAfter, "0") << "bgpd has no MainPID after commit";
  EXPECT_NE(pidAfter, pidBefore) << "bgpd MainPID unchanged (" << pidBefore
                                 << "); expected a restart on commit";
}

// Rolling back a committed BGP change restores the bgpd system config and
// restarts bgpd (not just the agent config).
TEST_F(ConfigBgpSessionTest, RollbackRestoresBgpConfig) {
  if (bgpDaemonActiveState() != "active") {
    GTEST_SKIP() << "bgpd is not active on this DUT; skipping rollback "
                    "verification";
  }
  discardSession();

  auto convergenceSeconds = [&]() {
    return readSystemBgpConfig()["graceful_restart_convergence_seconds"]
        .asInt();
  };

  // Commit A (111), then commit B (222).
  runBgpGlobal("graceful-restart-time", "111");
  std::string shaA = commitAndGetSha();
  ASSERT_FALSE(shaA.empty()) << "could not parse commit A sha";
  ASSERT_TRUE(waitForBgpDaemonActive());

  runBgpGlobal("graceful-restart-time", "222");
  ASSERT_FALSE(commitAndGetSha().empty()) << "could not parse commit B sha";
  ASSERT_TRUE(waitForBgpDaemonActive());

  EXPECT_EQ(convergenceSeconds(), 222)
      << "system bgp config should reflect the latest commit";

  // Roll back to commit A -> system bgp config restored to 111, bgpd restart.
  resetBgpDaemonLimit(); // avoid systemd start-limit on the rollback's restart
  auto rb = runCli({"config", "rollback", shaA});
  EXPECT_EQ(rb.exitCode, 0) << "stderr=" << rb.stderr;
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after rollback";

  EXPECT_EQ(convergenceSeconds(), 111)
      << "rollback must restore the bgpd system config";
}

// Committing a BGP config identical to what is already running must be a no-op:
// "Nothing to commit" and, crucially, NO bgpd restart (a restart is traffic
// affecting). saveBgpConfig() always records SERVICE_RESTART, so commit() has
// to compare the staged config against the running one.
TEST_F(ConfigBgpSessionTest, CommitUnchangedBgpConfigDoesNotRestart) {
  if (bgpDaemonActiveState() != "active") {
    GTEST_SKIP() << "bgpd is not active on this DUT; skipping no-op restart "
                    "verification";
  }
  discardSession();

  // Establish a known running value.
  runBgpGlobal("graceful-restart-time", "88");
  ASSERT_FALSE(commitAndGetSha().empty());
  ASSERT_TRUE(waitForBgpDaemonActive());
  const std::string pidBefore = bgpDaemonMainPid();
  ASSERT_NE(pidBefore, "0");

  // Stage the SAME value and commit again -> nothing to commit, no restart.
  runBgpGlobal("graceful-restart-time", "88");
  auto result = runCli({"config", "session", "commit"});
  EXPECT_EQ(result.exitCode, 0) << "stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Nothing to commit"))
      << "an unchanged BGP commit should report nothing to commit";

  EXPECT_EQ(bgpDaemonMainPid(), pidBefore)
      << "bgpd was restarted for an unchanged BGP config (pidBefore="
      << pidBefore << ")";
}

// An agent-only commit (no BGP staged) must still snapshot the running bgpd
// config, so a rollback to it restores BGP instead of wiping it. Pre-fix,
// bgpcpp.conf was untracked at such commits and rollback deleted it.
TEST_F(ConfigBgpSessionTest, AgentCommitSnapshotsBgpAndRollbackPreservesIt) {
  if (bgpDaemonActiveState() != "active") {
    GTEST_SKIP() << "bgpd is not active on this DUT; skipping cross-domain "
                    "rollback verification";
  }
  discardSession();
  clearBgpSession();

  // The running (default) BGP config that must survive a rollback unchanged.
  ASSERT_TRUE(fs::exists(systemBgpConfigPath()))
      << "expected a running bgpd config on an active-BGP DUT";
  const folly::dynamic defaultBgp = readSystemBgpConfig();

  // Step 1: an agent-only commit (interface description). No BGP is staged.
  Interface intf = getInterfaceInfo(getRandomInterfacePortName());
  auto setRes =
      runCli({"config", "interface", intf.name, "description", "bgp-rb-test"});
  ASSERT_EQ(setRes.exitCode, 0) << "stderr=" << setRes.stderr;
  ASSERT_FALSE(fs::exists(bgpSessionPath()))
      << "an interface-description change must not stage a BGP session";
  resetBgpDaemonLimit();
  commitConfig();
  waitForAgentReady();
  const std::string agentSha = gitHead();
  ASSERT_FALSE(agentSha.empty())
      << "could not read git HEAD after agent commit";

  // The fix: bgpcpp.conf must be tracked at the agent-only commit, so a later
  // rollback to it has a faithful BGP snapshot to restore.
  EXPECT_TRUE(bgpTrackedAtRevision(agentSha))
      << "agent-only commit " << agentSha
      << " did not snapshot bgpcpp.conf; a rollback to it would wipe BGP";

  // Step 2: stage + commit a BGP change so the running config diverges from the
  // snapshot captured at agentSha.
  runBgpGlobal("graceful-restart-time", "222");
  ASSERT_FALSE(commitAndGetSha().empty()) << "BGP commit produced no SHA";
  ASSERT_TRUE(waitForBgpDaemonActive());
  EXPECT_EQ(
      readSystemBgpConfig()["graceful_restart_convergence_seconds"].asInt(),
      222);

  // Step 3: roll back to the agent-only commit. BGP must be RESTORED to the
  // original default snapshot (not deleted), and bgpd must come back.
  resetBgpDaemonLimit();
  auto rb = runCli({"config", "rollback", agentSha});
  EXPECT_EQ(rb.exitCode, 0) << "stderr=" << rb.stderr;
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after rollback";

  EXPECT_TRUE(fs::exists(systemBgpConfigPath()))
      << "rollback to an agent-era commit wiped the running bgpd config";
  EXPECT_EQ(readSystemBgpConfig(), defaultBgp)
      << "rollback did not restore the original (default) bgpd config";
}
