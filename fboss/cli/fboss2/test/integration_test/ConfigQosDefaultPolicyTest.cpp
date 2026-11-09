// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Integration test for:
 *   fboss2-dev config qos default-policy <name>  (set)
 *   fboss2-dev delete qos default-policy  (clear)
 *
 * Both commands operate on sw.dataPlaneTrafficPolicy.defaultQosPolicy.
 *
 * A single test covers the full lifecycle: set the default policy, verify,
 * delete it, verify it is gone. The test creates its own uniquely named QoS
 * policy in the config session (via `config qos policy <name> map
 * tc-to-queue`) and uses that as the default, so it runs on any DUT config —
 * including one with no QoS policies at all. The original defaultQosPolicy is
 * captured from the running config and restored in TearDown. The test policy
 * itself may remain in the config afterwards (there is no delete verb for QoS
 * policies yet); its timestamped name keeps it from conflicting with
 * anything.
 */

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <optional>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Unique per run so stale policies from previous runs never conflict.
std::string generateTestPolicyName() {
  auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
  return fmt::format("cli_e2e_default_policy_test_{}", epochMs);
}
} // namespace

class ConfigQosDefaultPolicyTest : public Fboss2IntegrationTest {
 protected:
  std::optional<std::string> originalDefaultPolicy_;
  std::string testPolicyName_ = generateTestPolicyName();

  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    captureOriginalDefaultPolicy(getRunningConfig());
    createTestPolicy();
  }

  void TearDown() override {
    restoreOriginalPolicy();
    Fboss2IntegrationTest::TearDown();
  }

  // Returns sw.dataPlaneTrafficPolicy.defaultQosPolicy, or nullopt if absent.
  static std::optional<std::string> readDefaultPolicy(
      const folly::dynamic& config) {
    if (config.isObject() && config.count("sw")) {
      const auto& sw = config["sw"];
      if (sw.isObject() && sw.count("dataPlaneTrafficPolicy")) {
        const auto& policy = sw["dataPlaneTrafficPolicy"];
        if (policy.isObject() && policy.count("defaultQosPolicy")) {
          return policy["defaultQosPolicy"].asString();
        }
      }
    }
    return std::nullopt;
  }

  // Capture original defaultQosPolicy (may be absent) for restore.
  void captureOriginalDefaultPolicy(const folly::dynamic& config) {
    originalDefaultPolicy_ = readDefaultPolicy(config);
  }

  // Create the test QoS policy in the config session. A map subcommand
  // auto-creates the policy; default-policy validates names against the
  // session config, so no commit is needed before using it. The agent
  // requires a valid policy to carry a TC-to-Queue map plus a DSCP-to-TC or
  // PCP-to-TC map, so program both.
  void createTestPolicy() {
    XLOG(INFO) << "Creating test QoS policy: " << testPolicyName_;
    auto result = runCli(
        {"config",
         "qos",
         "policy",
         testPolicyName_,
         "map",
         "tc-to-queue",
         "0",
         "0"});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to create test QoS policy: " << result.stderr;
    result = runCli(
        {"config",
         "qos",
         "policy",
         testPolicyName_,
         "map",
         "dscp",
         "0",
         "traffic-class",
         "0"});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to add dscp map to test QoS policy: " << result.stderr;
  }

  void restoreOriginalPolicy() {
    discardSession();
    // Skip when the committed state already matches (the delete step at the
    // end of the test already cleared the field on a DUT that had no default
    // policy): a no-op delete stages no session change, and committing an
    // empty session fails.
    if (readDefaultPolicy(getRunningConfig()) == originalDefaultPolicy_) {
      return;
    }
    Result result;
    if (originalDefaultPolicy_.has_value()) {
      result =
          runCli({"config", "qos", "default-policy", *originalDefaultPolicy_});
    } else {
      result = runCli({"delete", "qos", "default-policy"});
    }
    if (result.exitCode == 0) {
      commitConfig();
      waitForAgentReady();
    } else {
      XLOG(WARN) << "Failed to restore original default QoS policy: "
                 << result.stderr;
      discardSession();
    }
  }
};

TEST_F(ConfigQosDefaultPolicyTest, SetAndDeleteDefaultPolicy) {
  // Set the test policy as the default and commit.
  XLOG(INFO) << "Setting default QoS policy to: " << testPolicyName_;
  auto setResult = runCli({"config", "qos", "default-policy", testPolicyName_});
  ASSERT_EQ(setResult.exitCode, 0) << "Set failed: " << setResult.stderr;
  EXPECT_THAT(setResult.stdout, ::testing::HasSubstr("Successfully set"));
  commitConfig();

  // Verify the running config reflects the new default.
  auto afterSet = waitForRunningConfig([this](const folly::dynamic& config) {
    return readDefaultPolicy(config) == testPolicyName_;
  });
  EXPECT_EQ(readDefaultPolicy(afterSet), testPolicyName_)
      << "defaultQosPolicy not set after set + commit";

  // Delete the default policy and commit.
  auto delResult = runCli({"delete", "qos", "default-policy"});
  ASSERT_EQ(delResult.exitCode, 0) << "Delete failed: " << delResult.stderr;
  EXPECT_THAT(delResult.stdout, ::testing::HasSubstr("Successfully removed"));
  commitConfig();

  // delete commits at DISRUPTIVE_SERVICE_RESTART; waitForRunningConfig
  // tolerates the restart window (thrift errors count as condition-not-met).
  auto afterDelete = waitForRunningConfig(
      [](const folly::dynamic& config) {
        return !readDefaultPolicy(config).has_value();
      },
      std::chrono::seconds(120));
  EXPECT_EQ(readDefaultPolicy(afterDelete), std::nullopt)
      << "defaultQosPolicy should be absent after delete";
  // Make sure the agent is fully back before TearDown's restore logic runs.
  waitForAgentReady();
}
