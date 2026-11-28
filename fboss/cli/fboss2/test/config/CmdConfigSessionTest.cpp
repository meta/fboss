// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/test/config/MockFbossServiceUtil.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"

namespace fs = std::filesystem;

namespace facebook::fboss {

class ConfigSessionTestFixture : public CmdConfigTestBase {
 public:
  ConfigSessionTestFixture()
      : CmdConfigTestBase(
            "fboss_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      },
      {
        "logicalID": 2,
        "name": "eth1/1/2",
        "state": 2,
        "speed": 100000
      }
    ]
  }
})") {}

  std::unique_ptr<TestableConfigSession> makeStrictSession() {
    return std::make_unique<TestableConfigSession>(
        (getTestHomeDir() / ".fboss2").string(),
        (getTestEtcDir() / "coop").string(),
        std::make_unique<::testing::StrictMock<MockFbossServiceUtil>>());
  }

  void useClassicBgpSystemd(TestableConfigSession& session) {
    session.setMockSystemdFactory([] {
      auto systemd =
          std::make_unique<::testing::NiceMock<MockSystemdInterface>>();
      ON_CALL(*systemd, getMatchingServices("bgpd.service"))
          .WillByDefault(
              ::testing::Return(std::vector<std::string>{"bgpd.service"}));
      return systemd;
    });
  }

  std::string commitDescription(const std::string& description) {
    auto mock = std::make_unique<::testing::StrictMock<MockFbossServiceUtil>>();
    auto* mockPtr = mock.get();
    EXPECT_CALL(
        *mockPtr,
        validateConfig(
            cli::ServiceType::AGENT, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);
    EXPECT_CALL(*mockPtr, reloadConfig(cli::ServiceType::AGENT, ::testing::_))
        .Times(1);
    TestableConfigSession session(
        (getTestHomeDir() / ".fboss2").string(),
        (getTestEtcDir() / "coop").string(),
        std::move(mock));
    session.setCommandLine(
        "config interface eth1/1/1 description " + description);
    (*session.getAgentConfig().sw()->ports())[0].description() = description;
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result = session.commit(localhost());
    EXPECT_FALSE(result.commitSha.empty());
    return result.commitSha;
  }
};

TEST_F(ConfigSessionTestFixture, sessionInitialization) {
  // Initially, session directory should not exist
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";
  EXPECT_FALSE(fs::exists(sessionDir));

  // Construction creates shared metadata but does not load either service.
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  EXPECT_TRUE(fs::exists(sessionDir));
  EXPECT_FALSE(session.sessionExists());
  EXPECT_FALSE(fs::exists(sessionConfig));

  // The first agent access materializes only the agent session.
  session.getAgentConfig();
  EXPECT_TRUE(session.sessionExists());
  std::string systemContent = readFile(cliConfigPath);
  std::string sessionContent = readFile(sessionConfig);
  EXPECT_EQ(systemContent, sessionContent);
}

TEST_F(
    ConfigSessionTestFixture,
    discoversCachesAndPersistsCurrentConfigPathLazily) {
  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path externalDir = getTestEtcDir() / "runtime";
  const fs::path externalConfig = externalDir / "agent_config";
  fs::create_directories(externalDir);
  fs::create_symlink(getSystemConfigPath(), externalConfig);

  int agentQueries = 0;
  int bgpQueries = 0;
  TestableConfigSession session(
      sessionDir.string(),
      coopDir.string(),
      ConfigSession::SessionInit::ReadOnly);
  session.setConfigPathResolver([&](cli::ServiceType service) {
    if (service == cli::ServiceType::AGENT) {
      ++agentQueries;
      return externalConfig.string();
    }
    ++bgpQueries;
    throw std::runtime_error("BGP should not be queried");
  });

  EXPECT_EQ(
      session.getCurrentConfigPath(cli::ServiceType::AGENT),
      getSystemConfigPath().string());
  EXPECT_EQ(
      session.getCurrentConfigPath(cli::ServiceType::AGENT),
      getSystemConfigPath().string());
  EXPECT_EQ(agentQueries, 1);
  EXPECT_EQ(bgpQueries, 0);

  session.setCommandLine("config interface eth1/1/1 speed 100G");
  session.recordServiceAction(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  const auto metadata =
      folly::parseJson(readFile(sessionDir / "cli_metadata.json"));
  EXPECT_EQ(
      metadata["currentConfigPaths"]["AGENT"].asString(),
      getSystemConfigPath().string());
}

TEST_F(ConfigSessionTestFixture, fallsBackToCommittedCurrentConfigPath) {
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path currentPath = coopDir / "agent/current";
  createTestConfig(
      getCliConfigDir() / "cli_metadata.json",
      fmt::format(
          R"({{"currentConfigPaths":{{"AGENT":"{}"}}}})",
          currentPath.string()));

  TestableConfigSession session(
      (getTestHomeDir() / ".fboss2").string(),
      coopDir.string(),
      ConfigSession::SessionInit::ReadOnly);
  session.setConfigPathResolver([](cli::ServiceType) -> std::string {
    throw std::runtime_error("service unavailable");
  });

  EXPECT_EQ(
      session.getCurrentConfigPath(cli::ServiceType::AGENT),
      currentPath.string());
}

TEST_F(ConfigSessionTestFixture, acceptsLegacyMetadataWithoutConfigPaths) {
  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::create_directories(sessionDir);
  createTestConfig(sessionDir / "agent.conf", readFile(getSystemConfigPath()));
  createTestConfig(
      sessionDir / "cli_metadata.json",
      R"({"action":{"BGP":"SERVICE_RESTART"},"commands":[],"base":"legacy"})");

  TestableConfigSession session(
      sessionDir.string(),
      (getTestEtcDir() / "coop").string(),
      ConfigSession::SessionInit::ReadOnly);
  session.setConfigPathResolver(
      [&](cli::ServiceType) { return getSystemConfigPath().string(); });

  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::BGP),
      cli::ConfigActionLevel::SERVICE_RESTART);
  EXPECT_EQ(
      session.getCurrentConfigPath(cli::ServiceType::AGENT),
      getSystemConfigPath().string());
}

TEST_F(ConfigSessionTestFixture, sessionConfigModified) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Modify the session config through the ConfigSession API
  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());
  ports[0].description() = "Modified port";
  session.setCommandLine("config interface eth1/1/1 description Modified port");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Verify session config is modified
  std::string sessionContent = readFile(sessionConfig);
  std::string systemContent = readFile(cliConfigPath);
  EXPECT_NE(sessionContent, systemContent);
  EXPECT_THAT(sessionContent, ::testing::HasSubstr("Modified port"));
}

TEST_F(ConfigSessionTestFixture, sessionCommit) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Setup mock agent server
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(2);

  std::string firstCommitSha;
  std::string secondCommitSha;

  // First commit: Create a ConfigSession and commit a change
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Simulate a CLI command being tracked
    session.setCommandLine(
        "config interface eth1/1/1 description First commit");

    // Modify the session config
    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "First commit";
    session.setCommandLine(
        "config interface eth1/1/1 description First commit");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    // Commit the session
    auto result = session.commit(localhost());

    // Verify session config no longer exists (removed after commit)
    EXPECT_FALSE(fs::exists(sessionConfig));

    // Verify commit SHA was returned
    EXPECT_FALSE(result.commitSha.empty());
    EXPECT_EQ(result.commitSha.length(), 40); // Full SHA1 is 40 chars
    firstCommitSha = result.commitSha;

    // Verify metadata file was created alongside the config revision
    fs::path targetMetadata =
        getTestEtcDir() / "coop" / "cli" / "cli_metadata.json";
    EXPECT_TRUE(fs::exists(targetMetadata));

    // Verify system config was updated
    EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("First commit"));
  }

  // Second commit: Create a new session and verify it's based on first commit
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Simulate a CLI command being tracked
    session.setCommandLine(
        "config interface eth1/1/1 description Second commit");

    // Verify the new session is based on the latest committed revision
    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    EXPECT_EQ(*ports[0].description(), "First commit");

    // Make another change to the same port
    ports[0].description() = "Second commit";
    session.setCommandLine(
        "config interface eth1/1/1 description Second commit");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    // Commit the second change
    auto result = session.commit(localhost());

    // Verify new commit SHA was returned
    EXPECT_FALSE(result.commitSha.empty());
    EXPECT_NE(result.commitSha, firstCommitSha);
    secondCommitSha = result.commitSha;

    // Verify metadata file was created alongside the config revision
    fs::path targetMetadata =
        getTestEtcDir() / "coop" / "cli" / "cli_metadata.json";
    EXPECT_TRUE(fs::exists(targetMetadata));

    // Verify system config was updated
    EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("Second commit"));

    // Verify Git history has all commits
    auto& git = session.getGit();
    auto commits = git.log(cliConfigPath.string());
    EXPECT_EQ(commits.size(), 3); // Initial + 2 commits

    // Verify metadata file was also committed to git
    auto metadataCommits = git.log(targetMetadata.string());
    EXPECT_EQ(metadataCommits.size(), 3); // Initial baseline + 2 commits
  }
}

