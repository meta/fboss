/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/StateUtils.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/fsdb/common/Utils.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using std::make_shared;
using std::shared_ptr;

namespace {

HwSwitchMatcher hwMatcher() {
  return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
}

HwSwitchMatcher hwMatcher(SwitchID switchId) {
  return HwSwitchMatcher(std::unordered_set<SwitchID>({switchId}));
}

} // namespace

class ComputeReversedDeltasTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
    auto emptyConfig = cfg::SwitchConfig();
    baseState_ = publishAndApplyConfig(
        make_shared<SwitchState>(), &emptyConfig, platform_.get());
    addSwitchInfo(baseState_);
    baseState_->publish();
  }

  std::shared_ptr<SwitchState> addPort(
      const std::shared_ptr<SwitchState>& state,
      PortID portId) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->modify(&newState);
    state::PortFields portFields;
    portFields.portId() = portId;
    portFields.portName() = fmt::format("port{}", static_cast<int>(portId));
    auto port = std::make_shared<Port>(std::move(portFields));
    ports->addNode(port, hwMatcher());
    newState->publish();
    return newState;
  }

  std::shared_ptr<SwitchState> removePort(
      const std::shared_ptr<SwitchState>& state,
      PortID portId) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->modify(&newState);
    ports->removeNode(portId);
    newState->publish();
    return newState;
  }

  std::vector<fsdb::OperDelta> buildOperDeltas(
      const std::vector<std::pair<
          std::shared_ptr<SwitchState>,
          std::shared_ptr<SwitchState>>>& statePairs) {
    std::vector<fsdb::OperDelta> operDeltas;
    for (const auto& [oldState, newState] : statePairs) {
      auto operDelta = fsdb::computeOperDelta(oldState, newState, {}, true);
      operDeltas.push_back(std::move(operDelta));
    }
    return operDeltas;
  }

  std::unique_ptr<MockPlatform> platform_;
  std::shared_ptr<SwitchState> baseState_;
};

TEST_F(ComputeReversedDeltasTest, EmptyDeltas) {
  std::vector<fsdb::OperDelta> emptyDeltas;

  auto reversedDeltas =
      utility::computeReversedDeltas(emptyDeltas, baseState_, baseState_);

  EXPECT_TRUE(reversedDeltas.empty());
}

TEST_F(ComputeReversedDeltasTest, SingleDeltaFailureAtFirstDelta) {
  // Current state is baseState_
  // We're adding a port (state1)
  // But intermediate state == current state means failure happened at first
  // delta In this case, we should get an empty reversed deltas vector (go
  // directly to initial state)
  auto state1 = addPort(baseState_, PortID(100));

  auto operDeltas = buildOperDeltas({{baseState_, state1}});

  // When intermediateState == currentState, no rollback deltas are needed
  // because we failed at the very first delta
  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, baseState_);

  EXPECT_TRUE(reversedDeltas.empty());
}

TEST_F(ComputeReversedDeltasTest, SingleDeltaSuccessfullyApplied) {
  // Current state is baseState_
  // We successfully applied delta to get state1
  // Then something caused rollback
  // intermediate state = state1 (last successfully applied state)
  auto state1 = addPort(baseState_, PortID(100));

  auto operDeltas = buildOperDeltas({{baseState_, state1}});

  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, state1);

  // Should have one reversed delta: state1 -> baseState_
  ASSERT_EQ(reversedDeltas.size(), 1);
  EXPECT_EQ(*reversedDeltas[0].oldState(), *state1);
  EXPECT_EQ(*reversedDeltas[0].newState(), *baseState_);
}

TEST_F(ComputeReversedDeltasTest, MultipleDeltasAllApplied) {
  // baseState_ -> state1 -> state2 -> state3
  // All deltas applied successfully, then rollback from state3
  auto state1 = addPort(baseState_, PortID(100));
  auto state2 = addPort(state1, PortID(101));
  auto state3 = addPort(state2, PortID(102));

  auto operDeltas = buildOperDeltas({
      {baseState_, state1},
      {state1, state2},
      {state2, state3},
  });

  // Intermediate state = state3 (all deltas applied)
  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, state3);

  // Should have 3 reversed deltas in order: (state3->state2), (state2->state1),
  // (state1->baseState_)
  ASSERT_EQ(reversedDeltas.size(), 3);

  // First reversed delta: state3 -> state2
  EXPECT_EQ(*reversedDeltas[0].oldState(), *state3);
  EXPECT_EQ(*reversedDeltas[0].newState(), *state2);

  // Second reversed delta: state2 -> state1
  EXPECT_EQ(*reversedDeltas[1].oldState(), *state2);
  EXPECT_EQ(*reversedDeltas[1].newState(), *state1);

  // Third reversed delta: state1 -> baseState_
  EXPECT_EQ(*reversedDeltas[2].oldState(), *state1);
  EXPECT_EQ(*reversedDeltas[2].newState(), *baseState_);
}

