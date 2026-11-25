// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "fboss/agent/VrfResourceAccountant.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

using VrfResourceAccountantTest = ResourceAccountantTestBase;

TEST_F(VrfResourceAccountantTest, vrfResourceExceeded) {
  // MockAsic supports 8 VRFs, including the default VRF
  HwSwitchMatcher vrfScope(std::unordered_set<SwitchID>{SwitchID(0)});
  auto addVrfs = [&](const std::shared_ptr<SwitchState>& state,
                     int32_t first,
                     int32_t last) {
    auto newState = state->clone();
    auto vrfs = newState->getVrfs()->modify(&newState);
    for (auto id = first; id <= last; ++id) {
      vrfs->addNode(std::make_shared<Vrf>(RouterID(id)), vrfScope);
    }
    newState->publish();
    return newState;
  };

  VrfResourceAccountant accountant(asicTable_.get());
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();
  auto stateWith7 = addVrfs(oldState, 1, 7);
  EXPECT_TRUE(accountant.isValidUpdate(StateDelta(oldState, stateWith7)));
  EXPECT_EQ(accountant.vrfUsage_, 7);

  auto stateWith8 = addVrfs(stateWith7, 8, 8);
  EXPECT_FALSE(accountant.isValidUpdate(StateDelta(stateWith7, stateWith8)));
}

} // namespace facebook::fboss