TEST_F(
    ConfigSessionTestFixture,
    commitRepairsMissingCurrentPathWithoutConfigChange) {
  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path current = coopDir / "agent.conf";
  const fs::path desired = coopDir / "cli/agent.conf";

  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  TestableConfigSession session(sessionDir.string(), coopDir.string());
  int agentQueries = 0;
  int bgpQueries = 0;
  session.setConfigPathResolver([&](cli::ServiceType service) {
    if (service == cli::ServiceType::AGENT) {
      ++agentQueries;
      return current.string();
    }
    ++bgpQueries;
    throw std::runtime_error("BGP should not be queried");
  });
  session.getAgentConfig();
  session.setCommandLine("config interface eth1/1/1 speed 100G");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  ASSERT_TRUE(fs::remove(current));

  const auto result = session.commit(localhost());
  EXPECT_FALSE(result.commitSha.empty());
  EXPECT_TRUE(fs::is_symlink(current));
  EXPECT_EQ(
      fs::read_symlink(current),
      desired.lexically_relative(current.parent_path()));
  EXPECT_EQ(readFile(current), readFile(desired));
  EXPECT_NO_THROW(
      session.getGit().fileAtRevision(result.commitSha, "agent.conf"));
  EXPECT_EQ(agentQueries, 1);
  EXPECT_EQ(bgpQueries, 0);
}

TEST_F(ConfigSessionTestFixture, bgpOnlyCommitDoesNotResolveAgent) {
  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path bgpCurrent = coopDir / "bgpcpp.conf";
  const fs::path bgpDesired = coopDir / "bgpcpp/bgpcpp.conf";
  createTestConfig(bgpCurrent, R"({"router_id":"1.1.1.1"})");

  TestableConfigSession session(sessionDir.string(), coopDir.string());
  useClassicBgpSystemd(session);
  int agentQueries = 0;
  int bgpQueries = 0;
  session.setConfigPathResolver([&](cli::ServiceType service) {
    if (service == cli::ServiceType::BGP) {
      ++bgpQueries;
      return bgpCurrent.string();
    }
    ++agentQueries;
    throw std::runtime_error("agent should not be queried");
  });

  session.getBgpConfig().router_id() = "2.2.2.2";
  session.setCommandLine("config protocol bgp global router-id 2.2.2.2");
  session.saveBgpConfig();
  EXPECT_FALSE(session.commit(localhost()).commitSha.empty());

  EXPECT_EQ(agentQueries, 0);
  EXPECT_EQ(bgpQueries, 1);
  EXPECT_THAT(readFile(bgpCurrent), ::testing::HasSubstr("2.2.2.2"));
  EXPECT_THAT(readFile(bgpDesired), ::testing::HasSubstr("2.2.2.2"));
}

TEST_F(ConfigSessionTestFixture, stagedConfigLoadsWithoutServiceDiscovery) {
  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::create_directories(sessionDir);
  createTestConfig(
      sessionDir / "agent.conf",
      R"({"sw":{"ports":[{"logicalID":1,"name":"eth1/1/1","description":"staged","state":2,"speed":100000}]}})");
  createTestConfig(
      sessionDir / "cli_metadata.json",
      R"({"action":{},"commands":[],"base":""})");

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());
  session.setConfigPathResolver([](cli::ServiceType) -> std::string {
    throw std::runtime_error("service unavailable");
  });

  EXPECT_EQ(
      *session.getAgentConfig().sw()->ports()->at(0).description(), "staged");
}

// Ensure commit() works on a newly initialized session
// This verifies that initializeSession() creates the metadata file
TEST_F(ConfigSessionTestFixture, commitOnNewlyInitializedSession) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigDir = getTestEtcDir() / "coop" / "cli";

  // Setup mock agent server
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // Create a new session
  // This tests that metadata file is created during session initialization
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Verify metadata file was created during session initialization
  fs::path metadataPath = sessionDir / "cli_metadata.json";
  EXPECT_TRUE(fs::exists(metadataPath));

  // Make a change so commit has something to commit
  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());
  ports[0].description() = "Test change for commit";
  session.setCommandLine(
      "config interface eth1/1/1 description Test change for commit");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Commit should succeed
  auto result = session.commit(localhost());
  EXPECT_FALSE(result.commitSha.empty());

  // Verify metadata file was copied to CLI config directory
  fs::path targetMetadata = cliConfigDir / "cli_metadata.json";
  EXPECT_TRUE(fs::exists(targetMetadata));
}

TEST_F(ConfigSessionTestFixture, multipleChangesInOneSession) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Make first change
  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());
  ports[0].description() = "Change 1";
  session.setCommandLine("config interface eth1/1/1 description Change 1");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  EXPECT_THAT(readFile(sessionConfig), ::testing::HasSubstr("Change 1"));

  // Make second change
  ports[0].description() = "Change 2";
  session.setCommandLine("config interface eth1/1/1 description Change 2");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  EXPECT_THAT(readFile(sessionConfig), ::testing::HasSubstr("Change 2"));

  // Make third change
  ports[0].description() = "Change 3";
  session.setCommandLine("config interface eth1/1/1 description Change 3");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  EXPECT_THAT(readFile(sessionConfig), ::testing::HasSubstr("Change 3"));
}

TEST_F(ConfigSessionTestFixture, sessionPersistsAcrossCommands) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";

  // Create first ConfigSession and modify config
  {
    TestableConfigSession session1(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session1.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "Persistent change";
    session1.setCommandLine(
        "config interface eth1/1/1 description Persistent change");
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  }

  // Verify session persists (file still exists with same content)
  EXPECT_TRUE(fs::exists(sessionConfig));
  EXPECT_THAT(
      readFile(sessionConfig), ::testing::HasSubstr("Persistent change"));

  // Create another ConfigSession to simulate another command reading the
  // session
  {
    TestableConfigSession session2(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session2.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    // Verify the change persisted
    EXPECT_EQ(*ports[0].description(), "Persistent change");
  }
}

TEST_F(ConfigSessionTestFixture, configRollbackOnFailure) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path coopDir = getTestEtcDir() / "coop";
  fs::path cliConfigPath = coopDir / "cli" / "agent.conf";
  fs::path systemConfigPath = coopDir / "agent.conf";
  fs::path originalCurrent = coopDir / "agent" / "current";

  // Save the original config content
  std::string originalContent = readFile(cliConfigPath);
  fs::create_directories(originalCurrent.parent_path());
  createTestConfig(originalCurrent, originalContent);
  ASSERT_TRUE(fs::remove(systemConfigPath));
  const auto originalTarget = fs::path("agent/current");
  fs::create_symlink(originalTarget, systemConfigPath);

  // Setup mock agent server to fail reloadConfig on first call (the commit),
  // but succeed on second call (the rollback reload)
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig())
      .WillOnce(::testing::Throw(std::runtime_error("Reload failed")))
      .WillOnce(::testing::Return());

  // Create a ConfigSession and try to commit
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());
  ports[0].description() = "Failed change";
  session.setCommandLine("config interface eth1/1/1 description Failed change");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Commit should fail and rollback the config
  EXPECT_THROW(session.commit(localhost()), std::runtime_error);

  // Verify config was rolled back to original content
  std::string currentContent = readFile(cliConfigPath);
  EXPECT_EQ(currentContent, originalContent);
  EXPECT_TRUE(fs::is_symlink(systemConfigPath));
  EXPECT_EQ(fs::read_symlink(systemConfigPath), originalTarget);
  EXPECT_EQ(readFile(systemConfigPath), originalContent);

  // Verify session config still exists (not removed on failed commit)
  EXPECT_TRUE(fs::exists(sessionConfig));
}

