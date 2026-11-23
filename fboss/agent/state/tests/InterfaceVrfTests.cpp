// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/InterfaceVrf.h"
#include "fboss/agent/state/InterfaceVrfMap.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

using namespace facebook::fboss;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

TEST(InterfaceVrf, SerDeser) {
  auto entry = std::make_shared<InterfaceVrf>(InterfaceID(55), RouterID(1001));
  auto entryBack = std::make_shared<InterfaceVrf>(entry->toThrift());
  EXPECT_EQ(*entry, *entryBack);
}

TEST(InterfaceVrf, MissingEntryIsDefaultVrf) {
  auto state = std::make_shared<SwitchState>();
  auto interfaceVrfs = state->getInterfaceVrfs()->modify(&state);
  interfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(55), RouterID(1001)), scope());

  EXPECT_EQ(state->getInterfaceVrfs()->getVrf(InterfaceID(55)), RouterID(1001));
  EXPECT_EQ(state->getInterfaceVrfs()->getVrf(InterfaceID(1)), RouterID(0));
}

TEST(InterfaceVrf, SerDeserSwitchState) {
  auto state = std::make_shared<SwitchState>();
  auto interfaceVrfs = state->getInterfaceVrfs()->modify(&state);
  interfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(55), RouterID(1001)), scope());
  interfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(56), RouterID(1002)), scope());

  auto stateBack = SwitchState::fromThrift(state->toThrift());

  EXPECT_EQ(
      state->getInterfaceVrfs()->toThrift(),
      stateBack->getInterfaceVrfs()->toThrift());
}

TEST(InterfaceVrf, Delta) {
  auto oldState = std::make_shared<SwitchState>();
  auto oldInterfaceVrfs = oldState->getInterfaceVrfs()->modify(&oldState);
  oldInterfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(55), RouterID(1001)), scope());
  oldInterfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(56), RouterID(1001)), scope());
  oldState->publish();

  auto newState = oldState->clone();
  auto newInterfaceVrfs = newState->getInterfaceVrfs()->modify(&newState);
  newInterfaceVrfs->removeNode(InterfaceID(55));
  newInterfaceVrfs->updateNode(
      std::make_shared<InterfaceVrf>(InterfaceID(56), RouterID(1002)), scope());
  newInterfaceVrfs->addNode(
      std::make_shared<InterfaceVrf>(InterfaceID(57), RouterID(1001)), scope());
  newState->publish();

  std::set<InterfaceID> added, removed, changed;
  DeltaFunctions::forEachChanged(
      StateDelta(oldState, newState).getInterfaceVrfsDelta(),
      [&](const auto& oldEntry, const auto& /*newEntry*/) {
        changed.insert(oldEntry->getID());
      },
      [&](const auto& entry) { added.insert(entry->getID()); },
      [&](const auto& entry) { removed.insert(entry->getID()); });

  EXPECT_EQ(added, std::set<InterfaceID>{InterfaceID(57)});
  EXPECT_EQ(removed, std::set<InterfaceID>{InterfaceID(55)});
  EXPECT_EQ(changed, std::set<InterfaceID>{InterfaceID(56)});
}
