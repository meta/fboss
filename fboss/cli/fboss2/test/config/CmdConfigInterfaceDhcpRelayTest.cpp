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

#include "fboss/cli/fboss2/commands/config/interface/dhcp/relay/CmdConfigInterfaceDhcpRelay.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

using namespace ::testing;

namespace facebook::fboss {

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

// Seed mirrors the production shape from Meta-deployed configs: an SVI
// ("downlinks"-style interface with a matching VLAN) that carries
// dhcpRelayAddressV4/V6 on both the interface and the VLAN, plus a routed
// interface with no VLAN entry.
class CmdConfigInterfaceDhcpRelayTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigInterfaceDhcpRelayTestFixture()
      : CmdConfigTestBase(
            "fboss_dhcp_relay_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [],
    "vlans": [
      {
        "name": "downlinks",
        "id": 2000
      }
    ],
    "interfaces": [
      {
        "intfID": 2000,
        "routerID": 0,
        "vlanID": 2000,
        "name": "eth1/1/1",
        "mtu": 9000
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
  const std::string cmdPrefix_ = "config interface";
};

// ---------------------------------------------------------------------------
// DhcpRelayConfigAttrs parsing tests
// ---------------------------------------------------------------------------

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsEmpty) {
  setupTestableConfigSession();
  EXPECT_THROW(DhcpRelayConfigAttrs({}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsUnknownAttr) {
  setupTestableConfigSession();
  try {
    DhcpRelayConfigAttrs( // NOLINT(bugprone-unused-raii)
        {"bogus-attr", "10.0.0.1"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unknown dhcp relay attribute"));
    EXPECT_THAT(e.what(), HasSubstr("bogus-attr"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsMissingValue) {
  setupTestableConfigSession();
  try {
    DhcpRelayConfigAttrs({"ip-address"}); // NOLINT(bugprone-unused-raii)
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Missing address"));
    EXPECT_THAT(e.what(), HasSubstr("ip-address"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsValueIsAnotherAttr) {
  setupTestableConfigSession();
  try {
    DhcpRelayConfigAttrs( // NOLINT(bugprone-unused-raii)
        {"ip-address", "ipv6-address"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Missing address"));
    EXPECT_THAT(e.what(), HasSubstr("ip-address"));
    EXPECT_THAT(e.what(), HasSubstr("ipv6-address"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsDuplicateRejected) {
  setupTestableConfigSession();
  try {
    DhcpRelayConfigAttrs( // NOLINT(bugprone-unused-raii)
        {"ip-address", "10.0.0.1", "ip-address", "10.0.0.2"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Duplicate dhcp relay attribute"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsCaseInsensitive) {
  setupTestableConfigSession();
  DhcpRelayConfigAttrs attrs({"IP-ADDRESS", "10.127.255.67"});
  ASSERT_EQ(attrs.getAttributes().size(), 1);
  EXPECT_EQ(attrs.getAttributes()[0].first, "ip-address");
  EXPECT_EQ(attrs.getAttributes()[0].second, "10.127.255.67");
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, attrsBoth) {
  setupTestableConfigSession();
  DhcpRelayConfigAttrs attrs(
      {"ip-address", "10.127.255.67", "ipv6-address", "2401:db00:eef0:a67::"});
  ASSERT_EQ(attrs.getAttributes().size(), 2);
  EXPECT_EQ(attrs.getAttributes()[0].first, "ip-address");
  EXPECT_EQ(attrs.getAttributes()[1].first, "ipv6-address");
}

// ---------------------------------------------------------------------------
// queryClient: set relay destinations
// ---------------------------------------------------------------------------

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientSetsIpv4Relay) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address 10.127.255.67");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "10.127.255.67"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("ip-address=10.127.255.67"));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  auto& ifaces = *sw.interfaces();
  ASSERT_TRUE(ifaces[0].dhcpRelayAddressV4().has_value());
  EXPECT_EQ(*ifaces[0].dhcpRelayAddressV4(), "10.127.255.67");
  // SVI: the matching VLAN gets the same relay destination
  auto& vlans = *sw.vlans();
  ASSERT_TRUE(vlans[0].dhcpRelayAddressV4().has_value());
  EXPECT_EQ(*vlans[0].dhcpRelayAddressV4(), "10.127.255.67");
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientSetsIpv6Relay) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address 2401:db00:eef0:a67::");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ipv6-address", "2401:db00:eef0:a67::"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("ipv6-address=2401:db00:eef0:a67::"));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  ASSERT_TRUE((*sw.interfaces())[0].dhcpRelayAddressV6().has_value());
  EXPECT_EQ(
      *(*sw.interfaces())[0].dhcpRelayAddressV6(), "2401:db00:eef0:a67::");
  ASSERT_TRUE((*sw.vlans())[0].dhcpRelayAddressV6().has_value());
  EXPECT_EQ(*(*sw.vlans())[0].dhcpRelayAddressV6(), "2401:db00:eef0:a67::");
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientSetsBoth) {
  setupTestableConfigSession(
      cmdPrefix_,
      "eth1/1/1 dhcp relay ip-address 10.127.255.67 "
      "ipv6-address 2401:db00:eef0:a67::");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs(
      {"ip-address", "10.127.255.67", "ipv6-address", "2401:db00:eef0:a67::"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("ip-address=10.127.255.67"));
  EXPECT_THAT(result, HasSubstr("ipv6-address=2401:db00:eef0:a67::"));

  auto& ifaces =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  EXPECT_EQ(*ifaces[0].dhcpRelayAddressV4(), "10.127.255.67");
  EXPECT_EQ(*ifaces[0].dhcpRelayAddressV6(), "2401:db00:eef0:a67::");
}

// A routed interface whose vlanID has no VLAN entry only gets the
// interface-side field set.
TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientNoVlanEntry) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/2/1 dhcp relay ip-address 10.0.0.1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/2/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "10.0.0.1"});

  EXPECT_NO_THROW(cmd.queryClient(localhost(), interfaces, attrs));

  auto& sw = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_EQ(*(*sw.interfaces())[1].dhcpRelayAddressV4(), "10.0.0.1");
  // VLAN 2000 belongs to eth1/1/1 and must be untouched
  EXPECT_FALSE((*sw.vlans())[0].dhcpRelayAddressV4().has_value());
}

// Non-canonical IPv6 input is stored in canonical (compressed) form.
TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientIpv6Normalized) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address 2401:DB00:0:0:0:0:0:1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ipv6-address", "2401:DB00:0:0:0:0:0:1"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("ipv6-address=2401:db00::1"));

  auto& ifaces =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  EXPECT_EQ(*ifaces[0].dhcpRelayAddressV6(), "2401:db00::1");
}

// ---------------------------------------------------------------------------
// queryClient: validation errors
// ---------------------------------------------------------------------------

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientIpv4Invalid) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address not-an-ip");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "not-an-ip"});

  try {
    cmd.queryClient(localhost(), interfaces, attrs);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid IPv4 address"));
    EXPECT_THAT(e.what(), HasSubstr("not-an-ip"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientIpv6ForIpv4Rejected) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address 2401:db00::1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "2401:db00::1"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), interfaces, attrs), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientIpv4ForIpv6Rejected) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address 10.0.0.1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ipv6-address", "10.0.0.1"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), interfaces, attrs), std::invalid_argument);
}

TEST_F(
    CmdConfigInterfaceDhcpRelayTestFixture,
    queryClientIpv4MappedIpv6Rejected) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address ::ffff:10.0.0.1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ipv6-address", "::ffff:10.0.0.1"});

  try {
    cmd.queryClient(localhost(), interfaces, attrs);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("IPv4-mapped"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientZeroIpv4Rejected) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 dhcp relay ip-address 0.0.0.0");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "0.0.0.0"});

  try {
    cmd.queryClient(localhost(), interfaces, attrs);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("disables DHCP relay"));
  }
}

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientZeroIpv6Rejected) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 dhcp relay ipv6-address ::");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs({"ipv6-address", "::"});

  try {
    cmd.queryClient(localhost(), interfaces, attrs);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("disables DHCPv6 relay"));
  }
}

// A parse error on a later attribute must not leave earlier attributes
// applied to the in-memory session config.
TEST_F(
    CmdConfigInterfaceDhcpRelayTestFixture,
    queryClientLaterAttrErrorLeavesConfigUntouched) {
  setupTestableConfigSession(
      cmdPrefix_,
      "eth1/1/1 dhcp relay ip-address 10.0.0.1 ipv6-address not-an-ip");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1"});
  DhcpRelayConfigAttrs attrs(
      {"ip-address", "10.0.0.1", "ipv6-address", "not-an-ip"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), interfaces, attrs), std::invalid_argument);

  auto& ifaces =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  EXPECT_FALSE(ifaces[0].dhcpRelayAddressV4().has_value());
}

// ---------------------------------------------------------------------------
// queryClient: multiple interfaces
// ---------------------------------------------------------------------------

TEST_F(CmdConfigInterfaceDhcpRelayTestFixture, queryClientMultiInterface) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 dhcp relay ip-address 10.0.0.1");
  auto cmd = CmdConfigInterfaceDhcpRelay();
  utils::InterfaceList interfaces({"eth1/1/1", "eth1/2/1"});
  DhcpRelayConfigAttrs attrs({"ip-address", "10.0.0.1"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& ifaces =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  EXPECT_EQ(*ifaces[0].dhcpRelayAddressV4(), "10.0.0.1");
  EXPECT_EQ(*ifaces[1].dhcpRelayAddressV4(), "10.0.0.1");
}

} // namespace facebook::fboss
