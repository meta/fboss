// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ValidateInterfaceDelta.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace facebook::fboss {

namespace {

const MacAddress kMacA("02:00:00:00:00:01");
const MacAddress kMacB("02:00:00:00:00:02");

// Helper to create a minimal state with switch settings.
std::shared_ptr<SwitchState> createBaseState() {
  auto state = std::make_shared<SwitchState>();
  addSwitchInfo(state);
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  registerPort(state, PortID(1), "port1", matcher);
  registerPort(state, PortID(2), "port2", matcher);
  return state;
}

// Helper to add a PORT-type interface (non-VLAN) to the state.
std::shared_ptr<Interface> addPortInterface(
    std::shared_ptr<SwitchState>& state,
    const InterfaceID& intfId,
    const PortID& portId) {
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  auto intf = std::make_shared<Interface>(
      intfId,
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece(folly::to<std::string>("fboss", intfId)),
      MacAddress("00:02:00:00:00:55"),
      9000,
      false,
      false,
      cfg::InterfaceType::PORT);
  intf->setPortID(portId);
  Interface::Addresses addrs;
  addrs.emplace(IPAddressV4("10.0.0.1"), 24);
  addrs.emplace(IPAddressV6("2401:db00:2110:3001::1"), 64);
  intf->setAddresses(addrs);
  state->getInterfaces()->modify(&state)->addNode(intf, matcher);
  return intf;
}

// Helper to add a VLAN-type interface to the state.
std::shared_ptr<Interface> addVlanInterface(
    std::shared_ptr<SwitchState>& state,
    const InterfaceID& intfId,
    VlanID vlanId) {
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  auto vlan = std::make_shared<Vlan>(vlanId, std::string("TestVlan"));
  state->getVlans()->addNode(vlan, matcher);
  auto intf = std::make_shared<Interface>(
      intfId,
      std::optional<VlanID>(vlanId),
      folly::StringPiece(folly::to<std::string>("fboss", intfId)),
      MacAddress("00:02:00:00:00:55"),
      9000,
      false,
      false,
      cfg::InterfaceType::VLAN);
  Interface::Addresses addrs;
  addrs.emplace(IPAddressV4("10.0.1.1"), 24);
  addrs.emplace(IPAddressV6("2401:db00:2110:3002::1"), 64);
  intf->setAddresses(addrs);
  state->getInterfaces()->modify(&state)->addNode(intf, matcher);
  return intf;
}

// Helper to add an ARP neighbor to an interface.
void addArpEntry(
    std::shared_ptr<SwitchState>& state,
    InterfaceID intfId,
    IPAddressV4 ip,
    MacAddress mac,
    const PortID& portId,
    NeighborState nbrState = NeighborState::REACHABLE) {
  auto arpTable =
      state->getInterfaces()->getNode(intfId)->getArpTable()->modify(
          intfId, &state);
  arpTable->addEntry(ip, mac, PortDescriptor(portId), intfId, nbrState);
}

// Helper to add an NDP neighbor to an interface.
void addNdpEntry(
    std::shared_ptr<SwitchState>& state,
    InterfaceID intfId,
    IPAddressV6 ip,
    MacAddress mac,
    const PortID& portId,
    NeighborState nbrState = NeighborState::REACHABLE) {
  auto ndpTable =
      state->getInterfaces()->getNode(intfId)->getNdpTable()->modify(
          intfId, &state);
  ndpTable->addEntry(ip, mac, PortDescriptor(portId), intfId, nbrState);
}

} // namespace

class IntfDeltaValidatorTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enforce_single_nbr_mac_per_intf = true;
  }
  void TearDown() override {
    FLAGS_enforce_single_nbr_mac_per_intf = false;
  }
};

// --- PORT interface: same MAC for all neighbors (valid) ---

