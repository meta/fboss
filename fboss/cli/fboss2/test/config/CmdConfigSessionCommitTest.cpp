/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionCommit.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace facebook::fboss {

class CmdConfigSessionCommitTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigSessionCommitTestFixture()
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
      }
    ]
  }
})") {}
};

TEST_F(CmdConfigSessionCommitTestFixture, commitWithoutStagedSession) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Setup mock agent server (should not be called for empty commit)
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  // Create a session but don't make any changes
  TestableConfigSession::setInstance(
      std::make_unique<TestableConfigSession>(
          sessionDir.string(), (getTestEtcDir() / "coop").string()));

  auto cmd = CmdConfigSessionCommit();
  auto result = cmd.queryClient(localhost());

  EXPECT_EQ(result, "No config session exists. Make a config change first.");
}

TEST_F(CmdConfigSessionCommitTestFixture, commitWithChanges) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Setup mock agent server
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // Create a session and make a change
  auto session = std::make_unique<TestableConfigSession>(
      sessionDir.string(), (getTestEtcDir() / "coop").string());

  auto& config = session->getAgentConfig();
  auto& ports = *config.sw()->ports();
  ASSERT_FALSE(ports.empty());
  ports[0].description() = "Test change";
  session->setCommandLine(
      "fboss2-dev config interface eth1/1/1 description \"Test change\"");
  session->saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  TestableConfigSession::setInstance(std::move(session));

  auto cmd = CmdConfigSessionCommit();
  auto result = cmd.queryClient(localhost());

  // Verify the message indicates successful commit
  EXPECT_THAT(
      result, ::testing::HasSubstr("Config session committed successfully"));
  EXPECT_THAT(result, ::testing::HasSubstr("config reloaded for wedge_agent"));
}

TEST_F(CmdConfigSessionCommitTestFixture, commitTwiceSecondReportsNoSession) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  // Setup mock agent server (should only be called once for the first commit)
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  // First commit: make a change and commit
  {
    auto session = std::make_unique<TestableConfigSession>(
        sessionDir.string(), (getTestEtcDir() / "coop").string());

    auto& config = session->getAgentConfig();
    auto& ports = *config.sw()->ports();
    ASSERT_FALSE(ports.empty());
    ports[0].description() = "First commit change";
    session->setCommandLine(
        "fboss2-dev config interface eth1/1/1 description \"First commit change\"");
    session->saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

    TestableConfigSession::setInstance(std::move(session));

    auto cmd = CmdConfigSessionCommit();
    auto result = cmd.queryClient(localhost());

    EXPECT_THAT(
        result, ::testing::HasSubstr("Config session committed successfully"));
  }

  // Second commit: try to commit again without making changes
  {
    TestableConfigSession::setInstance(
        std::make_unique<TestableConfigSession>(
            sessionDir.string(), (getTestEtcDir() / "coop").string()));

    auto cmd = CmdConfigSessionCommit();
    auto result = cmd.queryClient(localhost());

    EXPECT_EQ(result, "No config session exists. Make a config change first.");
  }
}

// The agent is asked to validate the candidate config before anything is
// written, and only a validated config is reloaded.
TEST_F(CmdConfigSessionCommitTestFixture, commitValidatesConfigBeforeApplying) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";

  setupMockedAgentServer();
  ::testing::InSequence seq;
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillOnce([](thrift::ConfigValidationResult& /* result */,
                   std::unique_ptr<std::string> config,
                   thrift::ConfigApplyMethod applyMethod) {
        // Leaving result.errors empty accepts the config.
        // The agent receives the staged config file contents.
        EXPECT_THAT(*config, ::testing::HasSubstr("Validated change"));
        // A hitless change is validated for a reloadConfig().
        EXPECT_EQ(applyMethod, thrift::ConfigApplyMethod::RELOAD);
      });
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(1);

  auto session = std::make_unique<TestableConfigSession>(
      sessionDir.string(), (getTestEtcDir() / "coop").string());
  (*session->getAgentConfig().sw()->ports())[0].description() =
      "Validated change";
  session->setCommandLine(
      "fboss2-dev config interface eth1/1/1 description \"Validated change\"");
  session->saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  TestableConfigSession::setInstance(std::move(session));

  auto result = CmdConfigSessionCommit().queryClient(localhost());
  EXPECT_THAT(
      result, ::testing::HasSubstr("Config session committed successfully"));
}

