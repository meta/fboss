// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gtest/gtest.h>
#include <filesystem>

#include "fboss/cli/fboss2/commands/config/history/CmdConfigHistory.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
const std::string initialConfigContents = R"({
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
})";
}

class CmdConfigHistoryTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigHistoryTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_history_test_%%%%-%%%%-%%%%-%%%%", // unique_path
            initialConfigContents) {}
};

TEST_F(CmdConfigHistoryTestFixture, historyListsRevisions) {
  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";

  // Second commit
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 200000}]}})");
  git().commit({"cli/agent.conf"}, "Second commit");

  // Third commit
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 300000}]}})");
  git().commit({"cli/agent.conf"}, "Third commit");

  setupTestableConfigSession();

  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  EXPECT_NE(result.find("Initial commit"), std::string::npos);
  EXPECT_NE(result.find("Second commit"), std::string::npos);
  EXPECT_NE(result.find("Third commit"), std::string::npos);
  EXPECT_NE(result.find("Commit"), std::string::npos);
  EXPECT_NE(result.find("Author"), std::string::npos);
  EXPECT_NE(result.find("Commit Time"), std::string::npos);
  EXPECT_NE(result.find("Message"), std::string::npos);

  // Verify the timestamp is formatted correctly (not epoch).
  // Git returns Unix timestamps in seconds, so if the code incorrectly
  // treats them as nanoseconds, we'd see dates near the Unix epoch.
  // Depending on timezone, epoch could show as 1970-01-01 or 1969-12-31.
  EXPECT_EQ(result.find("1970-"), std::string::npos)
      << "Timestamp appears to be incorrectly parsed (showing 1970 epoch)";
  EXPECT_EQ(result.find("1969-"), std::string::npos)
      << "Timestamp appears to be incorrectly parsed (showing 1969 epoch)";
  // Check that the current year appears in the output
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  std::string currentYear = std::to_string(1900 + tm.tm_year);
  EXPECT_NE(result.find(currentYear + "-"), std::string::npos)
      << "Expected timestamp with year " << currentYear << ", got: " << result;
}

TEST_F(CmdConfigHistoryTestFixture, historyIgnoresNonMatchingFiles) {
  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";

  // Second commit for cli/agent.conf
  createTestConfig(
      cliConfigPath,
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 200000}]}})");
  git().commit({"cli/agent.conf"}, "Config update");

  // Create and commit a different file (should not appear in history)
  createTestConfig(getTestEtcDir() / "coop" / "other.txt", "other content");
  git().commit({"other.txt"}, "Other file commit");

  setupTestableConfigSession();

  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  EXPECT_NE(result.find("Initial commit"), std::string::npos);
  EXPECT_NE(result.find("Config update"), std::string::npos);
  // The "Other file commit" should not appear since it doesn't touch agent.conf
  EXPECT_EQ(result.find("Other file commit"), std::string::npos);
}

TEST_F(CmdConfigHistoryTestFixture, historyEmptyDirectory) {
  // Directory exists but has no revision files
  EXPECT_TRUE(cliConfigDirExists());

  // Remove the initial revision file created by SetUp() to simulate empty dir
  removeInitialRevisionFile();

  // Initialize ConfigSession singleton with test paths
  setupTestableConfigSession();

  // Create and execute the command
  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  // Verify the output indicates only an initial commit
  EXPECT_NE(result.find("Initial commit"), std::string::npos);
}

TEST_F(CmdConfigHistoryTestFixture, historyShowsCommitShas) {
  // Initialize ConfigSession singleton with test paths
  setupTestableConfigSession();

  // Create and execute the command
  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  // Verify the output contains a commit SHA (8 hex characters)
  // The SHA should be in the first column
  bool foundSha = false;
  for (size_t i = 0; i + 7 < result.size(); ++i) {
    bool isHex = true;
    for (size_t j = 0; j < 8 && isHex; ++j) {
      char c = result[i + j];
      if (!std::isxdigit(c)) {
        isHex = false;
      }
    }
    if (isHex) {
      foundSha = true;
      break;
    }
  }
  EXPECT_TRUE(foundSha) << "Expected to find a commit SHA in the output";
}