TEST_F(ConfigSessionTestFixture, configSaveRejectsUnwritableDestination) {
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root bypasses directory permission bits";
  }

  const fs::path sessionDir = getTestHomeDir() / ".fboss2";
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path cliDir = coopDir / "cli";
  TestableConfigSession session(sessionDir.string(), coopDir.string());
  auto& config = session.getAgentConfig();
  (*config.sw()->ports())[0].description() = "Must not be staged";
  fs::permissions(
      cliDir,
      fs::perms::owner_read | fs::perms::owner_exec,
      fs::perm_options::replace);

  EXPECT_THROW(
      session.saveConfig(
          cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS),
      std::system_error);
  EXPECT_THAT(
      readFile(session.getSessionConfigPath()),
      ::testing::Not(::testing::HasSubstr("Must not be staged")));

  fs::permissions(cliDir, fs::perms::owner_all, fs::perm_options::replace);
}

TEST_F(ConfigSessionTestFixture, concurrentCommits) {
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Setup mock agent server
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(2);

  // Run two sequential commits to test Git commit functionality
  // Note: Git doesn't handle truly concurrent commits well due to index.lock,
  // so we run them sequentially to avoid race conditions.
  std::string commitSha1;
  std::string commitSha2;

  // First commit
  {
    fs::path sessionDir = getTestHomeDir() / ".fboss2_user1";

    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "First commit";
    session.setCommandLine(
        "config interface eth1/1/1 description First commit");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    auto result = session.commit(localhost());
    commitSha1 = result.commitSha;
  }

  // Second commit
  {
    fs::path sessionDir = getTestHomeDir() / ".fboss2_user2";

    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "Second commit";
    session.setCommandLine(
        "config interface eth1/1/1 description Second commit");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    auto result = session.commit(localhost());
    commitSha2 = result.commitSha;
  }

  // Both commits should succeed with different commit SHAs
  EXPECT_FALSE(commitSha1.empty());
  EXPECT_FALSE(commitSha2.empty());
  EXPECT_NE(commitSha1, commitSha2);

  // Verify Git history contains both commits
  Git git((getTestEtcDir() / "coop").string());
  auto commits = git.log(cliConfigPath.string());
  EXPECT_GE(commits.size(), 3); // Initial + 2 commits
}

TEST_F(ConfigSessionTestFixture, rollbackToSpecificCommit) {
  // This test calls the rollback() method with a specific commit SHA
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";
  fs::path metadataPath =
      getTestEtcDir() / "coop" / "cli" / "cli_metadata.json";

  // Setup mock agent server
  setupMockedAgentServer();
  // 2 commits + 1 rollback = 3 reloadConfig calls
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(3);

  // Create a session and make several commits to build history
  std::string firstCommitSha;
  std::string secondCommitSha;
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Simulate CLI command for first commit
    session.setCommandLine(
        "config interface eth1/1/1 description First version");

    // First commit
    auto& config1 = session.getAgentConfig();
    (*config1.sw()->ports())[0].description() = "First version";
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result1 = session.commit(localhost());
    firstCommitSha = result1.commitSha;

    // Second commit (need new session after commit)
  }
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Simulate CLI command for second commit
    session.setCommandLine(
        "config interface eth1/1/1 description Second version");

    auto& config2 = session.getAgentConfig();
    (*config2.sw()->ports())[0].description() = "Second version";
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result2 = session.commit(localhost());
    secondCommitSha = result2.commitSha;
  }

  // Verify current content is "Second version"
  EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("Second version"));
  // Verify current metadata contains second command
  EXPECT_THAT(
      readFile(metadataPath), ::testing::HasSubstr("description Second"));

  // Now rollback to first commit
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    session.setConfigPathResolver([](cli::ServiceType service) -> std::string {
      if (service == cli::ServiceType::BGP) {
        throw std::runtime_error("BGP should not be queried");
      }
      throw std::runtime_error("agent path should come from metadata");
    });

    std::string rollbackSha = session.rollback(localhost(), firstCommitSha);

    // Verify rollback created a new commit
    EXPECT_FALSE(rollbackSha.empty());
    EXPECT_NE(rollbackSha, firstCommitSha);
    EXPECT_NE(rollbackSha, secondCommitSha);

    // Verify config content is now "First version"
    EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("First version"));

    // Verify system metadata (in /etc/coop) was also rolled back to first
    // version
    std::string metadataContent = readFile(metadataPath);
    EXPECT_THAT(metadataContent, ::testing::HasSubstr("description First"));
    EXPECT_THAT(
        metadataContent,
        ::testing::Not(::testing::HasSubstr("description Second")));

    // Rollback does not materialize an otherwise absent service session.
    fs::path sessionConfigPath = sessionDir / "agent.conf";
    EXPECT_FALSE(fs::exists(sessionConfigPath));

    // Verify session metadata was updated with the new base and empty commands
    fs::path sessionMetadataPath = sessionDir / "cli_metadata.json";
    std::string sessionMetadataContent = readFile(sessionMetadataPath);
    folly::dynamic sessionMetadata = folly::parseJson(sessionMetadataContent);
    // The base should be set to the rollback commit SHA
    EXPECT_EQ(sessionMetadata["base"].asString(), rollbackSha)
        << "Session metadata base should be set to the rollback commit SHA";
    // Commands should be empty (clean session)
    EXPECT_TRUE(sessionMetadata["commands"].isArray())
        << "Session metadata commands should be an array";
    EXPECT_EQ(sessionMetadata["commands"].size(), 0)
        << "Session metadata commands array should be empty after rollback to "
           "clean session";

    // Verify Git history has the rollback commit
    auto& git = session.getGit();
    auto commits = git.log(cliConfigPath.string());
    EXPECT_EQ(commits.size(), 4); // Initial + 2 commits + rollback

    // Verify metadata file history
    auto metadataCommits = git.log(metadataPath.string());
    EXPECT_EQ(
        metadataCommits.size(), 4); // Initial baseline + 2 commits + rollback
  }
}

TEST_F(ConfigSessionTestFixture, rollbackToPreviousCommit) {
  // This test calls the rollback() method without a commit SHA argument
  // to rollback to the previous commit
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Setup mock agent server
  setupMockedAgentServer();
  // 2 commits + 1 rollback = 3 reloadConfig calls
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(3);

  // Create commits to build history
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config1 = session.getAgentConfig();
    (*config1.sw()->ports())[0].description() = "First version";
    session.setCommandLine(
        "config interface eth1/1/1 description First version");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    session.commit(localhost());
  }
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config2 = session.getAgentConfig();
    (*config2.sw()->ports())[0].description() = "Second version";
    session.setCommandLine(
        "config interface eth1/1/1 description Second version");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    session.commit(localhost());
  }

  // Verify current content is "Second version"
  EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("Second version"));

  // Rollback to previous commit (no argument)
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    std::string rollbackSha = session.rollback(localhost());

    // Verify rollback succeeded
    EXPECT_FALSE(rollbackSha.empty());

    // Verify content is now "First version" (from previous commit)
    EXPECT_THAT(readFile(cliConfigPath), ::testing::HasSubstr("First version"));

    // Rollback does not materialize an otherwise absent service session.
    fs::path sessionConfigPath = sessionDir / "agent.conf";
    EXPECT_FALSE(fs::exists(sessionConfigPath));

    // Verify session metadata was updated with the new base and empty commands
    fs::path sessionMetadataPath = sessionDir / "cli_metadata.json";
    std::string sessionMetadataContent = readFile(sessionMetadataPath);
    folly::dynamic sessionMetadata = folly::parseJson(sessionMetadataContent);
    // The base should be set to the rollback commit SHA
    EXPECT_EQ(sessionMetadata["base"].asString(), rollbackSha)
        << "Session metadata base should be set to the rollback commit SHA";
    // Commands should be empty (clean session)
    EXPECT_TRUE(sessionMetadata["commands"].isArray())
        << "Session metadata commands should be an array";
    EXPECT_EQ(sessionMetadata["commands"].size(), 0)
        << "Session metadata commands array should be empty after rollback to "
           "clean session";
  }
}

TEST_F(ConfigSessionTestFixture, actionLevelDefaultIsHitless) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Default action level should be HITLESS
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::HITLESS);
}

