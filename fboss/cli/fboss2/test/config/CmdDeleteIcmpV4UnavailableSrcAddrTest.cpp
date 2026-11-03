// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/delete/switch/icmpv4_unavailable_src_addr/CmdDeleteIcmpV4UnavailableSrcAddr.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
constexpr std::string_view kCmdPrefix =
    "delete switch icmpv4-unavailable-src-addr";
} // namespace

// Seed mirrors a device with icmpV4UnavailableSrcAddress already configured
class CmdDeleteIcmpV4UnavailableSrcAddrTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteIcmpV4UnavailableSrcAddrTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_icmpv4_unavailable_src_addr_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "icmpV4UnavailableSrcAddress": "10.0.0.1",
    "arpTimeoutSeconds": 60
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession(std::string(kCmdPrefix), "");
  }
};

// Seed mirrors a fresh device where icmpV4UnavailableSrcAddress is absent
class CmdDeleteIcmpV4UnavailableSrcAddrAbsentTestFixture
    : public CmdConfigTestBase {
 public:
  CmdDeleteIcmpV4UnavailableSrcAddrAbsentTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_icmpv4_unavailable_src_addr_absent_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {}
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession(std::string(kCmdPrefix), "");
  }
};

TEST_F(CmdDeleteIcmpV4UnavailableSrcAddrTestFixture, deleteWhenSet) {
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  ASSERT_TRUE(swConfig.icmpV4UnavailableSrcAddress().has_value());

  CmdDeleteIcmpV4UnavailableSrcAddr cmd;
  auto result = cmd.queryClient(HostInfo("testhost"));

  EXPECT_THAT(result, HasSubstr("removed ICMPv4 unavailable source address"));
  EXPECT_THAT(result, HasSubstr("10.0.0.1"));

  // Field is unset; unrelated fields are untouched.
  EXPECT_FALSE(swConfig.icmpV4UnavailableSrcAddress().has_value());
  EXPECT_EQ(swConfig.arpTimeoutSeconds(), 60);
}

TEST_F(
    CmdDeleteIcmpV4UnavailableSrcAddrAbsentTestFixture,
    deleteWhenAbsentIsNoop) {
  const auto sessionBefore = sessionConfigText();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  ASSERT_FALSE(swConfig.icmpV4UnavailableSrcAddress().has_value());

  CmdDeleteIcmpV4UnavailableSrcAddr cmd;
  auto result = cmd.queryClient(HostInfo("testhost"));
  EXPECT_THAT(result, HasSubstr("nothing to delete"));

  EXPECT_FALSE(swConfig.icmpV4UnavailableSrcAddress().has_value());
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