TEST_F(IntfDeltaValidatorTest, PortIntfSameMacAllNeighborsValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  addNdpEntry(
      state1,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacA,
      PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- PORT interface: different MAC for different neighbors (invalid) ---

TEST_F(IntfDeltaValidatorTest, PortIntfDifferentMacNeighborsInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacB, PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- PORT interface: different MAC across ARP and NDP (invalid) ---

TEST_F(IntfDeltaValidatorTest, PortIntfDifferentMacArpNdpInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addNdpEntry(
      state1,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacB,
      PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- VLAN interface: different MACs allowed (not validated) ---

TEST_F(IntfDeltaValidatorTest, VlanIntfDifferentMacNeighborsValid) {
  auto state0 = createBaseState();
  addVlanInterface(state0, InterfaceID(100), VlanID(100));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(100), IPAddressV4("10.0.1.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(100), IPAddressV4("10.0.1.3"), kMacB, PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- FLAG disabled: different MACs on PORT interface allowed ---

TEST_F(IntfDeltaValidatorTest, FlagDisabledPortIntfDifferentMacAllowed) {
  FLAGS_enforce_single_nbr_mac_per_intf = false;

  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacB, PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- FLAG enabled: same MAC on PORT interface valid ---

TEST_F(IntfDeltaValidatorTest, FlagEnabledPortIntfSameMacValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addNdpEntry(
      state1,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacA,
      PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Neighbor MAC change: same new MAC for all neighbors (valid) ---

TEST_F(IntfDeltaValidatorTest, NeighborMacChangeSameNewMacValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  // Build validator from state0
  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change neighbor MAC from A to B
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Neighbor MAC change: two neighbors, different new MACs (invalid) ---

TEST_F(IntfDeltaValidatorTest, TwoNeighborsMacChangeDifferentNewMacsInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change both neighbors, one to MAC_B while the other stays MAC_A
  // This creates a transient multi-MAC state
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Two neighbors same MAC, both change to same new MAC (invalid) ---
// SaiSwitch processes changes one at a time. After changeNeighbor(N1):
// unref(MAC_A) decrements refcount to 1 (N2 still references MAC_A),
// then ref(MAC_B) fails because MAC_A is still the stored value.
// Hardware would transiently see N1=MAC_B and N2=MAC_A.

TEST_F(IntfDeltaValidatorTest, TwoNeighborsBothChangeSameNewMacInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change both neighbors from MAC_A to MAC_B
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Pending neighbors are not tracked ---

TEST_F(IntfDeltaValidatorTest, PendingNeighborsNotTracked) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  // Add two pending neighbors with different MACs - should be fine
  auto state1 = state0->clone();
  addArpEntry(
      state1,
      InterfaceID(1),
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortID(1),
      NeighborState::PENDING);
  addArpEntry(
      state1,
      InterfaceID(1),
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortID(1),
      NeighborState::PENDING);
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Reachable to pending transition (decrement) ---

TEST_F(IntfDeltaValidatorTest, ReachableToPendingDecrementsRefCount) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change one neighbor to pending, add a new one with different MAC
  // After the pending transition, only one neighbor remains with kMacA.
  // Then adding a new neighbor with kMacB while kMacA still exists should fail.
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::PENDING);
  arpTable->addEntry(
      IPAddressV4("10.0.0.4"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- All neighbors go pending, then new MAC is allowed ---

TEST_F(IntfDeltaValidatorTest, AllNeighborsPendingThenNewMacAllowed) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Transition the only neighbor to pending, and add a new reachable one
  // with a different MAC. Since all old neighbors are gone, new MAC is OK.
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::PENDING);
  arpTable->addEntry(
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Two neighbors go pending, then resolve with new MAC (valid) ---
// Delta 1: both REACHABLE(MAC_A) → PENDING — refcount drops to 0, MAC_A
// removed. Delta 2: both PENDING → REACHABLE(MAC_B) — fresh start, MAC_B is the
// new value.

TEST_F(IntfDeltaValidatorTest, BothPendingThenResolveNewMacValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Delta 1: both neighbors become pending
  auto state1 = state0->clone();
  auto arpTable1 = state1->getInterfaces()
                       ->getNode(InterfaceID(1))
                       ->getArpTable()
                       ->modify(InterfaceID(1), &state1);
  arpTable1->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::PENDING);
  arpTable1->updateEntry(
      IPAddressV4("10.0.0.3"),
      kMacA,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::PENDING);
  state1->publish();

  // isValidDelta mutates internal state, so no need to call stateChanged
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));

  // Delta 2: both neighbors resolve with MAC_B
  auto state2 = state1->clone();
  auto arpTable2 = state2->getInterfaces()
                       ->getNode(InterfaceID(1))
                       ->getArpTable()
                       ->modify(InterfaceID(1), &state2);
  arpTable2->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  arpTable2->updateEntry(
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state2->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state1, state2)));
}

// --- stateChanged updates internal state for subsequent validations ---

TEST_F(IntfDeltaValidatorTest, StateChangedUpdatesForSubsequentValidation) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  IntfDeltaValidator validator;

  // First delta: add neighbor with kMacA
  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state1->publish();

  // isValidDelta mutates internal state, no need to call stateChanged
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));

  // Second delta: add another neighbor with kMacB - should fail because
  // validator now knows about kMacA
  auto state2 = state1->clone();
  addArpEntry(
      state2, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacB, PortID(1));
  state2->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state1, state2)));
}

// --- Interface removal clears state ---

TEST_F(IntfDeltaValidatorTest, InterfaceRemovalClearsState) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Remove the interface
  auto state1 = state0->clone();
  state1->getInterfaces()->modify(&state1)->removeNode(InterfaceID(1));
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- stateChanged from SwitchState builds correct state ---

TEST_F(IntfDeltaValidatorTest, StateChangedFromState) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  // Validator initialized from state should know about kMacA
  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Adding a neighbor with different MAC should fail
  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.4"), kMacB, PortID(1));
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- FLAG disabled: stateChanged is a no-op ---

TEST_F(IntfDeltaValidatorTest, FlagDisabledStateChangedNoOp) {
  FLAGS_enforce_single_nbr_mac_per_intf = false;

  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  state0->publish();

  IntfDeltaValidator validator;

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state1->publish();

  // stateChanged should be no-op when flag is disabled
  validator.updateRejected(StateDelta(state0, state1));

  // Re-enable flag - validator should have no state, so different MAC is fine
  FLAGS_enforce_single_nbr_mac_per_intf = true;

  auto state2 = state1->clone();
  addArpEntry(
      state2, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacB, PortID(1));
  state2->publish();

  // Since stateChanged was a no-op, validator doesn't know about kMacA,
  // but this delta adds both kMacA (existing in state1) and kMacB (new).
  // The delta only sees the addition of the new neighbor, so kMacB is the
  // first MAC seen and it succeeds.
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state1, state2)));
}

