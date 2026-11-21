// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <gtest/gtest.h>
#include <filesystem>

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionDiff.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

#include <string>
#include <vector>

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigSessionDiffTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigSessionDiffTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_session_diff_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ]
  }
})") {}
};

TEST_F(CmdConfigSessionDiffTestFixture, diffNoSession) {
  // Initialize the session (which creates the session config file)
  setupTestableConfigSession();

  // Then delete the session config file to simulate "no session" case
  std::filesystem::remove(getSessionConfigPath());

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});

  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_EQ(result, "No config session exists. Make a config change first.");
}

TEST_F(CmdConfigSessionDiffTestFixture, diffIdenticalConfigs) {
  setupTestableConfigSession();
  ConfigSession::getInstance().getAgentConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});

  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_NE(
      result.find(
          "No differences between current live config and session config"),
      std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffDifferentConfigs) {
  setupTestableConfigSession();

  // Create session directory and modify the config
  std::filesystem::create_directories(getSessionConfigPath().parent_path());
  createTestConfig(
      getSessionConfigPath(),
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "description": "Modified port",
        "state": 2,
        "speed": 100000
      }
    ]
  }
})");

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});

  auto result = cmd.queryClient(localhost(), emptyRevisions);

  // Should show a diff with the added "description" field
  EXPECT_NE(result.find("description"), std::string::npos);
  EXPECT_NE(result.find("Modified port"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffSessionVsRevision) {
  setupTestableConfigSession();

  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  // Create a commit with state: 1
  createTestConfig(
      cliConfigPath,
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 1
      }
    ]
  }
})");

  std::string commitSha = git().commit({cliConfigPath.string()}, "State 1");

  // Create session with different content
  std::filesystem::create_directories(getSessionConfigPath().parent_path());
  createTestConfig(
      getSessionConfigPath(),
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2
      }
    ]
  }
})");

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList revisions(std::vector<std::string>{commitSha});

  auto result = cmd.queryClient(localhost(), revisions);

  // Should show a diff between the commit and session (state changed from 1 to
  // 2)
  EXPECT_NE(result.find("-        \"state\": 1"), std::string::npos);
  EXPECT_NE(result.find("+        \"state\": 2"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffTwoRevisions) {
  setupTestableConfigSession();

  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  // Create first commit with state: 2
  createTestConfig(
      cliConfigPath,
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2
      }
    ]
  }
})");
  std::string commit1 = git().commit({cliConfigPath.string()}, "Commit 1");

  // Create second commit with description added
  createTestConfig(
      cliConfigPath,
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "description": "Added description",
        "state": 2
      }
    ]
  }
})");
  std::string commit2 = git().commit({cliConfigPath.string()}, "Commit 2");

  setupTestableConfigSession();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList revisions(std::vector<std::string>{commit1, commit2});

  auto result = cmd.queryClient(localhost(), revisions);

  // Should show the added "description" field
  EXPECT_NE(result.find("description"), std::string::npos);
  EXPECT_NE(result.find("Added description"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffWithCurrentKeyword) {
  // Remove the initial revision file created by SetUp() to simulate empty dir
  removeInitialRevisionFile();
  setupTestableConfigSession();

  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  // Create a commit with state: 1
  createTestConfig(
      cliConfigPath,
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 1
      }
    ]
  }
})");
  std::string commit1 = git().commit({cliConfigPath.string()}, "State 1");

  // Update system config to state: 2 with speed (this is "current")
  createTestConfig(
      cliConfigPath,
      R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ]
  }
})");
  git().commit({cliConfigPath.string()}, "Current state");

  setupTestableConfigSession();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList revisions(std::vector<std::string>{commit1, "current"});

  auto result = cmd.queryClient(localhost(), revisions);

  // Should show a diff between commit1 and current system config (state changed
  // from 1 to 2, speed added)
  EXPECT_NE(result.find("-        \"state\": 1"), std::string::npos);
  EXPECT_NE(result.find("+        \"state\": 2"), std::string::npos);
  EXPECT_NE(result.find("+        \"speed\": 100000"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffNonexistentRevision) {
  setupTestableConfigSession();
  ConfigSession::getInstance().getAgentConfig();

  auto cmd = CmdConfigSessionDiff();
  // Use a fake SHA that doesn't exist
  utils::RevisionList revisions(
      std::vector<std::string>{"0000000000000000000000000000000000000000"});

  // Should throw an exception for nonexistent revision
  // Git throws runtime_error when the commit doesn't exist
  EXPECT_THROW(cmd.queryClient(localhost(), revisions), std::runtime_error);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffTooManyArguments) {
  setupTestableConfigSession();

  auto cmd = CmdConfigSessionDiff();
  // Use fake SHAs for the test
  utils::RevisionList revisions(
      std::vector<std::string>{
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
          "cccccccccccccccccccccccccccccccccccccccc"});

  // Should throw an exception for too many arguments
  EXPECT_THROW(cmd.queryClient(localhost(), revisions), std::invalid_argument);
}

TEST_F(CmdConfigSessionDiffTestFixture, printOutput) {
  auto cmd = CmdConfigSessionDiff();
  std::string diffOutput =
      "--- a/config\n+++ b/config\n@@ -1 +1 @@\n-old\n+new";

  // Redirect cout to capture output
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

  cmd.printOutput(diffOutput);

  // Restore cout
  std::cout.rdbuf(old);

  std::string output = buffer.str();

  EXPECT_NE(output.find("--- a/config"), std::string::npos);
  EXPECT_NE(output.find("+++ b/config"), std::string::npos);
  EXPECT_NE(output.find("-old"), std::string::npos);
  EXPECT_NE(output.find("+new"), std::string::npos);
}

TEST_F(
    CmdConfigSessionDiffTestFixture,
    diffNoSessionDoesNotCreateSessionFiles) {
  setupReadOnlyTestableConfigSession();

  std::filesystem::path sessionDir = getTestHomeDir() / ".fboss2";
  ASSERT_FALSE(std::filesystem::exists(sessionDir));

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_EQ(result, "No config session exists. Make a config change first.");
  EXPECT_FALSE(std::filesystem::exists(getSessionConfigPath()));
  EXPECT_FALSE(std::filesystem::exists(sessionDir / "cli_metadata.json"));
  EXPECT_FALSE(std::filesystem::exists(sessionDir / "bgp_config.json"));
}

TEST_F(
    CmdConfigSessionDiffTestFixture,
    diffTwoRevisionsDoesNotCreateSessionFiles) {
  auto commits = git().log((getCliConfigDir() / "agent.conf").string());
  std::string firstCommit = commits.back().sha1;

  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 200000}]}})");
  std::string secondCommit = git().commit({"cli/agent.conf"}, "Second commit");

  setupReadOnlyTestableConfigSession();
  auto& session = static_cast<TestableConfigSession&>(
      ConfigSession::getInstance(ConfigSession::SessionInit::ReadOnly));
  session.setConfigPathResolver([](cli::ServiceType) -> std::string {
    ADD_FAILURE() << "two-revision diff must not query a service";
    return {};
  });

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList revisions(
      std::vector<std::string>{firstCommit, secondCommit});
  auto result = cmd.queryClient(localhost(), revisions);

  EXPECT_NE(result.find("200000"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(getTestHomeDir() / ".fboss2"));
}

