/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/types.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

/*
 * Tests for ManagedVlanMember pub/sub behavior.
 *
 * ManagedVlanMember subscribes to SaiBridgePortTraits publisher.
 * When a bridge port appears (port added), ManagedVlanMember::createObject
 * creates the SAI vlan member. When the bridge port disappears (port removed),
 * ManagedVlanMember::removeObject cleans up the vlan member.
 */
class ManagedVlanMemberPubSubTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
  }

  size_t getVlanMemberCount(VlanID vlanId) const {
    auto& vlanApi = saiApiTable->vlanApi();
    auto handle = saiManagerTable->vlanManager().getVlanHandle(vlanId);
    CHECK(handle);
    auto saiVlanId = handle->vlan->adapterKey();
    auto members = vlanApi.getAttribute(
        saiVlanId, SaiVlanTraits::Attributes::MemberList{});
    return members.size();
  }

  TestInterface intf0;
};

TEST_F(ManagedVlanMemberPubSubTest, vlanMemberCreatedViaBridgePortPubSub) {
  // After setup with PORT | VLAN, bridge ports exist and vlan members
  // should have been created via the pub/sub notification.
  auto vlanId = VlanID(intf0.id);
  auto handle = saiManagerTable->vlanManager().getVlanHandle(vlanId);
  ASSERT_NE(handle, nullptr);

  // intf0 has 4 remote hosts, each with a port → 4 vlan members expected
  EXPECT_EQ(handle->vlanMembers.size(), 4);

  // Verify SAI vlan member objects were actually created
  EXPECT_EQ(getVlanMemberCount(vlanId), 4);
}

TEST_F(ManagedVlanMemberPubSubTest, vlanMemberRemovedOnPortRemoval) {
  auto vlanId = VlanID(intf0.id);
  EXPECT_EQ(getVlanMemberCount(vlanId), 4);

  // Remove one port via state change. This destroys its bridge port,
  // which triggers ManagedVlanMember::removeObject via pub/sub.
  auto portToRemove = intf0.remoteHosts[0].port;
  auto newState = programmedState->clone();
  auto* ports = newState->getPorts()->modify(&newState);
  ports->removeNode(PortID(portToRemove.id));
  applyNewState(newState);

  // The managed vlan member subscription still exists in the map,
  // but the SAI vlan member object should have been removed because
  // the bridge port publisher disappeared.
  EXPECT_EQ(getVlanMemberCount(vlanId), 3);
}

TEST_F(ManagedVlanMemberPubSubTest, vlanMemberRecreatedOnPortReAdd) {
  auto vlanId = VlanID(intf0.id);
  EXPECT_EQ(getVlanMemberCount(vlanId), 4);

  // Remove a port
  auto removedHost = intf0.remoteHosts[0];
  auto newState = programmedState->clone();
  auto* ports = newState->getPorts()->modify(&newState);
  ports->removeNode(PortID(removedHost.port.id));
  applyNewState(newState);
  EXPECT_EQ(getVlanMemberCount(vlanId), 3);

  // Re-add the same port. This creates a new bridge port,
  // which triggers ManagedVlanMember::createObject via pub/sub,
  // recreating the SAI vlan member.
  auto reAddState = programmedState->clone();
  auto* portsAgain = reAddState->getPorts()->modify(&reAddState);
  auto swPort = makePort(removedHost.port);
  portsAgain->addNode(swPort, scopeResolver().scope(swPort));
  applyNewState(reAddState);

  EXPECT_EQ(getVlanMemberCount(vlanId), 4);
}
