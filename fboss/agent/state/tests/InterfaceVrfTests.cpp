// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "fboss/agent/state/InterfaceVrf.h"
#include "fboss/agent/state/InterfaceVrfMap.h"
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
