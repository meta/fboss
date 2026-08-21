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
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors the loopback layout real devices carry:
//  - interface intfID 10 / VLAN 10 ("fbossLoopback0"): the bootstrap-config
//    loopback0, virtual and UNNAMED (the bootstrap config emits it without a
//    name), so resolution must fall back to the conventional intfID;
//  - interface intfID 11 / VLAN 11: a Meta-style loopback carrying the
//    interface name "fbossLoopback1" and production-shaped /32 + /128 host
//    addresses;
//  - VLAN 12 is an ordinary data VLAN with a member port, occupying
//    loopback2's conventional VLAN id;
//  - interface intfID 13 is a non-loopback SVI occupying loopback3's
//    conventional interface id.
class CmdConfigInterfaceLoopbackTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigInterfaceLoopbackTestFixture()
      : CmdConfigTestBase(
            "fboss_intf_loopback_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ],
    "vlans": [
      {"name": "fbossLoopback0", "id": 10, "recordStats": true, "routable": true},
      {"name": "fbossLoopback1", "id": 11, "recordStats": true, "routable": true},
      {"name": "Vlan12", "id": 12, "recordStats": true, "routable": false},
      {"name": "Vlan2001", "id": 2001, "recordStats": true, "routable": true},
      {"name": "default", "id": 4094, "recordStats": true, "routable": false}
    ],
    "vlanPorts": [
      {"vlanID": 12, "logicalPort": 1}
    ],
    "defaultVlan": 4094,
    "interfaces": [
      {
        "intfID": 10,
        "routerID": 0,
        "vlanID": 10,
        "mtu": 9412,
        "isVirtual": true
      },
      {
        "intfID": 11,
        "routerID": 0,
        "vlanID": 11,
        "name": "fbossLoopback1",
        "mtu": 9000,
        "isVirtual": true,
        "ipAddresses": ["10.254.113.0/32", "fc00:0:0:1::/128"]
      },
      {
        "intfID": 13,
        "routerID": 0,
        "vlanID": 2001,
        "name": "svi2001",
        "mtu": 9000
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config interface";

  static const cfg::Interface* findIntf(int32_t intfId) {
    const auto& intfs =
        *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
    for (const auto& intf : intfs) {
      if (*intf.intfID() == intfId) {
        return &intf;
      }
    }
    return nullptr;
  }

  static const cfg::Vlan* findVlan(int32_t vlanId) {
    const auto& vlans =
        *ConfigSession::getInstance().getAgentConfig().sw()->vlans();
    for (const auto& vlan : vlans) {
      if (*vlan.id() == vlanId) {
        return &vlan;
      }
    }
    return nullptr;
  }
};

// Loopback token parsing: shape, case, and index bounds
TEST_F(CmdConfigInterfaceLoopbackTestFixture, parseLoopbackIndex) {
  EXPECT_EQ(utils::parseLoopbackIndex("loopback0"), 0);
  EXPECT_EQ(utils::parseLoopbackIndex("loopback99"), 99);
  EXPECT_EQ(utils::parseLoopbackIndex("Loopback1"), 1);
  EXPECT_EQ(utils::parseLoopbackIndex("LOOPBACK2"), 2);

  EXPECT_EQ(utils::parseLoopbackIndex("loopback"), std::nullopt);
  EXPECT_EQ(utils::parseLoopbackIndex("loopback100"), std::nullopt);
  EXPECT_EQ(utils::parseLoopbackIndex("loopback-1"), std::nullopt);
  EXPECT_EQ(utils::parseLoopbackIndex("loopback1x"), std::nullopt);
  EXPECT_EQ(utils::parseLoopbackIndex("xloopback1"), std::nullopt);
  EXPECT_EQ(utils::parseLoopbackIndex("eth1/1/1"), std::nullopt);
}

// The bootstrap config's loopback0 has no interface name; the token must
// resolve through the conventional intfID and take the address.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, addAddressToUnnamedLoopback0) {
  setupTestableConfigSession(
      cmdPrefix_, "loopback0 ip-address 10.254.113.1/32");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback0", "ip-address", "10.254.113.1/32"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  const auto* intf = findIntf(10);
  ASSERT_NE(intf, nullptr);
  EXPECT_THAT(*intf->ipAddresses(), ElementsAre("10.254.113.1/32"));
}

// A Meta-style loopback named fbossLoopback1 resolves from the loopback1
// token; adding a v6 host address keeps the existing addresses.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, addAddressToNamedLoopback1) {
  setupTestableConfigSession(
      cmdPrefix_, "loopback1 ipv6-address 2401:db00:e717:100::d:0/128");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"loopback1", "ipv6-address", "2401:db00:e717:100::d:0/128"});

  cmd.queryClient(localhost(), config);

  const auto* intf = findIntf(11);
  ASSERT_NE(intf, nullptr);
  EXPECT_THAT(
      *intf->ipAddresses(),
      UnorderedElementsAre(
          "10.254.113.0/32",
          "fc00:0:0:1::/128",
          "2401:db00:e717:100::d:0/128"));
}

