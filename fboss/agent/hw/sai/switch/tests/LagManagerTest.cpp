/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/types.h"

#include <string>

using namespace facebook::fboss;

class LagManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
    intf1 = testInterfaces[1];
  }

  /*
   * This assumes one lane ports where the HwLaneList attribute
   * has the port id in "expectedPorts"
   */
  void checkLagMembers(
      LagSaiId saiLagId,
      const std::unordered_set<uint32_t>& expectedPorts) const {
    XLOG(DBG2) << "check LAG members " << saiLagId;
    std::unordered_set<uint32_t> observedPorts;
    auto& lagApi = saiApiTable->lagApi();
    auto& portApi = saiApiTable->portApi();
    auto lagMembers =
        lagApi.getAttribute(saiLagId, SaiLagTraits::Attributes::PortList{});
    for (const auto& lm : lagMembers) {
      PortSaiId portId{lagApi.getAttribute(
          LagMemberSaiId{lm}, SaiLagMemberTraits::Attributes::PortId{})};
      auto lanes =
          portApi.getAttribute(portId, SaiPortTraits::Attributes::HwLaneList{});
      observedPorts.insert(lanes[0]);
    }
    EXPECT_EQ(observedPorts, expectedPorts);
  }

  void setLagAttributeLabel(
      LagSaiId lag,
      std::shared_ptr<AggregatePort> swAggrPort) {
    auto name = swAggrPort->getName();
    std::array<char, 32> labelValue{};
    for (auto i = 0; i < 32 && i < name.length(); i++) {
      labelValue[i] = name[i];
    }
    saiApiTable->lagApi().setAttribute(
        lag, SaiLagTraits::Attributes::Label(labelValue));
  }

  // Moves a port to another vlan the way a live config change does, which
  // SaiSwitch applies before changing the LAGs in the same delta.
  void movePortToVlan(const TestPort& testPort, VlanID vlan) {
    auto oldPort = makePort(testPort);
    auto newPort = oldPort->clone();
    newPort->setIngressVlan(vlan);
    newPort->setVlans(
        PortFields::VlanMembership{{vlan, PortFields::VlanInfo{false, false}}});
    saiManagerTable->portManager().changePort(oldPort, newPort);
  }

  std::optional<VlanID> rxVlanIndex(LagSaiId lagSaiId) const {
    const auto& indices = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch())
                              ->concurrentIndices();
    auto it = indices.vlanIds.find(PortDescriptorSaiId(lagSaiId));
    if (it == indices.vlanIds.cend()) {
      return std::nullopt;
    }
    return it->second;
  }

  // Fake SAI numbers each object type from zero, so a LAG's id can equal a
  // port's and its bridge port aliases the port's in the store; the member's
  // SAI object is then never created. Check the managed member instead.
  bool isLagVlanMember(VlanID vlan, AggregatePortID aggPort) const {
    const auto* vlanHandle = saiManagerTable->vlanManager().getVlanHandle(vlan);
    return vlanHandle &&
        vlanHandle->vlanMembers.find(SaiPortDescriptor(aggPort)) !=
        vlanHandle->vlanMembers.end();
  }

  TestInterface intf0;
  TestInterface intf1;
};

TEST_F(LagManagerTest, addLag) {
  std::shared_ptr<AggregatePort> swAggregatePort = makeAggregatePort(intf0);
  LagSaiId saiId = saiManagerTable->lagManager().addLag(swAggregatePort);
  setLagAttributeLabel(saiId, swAggregatePort);
  auto label = saiApiTable->lagApi().getAttribute(
      saiId, SaiLagTraits::Attributes::Label{});
  std::string value(label.data());
  EXPECT_EQ("lag0", value);
}

// False/unset TTL state must not require a Cisco-only extension in fake SAI.
TEST_F(LagManagerTest, explicitFalseTtlWithoutExtension) {
  auto aggregate = makeAggregatePort(intf0);
  aggregate->setTTLDisableDecrement(false);
  ASSERT_NO_THROW(saiManagerTable->lagManager().addLag(aggregate));

  auto unset = aggregate->clone();
  unset->setTTLDisableDecrement(std::nullopt);
  EXPECT_NO_THROW(saiManagerTable->lagManager().changeLag(aggregate, unset));

  auto explicitFalse = unset->clone();
  explicitFalse->setTTLDisableDecrement(false);
  EXPECT_NO_THROW(
      saiManagerTable->lagManager().changeLag(unset, explicitFalse));
}

TEST_F(LagManagerTest, removeLag) {
  std::shared_ptr<AggregatePort> swAggregatePort = makeAggregatePort(intf0);
  LagSaiId saiId = saiManagerTable->lagManager().addLag(swAggregatePort);
  setLagAttributeLabel(saiId, swAggregatePort);
  auto label = saiApiTable->lagApi().getAttribute(
      saiId, SaiLagTraits::Attributes::Label{});
  std::string value(label.data());
  EXPECT_EQ("lag0", value);

  EXPECT_NO_THROW(saiManagerTable->lagManager().removeLag(swAggregatePort));
}

