/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// Seed JSON is synthetic (minimal `{"sw": {}}`) — no production sample was
// available for the SwitchConfig-level dhcp{Relay,Reply}SrcOverride{V4,V6}
// fields at the time this test was written. The integration test in
// ConfigDhcpSourceOverrideTest.cpp exercises the real running-config shape.

#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/dhcp/relay_source_override/CmdConfigDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/dhcp/reply_source_override/CmdConfigDhcpReplySourceOverride.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigDhcpTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigDhcpTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({ "sw": {} })") {}
};

// Fixture for reply-source-override tests: seed includes one interface with
// the IPs used in the positive tests so the interface-IP validation passes.
class CmdConfigDhcpReplyTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigDhcpReplyTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_reply_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "interfaces": [{
      "intfID": 1,
      "routerID": 0,
      "vlanID": 1,
      "name": "eth1/1/1",
      "mtu": 1500,
      "ipAddresses": ["10.5.6.8/24", "2401:db00:eef0:a67::2/64"]
    }]
  }
})") {}
};

// =============================================================
// DhcpSourceOverrideArgs validation tests
// =============================================================

TEST_F(CmdConfigDhcpTestFixture, argValidation_valid) {
  DhcpSourceOverrideArgs a({"ipv4", "10.1.2.3"});
  EXPECT_EQ(a.getFamily(), "ipv4");
  EXPECT_EQ(a.getIpAddress(), "10.1.2.3");

  DhcpSourceOverrideArgs b({"ipv6", "2401:db00::1"});
  EXPECT_EQ(b.getFamily(), "ipv6");
  EXPECT_EQ(b.getIpAddress(), "2401:db00::1");

  // IPv6 loopback / zero / common formats
  DhcpSourceOverrideArgs c({"ipv6", "::1"});
  EXPECT_EQ(c.getIpAddress(), "::1");
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_badArity) {
  EXPECT_THROW(DhcpSourceOverrideArgs({}), std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideArgs({"ipv4"}), std::invalid_argument);
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv4", "10.1.2.3", "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_caseInsensitive) {
  // Family token is normalised to lowercase before comparison.
  DhcpSourceOverrideArgs a({"IPV4", "10.1.2.3"});
  EXPECT_EQ(a.getFamily(), "ipv4");
  DhcpSourceOverrideArgs b({"IPv6", "2001:db8::1"});
  EXPECT_EQ(b.getFamily(), "ipv6");
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_unknownFamily) {
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"v4", "10.1.2.3"}), std::invalid_argument);
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ip4", "10.1.2.3"}), std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideArgs({"", "10.1.2.3"}), std::invalid_argument);
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_invalidIpv4) {
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv4", "not-an-ip"}), std::invalid_argument);
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv4", "999.1.1.1"}), std::invalid_argument);
  // CIDR form should fail (Thrift field is a bare IP, not a network)
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv4", "10.1.2.3/24"}), std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideArgs({"ipv4", ""}), std::invalid_argument);
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_invalidIpv6) {
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv6", "not-an-ip"}), std::invalid_argument);
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv6", "2401:db00::1/64"}),
      std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideArgs({"ipv6", ""}), std::invalid_argument);
}

TEST_F(CmdConfigDhcpTestFixture, argValidation_familyMismatch) {
  // IPv6 address passed as ipv4 family → IPv4 parser rejects
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv4", "2401:db00::1"}), std::invalid_argument);
  // IPv4 address passed as ipv6 family → IPv6 parser rejects
  EXPECT_THROW(
      DhcpSourceOverrideArgs({"ipv6", "10.1.2.3"}), std::invalid_argument);
}