TEST_F(ConfigSessionTestFixture, actionLevelUpdateAndGet) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Update to SERVICE_RESTART
  session.updateRequiredAction(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  // Verify the action level was updated
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(ConfigSessionTestFixture, actionLevelHigherTakesPrecedence) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Update to SERVICE_RESTART first
  session.updateRequiredAction(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  // Try to "downgrade" to HITLESS - should be ignored
  session.updateRequiredAction(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Verify action level remains at SERVICE_RESTART
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(ConfigSessionTestFixture, actionLevelReset) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Set to SERVICE_RESTART
  session.updateRequiredAction(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  // Reset the action level
  session.resetRequiredAction(cli::ServiceType::AGENT);

  // Verify action level was reset to HITLESS
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::HITLESS);
}

TEST_F(ConfigSessionTestFixture, actionLevelPersistsToMetadataFile) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path metadataFile = sessionDir / "cli_metadata.json";

  // Create a ConfigSession and set action level via saveConfig
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Load the config (required before saveConfig)
    session.getAgentConfig();
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);
  }

  // Verify metadata file exists and has correct JSON format
  EXPECT_TRUE(fs::exists(metadataFile));
  std::string content = readFile(metadataFile);

  // Parse the JSON and verify structure - uses symbolic enum names
  folly::dynamic json = folly::parseJson(content);
  EXPECT_TRUE(json.isObject());
  EXPECT_TRUE(json.count("action"));
  EXPECT_TRUE(json["action"].isObject());
  EXPECT_TRUE(json["action"].count("AGENT"));
  EXPECT_EQ(json["action"]["AGENT"].asString(), "SERVICE_RESTART");
}

TEST_F(ConfigSessionTestFixture, actionLevelLoadsFromMetadataFile) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path metadataFile = sessionDir / "cli_metadata.json";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Create session directory and metadata file manually
  fs::create_directories(sessionDir);
  std::ofstream metaFile(metadataFile);
  // Use symbolic enum names for human readability
  metaFile << R"({"action":{"AGENT":"SERVICE_RESTART"}})";
  metaFile.close();

  // Also create the session config file (otherwise session will overwrite from
  // system)
  fs::copy_file(cliConfigPath, sessionConfig);

  // Create a ConfigSession - should load action level from metadata file
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Verify action level was loaded
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(ConfigSessionTestFixture, actionLevelPersistsAcrossSessions) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // First session: set action level via saveConfig
  {
    TestableConfigSession session1(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Load the config (required before saveConfig)
    session1.getAgentConfig();
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);
  }

  // Second session: verify action level was persisted
  {
    TestableConfigSession session2(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    EXPECT_EQ(
        session2.getRequiredAction(cli::ServiceType::AGENT),
        cli::ConfigActionLevel::SERVICE_RESTART);
  }
}

TEST_F(ConfigSessionTestFixture, commandTrackingBasic) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path metadataFile = sessionDir / "cli_metadata.json";

  // Create a ConfigSession, execute command, and verify persistence
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    // Initially, no commands should be recorded
    EXPECT_TRUE(session.getCommands().empty());

    // Simulate a command and save config
    session.setCommandLine("config interface eth1/1/1 description Test change");
    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "Test change";
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    // Verify command was recorded in memory
    EXPECT_EQ(1, session.getCommands().size());
    EXPECT_EQ(
        "config interface eth1/1/1 description Test change",
        session.getCommands()[0]);
  }

  // Verify metadata file exists and has commands persisted
  EXPECT_TRUE(fs::exists(metadataFile));
  std::string content = readFile(metadataFile);

  // Parse the JSON and verify structure
  folly::dynamic json = folly::parseJson(content);
  EXPECT_TRUE(json.isObject());
  EXPECT_TRUE(json.count("commands"));
  EXPECT_TRUE(json["commands"].isArray());
  EXPECT_EQ(1, json["commands"].size());
  EXPECT_EQ(
      "config interface eth1/1/1 description Test change",
      json["commands"][0].asString());
}

TEST_F(ConfigSessionTestFixture, commandTrackingMultipleCommands) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Execute multiple commands
  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());

  session.setCommandLine("config interface eth1/1/1 mtu 9000");
  ports[0].description() = "First change";
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  session.setCommandLine("config interface eth1/1/1 description Test");
  ports[0].description() = "Second change";
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  session.setCommandLine("config interface eth1/1/1 speed 100G");
  ports[0].description() = "Third change";
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Verify all commands were recorded in order
  EXPECT_EQ(3, session.getCommands().size());
  EXPECT_EQ("config interface eth1/1/1 mtu 9000", session.getCommands()[0]);
  EXPECT_EQ(
      "config interface eth1/1/1 description Test", session.getCommands()[1]);
  EXPECT_EQ("config interface eth1/1/1 speed 100G", session.getCommands()[2]);
}

TEST_F(ConfigSessionTestFixture, commandTrackingPersistsAcrossSessions) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // First session: execute some commands
  {
    TestableConfigSession session1(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session1.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());

    session1.setCommandLine("config interface eth1/1/1 mtu 9000");
    ports[0].description() = "First change";
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    session1.setCommandLine("config interface eth1/1/1 description Test");
    ports[0].description() = "Second change";
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  }

  // Second session: verify commands were persisted
  {
    TestableConfigSession session2(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    EXPECT_EQ(2, session2.getCommands().size());
    EXPECT_EQ("config interface eth1/1/1 mtu 9000", session2.getCommands()[0]);
    EXPECT_EQ(
        "config interface eth1/1/1 description Test",
        session2.getCommands()[1]);
  }
}

TEST_F(ConfigSessionTestFixture, commandTrackingClearedOnReset) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Create a ConfigSession and add some commands
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());

  session.setCommandLine("config interface eth1/1/1 mtu 9000");
  ports[0].description() = "Test change";
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  EXPECT_EQ(1, session.getCommands().size());

  // Reset the action level (which also clears commands)
  session.resetRequiredAction(cli::ServiceType::AGENT);

  // Verify commands were cleared
  EXPECT_TRUE(session.getCommands().empty());
}

TEST_F(ConfigSessionTestFixture, commandTrackingLoadsFromMetadataFile) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path sessionConfig = sessionDir / "agent.conf";
  fs::path metadataFile = sessionDir / "cli_metadata.json";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Create session directory and metadata file manually
  fs::create_directories(sessionDir);
  std::ofstream metaFile(metadataFile);
  metaFile << R"({
    "action": {"AGENT": "HITLESS"},
    "commands": ["cmd1", "cmd2", "cmd3"]
  })";
  metaFile.close();

  // Also create the session config file
  fs::copy_file(cliConfigPath, sessionConfig);

  // Create a ConfigSession - should load commands from metadata file
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Verify commands were loaded
  EXPECT_EQ(3, session.getCommands().size());
  EXPECT_EQ("cmd1", session.getCommands()[0]);
  EXPECT_EQ("cmd2", session.getCommands()[1]);
  EXPECT_EQ("cmd3", session.getCommands()[2]);
}

// Test that concurrent sessions are detected and rejected
// Scenario: user1 and user2 both start sessions based on the same commit,
// user1 commits first, then user2 tries to commit and should fail.
TEST_F(ConfigSessionTestFixture, concurrentSessionConflict) {
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";

  // Setup mock agent server
  setupMockedAgentServer();
  // Only user1's commit should succeed, so only 1 reloadConfig call
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // User1 creates a session (captures current HEAD as base)
  TestableConfigSession session1(
      sessionDir1.string(), (getTestEtcDir() / "coop").string());

  // User2 also creates a session at the same time (same base)
  TestableConfigSession session2(
      sessionDir2.string(), (getTestEtcDir() / "coop").string());

  // User1 makes a change and commits
  auto& config1 = session1.getAgentConfig();
  (*config1.sw()->ports())[0].description() = "User1 change";
  session1.setCommandLine("config interface eth1/1/1 description User1 change");
  session1.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  auto result1 = session1.commit(localhost());
  EXPECT_FALSE(result1.commitSha.empty());

  // User2 makes a different change
  auto& config2 = session2.getAgentConfig();
  (*config2.sw()->ports())[0].description() = "User2 change";
  session2.setCommandLine("config interface eth1/1/1 description User2 change");
  session2.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // User2 tries to commit but should fail because user1 already committed
  EXPECT_THROW(
      {
        try {
          session2.commit(localhost());
        } catch (const std::runtime_error& e) {
          // Verify the error message mentions the conflict
          EXPECT_THAT(
              e.what(),
              ::testing::HasSubstr("system configuration has changed"));
          throw;
        }
      },
      std::runtime_error);

  // Verify that only user1's change is in the system config
  Git git((getTestEtcDir() / "coop").string());
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";
  std::string content;
  EXPECT_TRUE(folly::readFile(cliConfigPath.c_str(), content));
  EXPECT_THAT(content, ::testing::HasSubstr("User1 change"));
  EXPECT_THAT(content, ::testing::Not(::testing::HasSubstr("User2 change")));
}

