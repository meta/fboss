// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/global/CmdConfigProtocolBgpGlobal.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// The global dispatcher only touches the BGP side of ConfigSession, which
// seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpNeighborTest).
class CmdConfigBgpGlobalTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpGlobalTestFixture()
      : CmdConfigTestBase("bgp_global_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpGlobal cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpGlobalConfig(tokens));
  }

  bgp::thrift::BgpConfig& config() {
    return ConfigSession::getInstance().getBgpConfig();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpGlobalConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpGlobalTestFixture, argValidation) {
  auto parsed = BgpGlobalConfig({"router-id", "10.0.0.1"});
  EXPECT_EQ(parsed.attr(), "router-id");
  EXPECT_EQ(parsed.values(), std::vector<std::string>({"10.0.0.1"}));

  // Invalid: empty, unknown attribute.
  EXPECT_THROW(BgpGlobalConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpGlobalConfig({"no-such-attr", "1"}), std::invalid_argument);
}

// ==============================================================================
// router-id
// ==============================================================================

TEST_F(CmdConfigBgpGlobalTestFixture, routerIdAccepted) {
  auto result = run({"router-id", "10.0.0.1"});
  EXPECT_THAT(result, HasSubstr("Successfully set BGP router-id to: 10.0.0.1"));
  EXPECT_EQ(*config().router_id(), "10.0.0.1");
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpGlobalTestFixture, routerIdInvalidRejected) {
  const auto before = *config().router_id();
  auto result = run({"router-id", "not-an-ip"});
  EXPECT_THAT(result, HasSubstr("Invalid router-id address"));
  EXPECT_EQ(*config().router_id(), before);
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpGlobalTestFixture, routerIdV6Rejected) {
  const auto before = *config().router_id();
  auto result = run({"router-id", "2001:db8::1"});
  EXPECT_THAT(result, HasSubstr("requires an IPv4 address"));
  EXPECT_EQ(*config().router_id(), before);
  EXPECT_FALSE(sessionFileExists());
}

TEST_F(CmdConfigBgpGlobalTestFixture, routerIdArityRejected) {
  EXPECT_THAT(run({"router-id"}), HasSubstr("requires <ip-address>"));
  EXPECT_THAT(
      run({"router-id", "1.1.1.1", "2.2.2.2"}),
      HasSubstr("requires <ip-address>"));
  EXPECT_FALSE(sessionFileExists());
}

TEST_F(CmdConfigBgpGlobalTestFixture, rejectedRouterIdKeepsStagedValue) {
  run({"router-id", "10.0.0.1"});
  auto result = run({"router-id", "not-an-ip"});
  EXPECT_THAT(result, HasSubstr("Invalid router-id address"));
  EXPECT_EQ(*config().router_id(), "10.0.0.1");
}

// ==============================================================================
// switch-limit-overload-protection-mode
// ==============================================================================

TEST_F(CmdConfigBgpGlobalTestFixture, overloadProtectionModeIntegerAccepted) {
  auto result = run({"switch-limit-overload-protection-mode", "1"});
  EXPECT_THAT(result, HasSubstr("overload_protection_mode to: 1"));
  ASSERT_TRUE(config().switch_limit_config().has_value());
  EXPECT_EQ(
      *config().switch_limit_config()->overload_protection_mode(),
      bgp::thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES);

  result = run({"switch-limit-overload-protection-mode", "2"});
  EXPECT_THAT(result, HasSubstr("overload_protection_mode to: 2"));
  EXPECT_EQ(
      *config().switch_limit_config()->overload_protection_mode(),
      bgp::thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY);
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpGlobalTestFixture, overloadProtectionModeNameAccepted) {
  auto result =
      run({"switch-limit-overload-protection-mode", "DROP_EXCESS_PREFIXES"});
  // The success message prints the integer so the j2c round-trip is stable.
  EXPECT_THAT(result, HasSubstr("overload_protection_mode to: 1"));
  ASSERT_TRUE(config().switch_limit_config().has_value());
  EXPECT_EQ(
      *config().switch_limit_config()->overload_protection_mode(),
      bgp::thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES);

  result = run(
      {"switch-limit-overload-protection-mode", "APPLY_GOLDEN_PREFIX_POLICY"});
  EXPECT_THAT(result, HasSubstr("overload_protection_mode to: 2"));
  EXPECT_EQ(
      *config().switch_limit_config()->overload_protection_mode(),
      bgp::thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY);
}

TEST_F(CmdConfigBgpGlobalTestFixture, overloadProtectionModeInvalidRejected) {
  for (const auto& bad : {"0", "-5", "999", "fast"}) {
    auto result = run({"switch-limit-overload-protection-mode", bad});
    EXPECT_THAT(result, HasSubstr("is not a valid mode")) << bad;
    // The rejection lists every valid name.
    EXPECT_THAT(result, HasSubstr("DROP_EXCESS_PREFIXES")) << bad;
    EXPECT_THAT(result, HasSubstr("APPLY_GOLDEN_PREFIX_POLICY")) << bad;
  }
  // Validation happens before ensure(): a rejected value must not create the
  // nested switch_limit_config.
  EXPECT_FALSE(config().switch_limit_config().has_value());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpGlobalTestFixture,
    rejectedOverloadProtectionModeKeepsStagedValue) {
  run({"switch-limit-overload-protection-mode", "2"});
  auto result = run({"switch-limit-overload-protection-mode", "999"});
  EXPECT_THAT(result, HasSubstr("is not a valid mode"));
  ASSERT_TRUE(config().switch_limit_config().has_value());
  EXPECT_EQ(
      *config().switch_limit_config()->overload_protection_mode(),
      bgp::thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY);
}

} // namespace facebook::fboss