TEST_F(CmdConfigHistoryTestFixture, historyMultipleCommits) {
  std::filesystem::path cliConfigPath = getCliConfigDir() / "agent.conf";

  // Create 5 more commits
  for (int i = 2; i <= 6; ++i) {
    createTestConfig(
        cliConfigPath,
        fmt::format(
            R"({{"sw": {{"ports": [{{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": {}}}]}}}})",
            i * 100000));
    git().commit({"cli/agent.conf"}, fmt::format("Commit {}", i));
  }

  setupTestableConfigSession();

  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  EXPECT_NE(result.find("Commit 6"), std::string::npos);
  EXPECT_NE(result.find("Commit 5"), std::string::npos);
  EXPECT_NE(result.find("Commit 4"), std::string::npos);
  EXPECT_NE(result.find("Commit 3"), std::string::npos);
  EXPECT_NE(result.find("Commit 2"), std::string::npos);
  EXPECT_NE(result.find("Initial commit"), std::string::npos);

  // Verify they appear in reverse chronological order (most recent first)
  auto pos_6 = result.find("Commit 6");
  auto pos_5 = result.find("Commit 5");
  auto pos_initial = result.find("Initial commit");
  EXPECT_LT(pos_6, pos_5);
  EXPECT_LT(pos_5, pos_initial);
}

TEST_F(CmdConfigHistoryTestFixture, historyDoesNotCreateSessionFiles) {
  setupReadOnlyTestableConfigSession();

  std::filesystem::path sessionDir = getTestHomeDir() / ".fboss2";
  ASSERT_FALSE(std::filesystem::exists(sessionDir));

  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  EXPECT_NE(result.find("Initial commit"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(getSessionConfigPath()));
  EXPECT_FALSE(std::filesystem::exists(sessionDir / "cli_metadata.json"));
  EXPECT_FALSE(std::filesystem::exists(sessionDir / "bgp_config.json"));
  EXPECT_FALSE(ConfigSession::getInstance().hasActiveSession());
}

TEST_F(CmdConfigHistoryTestFixture, historyLeavesExistingSessionUntouched) {
  const std::string staged =
      R"({"sw": {"ports": [{"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 400000}]}})";
  std::filesystem::create_directories(getSessionConfigPath().parent_path());
  createTestConfig(getSessionConfigPath(), staged);

  setupReadOnlyTestableConfigSession();

  auto cmd = CmdConfigHistory();
  auto result = cmd.queryClient(localhost());

  EXPECT_NE(result.find("Initial commit"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(getSessionConfigPath()));
  EXPECT_EQ(readFile(getSessionConfigPath()), staged);
  EXPECT_TRUE(ConfigSession::getInstance().hasActiveSession());
}

TEST_F(CmdConfigHistoryTestFixture, defaultSessionDefersServiceConfig) {
  ASSERT_FALSE(std::filesystem::exists(getSessionConfigPath()));

  setupTestableConfigSession();

  EXPECT_FALSE(std::filesystem::exists(getSessionConfigPath()));
  EXPECT_FALSE(ConfigSession::getInstance().hasActiveSession());
}

TEST_F(CmdConfigHistoryTestFixture, printOutput) {
  auto cmd = CmdConfigHistory();
  std::string tableOutput =
      "Commit    Author  Commit Time          Message\nabcd1234  user1   2024-01-01 12:00:00  Initial commit";

  // Redirect cout to capture output
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

  cmd.printOutput(tableOutput);

  // Restore cout
  std::cout.rdbuf(old);

  std::string output = buffer.str();

  EXPECT_NE(output.find("Commit"), std::string::npos);
  EXPECT_NE(output.find("abcd1234"), std::string::npos);
  EXPECT_NE(output.find("user1"), std::string::npos);
}

} // namespace facebook::fboss