// BGP analog of concurrentSessionConflict: two BGP sessions start from the same
// base; once user1 commits (advancing HEAD), user2's commit must be rejected
// because its base is now stale -- one session "steps onto" the other. Only
// user1's BGP change reaches the running bgpd config.
TEST_F(ConfigSessionTestFixture, concurrentBgpSessionConflict) {
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";
  fs::path bgpSys = getTestEtcDir() / "coop" / "bgpcpp" / "bgpcpp.conf";

  auto makeSession = [&](const fs::path& dir) {
    auto s = std::make_unique<TestableConfigSession>(
        dir.string(), (getTestEtcDir() / "coop").string());
    // BGP commits restart bgpd via systemd; mock it out. Only user1 commits, so
    // no agent reload is triggered (no mocked agent server needed).
    useClassicBgpSystemd(*s);
    return s;
  };

  // Both users start sessions at the same base (current HEAD).
  auto session1 = makeSession(sessionDir1);
  auto session2 = makeSession(sessionDir2);

  // User1 stages a BGP change and commits -> HEAD advances.
  session1->getBgpConfig().router_id() = "1.1.1.1";
  session1->setCommandLine("config protocol bgp global router-id 1.1.1.1");
  session1->saveBgpConfig();
  EXPECT_FALSE(session1->commit(localhost()).commitSha.empty());

  // User2 stages a different BGP change on top of the now-stale base.
  session2->getBgpConfig().router_id() = "2.2.2.2";
  session2->setCommandLine("config protocol bgp global router-id 2.2.2.2");
  session2->saveBgpConfig();

  // User2's commit must fail because user1 already advanced HEAD.
  EXPECT_THROW(
      {
        try {
          session2->commit(localhost());
        } catch (const std::runtime_error& e) {
          EXPECT_THAT(
              e.what(),
              ::testing::HasSubstr("system configuration has changed"));
          throw;
        }
      },
      std::runtime_error);

  // Only user1's BGP change reached the running bgpd config.
  std::string content;
  ASSERT_TRUE(folly::readFile(bgpSys.string().c_str(), content));
  EXPECT_THAT(content, ::testing::HasSubstr("1.1.1.1"));
  EXPECT_THAT(content, ::testing::Not(::testing::HasSubstr("2.2.2.2")));
}

TEST_F(ConfigSessionTestFixture, rebaseSuccessNoConflict) {
  // Test successful rebase when user2's changes don't conflict with user1's
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";

  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(2);

  // User1 creates a session
  TestableConfigSession session1(
      sessionDir1.string(), (getTestEtcDir() / "coop").string());

  // User2 also creates a session at the same time (same base)
  TestableConfigSession session2(
      sessionDir2.string(), (getTestEtcDir() / "coop").string());

  // User1 changes port[0] description and commits
  auto& config1 = session1.getAgentConfig();
  (*config1.sw()->ports())[0].description() = "User1 change";
  session1.setCommandLine("config interface eth1/1/1 description User1 change");
  session1.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  auto result1 = session1.commit(localhost());
  EXPECT_FALSE(result1.commitSha.empty());

  // User2 changes port[1] description (non-conflicting - different port)
  auto& config2 = session2.getAgentConfig();
  ASSERT_GE(config2.sw()->ports()->size(), 2) << "Need at least 2 ports";
  (*config2.sw()->ports())[1].description() = "User2 change";
  session2.setCommandLine("config interface eth1/1/2 description User2 change");
  session2.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // User2 tries to commit but fails due to stale base
  EXPECT_THROW(session2.commit(localhost()), std::runtime_error);

  // User2 rebases - should succeed since changes don't conflict
  EXPECT_NO_THROW(session2.rebase());

  // Now user2 can commit
  auto result2 = session2.commit(localhost());
  EXPECT_FALSE(result2.commitSha.empty());

  // Verify both changes are in the final config
  Git git((getTestEtcDir() / "coop").string());
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";
  std::string content;
  EXPECT_TRUE(folly::readFile(cliConfigPath.c_str(), content));
  EXPECT_THAT(content, ::testing::HasSubstr("User1 change"));
  EXPECT_THAT(content, ::testing::HasSubstr("User2 change"));
}