TEST_F(CmdConfigSessionDiffTestFixture, diffReadOnlyStillSeesStagedSession) {
  const std::string staged =
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 400000}]}})";
  std::filesystem::create_directories(getSessionConfigPath().parent_path());
  createTestConfig(getSessionConfigPath(), staged);

  setupReadOnlyTestableConfigSession();
  int agentQueries = 0;
  int bgpQueries = 0;
  auto& session = static_cast<TestableConfigSession&>(
      ConfigSession::getInstance(ConfigSession::SessionInit::ReadOnly));
  session.setConfigPathResolver([&](cli::ServiceType service) {
    if (service == cli::ServiceType::AGENT) {
      ++agentQueries;
      return getSystemConfigPath().string();
    }
    ++bgpQueries;
    return (getTestEtcDir() / "coop/bgpcpp.conf").string();
  });

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_NE(result.find("400000"), std::string::npos);
  EXPECT_EQ(readFile(getSessionConfigPath()), staged);
  EXPECT_EQ(agentQueries, 1);
  EXPECT_EQ(bgpQueries, 0);
}

TEST_F(CmdConfigSessionDiffTestFixture, printOutputNoDifferences) {
  auto cmd = CmdConfigSessionDiff();
  std::string noDiffMessage =
      "No differences between current live config and session config.";

  // Redirect cout to capture output
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

  cmd.printOutput(noDiffMessage);

  // Restore cout
  std::cout.rdbuf(old);

  std::string output = buffer.str();

  EXPECT_NE(output.find("No differences"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffBgpSession) {
  setupTestableConfigSession();
  // Remove the agent session so only a BGP session is staged.
  std::filesystem::remove(getSessionConfigPath());

  auto& session = ConfigSession::getInstance();
  session.getBgpConfig().router_id() = "9.9.9.9";
  session.saveBgpConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  // A BGP-only session must be visible (not gated out as "no session"), and
  // the staged router_id must show up in the diff.
  EXPECT_EQ(result.find("No config session exists"), std::string::npos);
  EXPECT_NE(result.find("9.9.9.9"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffBgpSessionHasOnlyTheUserChange) {
  setupTestableConfigSession();
  std::filesystem::remove(getSessionConfigPath());
  // The keys are not in alphabetical order.
  createTestConfig(
      getTestEtcDir() / "coop/bgpcpp.conf",
      R"({"router_id": "1.1.1.1", "peers": [{"peer_addr": "10.0.0.1",
"local_addr": "10.0.0.0", "bgp_peer_timers": {"keep_alive_seconds": 30,
"hold_time_seconds": 90}}]})");

  auto& session = ConfigSession::getInstance();
  session.getBgpConfig()
      .peers()
      ->at(0)
      .bgp_peer_timers()
      .ensure()
      .hold_time_seconds() = 45;
  session.saveBgpConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  std::vector<std::string> lines;
  folly::split('\n', result, lines);
  int changedLines = 0;
  for (const auto& line : lines) {
    if (line.empty() || (line[0] != '+' && line[0] != '-') ||
        line.starts_with("+++") || line.starts_with("---")) {
      continue;
    }
    ++changedLines;
    EXPECT_NE(line.find("hold_time_seconds"), std::string::npos) << line;
  }
  EXPECT_EQ(changedLines, 2) << result;
  EXPECT_NE(
      result.find("--- current live config (normalized)"), std::string::npos);
  EXPECT_NE(result.find("+++ session config\n"), std::string::npos);
  EXPECT_EQ(result.find("WARNING"), std::string::npos);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffWarnsAboutUnknownKey) {
  setupTestableConfigSession();
  std::filesystem::remove(getSessionConfigPath());
  createTestConfig(
      getTestEtcDir() / "coop/bgpcpp.conf",
      R"({"router_id": "1.1.1.1", "peers": [{"peer_addr": "10.0.0.1",
"local_addr": "10.0.0.0", "not_in_schema": 1}]})");

  auto& session = ConfigSession::getInstance();
  session.getBgpConfig().router_id() = "9.9.9.9";
  session.saveBgpConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_TRUE(result.starts_with(
      "WARNING: 1 key(s) that are not in the CLI schema are different in "
      "the 2 texts. The diff does not show them and a commit adds, changes or "
      "drops them: peers[0].not_in_schema\n"))
      << result;
  EXPECT_EQ(
      cmd.queryClient(
          localhost(),
          utils::RevisionList(std::vector<std::string>{"current"})),
      result);
}

