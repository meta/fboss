// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config reload [hitless|warmboot|coldboot]`.
 *
 * Tests:
 *   - HitlessDefault  — `config reload` (no arg) applies config diff in-place
 *                        via thrift; agent stays up, no restart.
 *   - HitlessExplicit — `config reload hitless` same as above.
 *   - Warmboot        — `config reload warmboot` restarts the agent services;
 *                        agent comes back up cleanly.
 *   - Coldboot        — `config reload coldboot` creates coldboot markers,
 *                        restarts agent services; agent comes back up cleanly.
 *   - InvalidBootType — `config reload badarg` exits non-zero with a
 *                        descriptive error message.
 *
 * Requirements:
 *   - FBOSS agent running with a valid /etc/coop/agent.conf
 *   - Test must run as root on a DUT (warmboot/coldboot invoke systemctl)
 */

#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

class ConfigReloadTest : public Fboss2IntegrationTest {};

// Hitless reload (default, no arg): agent stays up, no restart.
TEST_F(ConfigReloadTest, HitlessDefault) {
  auto result = runCli({"config", "reload"});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Config reloaded successfully"));
  waitForAgentReady();
}

// Hitless reload (explicit token): same behavior as omitting the token.
TEST_F(ConfigReloadTest, HitlessExplicit) {
  auto result = runCli({"config", "reload", "hitless"});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Config reloaded successfully"));
  waitForAgentReady();
}

// Warmboot: agent services are restarted; the agent comes back up cleanly.
TEST_F(ConfigReloadTest, Warmboot) {
  XLOG(INFO) << "Running config reload warmboot — agent will restart";
  auto result = runCli({"config", "reload", "warmboot"});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Config reloaded successfully via"));
  EXPECT_THAT(result.stdout, HasSubstr("(warmboot)"));
  // restartService() waits for each service to reach the systemd "active"
  // state, but the thrift port may not be listening yet — wait for the agent
  // to be fully ready to serve requests.
  waitForAgentReady();
}

// Coldboot: coldboot markers are written; agent services are restarted; the
// agent programs the ASIC from scratch and comes back up cleanly.
TEST_F(ConfigReloadTest, Coldboot) {
  XLOG(INFO) << "Running config reload coldboot — agent will restart";
  auto result = runCli({"config", "reload", "coldboot"});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Config reloaded successfully via"));
  EXPECT_THAT(result.stdout, HasSubstr("(coldboot)"));
  waitForAgentReady();
}

// Invalid boot type: CLI exits non-zero with a descriptive error message.
TEST_F(ConfigReloadTest, InvalidBootType) {
  auto result = runCli({"config", "reload", "badarg"});
  EXPECT_NE(result.exitCode, 0);
  EXPECT_THAT(result.stderr, HasSubstr("Invalid boot type"));
}