TEST_F(ConfigSessionTestFixture, rebaseFailsOnConflict) {
  // Test that rebase fails when user2's changes conflict with user1's
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";

  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // User1 creates a session
  TestableConfigSession session1(
      sessionDir1.string(), (getTestEtcDir() / "coop").string());

  // User2 also creates a session at the same time (same base)
  TestableConfigSession session2(
      sessionDir2.string(), (getTestEtcDir() / "coop").string());

  // User1 changes port[0] description to "User1 change"
  auto& config1 = session1.getAgentConfig();
  (*config1.sw()->ports())[0].description() = "User1 change";
  session1.setCommandLine("config interface eth1/1/1 description User1 change");
  session1.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  auto result1 = session1.commit(localhost());
  EXPECT_FALSE(result1.commitSha.empty());

  // User2 changes the SAME port[0] description to "User2 change" (conflict!)
  auto& config2 = session2.getAgentConfig();
  (*config2.sw()->ports())[0].description() = "User2 change";
  session2.setCommandLine("config interface eth1/1/1 description User2 change");
  session2.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // User2 tries to rebase but should fail due to conflict
  EXPECT_THROW(
      {
        try {
          session2.rebase();
        } catch (const std::runtime_error& e) {
          EXPECT_THAT(e.what(), ::testing::HasSubstr("conflict"));
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ConfigSessionTestFixture, rebaseNotNeeded) {
  // Test that rebase throws when session is already up-to-date
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Make a change but don't commit yet
  auto& config = session.getAgentConfig();
  (*config.sw()->ports())[0].description() = "My change";
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  // Try to rebase - should fail because we're already on HEAD
  EXPECT_THROW(
      {
        try {
          session.rebase();
        } catch (const std::runtime_error& e) {
          EXPECT_THAT(e.what(), ::testing::HasSubstr("No rebase needed"));
          throw;
        }
      },
      std::runtime_error);
}

// Tests 3-way merge algorithm through rebase() covering:
// - Only session changed (head == base)
// - Only head changed (session == base)
// - Both changed to same value (no conflict)
// - Both changed to different values (conflict)
TEST_F(ConfigSessionTestFixture, threeWayMergeScenarios) {
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  setupMockedAgentServer();
  // Reloads happen only when a commit actually changes the promoted config
  // (agent skip-when-unchanged): scenario 1 = 2 commits (both change config);
  // scenario 2's session2 rebases to the SAME value session1 committed, so its
  // commit is a no-op and does NOT reload -> only 1 reload there; scenario 3 =
  // 1 commit (session2's rebase throws before committing). Total = 2 + 1 + 1.
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(4);

  // Scenario 1: Only session changed, head unchanged
  // User1 commits, User2 changes different field - should merge cleanly
  {
    TestableConfigSession session1(
        sessionDir1.string(), (getTestEtcDir() / "coop").string());
    TestableConfigSession session2(
        sessionDir2.string(), (getTestEtcDir() / "coop").string());

    (*session1.getAgentConfig().sw()->ports())[0].name() = "port0_renamed";
    session1.setCommandLine("config interface eth1/1/1 name port0_renamed");
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result1 = session1.commit(localhost());
    EXPECT_FALSE(result1.commitSha.empty());

    (*session2.getAgentConfig().sw()->ports())[1].description() = "port1_desc";
    session2.setCommandLine("config interface eth1/1/2 description port1_desc");
    session2.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    EXPECT_NO_THROW(session2.rebase());
    auto result2 = session2.commit(localhost());
    EXPECT_FALSE(result2.commitSha.empty());

    std::string content;
    EXPECT_TRUE(folly::readFile(cliConfigPath.c_str(), content));
    EXPECT_THAT(content, ::testing::HasSubstr("port0_renamed"));
    EXPECT_THAT(content, ::testing::HasSubstr("port1_desc"));
  }

  // Scenario 2: Both changed same field to identical value - no conflict
  {
    TestableConfigSession session1(
        sessionDir1.string(), (getTestEtcDir() / "coop").string());
    TestableConfigSession session2(
        sessionDir2.string(), (getTestEtcDir() / "coop").string());

    (*session1.getAgentConfig().sw()->ports())[0].description() = "same_value";
    session1.setCommandLine("config interface eth1/1/1 description same_value");
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result1 = session1.commit(localhost());
    EXPECT_FALSE(result1.commitSha.empty());

    (*session2.getAgentConfig().sw()->ports())[0].description() = "same_value";
    session2.setCommandLine("config interface eth1/1/1 description same_value");
    session2.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    EXPECT_NO_THROW(session2.rebase());
    // Rebased session is identical to what session1 committed -> no-op commit
    // (empty sha, no reload).
    auto result2 = session2.commit(localhost());
    EXPECT_TRUE(result2.commitSha.empty());

    std::string content;
    EXPECT_TRUE(folly::readFile(cliConfigPath.c_str(), content));
    EXPECT_THAT(content, ::testing::HasSubstr("same_value"));
  }

  // Scenario 3: Both changed same field to different values - conflict
  {
    TestableConfigSession session1(
        sessionDir1.string(), (getTestEtcDir() / "coop").string());
    TestableConfigSession session2(
        sessionDir2.string(), (getTestEtcDir() / "coop").string());

    (*session1.getAgentConfig().sw()->ports())[0].description() = "user1_value";
    session1.setCommandLine(
        "config interface eth1/1/1 description user1_value");
    session1.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result1 = session1.commit(localhost());
    EXPECT_FALSE(result1.commitSha.empty());

    (*session2.getAgentConfig().sw()->ports())[0].description() = "user2_value";
    session2.setCommandLine(
        "config interface eth1/1/1 description user2_value");
    session2.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    EXPECT_THROW(
        {
          try {
            session2.rebase();
          } catch (const std::runtime_error& e) {
            EXPECT_THAT(e.what(), ::testing::HasSubstr("conflict"));
            throw;
          }
        },
        std::runtime_error);
  }
}

// Test that rebuildPortMap() regenerates the cached PortMap from the current
// in-memory agentConfig_, so a newly added port becomes visible.
TEST_F(ConfigSessionTestFixture, rebuildPortMap) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  const std::string newName = "eth1/1/3";

  // Add a new port to the in-memory config.
  auto& config = session.getAgentConfig();
  auto& ports = *config.sw()->ports();
  cfg::Port newPort;
  newPort.logicalID() = 3;
  newPort.name() = newName;
  ports.push_back(newPort);

  // The cached PortMap still reflects the config as loaded from disk, so the
  // new port is not visible yet.
  EXPECT_EQ(session.getPortMap().getPort(newName), nullptr);

  // After rebuilding, the new port is resolvable.
  session.rebuildPortMap();
  auto* port = session.getPortMap().getPort(newName);
  ASSERT_NE(port, nullptr);
  EXPECT_EQ(*port->name(), newName);
}

// Constructing a session no longer stages the agent config by itself.
TEST_F(ConfigSessionTestFixture, emptyCommit) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Setup mock agent server (should not be called for empty commit)
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  // Create a session but don't make any changes
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Get the current git HEAD before commit
  Git git((getTestEtcDir() / "coop").string());
  auto commitsBefore = git.log(cliConfigPath.string(), 10);

  EXPECT_THROW(session.commit(localhost()), std::runtime_error);

  // Verify no new git commit was created
  auto commitsAfter = git.log(cliConfigPath.string(), 10);
  EXPECT_EQ(commitsBefore.size(), commitsAfter.size());

  EXPECT_FALSE(session.sessionExists());
}

// Re-committing an agent config that is byte-identical to what is already
// promoted must be a no-op: no git revision and (crucially) no reloadConfig().
// A config command records an AGENT action even when it sets a field to its
// current value, so commit() must skip based on content equality (the same
// skip-when-unchanged rule BGP uses).
TEST_F(ConfigSessionTestFixture, commitUnchangedAgentConfigIsNoOp) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  setupMockedAgentServer();
  // The first (real) commit reloads once; the second (unchanged) commit must
  // NOT reload.
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // First commit: set a description and commit. This normalizes cli/agent.conf
  // into the canonical serialized form.
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    (*session.getAgentConfig().sw()->ports())[0].description() = "same_desc";
    session.setCommandLine("config interface eth1/1/1 description same_desc");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    ASSERT_FALSE(session.commit(localhost()).commitSha.empty());
  }

  // Second commit: set the SAME description again -> staged config is identical
  // to what is running, so the commit is a no-op (empty commitSha, no reload).
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    (*session.getAgentConfig().sw()->ports())[0].description() = "same_desc";
    session.setCommandLine("config interface eth1/1/1 description same_desc");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    auto result = session.commit(localhost());
    EXPECT_TRUE(result.commitSha.empty())
        << "re-committing an unchanged agent config should be a no-op (no reload)";
    EXPECT_EQ(result.actions.count(cli::ServiceType::AGENT), 0u)
        << "unchanged agent config must not apply a reload";
  }
}

// Test that committing twice in a row - second commit should be empty
TEST_F(ConfigSessionTestFixture, commitTwiceSecondIsEmpty) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  // Setup mock agent server (should only be called once for the first commit)
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // First commit: make a change and commit
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session.getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "First commit change";
    session.setCommandLine(
        "config interface eth1/1/1 description \"First commit change\"");
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    auto result = session.commit(localhost());
    EXPECT_FALSE(result.commitSha.empty());
    EXPECT_FALSE(result.actions.empty());
  }

  // A fresh invocation after commit has no staged session.
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    EXPECT_THROW(session.commit(localhost()), std::runtime_error);
  }
}

// Editing one part of the typed BGP config (e.g. a top-level field) and saving
// must round-trip the WHOLE config, so peers/peer-groups already staged in the
// file survive. ConfigSession owns the entire typed bgp::thrift::BgpConfig and
// is agnostic about which section a command touches.
TEST_F(ConfigSessionTestFixture, bgpConfigEditPreservesOtherSections) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  // Stage a config that already has peers/peer-groups (as another command would
  // have done via the same typed config).
  fs::path bgpPath = sessionDir / "bgp_config.json";
  folly::dynamic staged = folly::dynamic::object(
      "peers",
      folly::dynamic::array(
          folly::dynamic::object("peer_addr", "2401:db00::1")(
              "is_passive", true)))(
      "peer_groups",
      folly::dynamic::array(folly::dynamic::object("name", "RACK")));
  ASSERT_TRUE(folly::writeFile(folly::toPrettyJson(staged), bgpPath.c_str()));

  // Edit a top-level field via the typed API and persist.
  auto& bgp = session.getBgpConfig();
  bgp.router_id() = "10.0.0.1";
  bgp.count_confeds_in_as_path_len() = true;
  session.setCommandLine(
      "fboss2-dev config protocol bgp global router-id 10.0.0.1");
  session.saveBgpConfig();

  // The saved file carries the edited fields AND the untouched peers/groups.
  std::string content;
  ASSERT_TRUE(folly::readFile(bgpPath.c_str(), content));
  auto saved = folly::parseJson(content);

  EXPECT_EQ(saved["router_id"].asString(), "10.0.0.1");
  ASSERT_TRUE(saved.count("count_confeds_in_as_path_len"));
  EXPECT_TRUE(saved["count_confeds_in_as_path_len"].asBool());

  ASSERT_TRUE(saved.count("peers"));
  ASSERT_EQ(saved["peers"].size(), 1);
  EXPECT_EQ(saved["peers"][0]["peer_addr"].asString(), "2401:db00::1");
  EXPECT_TRUE(saved["peers"][0]["is_passive"].asBool());

  ASSERT_TRUE(saved.count("peer_groups"));
  ASSERT_EQ(saved["peer_groups"].size(), 1);
  EXPECT_EQ(saved["peer_groups"][0]["name"].asString(), "RACK");

  // A BGP restart must be recorded so `config session commit` applies it.
  // bgpd has no hitless reload (and no warmboot), so the recorded level is
  // SERVICE_RESTART (a plain service restart).
  EXPECT_EQ(
      session.getRequiredAction(cli::ServiceType::BGP),
      cli::ConfigActionLevel::SERVICE_RESTART);
}