TEST_F(CmdConfigSessionDiffTestFixture, diffNoWarningWhenSessionHasNoChange) {
  setupTestableConfigSession();
  std::filesystem::remove(getSessionConfigPath());
  createTestConfig(
      getTestEtcDir() / "coop/bgpcpp.conf",
      R"({"router_id": "1.1.1.1", "not_in_schema": 1})");

  auto& session = ConfigSession::getInstance();
  session.getBgpConfig();
  session.saveBgpConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_TRUE(result.starts_with("No differences between ")) << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffWarnsForBareLiveWithNoChange) {
  setupTestableConfigSession();
  createTestConfig(
      getCliConfigDir() / "agent.conf",
      R"({"ports": [{"logicalID": 1, "name": "eth1/1/1", "not_in_schema": 1}]})");
  std::filesystem::remove(getSessionConfigPath());

  auto& session = ConfigSession::getInstance();
  session.getAgentConfig();
  session.saveConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_TRUE(result.starts_with("WARNING: 1 key(s) ")) << result;
  EXPECT_NE(
      result.find(": ports[0].not_in_schema\nNo differences between "),
      std::string::npos)
      << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffNoWarningForRawCopySession) {
  setupTestableConfigSession();
  createTestConfig(
      getCliConfigDir() / "agent.conf",
      R"({"sw": {"not_in_schema": 1, "ports": [{"logicalID": 1, "name": "eth1/1/1"}]}})");
  std::filesystem::remove(getSessionConfigPath());
  ConfigSession::getInstance().getAgentConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_TRUE(result.starts_with("No differences between ")) << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffWarnsAboutAgentKeys) {
  setupTestableConfigSession();
  createTestConfig(
      getCliConfigDir() / "agent.conf",
      R"({"sw": {"live_key": 1, "ports": [{"logicalID": 1}]}})");
  std::filesystem::create_directories(getSessionConfigPath().parent_path());
  createTestConfig(
      getSessionConfigPath(),
      R"({"sw": {"ports": [{"logicalID": 2, "session_key": 1}]}})");

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_TRUE(result.starts_with(
      "WARNING: 2 key(s) that are not in the CLI schema are different in "
      "the 2 texts. The diff does not show them and a commit adds, changes or "
      "drops them: sw.live_key, sw.ports[0].session_key\n"))
      << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffWarnsAboutBareSwitchConfigKey) {
  setupTestableConfigSession();
  createTestConfig(
      getCliConfigDir() / "agent.conf",
      R"({"ports": [{"logicalID": 1, "name": "eth1/1/1", "not_in_schema": 1}]})");
  std::filesystem::remove(getSessionConfigPath());

  auto& session = ConfigSession::getInstance();
  session.getAgentConfig().sw()->ports()->at(0).description() = "new text";
  session.saveConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  EXPECT_NE(result.find(": ports[0].not_in_schema\n"), std::string::npos)
      << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffTwoRevisionsUnknownKey) {
  setupTestableConfigSession();
  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  const auto commitConfig = [&](int keyValue, int logicalId) {
    createTestConfig(
        cliConfigPath,
        fmt::format(
            R"({{"sw": {{"not_in_schema": {}, "ports": [{{"logicalID": {}}}]}}}})",
            keyValue,
            logicalId));
    return git().commit({cliConfigPath.string()}, "Commit");
  };
  const std::string commit1 = commitConfig(5, 1);
  const std::string sameKey = commitConfig(5, 2);
  const std::string otherValue = commitConfig(6, 2);
  setupTestableConfigSession();
  auto cmd = CmdConfigSessionDiff();

  auto result = cmd.queryClient(
      localhost(),
      utils::RevisionList(std::vector<std::string>{commit1, sameKey}));
  EXPECT_EQ(result.find("WARNING"), std::string::npos) << result;
  EXPECT_NE(result.find("logicalID"), std::string::npos) << result;

  result = cmd.queryClient(
      localhost(),
      utils::RevisionList(std::vector<std::string>{sameKey, otherValue}));
  EXPECT_TRUE(result.starts_with(
      "WARNING: 1 key(s) that are not in the CLI schema are different in "
      "the 2 texts. The diff does not show them: sw.not_in_schema\n"))
      << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffAgentBareSwitchConfig) {
  setupTestableConfigSession();
  // The keys are not in alphabetical order.
  createTestConfig(
      getCliConfigDir() / "agent.conf",
      R"({"ports": [{"speed": 100000, "state": 2, "name": "eth1/1/1",
"logicalID": 1}]})");
  std::filesystem::remove(getSessionConfigPath());

  auto& session = ConfigSession::getInstance();
  session.getAgentConfig().sw()->ports()->at(0).description() = "new text";
  session.saveConfig();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList emptyRevisions(std::vector<std::string>{});
  auto result = cmd.queryClient(localhost(), emptyRevisions);

  std::vector<std::string> lines;
  folly::split('\n', result, lines);
  int changedLines = 0;
  for (const auto& line : lines) {
    if (line.empty() || (line[0] != '+' && line[0] != '-') ||
        line.starts_with("+++") || line.starts_with("---")) {
      continue;
    }
    ++changedLines;
    EXPECT_NE(line.find("description"), std::string::npos) << line;
  }
  EXPECT_EQ(changedLines, 1) << result;
  EXPECT_EQ(result.find("WARNING"), std::string::npos) << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, diffTwoRevisionsIsNormalized) {
  setupTestableConfigSession();
  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1"}]}})");
  std::string commit1 = git().commit({cliConfigPath.string()}, "Commit 1");
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"name": "eth1/1/1", "logicalID": 1}]}})");
  std::string commit2 = git().commit({cliConfigPath.string()}, "Commit 2");
  setupTestableConfigSession();

  auto cmd = CmdConfigSessionDiff();
  utils::RevisionList revisions(std::vector<std::string>{commit1, commit2});
  auto result = cmd.queryClient(localhost(), revisions);

  EXPECT_TRUE(result.starts_with("No differences between ")) << result;
}

TEST_F(CmdConfigSessionDiffTestFixture, normalizedConfigJsonKeepsBadText) {
  const std::string text = "{not json";
  testing::internal::CaptureStderr();
  EXPECT_EQ(
      ConfigSession::normalizedConfigJson(cli::ServiceType::AGENT, text), text);
  EXPECT_TRUE(
      testing::internal::GetCapturedStderr().starts_with(
          "Warning: The AGENT config text did not parse ("));
  EXPECT_EQ(
      ConfigSession::normalizedConfigJson(cli::ServiceType::BGP, text), text);

  testing::internal::CaptureStderr();
  EXPECT_EQ(ConfigSession::normalizedConfigJson(cli::ServiceType::BGP, ""), "");
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

} // namespace facebook::fboss
