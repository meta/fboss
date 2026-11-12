// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/state/VrfMap.h"

using namespace facebook::fboss;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

std::shared_ptr<Vrf> makeVrf(RouterID id, const std::string& name) {
  auto vrf = std::make_shared<Vrf>(id);
  vrf->setName(name);
  return vrf;
}
} // namespace

TEST(Vrf, NameIsOptional) {
  auto vrf = std::make_shared<Vrf>(RouterID(1001));
  EXPECT_EQ(vrf->getID(), RouterID(1001));
  EXPECT_EQ(vrf->getName(), std::nullopt);

  vrf->setName("red");
  EXPECT_EQ(vrf->getName(), "red");

  vrf->setName(std::nullopt);
  EXPECT_EQ(vrf->getName(), std::nullopt);
}

TEST(Vrf, SerDeser) {
  auto vrf = makeVrf(RouterID(1001), "red");
  auto vrfBack = std::make_shared<Vrf>(vrf->toThrift());
  EXPECT_EQ(*vrf, *vrfBack);
}

TEST(Vrf, AddRemove) {
  auto state = std::make_shared<SwitchState>();
  auto vrfs = state->getVrfs()->modify(&state);
  vrfs->addNode(makeVrf(RouterID(1001), "red"), scope());
  vrfs->addNode(makeVrf(RouterID(1002), "blue"), scope());

  vrfs->removeNode(RouterID(1001));

  EXPECT_EQ(state->getVrfs()->getNodeIf(RouterID(1001)), nullptr);
  EXPECT_EQ(state->getVrfs()->getNode(RouterID(1002))->getName(), "blue");
}

TEST(Vrf, SerDeserSwitchState) {
  auto state = std::make_shared<SwitchState>();
  auto vrfs = state->getVrfs()->modify(&state);
  vrfs->addNode(makeVrf(RouterID(1001), "red"), scope());
  vrfs->addNode(makeVrf(RouterID(1002), "blue"), scope());

  auto stateBack = SwitchState::fromThrift(state->toThrift());

  EXPECT_EQ(state->getVrfs()->toThrift(), stateBack->getVrfs()->toThrift());
}

TEST(Vrf, Delta) {
  auto oldState = std::make_shared<SwitchState>();
  auto oldVrfs = oldState->getVrfs()->modify(&oldState);
  oldVrfs->addNode(makeVrf(RouterID(1001), "red"), scope());
  oldVrfs->addNode(makeVrf(RouterID(1002), "blue"), scope());
  oldState->publish();

  auto newState = oldState->clone();
  auto newVrfs = newState->getVrfs()->modify(&newState);
  newVrfs->removeNode(RouterID(1001));
  newVrfs->updateNode(makeVrf(RouterID(1002), "green"), scope());
  newVrfs->addNode(makeVrf(RouterID(1003), "yellow"), scope());
  newState->publish();

  std::set<RouterID> added, removed, changed;
  DeltaFunctions::forEachChanged(
      StateDelta(oldState, newState).getVrfsDelta(),
      [&](const auto& oldVrf, const auto& /*newVrf*/) {
        changed.insert(oldVrf->getID());
      },
      [&](const auto& vrf) { added.insert(vrf->getID()); },
      [&](const auto& vrf) { removed.insert(vrf->getID()); });

  EXPECT_EQ(added, std::set<RouterID>{RouterID(1003)});
  EXPECT_EQ(removed, std::set<RouterID>{RouterID(1001)});
  EXPECT_EQ(changed, std::set<RouterID>{RouterID(1002)});
}