TEST_F(LagManagerTest, removeLagWithoutAdd) {
  std::shared_ptr<AggregatePort> swAggregatePort = makeAggregatePort(intf0);
  EXPECT_THROW(
      saiManagerTable->lagManager().removeLag(swAggregatePort), FbossError);
}

TEST_F(LagManagerTest, addTwoLags) {
  std::shared_ptr<AggregatePort> swAggregatePort0 = makeAggregatePort(intf0);
  std::shared_ptr<AggregatePort> swAggregatePort1 = makeAggregatePort(intf1);
  LagSaiId saiId0 = saiManagerTable->lagManager().addLag(swAggregatePort0);
  setLagAttributeLabel(saiId0, swAggregatePort0);
  LagSaiId saiId1 = saiManagerTable->lagManager().addLag(swAggregatePort1);
  setLagAttributeLabel(saiId1, swAggregatePort1);

  auto label0 = saiApiTable->lagApi().getAttribute(
      saiId0, SaiLagTraits::Attributes::Label{});
  std::string value0(label0.data());
  EXPECT_EQ("lag0", value0);

  auto label1 = saiApiTable->lagApi().getAttribute(
      saiId1, SaiLagTraits::Attributes::Label{});
  std::string value1(label1.data());
  EXPECT_EQ("lag1", value1);
}

TEST_F(LagManagerTest, addMembers) {
  TestInterface intf{0, 2};
  std::shared_ptr<AggregatePort> swAggregatePort = makeAggregatePort(intf);
  auto saiId = saiManagerTable->lagManager().addLag(swAggregatePort);
  checkLagMembers(saiId, {0, 1});
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(0)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(1)));
}

TEST_F(LagManagerTest, updateMembers) {
  TestInterface intf{0, 3};
  intf.remoteHosts[2].port.id = 3;
  std::shared_ptr<AggregatePort> swAggregatePort = makeAggregatePort(intf);
  intf.remoteHosts[0].port.id = 1;
  intf.remoteHosts[1].port.id = 2;
  intf.remoteHosts[2].port.id = 3;
  std::shared_ptr<AggregatePort> newSwAggregatePort = makeAggregatePort(intf);

  // Create LAG and verify members
  auto saiId = saiManagerTable->lagManager().addLag(swAggregatePort);
  checkLagMembers(saiId, {0, 1, 3});
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(0)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(1)));
  EXPECT_FALSE(saiManagerTable->lagManager().isLagMember(PortID(2)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(3)));

  // Update LAG and verify members
  saiManagerTable->lagManager().changeLag(swAggregatePort, newSwAggregatePort);
  checkLagMembers(saiId, {1, 2, 3});
  EXPECT_FALSE(saiManagerTable->lagManager().isLagMember(PortID(0)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(1)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(2)));
  EXPECT_TRUE(saiManagerTable->lagManager().isLagMember(PortID(3)));

  // accessing unknown port
  EXPECT_THROW(
      saiManagerTable->lagManager().isLagMember(PortID(100)), FbossError);
}

/*
 * A LAG takes its vlan from its members when it is created. Moving the members
 * to another vlan without recreating the LAG must move the LAG too: its port
 * vlan id, its vlan membership and the rx vlan index.
 */
TEST_F(LagManagerTest, changeLagFollowsMemberVlan) {
  auto swAggregatePort = makeAggregatePort(intf1);
  auto aggPortId = swAggregatePort->getID();
  auto& lagManager = saiManagerTable->lagManager();
  auto saiId = lagManager.addLag(swAggregatePort);
  lagManager.addBridgePort(swAggregatePort);

  auto* handle = lagManager.getLagHandle(aggPortId);
  EXPECT_EQ(handle->vlanId, VlanID(1));
  EXPECT_EQ(
      saiApiTable->lagApi().getAttribute(
          saiId, SaiLagTraits::Attributes::PortVlanId{}),
      1);
  EXPECT_TRUE(isLagVlanMember(VlanID(1), aggPortId));
  EXPECT_EQ(rxVlanIndex(saiId), VlanID(1));

  movePortToVlan(intf1.remoteHosts[0].port, VlanID(2));
  lagManager.changeLag(swAggregatePort, swAggregatePort->clone());
  lagManager.changeBridgePort(swAggregatePort, swAggregatePort);

  EXPECT_EQ(handle->vlanId, VlanID(2));
  EXPECT_EQ(
      saiApiTable->lagApi().getAttribute(
          saiId, SaiLagTraits::Attributes::PortVlanId{}),
      2);
  EXPECT_FALSE(isLagVlanMember(VlanID(1), aggPortId));
  EXPECT_TRUE(isLagVlanMember(VlanID(2), aggPortId));
  EXPECT_EQ(rxVlanIndex(saiId), VlanID(2));
}

