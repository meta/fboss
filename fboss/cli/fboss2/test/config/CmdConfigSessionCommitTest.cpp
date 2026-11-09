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
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
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

} // namespace facebook::fboss