// A staged BGP edit persists to disk and is seeded back by a fresh session via
// the typed view.
TEST_F(ConfigSessionTestFixture, bgpConfigEditPersistsAndSeeds) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path bgpPath = sessionDir / "bgp_config.json";
  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    auto& bgp = session.getBgpConfig();
    bgp.local_as_4_byte() = 65001;
    session.setCommandLine(
        "fboss2-dev config protocol bgp global local-asn 65001");
    session.saveBgpConfig();
  }
  ASSERT_TRUE(fs::exists(bgpPath));

  {
    TestableConfigSession session(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    auto& bgp = session.getBgpConfig();
    ASSERT_TRUE(bgp.local_as_4_byte().has_value());
    EXPECT_EQ(*bgp.local_as_4_byte(), 65001);
  }
}

// A service that is loaded after HEAD advances starts from that current HEAD,
// rather than inheriting a stale base from construction.
TEST_F(ConfigSessionTestFixture, lazyBgpSessionStartsFromCurrentHead) {
  fs::path sessionDir1 = getTestHomeDir() / ".fboss2_user1";
  fs::path sessionDir2 = getTestHomeDir() / ".fboss2_user2";

  setupMockedAgentServer();
  // Only user1's agent commit reloads the agent; rebase does not.
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  TestableConfigSession session1(
      sessionDir1.string(), (getTestEtcDir() / "coop").string());
  TestableConfigSession session2(
      sessionDir2.string(), (getTestEtcDir() / "coop").string());
  useClassicBgpSystemd(session2);

  // user1 commits an agent change -> HEAD advances, session2's base goes stale.
  (*session1.getAgentConfig().sw()->ports())[0].description() = "User1 change";
  session1.setCommandLine("config interface eth1/1/1 description User1");
  session1.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  EXPECT_FALSE(session1.commit(localhost()).commitSha.empty());

  // user2 stages a BGP change.
  session2.getBgpConfig().router_id() = "7.7.7.7";
  session2.setCommandLine("config protocol bgp global router-id 7.7.7.7");
  session2.saveBgpConfig();

  EXPECT_FALSE(session2.commit(localhost()).commitSha.empty());

  // The BGP change was committed from the current HEAD.
  std::string bgp;
  ASSERT_TRUE(
      folly::readFile(
          (getTestEtcDir() / "coop/bgpcpp/bgpcpp.conf").string().c_str(), bgp));
  EXPECT_THAT(bgp, ::testing::HasSubstr("7.7.7.7"));
  // The unrelated agent domain was never materialized in user2's session.
  EXPECT_FALSE(fs::exists(sessionDir2 / "agent.conf"));
}

// Rolling back to an earlier commit must restore the BGP system config and
// restart bgpd (not just the agent config).
TEST_F(ConfigSessionTestFixture, rollbackBgpConfig) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path bgpSys = getTestEtcDir() / "coop" / "bgpcpp" / "bgpcpp.conf";

  auto makeSession = [&]() {
    auto s = std::make_unique<TestableConfigSession>(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    // BGP commits/rollback restart bgpd via systemd; mock it out.
    useClassicBgpSystemd(*s);
    return s;
  };

  std::string sha1;
  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "1.1.1.1";
    s->setCommandLine("config protocol bgp global router-id 1.1.1.1");
    s->saveBgpConfig();
    sha1 = s->commit(localhost()).commitSha;
    ASSERT_FALSE(sha1.empty());
  }
  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "2.2.2.2";
    s->setCommandLine("config protocol bgp global router-id 2.2.2.2");
    s->saveBgpConfig();
    ASSERT_FALSE(s->commit(localhost()).commitSha.empty());
  }

  // System BGP config now reflects the latest commit.
  std::string content;
  ASSERT_TRUE(folly::readFile(bgpSys.string().c_str(), content));
  EXPECT_THAT(content, ::testing::HasSubstr("2.2.2.2"));

  // Roll back to the first commit -> BGP config restored to 1.1.1.1.
  {
    auto s = makeSession();
    std::string rb = s->rollback(localhost(), sha1);
    EXPECT_FALSE(rb.empty());
  }
  ASSERT_TRUE(folly::readFile(bgpSys.string().c_str(), content));
  EXPECT_THAT(content, ::testing::HasSubstr("1.1.1.1"));
  EXPECT_THAT(content, ::testing::Not(::testing::HasSubstr("2.2.2.2")));
}

// Re-committing a BGP config that is byte-identical to the running
// /etc/coop/bgpcpp/bgpcpp.conf must be a no-op: no git commit and (crucially)
// no disruptive bgpd restart. saveBgpConfig() records a restart
// unconditionally, so commit() compares staged vs running content.
TEST_F(ConfigSessionTestFixture, commitUnchangedBgpConfigIsNoOp) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  auto makeSession = [&]() {
    auto s = std::make_unique<TestableConfigSession>(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    useClassicBgpSystemd(*s);
    return s;
  };

  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "1.1.1.1";
    s->setCommandLine("config protocol bgp global router-id 1.1.1.1");
    s->saveBgpConfig();
    ASSERT_FALSE(s->commit(localhost()).commitSha.empty());
  }
  // Stage the SAME value again and commit -> no-op (empty commitSha, so no git
  // revision and no bgpd restart).
  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "1.1.1.1";
    s->setCommandLine("config protocol bgp global router-id 1.1.1.1");
    s->saveBgpConfig();
    auto result = s->commit(localhost());
    EXPECT_TRUE(result.commitSha.empty())
        << "committing an unchanged BGP config should be a no-op (no restart)";
    EXPECT_EQ(result.actions.count(cli::ServiceType::BGP), 0u)
        << "unchanged BGP config must not apply a bgpd restart";
  }
}

// commit() must abort if it cannot read the running bgpcpp.conf, instead of
// silently proceeding with an empty snapshot (which a later failed-commit
// rollback would write back, corrupting the running config). Mirrors the guard
// rollback() already has.
TEST_F(ConfigSessionTestFixture, commitThrowsWhenRunningBgpConfigUnreadable) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path bgpSys = getTestEtcDir() / "coop" / "bgpcpp" / "bgpcpp.conf";
  auto makeSession = [&]() {
    auto s = std::make_unique<TestableConfigSession>(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    useClassicBgpSystemd(*s);
    return s;
  };

  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "1.1.1.1";
    s->setCommandLine("config protocol bgp global router-id 1.1.1.1");
    s->saveBgpConfig();
    ASSERT_FALSE(s->commit(localhost()).commitSha.empty());
  }
  ASSERT_TRUE(fs::exists(bgpSys));

  // Make the running config unreadable; running as a non-root user this makes
  // folly::readFile fail. (Skip the assertion if we happen to run as root,
  // where permission bits are bypassed.)
  fs::permissions(bgpSys, fs::perms::none);
  if (::geteuid() != 0) {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "2.2.2.2";
    s->setCommandLine("config protocol bgp global router-id 2.2.2.2");
    s->saveBgpConfig();
    EXPECT_THROW(s->commit(localhost()), std::runtime_error);
  }
  // Restore perms so fixture teardown can remove the temp tree.
  fs::permissions(bgpSys, fs::perms::owner_all);
}

// A BGP-only session (bgp_config.json + metadata present, agent.conf session
// file absent) must RESUME on the next CLI invocation, not be misdetected as
// fresh -- otherwise requiredActions_ (the recorded bgpd restart) is cleared
// and the staged change is silently dropped at commit time.
TEST_F(ConfigSessionTestFixture, bgpOnlySessionResumesAcrossInvocations) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path agentSess = sessionDir / "agent.conf";
  fs::path bgpSess = sessionDir / "bgp_config.json";
  auto makeSession = [&]() {
    auto s = std::make_unique<TestableConfigSession>(
        sessionDir.string(), (getTestEtcDir() / "coop").string());
    useClassicBgpSystemd(*s);
    return s;
  };

  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "9.9.9.9";
    s->setCommandLine("config protocol bgp global router-id 9.9.9.9");
    s->saveBgpConfig();
  }
  // Simulate a pure BGP-only session: drop the agent session file that the
  // first construction seeded, leaving only the staged BGP config + metadata.
  fs::remove(agentSess);
  ASSERT_TRUE(fs::exists(bgpSess));
  ASSERT_FALSE(fs::exists(agentSess));

  // The next invocation must resume and still commit the staged BGP change.
  {
    auto s = makeSession();
    auto result = s->commit(localhost());
    EXPECT_FALSE(result.commitSha.empty())
        << "BGP-only session must resume, not be wiped as a fresh session";
  }
}