// A config the agent rejects fails the commit with every reason the agent
// gives, before any file is written or any service is touched. The session is
// kept so the user can fix it.
TEST_F(CmdConfigSessionCommitTestFixture, commitRejectedByAgentValidation) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  setupMockedAgentServer();
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillOnce([](thrift::ConfigValidationResult& result,
                   std::unique_ptr<std::string> /* config */,
                   thrift::ConfigApplyMethod /* applyMethod */) {
        for (const auto* message :
             {"Interface 1 refers to non-existent VLAN 4000",
              "Duplicate network IP address 10.1.1.1/31 in interface 2001"}) {
          thrift::ConfigValidationError error;
          error.message() = message;
          result.errors()->push_back(std::move(error));
        }
      });
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());
  (*session.getAgentConfig().sw()->ports())[0].description() = "Bad change";
  session.setCommandLine(
      "fboss2-dev config interface eth1/1/1 description \"Bad change\"");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  const auto headBefore = git().getHead();
  const auto configBefore = readFile(cliConfigPath);

  try {
    session.commit(localhost());
    FAIL() << "commit should fail when the agent rejects the config";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(
        ex.what(),
        ::testing::HasSubstr(
            "Agent rejected the config: Interface 1 refers to non-existent "
            "VLAN 4000; Duplicate network IP address 10.1.1.1/31 in "
            "interface 2001"));
    // Nothing was applied, so nothing was rolled back either.
    EXPECT_THAT(ex.what(), ::testing::Not(::testing::HasSubstr("rolled back")));
  }

  EXPECT_EQ(git().getHead(), headBefore);
  EXPECT_EQ(readFile(cliConfigPath), configBefore);
  EXPECT_TRUE(session.hasActiveSession());
  EXPECT_TRUE(fs::exists(session.getSessionConfigPath()));
}

// An agent that says it could not run the validation (as opposed to a verdict
// on the config) fails the commit too: reloadConfig() would fail the same way,
// so there is no point writing anything.
TEST_F(CmdConfigSessionCommitTestFixture, commitFailsWhenAgentCannotValidate) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  setupMockedAgentServer();
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(
          ::testing::Throw(
              thrift::FbossBaseError(
                  "switch is still initializing or is exiting and is not "
                  "fully configured yet")));
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());
  (*session.getAgentConfig().sw()->ports())[0].description() = "Too early";
  session.setCommandLine(
      "fboss2-dev config interface eth1/1/1 description \"Too early\"");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  const auto headBefore = git().getHead();
  const auto configBefore = readFile(cliConfigPath);

  try {
    session.commit(localhost());
    FAIL() << "commit should fail when the agent cannot validate the config";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(
        ex.what(),
        ::testing::HasSubstr(
            "Agent could not validate the config: switch is still "
            "initializing"));
  }
  EXPECT_EQ(git().getHead(), headBefore);
  EXPECT_EQ(readFile(cliConfigPath), configBefore);
  EXPECT_TRUE(session.hasActiveSession());
}

// The validation fallbacks keep a mixed-version fleet working: an agent that
// predates validateConfig() must not block commits. A thrift server answers a
// method its service does not define with UNKNOWN_METHOD from the dispatcher,
// before any handler runs (a handler cannot fake this: anything it throws is
// sent back as a generic application error). So stand in for an old agent with
// a server for a service that has no validateConfig().
TEST_F(CmdConfigSessionCommitTestFixture, validateConfigSkippedForOldAgent) {
  mockedAgentServer_.reset();
  auto oldAgent = std::make_shared<MockFbossHwCtrlAgent>();
  apache::thrift::ScopedServerInterfaceThread oldAgentServer(
      oldAgent, "::1", 0, createFastMockServerConfig());
  CmdGlobalOptions::getInstance()->setAgentThriftPort(
      oldAgentServer.getAddress().getPort());

  FbossServiceUtil util(
      std::vector<int>{},
      /*multiSwitch=*/false,
      std::make_unique<MockSystemdInterface>());
  EXPECT_NO_THROW(util.validateConfig(
      cli::ServiceType::AGENT,
      "{}",
      cli::ConfigActionLevel::HITLESS,
      localhost()));
}

// Only "method not found" is treated as an old agent. Any other application
// error from the RPC (here a handler failure, which thrift reports as a
// generic application error) fails the commit before anything is applied, so a
// regression in the exception matching cannot silently disable validation.
TEST_F(
    CmdConfigSessionCommitTestFixture,
    commitFailsOnOtherAgentValidationError) {
  fs::path sessionDir = getTestHomeDir() / ".fboss2";
  fs::path cliConfigPath = getTestEtcDir() / "coop" / "cli" / "agent.conf";

  setupMockedAgentServer();
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(
          ::testing::Throw(
              apache::thrift::TApplicationException(
                  apache::thrift::TApplicationException::INTERNAL_ERROR,
                  "something went wrong")));
  EXPECT_CALL(getMockAgent(), reloadConfig()).Times(0);

  TestableConfigSession session(
      sessionDir.string(), (getTestEtcDir() / "coop").string());
  (*session.getAgentConfig().sw()->ports())[0].description() = "Agent error";
  session.setCommandLine(
      "fboss2-dev config interface eth1/1/1 description \"Agent error\"");
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  const auto headBefore = git().getHead();
  const auto configBefore = readFile(cliConfigPath);

  try {
    session.commit(localhost());
    FAIL() << "commit should fail when validateConfig() errors out";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(
        ex.what(), ::testing::HasSubstr("could not validate the config"));
  }
  EXPECT_EQ(git().getHead(), headBefore);
  EXPECT_EQ(readFile(cliConfigPath), configBefore);
}