TEST_F(LagManagerTest, changeLagVlanAfterOldVlanRemoved) {
  auto swAggregatePort = makeAggregatePort(intf1);
  auto aggPortId = swAggregatePort->getID();
  auto& lagManager = saiManagerTable->lagManager();
  auto saiId = lagManager.addLag(swAggregatePort);
  lagManager.addBridgePort(swAggregatePort);

  // The same delta can remove the old vlan; vlans are processed before LAGs.
  movePortToVlan(intf1.remoteHosts[0].port, VlanID(2));
  saiManagerTable->vlanManager().removeVlan(makeVlan(intf1));
  EXPECT_NO_THROW(
      lagManager.changeLag(swAggregatePort, swAggregatePort->clone()));

  EXPECT_EQ(lagManager.getLagHandle(aggPortId)->vlanId, VlanID(2));
  EXPECT_TRUE(isLagVlanMember(VlanID(2), aggPortId));
  EXPECT_EQ(rxVlanIndex(saiId), VlanID(2));
}

TEST_F(LagManagerTest, changeLagWithNoMembers) {
  auto swAggregatePort = makeAggregatePort(intf1);
  auto aggPortId = swAggregatePort->getID();
  auto& lagManager = saiManagerTable->lagManager();
  auto saiId = lagManager.addLag(swAggregatePort);
  lagManager.addBridgePort(swAggregatePort);

  // A config change can empty an L2 LAG's member list.
  TestInterface emptyIntf{intf1.id, 0};
  EXPECT_NO_THROW(
      lagManager.changeLag(swAggregatePort, makeAggregatePort(emptyIntf)));

  checkLagMembers(saiId, {});
  EXPECT_EQ(lagManager.getLagHandle(aggPortId)->vlanId, VlanID(1));
  EXPECT_TRUE(isLagVlanMember(VlanID(1), aggPortId));
  EXPECT_EQ(rxVlanIndex(saiId), VlanID(1));
}

TEST_F(LagManagerTest, changeLagToRouted) {
  auto swAggregatePort = makeAggregatePort(intf1);
  auto aggPortId = swAggregatePort->getID();
  auto& lagManager = saiManagerTable->lagManager();
  auto saiId = lagManager.addLag(swAggregatePort);
  lagManager.addBridgePort(swAggregatePort);
  ASSERT_NE(lagManager.getLagHandle(aggPortId)->bridgePort, nullptr);

  // VlanID(0) is the port router interface sentinel.
  movePortToVlan(intf1.remoteHosts[0].port, VlanID(0));
  lagManager.changeLag(swAggregatePort, swAggregatePort->clone());
  lagManager.changeBridgePort(swAggregatePort, swAggregatePort);

  auto* handle = lagManager.getLagHandle(aggPortId);
  EXPECT_EQ(handle->vlanId, std::nullopt);
  EXPECT_EQ(handle->bridgePort, nullptr);
  EXPECT_FALSE(isLagVlanMember(VlanID(1), aggPortId));
  EXPECT_EQ(rxVlanIndex(saiId), std::nullopt);
}

/*
 * A LAG that carries a router interface of its own need not be a member of any
 * VLAN. Such a LAG gets no port vlan id, no vlan membership and no bridge
 * port. Ports are set up without VLANs here so addLag takes that path.
 */
class LagManagerNoVlanTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
  }

  TestInterface intf0;
};

TEST_F(LagManagerNoVlanTest, addLagWithoutVlan) {
  auto swAggregatePort = makeAggregatePort(intf0);
  std::ignore = saiManagerTable->lagManager().addLag(swAggregatePort);

  // Previously this aborted: addLag CHECKed that the member port was in a
  // VLAN.
  auto handle =
      saiManagerTable->lagManager().getLagHandle(swAggregatePort->getID());
  ASSERT_NE(handle, nullptr);
  ASSERT_NE(handle->lag, nullptr);
  EXPECT_EQ(handle->vlanId, std::nullopt);
  EXPECT_EQ(handle->bridgePort, nullptr);
}

TEST_F(LagManagerNoVlanTest, noBridgePortForLagWithoutVlan) {
  auto swAggregatePort = makeAggregatePort(intf0);
  std::ignore = saiManagerTable->lagManager().addLag(swAggregatePort);

  // A routed LAG does no l2 forwarding, so this is a no op rather than an
  // attempt to bridge a LAG that is in no VLAN.
  saiManagerTable->lagManager().addBridgePort(swAggregatePort);
  auto handle =
      saiManagerTable->lagManager().getLagHandle(swAggregatePort->getID());
  EXPECT_EQ(handle->bridgePort, nullptr);
}

TEST_F(LagManagerNoVlanTest, removeLagWithoutVlan) {
  auto swAggregatePort = makeAggregatePort(intf0);
  std::ignore = saiManagerTable->lagManager().addLag(swAggregatePort);

  // Teardown must not try to remove vlan membership that was never created.
  saiManagerTable->lagManager().removeLag(swAggregatePort);
  EXPECT_EQ(
      saiManagerTable->lagManager().getLagHandleIf(swAggregatePort->getID()),
      nullptr);
}