TEST_F(ComputeReversedDeltasTest, MultipleDeltasPartiallyApplied) {
  // baseState_ -> state1 -> state2 -> state3
  // Only state1 and state2 were applied, failed at state3
  // Intermediate state = state2
  auto state1 = addPort(baseState_, PortID(100));
  auto state2 = addPort(state1, PortID(101));
  auto state3 = addPort(state2, PortID(102));

  auto operDeltas = buildOperDeltas({
      {baseState_, state1},
      {state1, state2},
      {state2, state3},
  });

  // Intermediate state = state2 (failed at third delta)
  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, state2);

  // Should have 2 reversed deltas: (state2->state1), (state1->baseState_)
  ASSERT_EQ(reversedDeltas.size(), 2);

  // First reversed delta: state2 -> state1
  EXPECT_EQ(*reversedDeltas[0].oldState(), *state2);
  EXPECT_EQ(*reversedDeltas[0].newState(), *state1);

  // Second reversed delta: state1 -> baseState_
  EXPECT_EQ(*reversedDeltas[1].oldState(), *state1);
  EXPECT_EQ(*reversedDeltas[1].newState(), *baseState_);
}

TEST_F(ComputeReversedDeltasTest, MultipleDeltasOnlyFirstApplied) {
  // baseState_ -> state1 -> state2
  // Only state1 was applied, failed at state2
  // Intermediate state = state1
  auto state1 = addPort(baseState_, PortID(100));
  auto state2 = addPort(state1, PortID(101));

  auto operDeltas = buildOperDeltas({
      {baseState_, state1},
      {state1, state2},
  });

  // Intermediate state = state1 (failed at second delta)
  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, state1);

  // Should have 1 reversed delta: (state1->baseState_)
  ASSERT_EQ(reversedDeltas.size(), 1);

  // Reversed delta: state1 -> baseState_
  EXPECT_EQ(*reversedDeltas[0].oldState(), *state1);
  EXPECT_EQ(*reversedDeltas[0].newState(), *baseState_);
}

TEST_F(ComputeReversedDeltasTest, PortAddRemoveSequence) {
  // Test a sequence of add and remove operations
  // baseState_ -> state1 (add port100) -> state2 (add port101) -> state3
  // (remove port100)
  auto state1 = addPort(baseState_, PortID(100));
  auto state2 = addPort(state1, PortID(101));
  auto state3 = removePort(state2, PortID(100));

  auto operDeltas = buildOperDeltas({
      {baseState_, state1},
      {state1, state2},
      {state2, state3},
  });

  // All deltas applied, rollback from state3
  auto reversedDeltas =
      utility::computeReversedDeltas(operDeltas, baseState_, state3);

  ASSERT_EQ(reversedDeltas.size(), 3);

  // Verify the rollback will restore the states in reverse order
  EXPECT_EQ(*reversedDeltas[0].oldState(), *state3);
  EXPECT_EQ(*reversedDeltas[0].newState(), *state2);
  EXPECT_EQ(*reversedDeltas[1].oldState(), *state2);
  EXPECT_EQ(*reversedDeltas[1].newState(), *state1);
  EXPECT_EQ(*reversedDeltas[2].oldState(), *state1);
  EXPECT_EQ(*reversedDeltas[2].newState(), *baseState_);
}

// Tests for firstInterfaceIDWithPorts function
class FirstInterfaceIDWithPortsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
  }

  std::shared_ptr<Interface>
  createInterface(InterfaceID intfId, VlanID vlanId, bool isVirtual = false) {
    auto intf = std::make_shared<Interface>();
    intf->set<switch_state_tags::interfaceId>(intfId);
    intf->setType(cfg::InterfaceType::VLAN);
    intf->setVlanID(vlanId);
    intf->setName(fmt::format("intf{}", static_cast<int>(intfId)));
    intf->setMac(folly::MacAddress("00:00:00:00:00:01"));
    intf->setMtu(9000);
    intf->setIsVirtual(isVirtual);
    intf->setIsStateSyncDisabled(false);
    return intf;
  }

  std::shared_ptr<SwitchState> createStateWithInterface(
      InterfaceID intfId,
      VlanID vlanId,
      bool isVirtual = false,
      std::optional<SwitchID> switchId = std::nullopt) {
    auto state = make_shared<SwitchState>();

    auto intf = createInterface(intfId, vlanId, isVirtual);

    auto intfMap = std::make_shared<InterfaceMap>();
    intfMap->addNode(intf);

    auto multiIntfMap = std::make_shared<MultiSwitchInterfaceMap>();
    auto matcher =
        switchId.has_value() ? hwMatcher(*switchId) : hwMatcher(SwitchID(0));
    multiIntfMap->addMapNode(intfMap, matcher);

    state->resetIntfs(multiIntfMap);
    state->publish();
    return state;
  }

  std::shared_ptr<SwitchState> createStateWithMultipleInterfaces(
      const std::vector<std::tuple<InterfaceID, VlanID, bool, SwitchID>>&
          interfaces) {
    auto state = make_shared<SwitchState>();
    auto multiIntfMap = std::make_shared<MultiSwitchInterfaceMap>();

    // Group interfaces by switchId
    std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchIdToIntfMap;

    for (const auto& [intfId, vlanId, isVirtual, switchId] : interfaces) {
      auto intf = createInterface(intfId, vlanId, isVirtual);

      if (switchIdToIntfMap.find(switchId) == switchIdToIntfMap.end()) {
        switchIdToIntfMap[switchId] = std::make_shared<InterfaceMap>();
      }
      switchIdToIntfMap[switchId]->addNode(intf);
    }

    for (const auto& [switchId, intfMap] : switchIdToIntfMap) {
      multiIntfMap->addMapNode(intfMap, hwMatcher(switchId));
    }

    state->resetIntfs(multiIntfMap);
    state->publish();
    return state;
  }

  std::unique_ptr<MockPlatform> platform_;
};

