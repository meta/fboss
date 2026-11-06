// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

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

} // namespace facebook::fboss
