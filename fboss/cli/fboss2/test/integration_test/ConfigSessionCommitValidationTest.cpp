// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for the agent validating a config at
 * 'fboss2-dev config session commit'.
 *
 * The commit asks the running agent to validate the staged config through its
 * validateConfig() RPC before anything is applied. This test stages a config
 * the CLI accepts but the agent rejects, and checks that the commit fails with
 * the agent's reason and that nothing reaches the running agent.
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration
 * - The test must be run as root (or with appropriate permissions)
 * - The agent must have the validateConfig() RPC; the test is skipped
 *   otherwise, since the CLI then skips validation by design
 */

#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <memory>
#include <string>
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/agent/if/gen-cpp2/fboss_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace facebook::fboss;

class ConfigSessionCommitValidationTest : public Fboss2IntegrationTest {
 protected:
  // Whether the running agent has the validateConfig() RPC. An agent that
  // predates it answers with the dispatcher's UNKNOWN_METHOD.
  bool agentSupportsValidateConfig() const {
    auto client = utils::createClient<apache::thrift::Client<FbossCtrl>>(
        HostInfo("localhost"));
    thrift::ConfigValidationResult result;
    try {
      // The verdict on an empty config does not matter, only whether the
      // agent knows the method.
      client->sync_validateConfig(
          result, "{}", thrift::ConfigApplyMethod::RELOAD);
    } catch (const apache::thrift::TApplicationException& ex) {
      if (ex.getType() ==
          apache::thrift::TApplicationException::UNKNOWN_METHOD) {
        return false;
      }
      throw;
    }
    return true;
  }
};

TEST_F(ConfigSessionCommitValidationTest, CommitRejectedByAgent) {
  if (!agentSupportsValidateConfig()) {
    GTEST_SKIP() << "The agent predates validateConfig(), so the commit "
                    "skips validation by design";
  }
  const std::string interfaceName = getRandomInterfacePortName();
  XLOG(INFO) << "[Step 1] Using interface " << interfaceName;
  const auto runningConfigBefore = getRunningConfig();

  // The same IP with two prefix lengths: the CLI only de-duplicates addresses
  // by exact string, so it stages both, but the agent keys interface addresses
  // by IP and rejects the duplicate.
  XLOG(INFO) << "[Step 2] Staging a duplicate interface address...";
  for (const auto* prefix : {"192.0.2.1/24", "192.0.2.1/31"}) {
    auto result =
        runCli({"config", "interface", interfaceName, "ip-address", prefix});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to stage " << prefix << ": " << result.stderr;
  }

  XLOG(INFO) << "[Step 3] Committing...";
  auto commitResult = runCli({"config", "session", "commit"});
  const std::string output = commitResult.stdout + commitResult.stderr;
  XLOG(INFO) << "  Commit output: " << output;
  EXPECT_NE(commitResult.exitCode, 0)
      << "The agent should have rejected the commit";
  EXPECT_THAT(output, ::testing::HasSubstr("Agent rejected the config"));
  EXPECT_THAT(
      output, ::testing::HasSubstr("Duplicate network IP address 192.0.2.1"));

  // Validation happens before the config is applied, so the running agent
  // never saw it.
  XLOG(INFO) << "[Step 4] Checking the running config is unchanged...";
  EXPECT_EQ(getRunningConfig(), runningConfigBefore)
      << "A rejected commit must not change the running config";

  discardSession();
  XLOG(INFO) << "TEST PASSED";
}
