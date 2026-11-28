// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end tests for `fboss2-dev config switch resource-limit <attr>
 * <value>`.
 *
 * Covers the SW-agent ResourceAccountant tunables, which live in
 * AgentConfig.defaultCommandLineArgs and are applied on coldboot:
 *   - l2-max        -> max_l2_entries              (int)
 *   - arp-max       -> max_arp_entries             (int)
 *   - ndp-max       -> max_ndp_entries             (int)
 *   - hw-protection -> enable_hw_update_protection (bool)
 *
 * The tests assert on the agent's effective gflags (fb303 getOption), not
 * only on the committed defaultCommandLineArgs, so "written" and "applied"
 * are told apart.
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration
 *   - Test is run as root (or with sudo) on a DUT
 *   - Each commit coldboots the agent (~50s), so this suite is slow.
 */

#include <folly/Conv.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

namespace {
constexpr auto kHwProtectionFlag = "enable_hw_update_protection";
} // namespace

class ConfigResourceLimitTest : public Fboss2IntegrationTest {
 protected:
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    configSnapshotJson_ = snapshotConfig();
  }

  void TearDown() override {
    // The restore coldboots, so tests that never committed only drop
    // whatever they staged.
    if (committed_) {
      try {
        restoreConfig(
            configSnapshotJson_,
            cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);
      } catch (const std::exception& e) {
        ADD_FAILURE() << "TearDown: failed to restore config snapshot: "
                      << e.what();
      }
    } else {
      discardSession();
    }
    Fboss2IntegrationTest::TearDown();
  }

  // The running config's defaultCommandLineArgs, minus the flags this
  // command owns.
  std::map<std::string, std::string> getOtherCliArgs() const {
    std::map<std::string, std::string> result;
    auto config = getRunningConfig();
    const auto* args = config.get_ptr("defaultCommandLineArgs");
    if (!args || !args->isObject()) {
      return result;
    }
    for (const auto& [flag, value] : args->items()) {
      auto name = flag.asString();
      if (name != kHwProtectionFlag && name != "max_l2_entries" &&
          name != "max_arp_entries" && name != "max_ndp_entries") {
        result[name] = value.asString();
      }
    }
    return result;
  }

  // Running-config value of a defaultCommandLineArgs flag, or nullopt if
  // unset.
  std::optional<std::string> getCliArg(const std::string& flag) const {
    auto config = getRunningConfig();
    const auto* args = config.get_ptr("defaultCommandLineArgs");
    if (!args || !args->isObject()) {
      return std::nullopt;
    }
    const auto* val = args->get_ptr(flag);
    if (!val) {
      return std::nullopt;
    }
    return val->asString();
  }

  Result runResourceLimit(const std::string& attr, const std::string& value) {
    return runCli({"config", "switch", "resource-limit", attr, value});
  }

  void stageResourceLimit(const std::string& attr, const std::string& value) {
    auto result = runResourceLimit(attr, value);
    ASSERT_EQ(result.exitCode, 0) << "resource-limit " << attr << " " << value
                                  << " failed: " << result.stderr;
  }

  void commitAndWait() {
    committed_ = true;
    commitConfig();
    waitForAgentReady();
  }

  // `preferred`, or the next value up if the agent already runs with it, so
  // the post-commit check cannot pass on a no-op.
  std::string pickLimit(const std::string& flag, int preferred) const {
    auto current = getAgentOption(flag);
    return folly::to<std::string>(
        current == folly::to<std::string>(preferred) ? preferred + 1
                                                     : preferred);
  }

  void expectApplied(const std::string& flag, const std::string& value) const {
    EXPECT_EQ(getCliArg(flag).value_or("<unset>"), value)
        << flag << " not in the running config's defaultCommandLineArgs";
    EXPECT_EQ(getAgentOption(flag), value)
        << flag << " was committed but is not the agent's effective value";
  }

  std::string configSnapshotJson_;
  bool committed_{false};
};

TEST_F(ConfigResourceLimitTest, SetLimitsWithHwProtection) {
  // Values sit above the gflag defaults so enabling enforcement cannot
  // reject entries the DUT already holds.
  const auto l2Max = pickLimit("max_l2_entries", 7000);
  const auto arpMax = pickLimit("max_arp_entries", 9000);
  const auto ndpMax = pickLimit("max_ndp_entries", 5000);
  const auto otherArgsBefore = getOtherCliArgs();

  ASSERT_NO_FATAL_FAILURE(stageResourceLimit("hw-protection", "true"));
  ASSERT_NO_FATAL_FAILURE(stageResourceLimit("l2-max", l2Max));
  ASSERT_NO_FATAL_FAILURE(stageResourceLimit("arp-max", arpMax));
  ASSERT_NO_FATAL_FAILURE(stageResourceLimit("ndp-max", ndpMax));
  commitAndWait();

  expectApplied(kHwProtectionFlag, "true");
  expectApplied("max_l2_entries", l2Max);
  expectApplied("max_arp_entries", arpMax);
  expectApplied("max_ndp_entries", ndpMax);
  EXPECT_EQ(getOtherCliArgs(), otherArgsBefore)
      << "other defaultCommandLineArgs entries must survive";
}

TEST_F(ConfigResourceLimitTest, ToggleHwProtection) {
  const std::string target =
      getAgentOption(kHwProtectionFlag) == "true" ? "false" : "true";
  XLOG(INFO) << "Flipping hw-protection to " << target;

  ASSERT_NO_FATAL_FAILURE(stageResourceLimit("hw-protection", target));
  commitAndWait();

  expectApplied(kHwProtectionFlag, target);
}

TEST_F(ConfigResourceLimitTest, LimitRefusedWithoutHwProtection) {
  // The guard reads the session config, so a DUT that runs with protection
  // on is covered by staging it off first; nothing is committed.
  if (getCliArg(kHwProtectionFlag).has_value()) {
    ASSERT_NO_FATAL_FAILURE(stageResourceLimit("hw-protection", "false"));
  }

  for (const auto* attr : {"l2-max", "arp-max", "ndp-max"}) {
    auto result = runResourceLimit(attr, "7000");
    EXPECT_NE(result.exitCode, 0)
        << attr << " accepted while hw-protection is off";
    EXPECT_THAT(result.stderr, HasSubstr("hw-protection")) << attr;
  }
}
