// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/commands/config/switch/resource_limit/CmdConfigResourceLimit.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigResourceLimitTestFixture : public CmdConfigTestBase {
 public:
  // Seed mirrors the defaultCommandLineArgs block of a production agent.conf:
  // a populated map the CLI must read-modify-write without disturbing other
  // flags.
  CmdConfigResourceLimitTestFixture()
      : CmdConfigTestBase(
            "fboss_resource_limit_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "defaultCommandLineArgs": {
    "enable_acl_table_group": "true",
    "multi_switch": "true"
  },
  "sw": {
    "arpTimeoutSeconds": 60
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config switch resource-limit";

  // The *-max limits are refused until hw-protection is on in the session.
  void enableHwProtection() {
    CmdConfigResourceLimit cmd;
    cmd.queryClient(
        HostInfo("testhost"),
        ResourceLimitConfigArgs({"hw-protection", "true"}));
  }

  void expectLimitRefused(const std::string& attr) {
    CmdConfigResourceLimit cmd;
    try {
      cmd.queryClient(
          HostInfo("testhost"), ResourceLimitConfigArgs({attr, "7000"}));
      ADD_FAILURE() << attr << " accepted while hw-protection is off";
    } catch (const std::runtime_error& e) {
      EXPECT_THAT(e.what(), HasSubstr("hw-protection")) << attr;
    }
  }
};

