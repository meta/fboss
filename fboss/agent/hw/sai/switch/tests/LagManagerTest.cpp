/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
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
