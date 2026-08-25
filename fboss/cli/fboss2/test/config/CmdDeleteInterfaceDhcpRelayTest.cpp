/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/commands/delete/interface/dhcp/relay/CmdDeleteInterfaceDhcpRelay.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

using namespace ::testing;

namespace facebook::fboss {

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

// Seed mirrors the production shape from Meta-deployed configs: an SVI
// with dhcpRelayAddressV4/V6 present on both the interface and its VLAN.
class CmdDeleteInterfaceDhcpRelayTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteInterfaceDhcpRelayTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_relay_del_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [],
    "vlans": [
      {
        "name": "downlinks",
        "id": 2000,
        "dhcpRelayAddressV4": "10.127.255.67",
        "dhcpRelayAddressV6": "2401:db00:eef0:a67::"
      }
    ],
    "interfaces": [
      {
        "intfID": 2000,
        "routerID": 0,
        "vlanID": 2000,
        "name": "eth1/1/1",
        "mtu": 9000,
        "dhcpRelayAddressV4": "10.127.255.67",
        "dhcpRelayAddressV6": "2401:db00:eef0:a67::"
      },
      {
        "intfID": 2001,
        "routerID": 0,
        "vlanID": 2001,
        "name": "eth1/2/1",
        "mtu": 9000
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete interface";
};

// ---------------------------------------------------------------------------
// DhcpRelayDeleteAttrs parsing tests
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsEmpty) {
  setupTestableConfigSession();
  EXPECT_THROW(DhcpRelayDeleteAttrs({}), std::invalid_argument);
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsUnknownAttr) {
  setupTestableConfigSession();
  try {
    DhcpRelayDeleteAttrs({"bogus-attr"}); // NOLINT(bugprone-unused-raii)
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unknown dhcp relay attribute"));
  }
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsDuplicateRejected) {
  setupTestableConfigSession();
  EXPECT_THROW(
      DhcpRelayDeleteAttrs({"ip-address", "ip-address"}),
      std::invalid_argument);
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsBareAttr) {
  setupTestableConfigSession();
  DhcpRelayDeleteAttrs attrs({"ip-address"});
  ASSERT_EQ(attrs.getAttributes().size(), 1);
  EXPECT_EQ(attrs.getAttributes()[0].first, "ip-address");
  EXPECT_EQ(attrs.getAttributes()[0].second, "");
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsWithAddress) {
  setupTestableConfigSession();
  DhcpRelayDeleteAttrs attrs({"ip-address", "10.127.255.67"});
  ASSERT_EQ(attrs.getAttributes().size(), 1);
  EXPECT_EQ(attrs.getAttributes()[0].second, "10.127.255.67");
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, attrsBothBare) {
  setupTestableConfigSession();
  DhcpRelayDeleteAttrs attrs({"ip-address", "ipv6-address"});
  ASSERT_EQ(attrs.getAttributes().size(), 2);
  EXPECT_EQ(attrs.getAttributes()[0].first, "ip-address");
  EXPECT_EQ(attrs.getAttributes()[0].second, "");
  EXPECT_EQ(attrs.getAttributes()[1].first, "ipv6-address");
}

// ---------------------------------------------------------------------------
// queryClient: clearing relay destinations
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientClearsIpv4Relay) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 dhcp relay ip-address");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ip-address"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("ip-address"));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV4().has_value());
  EXPECT_FALSE((*sw.vlans())[0].dhcpRelayAddressV4().has_value());
  // The v6 relay must be untouched
  EXPECT_TRUE((*sw.interfaces())[0].dhcpRelayAddressV6().has_value());
  EXPECT_TRUE((*sw.vlans())[0].dhcpRelayAddressV6().has_value());
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientClearsIpv6Relay) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ipv6-address"});

  EXPECT_NO_THROW(cmd.queryClient(localhost(), interfaces, attrs));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV6().has_value());
  EXPECT_FALSE((*sw.vlans())[0].dhcpRelayAddressV6().has_value());
  EXPECT_TRUE((*sw.interfaces())[0].dhcpRelayAddressV4().has_value());
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientClearsBoth) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address ipv6-address");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ip-address", "ipv6-address"});

  EXPECT_NO_THROW(cmd.queryClient(localhost(), interfaces, attrs));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV4().has_value());
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV6().has_value());
}

// ---------------------------------------------------------------------------
// queryClient: expected-address matching
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientMatchingAddress) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address 10.127.255.67");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ip-address", "10.127.255.67"});

  EXPECT_NO_THROW(cmd.queryClient(localhost(), interfaces, attrs));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV4().has_value());
}

// Non-canonical form of the same address still matches.
TEST_F(
    CmdDeleteInterfaceDhcpRelayTestFixture,
    queryClientMatchingAddressNonCanonical) {
  setupTestableConfigSession(
      cmdPrefix_,
      "eth1/1/1 dhcp relay ipv6-address 2401:DB00:EEF0:0A67:0:0:0:0");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ipv6-address", "2401:DB00:EEF0:0A67:0:0:0:0"});

  EXPECT_NO_THROW(cmd.queryClient(localhost(), interfaces, attrs));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_FALSE((*sw.interfaces())[0].dhcpRelayAddressV6().has_value());
}

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientMismatchedAddress) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address 10.0.0.99");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayDeleteAttrs attrs({"ip-address", "10.0.0.99"});

  try {
    cmd.queryClient(localhost(), interfaces, attrs);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("10.127.255.67"));
    EXPECT_THAT(e.what(), HasSubstr("10.0.0.99"));
    EXPECT_THAT(e.what(), HasSubstr("nothing deleted"));
  }

  // Mismatch must leave the config untouched
  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_TRUE((*sw.interfaces())[0].dhcpRelayAddressV4().has_value());
}

// ---------------------------------------------------------------------------
// queryClient: idempotency
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceDhcpRelayTestFixture, queryClientNothingConfigured) {
  setupTestableConfigSession(cmdPrefix_, "eth1/2/1 dhcp relay ip-address");
  auto cmd = CmdDeleteInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/2/1"});
  DhcpRelayDeleteAttrs attrs({"ip-address"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("nothing to delete"));
}

} // namespace facebook::fboss
