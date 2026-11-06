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
#include <algorithm>
#include <string>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/commands/delete/vlan/CmdDeleteVlan.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed covers every deleteVlan branch:
//   100 - barebone VLAN + its paired interface (fboss100, no IPs): deletable,
//         and port 3 is a member of it alongside the default VLAN
//   200 - untagged ingress VLAN for port eth1/1/1
//   300 - switchport member only (VlanPort logicalPort 2): deletable, the
//         membership row is cascaded away
//   400 - backs a routed SVI (interface with an IP address): deletable, the
//         SVI is cascade-removed with it
//   1   - the global default VLAN
class CmdDeleteVlanTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteVlanTestFixture()
      : CmdConfigTestBase(
            "fboss2_vlan_delete_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "defaultVlan": 1,
    "vlans": [
      {"id": 1, "name": "default"},
      {"id": 100, "name": "Vlan100"},
      {"id": 200, "name": "Vlan200"},
      {"id": 300, "name": "Vlan300"},
      {"id": 400, "name": "Vlan400"}
    ],
    "interfaces": [
      {"intfID": 100, "vlanID": 100, "name": "fboss100", "ipAddresses": []},
      {"intfID": 400, "vlanID": 400, "name": "fboss400",
       "ipAddresses": ["10.0.4.1/24"]}
    ],
    "ports": [
      {"logicalID": 1, "name": "eth1/1/1", "ingressVlan": 200}
    ],
    "vlanPorts": [
      {"vlanID": 300, "logicalPort": 2},
      {"vlanID": 100, "logicalPort": 3},
      {"vlanID": 1, "logicalPort": 3}
    ],
    "staticMacAddrs": [
      {"vlanID": 100, "macAddress": "02:00:00:00:01:00",
       "egressLogicalPortID": 1}
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete vlan";

  cfg::SwitchConfig& swConfig() {
    return *ConfigSession::getInstance().getAgentConfig().sw();
  }
};

// ============================================================================
// VlanManager::deleteVlan — refuse matrix + not-found
// ============================================================================

TEST_F(CmdDeleteVlanTestFixture, notFoundIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "999");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteVlan cmd;
  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), VlanId({"999"})),
      HasSubstr("not found, nothing to delete"));
  // Nothing was touched.
  EXPECT_EQ(swConfig().vlans()->size(), 5);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteVlanTestFixture, refuseDefaultVlan) {
  setupTestableConfigSession(cmdPrefix_, "1");
  EXPECT_THROW(VlanManager::deleteVlan(swConfig(), VlanID(1)), FbossError);
}

TEST_F(CmdDeleteVlanTestFixture, refuseIngressVlanPort) {
  setupTestableConfigSession(cmdPrefix_, "200");
  try {
    VlanManager::deleteVlan(swConfig(), VlanID(200));
    FAIL() << "expected FbossError";
  } catch (const FbossError& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("eth1/1/1"));
  }
}

// An interface's vlanID must reference an existing VLAN, so a routed SVI
// (interface with IP addresses) is cascade-removed with its VLAN.
TEST_F(CmdDeleteVlanTestFixture, deleteSviWithIpCascadesInterface) {
  setupTestableConfigSession(cmdPrefix_, "400");
  VlanManager::deleteVlan(swConfig(), VlanID(400));

  const auto& vlans = *swConfig().vlans();
  EXPECT_TRUE(std::none_of(vlans.begin(), vlans.end(), [](const auto& v) {
    return *v.id() == 400;
  }));
  const auto& intfs = *swConfig().interfaces();
  EXPECT_TRUE(std::none_of(intfs.begin(), intfs.end(), [](const auto& i) {
    return *i.vlanID() == 400;
  }));
  // The other VLAN's interface is untouched.
  EXPECT_TRUE(std::any_of(intfs.begin(), intfs.end(), [](const auto& i) {
    return *i.vlanID() == 100;
  }));
}

// ============================================================================
// deleteVlan — happy path removes VLAN and its barebone interface
// ============================================================================

TEST_F(CmdDeleteVlanTestFixture, deleteUnreferencedRemovesVlanAndBareboneIntf) {
  setupTestableConfigSession(cmdPrefix_, "100");
  VlanManager::deleteVlan(swConfig(), VlanID(100));

  const auto& vlans = *swConfig().vlans();
  EXPECT_TRUE(std::none_of(vlans.begin(), vlans.end(), [](const auto& v) {
    return *v.id() == 100;
  }));
  // The paired barebone interface (vlanID 100, no IPs) is removed too.
  const auto& intfs = *swConfig().interfaces();
  EXPECT_TRUE(std::none_of(intfs.begin(), intfs.end(), [](const auto& i) {
    return *i.vlanID() == 100;
  }));
  // The SVI-backed VLAN's interface is untouched.
  EXPECT_TRUE(std::any_of(intfs.begin(), intfs.end(), [](const auto& i) {
    return *i.vlanID() == 400;
  }));
  // The VLAN's static MAC entry is cascade-removed (child object).
  ASSERT_TRUE(swConfig().staticMacAddrs().has_value());
  const auto& macs = *swConfig().staticMacAddrs();
  EXPECT_TRUE(std::none_of(macs.begin(), macs.end(), [](const auto& e) {
    return *e.vlanID() == 100;
  }));
  // Port 3's membership row for this VLAN goes with it, but its row for
  // the default VLAN is untouched — the cascade is scoped to the deleted VLAN.
  const auto& vlanPorts = *swConfig().vlanPorts();
  EXPECT_TRUE(
      std::none_of(vlanPorts.begin(), vlanPorts.end(), [](const auto& vp) {
        return *vp.vlanID() == 100;
      }));
  EXPECT_TRUE(
      std::any_of(vlanPorts.begin(), vlanPorts.end(), [](const auto& vp) {
        return *vp.vlanID() == 1 && *vp.logicalPort() == 3;
      }));
}

// A VLAN whose only referrer is switchport membership is deletable: the
// membership rows are cascaded away rather than refused.
TEST_F(CmdDeleteVlanTestFixture, deleteMemberOnlyCascadesVlanPorts) {
  setupTestableConfigSession(cmdPrefix_, "300");
  VlanManager::deleteVlan(swConfig(), VlanID(300));

  const auto& vlans = *swConfig().vlans();
  EXPECT_TRUE(std::none_of(vlans.begin(), vlans.end(), [](const auto& v) {
    return *v.id() == 300;
  }));
  const auto& vlanPorts = *swConfig().vlanPorts();
  EXPECT_TRUE(
      std::none_of(vlanPorts.begin(), vlanPorts.end(), [](const auto& vp) {
        return *vp.vlanID() == 300;
      }));
}

// ============================================================================
// queryClient — exercises the handler + saveConfig on the happy path
// ============================================================================

TEST_F(CmdDeleteVlanTestFixture, queryClientDeletesVlan) {
  setupTestableConfigSession(cmdPrefix_, "100");
  CmdDeleteVlan cmd;
  HostInfo hostInfo("testhost");
  VlanId arg({"100"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("100"));

  const auto& vlans = *swConfig().vlans();
  EXPECT_TRUE(std::none_of(vlans.begin(), vlans.end(), [](const auto& v) {
    return *v.id() == 100;
  }));
}

TEST_F(CmdDeleteVlanTestFixture, queryClientRefusesReferenced) {
  setupTestableConfigSession(cmdPrefix_, "200");
  CmdDeleteVlan cmd;
  HostInfo hostInfo("testhost");
  VlanId arg({"200"});

  EXPECT_THROW(cmd.queryClient(hostInfo, arg), FbossError);
}

} // namespace facebook::fboss