// Re-applying an already-configured address is a clean no-op (no duplicate)
TEST_F(CmdConfigInterfaceLoopbackTestFixture, reapplyAddressIsIdempotent) {
  setupTestableConfigSession(
      cmdPrefix_, "loopback1 ip-address 10.254.113.0/32");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback1", "ip-address", "10.254.113.0/32"});

  cmd.queryClient(localhost(), config);

  const auto* intf = findIntf(11);
  ASSERT_NE(intf, nullptr);
  EXPECT_THAT(
      *intf->ipAddresses(),
      UnorderedElementsAre("10.254.113.0/32", "fc00:0:0:1::/128"));
}

// A missing loopback is created on first use: virtual interface plus backing
// VLAN at the conventional ids, and the requested address applied.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, createsMissingLoopback) {
  setupTestableConfigSession(
      cmdPrefix_,
      "loopback5 ip-address 10.254.113.5/32 ipv6-address fc00:0:0:5::/128");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"loopback5",
       "ip-address",
       "10.254.113.5/32",
       "ipv6-address",
       "fc00:0:0:5::/128"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("created loopback interface(s): loopback5"));

  const auto* intf = findIntf(15);
  ASSERT_NE(intf, nullptr);
  EXPECT_EQ(*intf->vlanID(), 15);
  EXPECT_TRUE(*intf->isVirtual());
  EXPECT_EQ(intf->name().value_or(""), "loopback5");
  EXPECT_EQ(*intf->routerID(), 0);
  EXPECT_THAT(
      *intf->ipAddresses(),
      UnorderedElementsAre("10.254.113.5/32", "fc00:0:0:5::/128"));

  const auto* vlan = findVlan(15);
  ASSERT_NE(vlan, nullptr);
  EXPECT_EQ(*vlan->name(), "fbossLoopback5");
  EXPECT_TRUE(*vlan->routable());
}

// A bare `config interface loopback<N>` (no attributes) creates the interface
TEST_F(CmdConfigInterfaceLoopbackTestFixture, bareCreateLoopback) {
  setupTestableConfigSession(cmdPrefix_, "loopback6");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback6"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("created loopback interface(s): loopback6"));
  const auto* intf = findIntf(16);
  ASSERT_NE(intf, nullptr);
  EXPECT_TRUE(*intf->isVirtual());
  ASSERT_NE(findVlan(16), nullptr);
}

// A bare command on an EXISTING loopback still needs attributes
TEST_F(CmdConfigInterfaceLoopbackTestFixture, bareExistingLoopbackThrows) {
  setupTestableConfigSession(cmdPrefix_, "loopback0");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback0"});

  EXPECT_THROW(cmd.queryClient(localhost(), config), std::runtime_error);
}

// The conventional interface id is occupied by a non-loopback interface
TEST_F(CmdConfigInterfaceLoopbackTestFixture, intfIdCollisionThrows) {
  setupTestableConfigSession(cmdPrefix_, "loopback3 ip-address 10.0.3.1/32");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback3", "ip-address", "10.0.3.1/32"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("already in use"));
  }
}