// No-arg rollback() walks cli/cli_metadata.json history so BGP-only commits are
// reachable. The initial baseline commit must therefore also touch metadata, or
// rolling back to the baseline (after exactly one real commit) fails.
TEST_F(ConfigSessionTestFixture, noArgRollbackReachesBaseline) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  const fs::path coopDir = getTestEtcDir() / "coop";
  const fs::path bgpCurrent = coopDir / "bgpcpp.conf";
  const fs::path bgpDesired = coopDir / "bgpcpp/bgpcpp.conf";
  auto makeSession = [&]() {
    auto s = std::make_unique<TestableConfigSession>(
        sessionDir.string(), coopDir.string());
    useClassicBgpSystemd(*s);
    return s;
  };

  {
    auto s = makeSession();
    s->getBgpConfig().router_id() = "1.1.1.1";
    s->setCommandLine("config protocol bgp global router-id 1.1.1.1");
    s->saveBgpConfig();
    ASSERT_FALSE(s->commit(localhost()).commitSha.empty());
  }
  ASSERT_TRUE(fs::is_symlink(bgpCurrent));
  ASSERT_TRUE(fs::exists(bgpDesired));
  // Exactly one real commit sits on top of the baseline; no-arg rollback must
  // still reach the baseline (it appears in metadata history now).
  {
    auto s = makeSession();
    std::string rb;
    EXPECT_NO_THROW(rb = s->rollback(localhost()));
    EXPECT_FALSE(rb.empty());
  }
  EXPECT_TRUE(fs::is_symlink(bgpCurrent));
  EXPECT_TRUE(fs::exists(bgpDesired));
  EXPECT_THAT(
      readFile(bgpDesired), ::testing::Not(::testing::HasSubstr("1.1.1.1")));
}

// Rolling back a commit that required an agent warmboot (e.g. a VLAN
// membership change) must restart the agent, not apply the rolled-back config
// with a HITLESS reloadConfig(): undoing a change needs at least the action
// level that applying it needed.
TEST_F(ConfigSessionTestFixture, rollbackUsesRecordedActionLevel) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  auto makeSession = [&](MockFbossServiceUtil*& mockOut) {
    auto mock = std::make_unique<::testing::StrictMock<MockFbossServiceUtil>>();
    mockOut = mock.get();
    return std::make_unique<TestableConfigSession>(
        sessionDir.string(),
        (getTestEtcDir() / "coop").string(),
        std::move(mock));
  };

  std::string firstCommitSha;
  // First commit: a HITLESS change (e.g. a description edit).
  {
    MockFbossServiceUtil* mock = nullptr;
    auto session = makeSession(mock);
    EXPECT_CALL(
        *mock,
        validateConfig(
            cli::ServiceType::AGENT, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);
    EXPECT_CALL(*mock, reloadConfig(cli::ServiceType::AGENT, ::testing::_))
        .Times(1);
    session->setCommandLine(
        "config interface eth1/1/1 description First version");
    (*session->getAgentConfig().sw()->ports())[0].description() =
        "First version";
    session->saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    firstCommitSha = session->commit(localhost()).commitSha;
    ASSERT_FALSE(firstCommitSha.empty());
  }

  // Second commit: a change that requires an agent warmboot, like
  // `config interface eth1/1/1 switchport access vlan 3000` does.
  {
    MockFbossServiceUtil* mock = nullptr;
    auto session = makeSession(mock);
    EXPECT_CALL(
        *mock,
        validateConfig(
            cli::ServiceType::AGENT,
            ::testing::_,
            cli::ConfigActionLevel::SERVICE_RESTART,
            ::testing::_))
        .Times(1);
    EXPECT_CALL(
        *mock,
        restartService(
            cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART))
        .Times(1);
    EXPECT_CALL(*mock, isAgentConfigured(::testing::_))
        .WillOnce(::testing::Return(true));
    session->setCommandLine(
        "config interface eth1/1/1 switchport access vlan 3000");
    (*session->getAgentConfig().sw()->ports())[0].description() =
        "Second version";
    session->saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);
    ASSERT_FALSE(session->commit(localhost()).commitSha.empty());
  }

  // Rollback to the first commit: this undoes the warmboot-level change, so it
  // must warmboot-restart the agent. reloadConfig() must NOT be called (the
  // StrictMock enforces this).
  {
    MockFbossServiceUtil* mock = nullptr;
    auto session = makeSession(mock);
    EXPECT_CALL(
        *mock,
        restartService(
            cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART))
        .Times(1);
    EXPECT_CALL(*mock, isAgentConfigured(::testing::_))
        .WillOnce(::testing::Return(true));
    std::string rollbackSha = session->rollback(localhost(), firstCommitSha);
    EXPECT_FALSE(rollbackSha.empty());
  }

  // Rollback forward to the pre-rollback state (undoing the rollback commit)
  // crosses the same warmboot-level change again, so it too must restart the
  // agent. This exercises the rollback commit recording the action level it
  // applied.
  std::string headBeforeSecondRollback;
  {
    MockFbossServiceUtil* mock = nullptr;
    auto session = makeSession(mock);
    headBeforeSecondRollback = session->getGit().getHead();
    EXPECT_CALL(
        *mock,
        restartService(
            cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART))
        .Times(1);
    EXPECT_CALL(*mock, isAgentConfigured(::testing::_))
        .WillOnce(::testing::Return(true));
    // No-arg rollback: back to the "Second version" commit.
    std::string rollbackSha = session->rollback(localhost());
    EXPECT_FALSE(rollbackSha.empty());
  }
}

TEST_F(ConfigSessionTestFixture, rollbackRejectsUnreadableMetadataInUndoRange) {
  const auto firstCommitSha = commitDescription("First version");
  commitDescription("Second version");

  const auto cliConfigPath = getCliConfigDir() / "agent.conf";
  const auto metadataPath = getCliConfigDir() / "cli_metadata.json";
  createTestConfig(metadataPath, "not valid JSON");
  const auto corruptCommitSha =
      git().commit({"cli/cli_metadata.json"}, "Corrupt metadata");

  const auto headBeforeRollback = git().getHead();
  const auto configBeforeRollback = readFile(cliConfigPath);
  const auto metadataBeforeRollback = readFile(metadataPath);
  auto session = makeStrictSession();

  try {
    session->rollback(localhost(), firstCommitSha);
    FAIL() << "rollback should reject unreadable metadata in the undo range";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(
        ex.what(), ::testing::HasSubstr(Git::shortSha1(corruptCommitSha)));
  }
  EXPECT_EQ(git().getHead(), headBeforeRollback);
  EXPECT_EQ(readFile(cliConfigPath), configBeforeRollback);
  EXPECT_EQ(readFile(metadataPath), metadataBeforeRollback);
}

TEST_F(ConfigSessionTestFixture, rollbackRejectsUnreadableTargetMetadata) {
  commitDescription("First version");

  const auto cliConfigPath = getCliConfigDir() / "agent.conf";
  const auto metadataPath = getCliConfigDir() / "cli_metadata.json";
  createTestConfig(metadataPath, "not valid JSON");
  const auto targetCommitSha =
      git().commit({"cli/cli_metadata.json"}, "Corrupt target metadata");
  commitDescription("Second version");

  const auto headBeforeRollback = git().getHead();
  const auto configBeforeRollback = readFile(cliConfigPath);
  const auto metadataBeforeRollback = readFile(metadataPath);
  auto session = makeStrictSession();

  try {
    session->rollback(localhost(), targetCommitSha);
    FAIL() << "rollback should reject unreadable target metadata";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(
        ex.what(), ::testing::HasSubstr(Git::shortSha1(targetCommitSha)));
  }
  EXPECT_EQ(git().getHead(), headBeforeRollback);
  EXPECT_EQ(readFile(cliConfigPath), configBeforeRollback);
  EXPECT_EQ(readFile(metadataPath), metadataBeforeRollback);
}

} // namespace facebook::fboss