// =============================================================
// ResourceLimitConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_validInt) {
  ResourceLimitConfigArgs a({"l2-max", "7000"});
  EXPECT_EQ(a.getAttribute(), "l2-max");
  EXPECT_EQ(a.getGflagName(), "max_l2_entries");
  EXPECT_EQ(a.getGflagValue(), "7000");

  ResourceLimitConfigArgs b({"arp-max", "9000"});
  EXPECT_EQ(b.getGflagName(), "max_arp_entries");
  EXPECT_EQ(b.getGflagValue(), "9000");

  ResourceLimitConfigArgs c({"ndp-max", "0"});
  EXPECT_EQ(c.getGflagName(), "max_ndp_entries");
  EXPECT_EQ(c.getGflagValue(), "0");
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_validBool) {
  ResourceLimitConfigArgs t({"hw-protection", "true"});
  EXPECT_EQ(t.getGflagName(), "enable_hw_update_protection");
  EXPECT_EQ(t.getGflagValue(), "true");

  // Case-insensitive, and 1/0 accepted as bool aliases normalized to
  // true/false.
  EXPECT_EQ(
      ResourceLimitConfigArgs({"hw-protection", "False"}).getGflagValue(),
      "false");
  EXPECT_EQ(
      ResourceLimitConfigArgs({"hw-protection", "1"}).getGflagValue(), "true");
  EXPECT_EQ(
      ResourceLimitConfigArgs({"hw-protection", "0"}).getGflagValue(), "false");
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_badArity) {
  EXPECT_THROW(ResourceLimitConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(ResourceLimitConfigArgs({"l2-max"}), std::invalid_argument);
  EXPECT_THROW(
      ResourceLimitConfigArgs({"l2-max", "10", "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(
      ResourceLimitConfigArgs({"unknown", "10"}), std::invalid_argument);
  // Case-sensitive attribute names.
  EXPECT_THROW(
      ResourceLimitConfigArgs({"L2-Max", "10"}), std::invalid_argument);
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_nonInteger) {
  EXPECT_THROW(
      ResourceLimitConfigArgs({"l2-max", "abc"}), std::invalid_argument);
  EXPECT_THROW(
      ResourceLimitConfigArgs({"arp-max", "3.14"}), std::invalid_argument);
  EXPECT_THROW(ResourceLimitConfigArgs({"ndp-max", ""}), std::invalid_argument);
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_negative) {
  EXPECT_THROW(
      ResourceLimitConfigArgs({"l2-max", "-1"}), std::invalid_argument);
  EXPECT_THROW(
      ResourceLimitConfigArgs({"arp-max", "-5"}), std::invalid_argument);
}

TEST_F(CmdConfigResourceLimitTestFixture, argValidation_nonBool) {
  EXPECT_THROW(
      ResourceLimitConfigArgs({"hw-protection", "yes"}), std::invalid_argument);
  EXPECT_THROW(
      ResourceLimitConfigArgs({"hw-protection", "2"}), std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per attribute
// =============================================================

TEST_F(CmdConfigResourceLimitTestFixture, setL2Max) {
  setupTestableConfigSession(cmdPrefix_, "l2-max 7000");
  enableHwProtection();
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  ResourceLimitConfigArgs args({"l2-max", "7000"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("l2-max"));
  EXPECT_THAT(result, HasSubstr("7000"));

  auto& config = ConfigSession::getInstance().getAgentConfig();
  auto& cliArgs = *config.defaultCommandLineArgs();
  EXPECT_EQ(cliArgs.at("max_l2_entries"), "7000");
  // Pre-existing flags are preserved.
  EXPECT_EQ(cliArgs.at("multi_switch"), "true");
  EXPECT_EQ(cliArgs.at("enable_acl_table_group"), "true");
}

TEST_F(CmdConfigResourceLimitTestFixture, setArpMax) {
  setupTestableConfigSession(cmdPrefix_, "arp-max 9000");
  enableHwProtection();
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  ResourceLimitConfigArgs args({"arp-max", "9000"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("9000"));

  auto& config = ConfigSession::getInstance().getAgentConfig();
  EXPECT_EQ(config.defaultCommandLineArgs()->at("max_arp_entries"), "9000");
}

TEST_F(CmdConfigResourceLimitTestFixture, setNdpMax) {
  setupTestableConfigSession(cmdPrefix_, "ndp-max 4000");
  enableHwProtection();
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  ResourceLimitConfigArgs args({"ndp-max", "4000"});

  cmd.queryClient(hostInfo, args);

  auto& config = ConfigSession::getInstance().getAgentConfig();
  EXPECT_EQ(config.defaultCommandLineArgs()->at("max_ndp_entries"), "4000");
}

TEST_F(CmdConfigResourceLimitTestFixture, setHwProtection) {
  setupTestableConfigSession(cmdPrefix_, "hw-protection true");
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  ResourceLimitConfigArgs args({"hw-protection", "true"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("hw-protection"));

  auto& config = ConfigSession::getInstance().getAgentConfig();
  EXPECT_EQ(
      config.defaultCommandLineArgs()->at("enable_hw_update_protection"),
      "true");
}

TEST_F(CmdConfigResourceLimitTestFixture, idempotentSameValue) {
  setupTestableConfigSession(cmdPrefix_, "l2-max 5000");
  enableHwProtection();
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  ResourceLimitConfigArgs args({"l2-max", "5000"});

  cmd.queryClient(hostInfo, args);
  const auto sessionBefore = sessionConfigText();
  auto second = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(second, HasSubstr("already set"));
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op set must not save the config";

  auto& config = ConfigSession::getInstance().getAgentConfig();
  EXPECT_EQ(config.defaultCommandLineArgs()->at("max_l2_entries"), "5000");
}

// =============================================================
// The *-max limits need hw-protection
// =============================================================

TEST_F(CmdConfigResourceLimitTestFixture, limitRefusedWithoutHwProtection) {
  setupTestableConfigSession(cmdPrefix_, "l2-max 7000");

  for (const auto* attr : {"l2-max", "arp-max", "ndp-max"}) {
    expectLimitRefused(attr);
  }

  auto& cliArgs =
      *ConfigSession::getInstance().getAgentConfig().defaultCommandLineArgs();
  EXPECT_FALSE(cliArgs.contains("max_l2_entries"));
  EXPECT_FALSE(cliArgs.contains("max_arp_entries"));
  EXPECT_FALSE(cliArgs.contains("max_ndp_entries"));
}

TEST_F(CmdConfigResourceLimitTestFixture, limitRefusedWhenHwProtectionFalse) {
  setupTestableConfigSession(cmdPrefix_, "l2-max 7000");
  CmdConfigResourceLimit cmd;
  cmd.queryClient(
      HostInfo("testhost"),
      ResourceLimitConfigArgs({"hw-protection", "false"}));

  expectLimitRefused("l2-max");
  auto& cliArgs =
      *ConfigSession::getInstance().getAgentConfig().defaultCommandLineArgs();
  EXPECT_FALSE(cliArgs.contains("max_l2_entries"));
}

TEST_F(CmdConfigResourceLimitTestFixture, hwProtectionOffNotesInertLimits) {
  setupTestableConfigSession(cmdPrefix_, "hw-protection false");
  enableHwProtection();
  CmdConfigResourceLimit cmd;
  HostInfo hostInfo("testhost");
  cmd.queryClient(hostInfo, ResourceLimitConfigArgs({"l2-max", "7000"}));
  cmd.queryClient(hostInfo, ResourceLimitConfigArgs({"ndp-max", "5000"}));

  auto result = cmd.queryClient(
      hostInfo, ResourceLimitConfigArgs({"hw-protection", "false"}));
  EXPECT_THAT(result, HasSubstr("max_l2_entries, max_ndp_entries"));
  EXPECT_THAT(result, HasSubstr("not enforced"));
  EXPECT_THAT(result, Not(HasSubstr("max_arp_entries")));

  // The limits are left in place for when protection is turned back on.
  auto& cliArgs =
      *ConfigSession::getInstance().getAgentConfig().defaultCommandLineArgs();
  EXPECT_EQ(cliArgs.at("enable_hw_update_protection"), "false");
  EXPECT_EQ(cliArgs.at("max_l2_entries"), "7000");
}

TEST_F(CmdConfigResourceLimitTestFixture, hwProtectionOffWithoutLimits) {
  setupTestableConfigSession(cmdPrefix_, "hw-protection false");
  CmdConfigResourceLimit cmd;
  auto result = cmd.queryClient(
      HostInfo("testhost"),
      ResourceLimitConfigArgs({"hw-protection", "false"}));
  EXPECT_THAT(result, Not(HasSubstr("not enforced")));
}

} // namespace facebook::fboss