TEST_F(FirstInterfaceIDWithPortsTest, FindFirstInterfaceForSwitchId) {
  // Create state with a single interface
  auto state = createStateWithInterface(
      InterfaceID(100), VlanID(100), false /* isVirtual */);

  // Should find the interface for the given switchId
  auto result = utility::firstInterfaceIDWithPorts(state, SwitchID(0));
  EXPECT_EQ(result, InterfaceID(100));
}

TEST_F(FirstInterfaceIDWithPortsTest, FindFirstInterfaceForSpecificSwitchId) {
  // Create state with interfaces on different switches
  auto state = createStateWithMultipleInterfaces({
      {InterfaceID(100), VlanID(100), false, SwitchID(0)},
      {InterfaceID(200), VlanID(200), false, SwitchID(1)},
  });

  // Should find interface for switch 0
  auto result0 = utility::firstInterfaceIDWithPorts(state, SwitchID(0));
  EXPECT_EQ(result0, InterfaceID(100));

  // Should find interface for switch 1
  auto result1 = utility::firstInterfaceIDWithPorts(state, SwitchID(1));
  EXPECT_EQ(result1, InterfaceID(200));
}

TEST_F(FirstInterfaceIDWithPortsTest, SkipVirtualInterfaces) {
  // Create state with a virtual interface first, then a non-virtual one
  auto state = createStateWithMultipleInterfaces({
      {InterfaceID(100), VlanID(100), true /* virtual */, SwitchID(0)},
      {InterfaceID(101), VlanID(101), false /* non-virtual */, SwitchID(0)},
  });

  // Should skip virtual interface and return non-virtual one
  auto result = utility::firstInterfaceIDWithPorts(state, SwitchID(0));
  EXPECT_EQ(result, InterfaceID(101));
}

TEST_F(FirstInterfaceIDWithPortsTest, SkipVirtualInterfacesWithSwitchId) {
  // Create state with virtual and non-virtual interfaces on specific switch
  auto state = createStateWithMultipleInterfaces({
      {InterfaceID(100), VlanID(100), true /* virtual */, SwitchID(1)},
      {InterfaceID(101), VlanID(101), false /* non-virtual */, SwitchID(1)},
  });

  // Should skip virtual interface and return non-virtual one for switch 1
  auto result = utility::firstInterfaceIDWithPorts(state, SwitchID(1));
  EXPECT_EQ(result, InterfaceID(101));
}

TEST_F(FirstInterfaceIDWithPortsTest, ThrowWhenNoInterfaceFoundForSwitchId) {
  // Create state with interface only on switch 0
  auto state = createStateWithInterface(
      InterfaceID(100), VlanID(100), false /* isVirtual */, SwitchID(0));

  // Should throw when looking for interface on switch 1 (which has no
  // interfaces)
  EXPECT_THROW(
      utility::firstInterfaceIDWithPorts(state, SwitchID(1)), FbossError);
}

TEST_F(FirstInterfaceIDWithPortsTest, ThrowWhenNoInterfaceFoundAtAll) {
  // Create empty state
  auto state = make_shared<SwitchState>();
  state->publish();

  // Should throw when no interfaces exist
  EXPECT_THROW(
      utility::firstInterfaceIDWithPorts(state, SwitchID(0)), FbossError);
}

TEST_F(FirstInterfaceIDWithPortsTest, ThrowWhenOnlyVirtualInterfacesExist) {
  // Create state with only virtual interfaces
  auto state = createStateWithInterface(
      InterfaceID(100), VlanID(100), true /* virtual */
  );

  // Should throw when only virtual interfaces exist
  EXPECT_THROW(
      utility::firstInterfaceIDWithPorts(state, SwitchID(0)), FbossError);
}

TEST_F(
    FirstInterfaceIDWithPortsTest,
    ThrowWhenOnlyVirtualInterfacesExistForSwitchId) {
  // Create state with only virtual interface on specific switch
  auto state = createStateWithMultipleInterfaces({
      {InterfaceID(100), VlanID(100), true /* virtual */, SwitchID(0)},
  });

  // Should throw when only virtual interfaces exist for the specified switch
  EXPECT_THROW(
      utility::firstInterfaceIDWithPorts(state, SwitchID(0)), FbossError);
}