// The conventional VLAN id is occupied by a data VLAN with member ports
TEST_F(CmdConfigInterfaceLoopbackTestFixture, dataVlanCollisionThrows) {
  setupTestableConfigSession(cmdPrefix_, "loopback2 ip-address 10.0.2.1/32");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"loopback2", "ip-address", "10.0.2.1/32"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("data VLAN"));
  }
}

// Tokens above the loopback index bound are ordinary unknown names
TEST_F(CmdConfigInterfaceLoopbackTestFixture, outOfRangeIndexNotFound) {
  setupTestableConfigSession(cmdPrefix_, "loopback100 ip-address 10.0.0.1/32");
  EXPECT_THROW(
      InterfacesConfig({"loopback100", "ip-address", "10.0.0.1/32"}),
      std::invalid_argument);
}

// A non-loopback unknown name mixed with a loopback token is still rejected
TEST_F(CmdConfigInterfaceLoopbackTestFixture, mixedUnknownNameStillThrows) {
  setupTestableConfigSession(
      cmdPrefix_, "loopback7 eth9/9/9 ip-address 10.0.0.1/32");
  EXPECT_THROW(
      InterfacesConfig({"loopback7", "eth9/9/9", "ip-address", "10.0.0.1/32"}),
      std::invalid_argument);
}

// The shared resolution also serves `delete interface`: removing one address
// from a loopback leaves the others in place.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deleteAddressFromLoopback) {
  setupTestableConfigSession(
      "delete interface", "loopback1 ip-address 10.254.113.0/32");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig config({"loopback1", "ip-address", "10.254.113.0/32"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully removed"));
  const auto* intf = findIntf(11);
  ASSERT_NE(intf, nullptr);
  EXPECT_THAT(*intf->ipAddresses(), ElementsAre("fc00:0:0:1::/128"));
}

// `delete interface loopback<N>` removes the named loopback and its backing
// VLAN as a unit.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deleteNamedLoopback) {
  setupTestableConfigSession("delete interface", "loopback1");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig config({"loopback1"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Deleted interface(s): loopback1"));
  EXPECT_EQ(findIntf(11), nullptr);
  EXPECT_EQ(findVlan(11), nullptr);
  // Unrelated objects are untouched.
  EXPECT_NE(findIntf(10), nullptr);
  EXPECT_NE(findVlan(10), nullptr);
  EXPECT_NE(findIntf(13), nullptr);
}

// The unnamed bootstrap loopback0 resolves by conventional intfID and deletes
// the same way.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deleteUnnamedLoopback0) {
  setupTestableConfigSession("delete interface", "loopback0");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig config({"loopback0"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Deleted interface(s): loopback0"));
  EXPECT_EQ(findIntf(10), nullptr);
  EXPECT_EQ(findVlan(10), nullptr);
}

// Ports and loopbacks can be deleted in one command.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deletePortAndLoopbackTogether) {
  setupTestableConfigSession("delete interface", "eth1/1/1 loopback1");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig config({"eth1/1/1", "loopback1"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("loopback1"));
  EXPECT_TRUE(
      ConfigSession::getInstance().getAgentConfig().sw()->ports()->empty());
  EXPECT_EQ(findIntf(11), nullptr);
  EXPECT_EQ(findVlan(11), nullptr);
}

// A loopback that does not exist is an ordinary unknown interface.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deleteMissingLoopbackThrows) {
  setupTestableConfigSession("delete interface", "loopback5");
  EXPECT_THROW(InterfaceDeleteConfig({"loopback5"}), std::invalid_argument);
  EXPECT_NE(findIntf(10), nullptr);
}

// Whole-loopback delete is a config reload (HITLESS), like 'delete vlan'.
TEST_F(CmdConfigInterfaceLoopbackTestFixture, deleteLoopbackIsHitless) {
  setupTestableConfigSession("delete interface", "loopback1");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig config({"loopback1"});
  cmd.queryClient(localhost(), config);
  EXPECT_EQ(
      ConfigSession::getInstance().getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::HITLESS);
}

} // namespace facebook::fboss
