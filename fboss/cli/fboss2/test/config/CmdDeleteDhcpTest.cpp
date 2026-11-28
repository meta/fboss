/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// Seed JSON is synthetic (minimal `{"sw": {...}}` with all four DHCP
// source-override fields set) — the integration test in
// DeleteDhcpSourceOverrideTest.cpp exercises the real running-config shape.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/delete/dhcp/relay_source_override/CmdDeleteDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/delete/dhcp/reply_source_override/CmdDeleteDhcpReplySourceOverride.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteDhcpTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteDhcpTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_delete_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "dhcpRelaySrcOverrideV4": "192.0.2.1",
    "dhcpRelaySrcOverrideV6": "2001:db8::1",
    "dhcpReplySrcOverrideV4": "10.5.6.8",
    "dhcpReplySrcOverrideV6": "2401:db00:eef0:a67::2"
  }
})") {}
};

// Fixture with no overrides set, for delete-of-unset error tests.
class CmdDeleteDhcpEmptyTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteDhcpEmptyTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_delete_empty_test_%%%%-%%%%-%%%%-%%%%",
            R"({ "sw": {} })") {}
};

// =============================================================
// DhcpSourceOverrideDeleteArgs validation tests
// =============================================================

TEST_F(CmdDeleteDhcpTestFixture, argValidation_valid) {
  DhcpSourceOverrideDeleteArgs a({"ipv4"});
  EXPECT_EQ(a.getFamily(), "ipv4");

  DhcpSourceOverrideDeleteArgs b({"ipv6"});
  EXPECT_EQ(b.getFamily(), "ipv6");
}

TEST_F(CmdDeleteDhcpTestFixture, argValidation_caseInsensitive) {
  DhcpSourceOverrideDeleteArgs a({"IPV4"});
  EXPECT_EQ(a.getFamily(), "ipv4");
  DhcpSourceOverrideDeleteArgs b({"IPv6"});
  EXPECT_EQ(b.getFamily(), "ipv6");
}

TEST_F(CmdDeleteDhcpTestFixture, argValidation_badArity) {
  EXPECT_THROW(DhcpSourceOverrideDeleteArgs({}), std::invalid_argument);
  EXPECT_THROW(
      DhcpSourceOverrideDeleteArgs({"ipv4", "extra"}), std::invalid_argument);
}

TEST_F(CmdDeleteDhcpTestFixture, argValidation_unknownFamily) {
  EXPECT_THROW(DhcpSourceOverrideDeleteArgs({"v4"}), std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideDeleteArgs({"ip4"}), std::invalid_argument);
  EXPECT_THROW(DhcpSourceOverrideDeleteArgs({""}), std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per (handler x family) combination
// =============================================================

TEST_F(CmdDeleteDhcpTestFixture, deleteRelayIpv4) {
  setupTestableConfigSession("delete dhcp relay-source-override", "ipv4");
  CmdDeleteDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");

  auto result =
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv4"}));
  EXPECT_THAT(result, HasSubstr("relay-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv4"));
  EXPECT_THAT(result, HasSubstr("192.0.2.1"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(config.sw()->dhcpRelaySrcOverrideV4().has_value());
  // Other overrides untouched
  EXPECT_TRUE(config.sw()->dhcpRelaySrcOverrideV6().has_value());
  EXPECT_TRUE(config.sw()->dhcpReplySrcOverrideV4().has_value());
  EXPECT_TRUE(config.sw()->dhcpReplySrcOverrideV6().has_value());
}

TEST_F(CmdDeleteDhcpTestFixture, deleteRelayIpv6) {
  setupTestableConfigSession("delete dhcp relay-source-override", "ipv6");
  CmdDeleteDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");

  auto result =
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv6"}));
  EXPECT_THAT(result, HasSubstr("relay-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv6"));
  EXPECT_THAT(result, HasSubstr("2001:db8::1"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(config.sw()->dhcpRelaySrcOverrideV6().has_value());
  EXPECT_TRUE(config.sw()->dhcpRelaySrcOverrideV4().has_value());
}

TEST_F(CmdDeleteDhcpTestFixture, deleteReplyIpv4) {
  setupTestableConfigSession("delete dhcp reply-source-override", "ipv4");
  CmdDeleteDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");

  auto result =
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv4"}));
  EXPECT_THAT(result, HasSubstr("reply-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv4"));
  EXPECT_THAT(result, HasSubstr("10.5.6.8"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(config.sw()->dhcpReplySrcOverrideV4().has_value());
  EXPECT_TRUE(config.sw()->dhcpReplySrcOverrideV6().has_value());
}

TEST_F(CmdDeleteDhcpTestFixture, deleteReplyIpv6) {
  setupTestableConfigSession("delete dhcp reply-source-override", "ipv6");
  CmdDeleteDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");

  auto result =
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv6"}));
  EXPECT_THAT(result, HasSubstr("reply-source-override"));
  EXPECT_THAT(result, HasSubstr("ipv6"));
  EXPECT_THAT(result, HasSubstr("2401:db00:eef0:a67::2"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(config.sw()->dhcpReplySrcOverrideV6().has_value());
  EXPECT_TRUE(config.sw()->dhcpReplySrcOverrideV4().has_value());
}

// =============================================================
// Deleting an override that is not set must fail loudly
// =============================================================

TEST_F(CmdDeleteDhcpEmptyTestFixture, deleteUnsetRelayThrows) {
  setupTestableConfigSession("delete dhcp relay-source-override", "ipv4");
  CmdDeleteDhcpRelaySourceOverride cmd;
  HostInfo hostInfo("testhost");
  EXPECT_THROW(
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv4"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv6"})),
      std::invalid_argument);
}

TEST_F(CmdDeleteDhcpEmptyTestFixture, deleteUnsetReplyThrows) {
  setupTestableConfigSession("delete dhcp reply-source-override", "ipv4");
  CmdDeleteDhcpReplySourceOverride cmd;
  HostInfo hostInfo("testhost");
  EXPECT_THROW(
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv4"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(hostInfo, DhcpSourceOverrideDeleteArgs({"ipv6"})),
      std::invalid_argument);
}

} // namespace facebook::fboss