// --- FLAG disabled: stateChanged from state is a no-op ---

TEST_F(IntfDeltaValidatorTest, FlagDisabledStateChangedFromStateNoOp) {
  FLAGS_enforce_single_nbr_mac_per_intf = false;

  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  // stateChanged should be no-op when flag is disabled
  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Re-enable flag
  FLAGS_enforce_single_nbr_mac_per_intf = true;

  // Adding a neighbor with different MAC should pass because stateChanged
  // didn't build any state
  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacB, PortID(1));
  state1->publish();

  // Delta only shows addition of kMacB, and validator has no prior state,
  // so kMacB becomes the first MAC and succeeds.
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- REACHABLE -> REACHABLE with same MAC is a no-op ---

TEST_F(IntfDeltaValidatorTest, ReachableToReachableSameMacNoOp) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change neighbor but keep same MAC (e.g. port change)
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortDescriptor(PortID(2)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- PENDING -> PENDING change is skipped ---

TEST_F(IntfDeltaValidatorTest, PendingToPendingChangeSkipped) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0,
      InterfaceID(1),
      IPAddressV4("10.0.0.2"),
      kMacA,
      PortID(1),
      NeighborState::PENDING);
  state0->publish();

  IntfDeltaValidator validator;

  // Change pending neighbor's MAC (still pending) - should be ignored
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::PENDING);
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Pending neighbor removal is skipped ---

TEST_F(IntfDeltaValidatorTest, PendingNeighborRemovalSkipped) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0,
      InterfaceID(1),
      IPAddressV4("10.0.0.3"),
      kMacA,
      PortID(1),
      NeighborState::PENDING);
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Remove the pending neighbor - should not affect refcount
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->removeEntry(IPAddressV4("10.0.0.3"));
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- VLAN interface with same MAC: valid (VLAN skipped) ---