// An unreachable agent only skips validation: the commit may be what brings
// the agent back, and the reload or restart that follows reports a real
// problem anyway.
TEST_F(
    CmdConfigSessionCommitTestFixture,
    validateConfigSkippedWhenUnreachable) {
  // Stop the mock agent so nothing listens on the agent port.
  mockedAgentServer_.reset();

  FbossServiceUtil util(
      std::vector<int>{},
      /*multiSwitch=*/false,
      std::make_unique<MockSystemdInterface>());
  EXPECT_NO_THROW(util.validateConfig(
      cli::ServiceType::AGENT,
      "{}",
      cli::ConfigActionLevel::HITLESS,
      localhost()));
}

// The agent is told how the config will be applied, so it can accept changes
// that need a restart when the commit restarts it accordingly.
TEST_F(CmdConfigSessionCommitTestFixture, validateConfigPassesApplyMethod) {
  FbossServiceUtil util(
      std::vector<int>{},
      /*multiSwitch=*/false,
      std::make_unique<MockSystemdInterface>());
  for (auto [level, applyMethod] :
       {std::pair{
            cli::ConfigActionLevel::HITLESS, thrift::ConfigApplyMethod::RELOAD},
        std::pair{
            cli::ConfigActionLevel::SERVICE_RESTART,
            thrift::ConfigApplyMethod::RESTART},
        std::pair{
            cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART,
            thrift::ConfigApplyMethod::DISRUPTIVE_RESTART}}) {
    EXPECT_CALL(
        getMockAgent(), validateConfig(::testing::_, ::testing::_, applyMethod))
        .Times(1);
    EXPECT_NO_THROW(
        util.validateConfig(cli::ServiceType::AGENT, "{}", level, localhost()));
    ::testing::Mock::VerifyAndClearExpectations(&getMockAgent());
  }
}

// A valid change that the commit's restart cannot make is reported with the
// restart it needs.
TEST_F(
    CmdConfigSessionCommitTestFixture,
    validateConfigReportsRequiredRestart) {
  FbossServiceUtil util(
      std::vector<int>{},
      /*multiSwitch=*/false,
      std::make_unique<MockSystemdInterface>());
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillOnce([](thrift::ConfigValidationResult& result,
                   std::unique_ptr<std::string> /* config */,
                   thrift::ConfigApplyMethod /* applyMethod */) {
        thrift::ConfigValidationError error;
        error.message() = "ECMP width cannot change on a running agent";
        error.requiredApplyMethod() =
            thrift::ConfigApplyMethod::DISRUPTIVE_RESTART;
        result.errors()->push_back(std::move(error));
      });
  EXPECT_THAT(
      [&] {
        util.validateConfig(
            cli::ServiceType::AGENT,
            "{}",
            cli::ConfigActionLevel::SERVICE_RESTART,
            localhost());
      },
      ::testing::ThrowsMessage<std::runtime_error>(::testing::HasSubstr(
          "ECMP width cannot change on a running agent (needs an agent "
          "coldboot, this commit would warmboot)")));
}

// An agent that is up but cannot validate yet (not configured) only blocks a
// commit that would reload it: a commit that restarts the agent does not need
// it to be configured, and may be how the agent gets out of that state.
TEST_F(
    CmdConfigSessionCommitTestFixture,
    validateConfigSkippedWhenRestartingUnconfiguredAgent) {
  FbossServiceUtil util(
      std::vector<int>{},
      /*multiSwitch=*/false,
      std::make_unique<MockSystemdInterface>());
  EXPECT_CALL(
      getMockAgent(), validateConfig(::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(
          ::testing::Throw(
              thrift::FbossBaseError(
                  "switch is still initializing or is exiting and is not "
                  "fully configured yet")));
  for (auto level :
       {cli::ConfigActionLevel::SERVICE_RESTART,
        cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART}) {
    EXPECT_NO_THROW(
        util.validateConfig(cli::ServiceType::AGENT, "{}", level, localhost()));
  }
  EXPECT_THROW(
      util.validateConfig(
          cli::ServiceType::AGENT,
          "{}",
          cli::ConfigActionLevel::HITLESS,
          localhost()),
      std::runtime_error);
}

} // namespace facebook::fboss