TEST_F(CmdConfigDhcpTestFixture, sourceOverrideField_unknownKindThrows) {
  // kind dispatch must fail closed: anything other than "relay"/"reply"
  // (typo, case variant) must throw rather than silently pick a field.
  cfg::SwitchConfig swConfig;
  EXPECT_THROW(
      dhcpSourceOverrideField(swConfig, "replay", "ipv4"),
      std::invalid_argument);
  EXPECT_THROW(
      dhcpSourceOverrideField(swConfig, "Relay", "ipv4"),
      std::invalid_argument);
  EXPECT_THROW(
      dhcpSourceOverrideField(swConfig, "", "ipv6"), std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per (handler x family) combination
// =============================================================

TEST_F(CmdConfigDhcpTestFixture, setRelayIpv4) {
  setupTestableConfigSession(
      "config dhcp relay-source-override", "ipv4 10.5.6.7");
  CmdConfigDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");
  DhcpSourceOverrideArgs args({"ipv4", "10.5.6.7"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("relay-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv4"));
  EXPECT_THAT(result, HasSubstr("10.5.6.7"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->dhcpRelaySrcOverrideV4().has_value());
  EXPECT_EQ(*config.sw()->dhcpRelaySrcOverrideV4(), "10.5.6.7");
}

TEST_F(CmdConfigDhcpTestFixture, setRelayIpv6) {
  setupTestableConfigSession(
      "config dhcp relay-source-override", "ipv6 2401:db00:eef0:a67::1");
  CmdConfigDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");
  DhcpSourceOverrideArgs args({"ipv6", "2401:db00:eef0:a67::1"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("relay-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv6"));
  EXPECT_THAT(result, HasSubstr("2401:db00:eef0:a67::1"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->dhcpRelaySrcOverrideV6().has_value());
  EXPECT_EQ(*config.sw()->dhcpRelaySrcOverrideV6(), "2401:db00:eef0:a67::1");
}

TEST_F(CmdConfigDhcpReplyTestFixture, setReplyIpv4) {
  setupTestableConfigSession(
      "config dhcp reply-source-override", "ipv4 10.5.6.8");
  CmdConfigDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");
  DhcpSourceOverrideArgs args({"ipv4", "10.5.6.8"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("reply-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv4"));
  EXPECT_THAT(result, HasSubstr("10.5.6.8"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->dhcpReplySrcOverrideV4().has_value());
  EXPECT_EQ(*config.sw()->dhcpReplySrcOverrideV4(), "10.5.6.8");
}

TEST_F(CmdConfigDhcpReplyTestFixture, setReplyIpv6) {
  setupTestableConfigSession(
      "config dhcp reply-source-override", "ipv6 2401:db00:eef0:a67::2");
  CmdConfigDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");
  DhcpSourceOverrideArgs args({"ipv6", "2401:db00:eef0:a67::2"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("reply-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv6"));
  EXPECT_THAT(result, HasSubstr("2401:db00:eef0:a67::2"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->dhcpReplySrcOverrideV6().has_value());
  EXPECT_EQ(*config.sw()->dhcpReplySrcOverrideV6(), "2401:db00:eef0:a67::2");
}

// reply-source-override must be rejected when the IP is not configured on any
// interface.
TEST_F(CmdConfigDhcpReplyTestFixture, setReplyIpv4_rejectsBogusIp) {
  setupTestableConfigSession(
      "config dhcp reply-source-override", "ipv4 10.9.9.9");
  CmdConfigDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");
  // 10.9.9.9 is NOT in the seed interfaces (only 10.5.6.8/24 is).
  EXPECT_THROW(
      cmd.queryClient(hostInfo, DhcpSourceOverrideArgs({"ipv4", "10.9.9.9"})),
      std::invalid_argument);
}

TEST_F(CmdConfigDhcpReplyTestFixture, setReplyIpv6_rejectsBogusIp) {
  setupTestableConfigSession(
      "config dhcp reply-source-override", "ipv6 2001:db8::99");
  CmdConfigDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");
  // 2001:db8::99 is NOT in the seed interfaces (only 2401:db00:eef0:a67::2/64
  // is).
  EXPECT_THROW(
      cmd.queryClient(
          hostInfo, DhcpSourceOverrideArgs({"ipv6", "2001:db8::99"})),
      std::invalid_argument);
}

// Overwrite / idempotency
TEST_F(CmdConfigDhcpTestFixture, setRelayIpv4_overwrites) {
  setupTestableConfigSession(
      "config dhcp relay-source-override", "ipv4 192.0.2.10");
  CmdConfigDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");

  cmd.queryClient(hostInfo, DhcpSourceOverrideArgs({"ipv4", "192.0.2.10"}));
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->dhcpRelaySrcOverrideV4(), "192.0.2.10");

  cmd.queryClient(hostInfo, DhcpSourceOverrideArgs({"ipv4", "192.0.2.11"}));
  EXPECT_EQ(*config.sw()->dhcpRelaySrcOverrideV4(), "192.0.2.11");
}

} // namespace facebook::fboss