TEST_F(IntfDeltaValidatorTest, VlanIntfSameMacValid) {
  auto state0 = createBaseState();
  addVlanInterface(state0, InterfaceID(100), VlanID(100));
  state0->publish();

  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(100), IPAddressV4("10.0.1.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(100), IPAddressV4("10.0.1.3"), kMacA, PortID(1));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Neighbor removed: valid ---

TEST_F(IntfDeltaValidatorTest, NeighborRemovedValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.3"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Remove one neighbor
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->removeEntry(IPAddressV4("10.0.0.2"));
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- NDP neighbor change with different MAC (invalid) ---

TEST_F(IntfDeltaValidatorTest, NdpNeighborMacChangeInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addNdpEntry(
      state0,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacA,
      PortID(1));
  addNdpEntry(
      state0,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::3"),
      kMacA,
      PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change one NDP neighbor's MAC while the other stays
  auto state1 = state0->clone();
  auto ndpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getNdpTable()
                      ->modify(InterfaceID(1), &state1);
  ndpTable->updateEntry(
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- NDP neighbor removal valid ---

TEST_F(IntfDeltaValidatorTest, NdpNeighborRemovedValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addNdpEntry(
      state0,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacA,
      PortID(1));
  addNdpEntry(
      state0,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::3"),
      kMacA,
      PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  auto state1 = state0->clone();
  auto ndpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getNdpTable()
                      ->modify(InterfaceID(1), &state1);
  ndpTable->removeEntry(IPAddressV6("2401:db00:2110:3001::2"));
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Mixed ARP and NDP changes in same delta (invalid) ---

TEST_F(IntfDeltaValidatorTest, MixedArpNdpChangesSameDeltaInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addNdpEntry(
      state0,
      InterfaceID(1),
      IPAddressV6("2401:db00:2110:3001::2"),
      kMacA,
      PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Change ARP neighbor to MAC_B while NDP neighbor still has MAC_A
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.2"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Remove all neighbors then add with different MAC in same delta (valid)
// ---

TEST_F(IntfDeltaValidatorTest, RemoveAllThenAddDifferentMacValid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Remove old neighbor and add new one with different MAC in same delta
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->removeEntry(IPAddressV4("10.0.0.2"));
  arpTable->addEntry(
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Multiple interfaces in one delta: independent tracking ---

TEST_F(IntfDeltaValidatorTest, MultipleInterfacesIndependentTracking) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addPortInterface(state0, InterfaceID(2), PortID(2));
  state0->publish();

  // Add neighbors with different MACs on different interfaces - should be valid
  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(2), IPAddressV4("10.0.0.3"), kMacB, PortID(2));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_TRUE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- Multiple interfaces: one valid, one invalid ---

TEST_F(IntfDeltaValidatorTest, MultipleInterfacesOneInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addPortInterface(state0, InterfaceID(2), PortID(2));
  state0->publish();

  // Interface 1: same MAC (valid), Interface 2: different MACs (invalid)
  auto state1 = state0->clone();
  addArpEntry(
      state1, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state1, InterfaceID(2), IPAddressV4("10.0.0.3"), kMacA, PortID(2));
  addArpEntry(
      state1, InterfaceID(2), IPAddressV4("10.0.0.4"), kMacB, PortID(2));
  state1->publish();

  IntfDeltaValidator validator;
  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

// --- PENDING -> REACHABLE with conflicting MAC (invalid) ---

TEST_F(IntfDeltaValidatorTest, PendingToReachableConflictingMacInvalid) {
  auto state0 = createBaseState();
  addPortInterface(state0, InterfaceID(1), PortID(1));
  addArpEntry(
      state0, InterfaceID(1), IPAddressV4("10.0.0.2"), kMacA, PortID(1));
  addArpEntry(
      state0,
      InterfaceID(1),
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortID(1),
      NeighborState::PENDING);
  state0->publish();

  IntfDeltaValidator validator;
  validator.updateRejected(StateDelta(std::make_shared<SwitchState>(), state0));

  // Pending neighbor resolves with MAC_B, conflicting with existing MAC_A
  auto state1 = state0->clone();
  auto arpTable = state1->getInterfaces()
                      ->getNode(InterfaceID(1))
                      ->getArpTable()
                      ->modify(InterfaceID(1), &state1);
  arpTable->updateEntry(
      IPAddressV4("10.0.0.3"),
      kMacB,
      PortDescriptor(PortID(1)),
      InterfaceID(1),
      NeighborState::REACHABLE);
  state1->publish();

  EXPECT_FALSE(validator.isValidDelta(StateDelta(state0, state1)));
}

} // namespace facebook::fboss
