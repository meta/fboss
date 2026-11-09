// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/rib/MySidMapUpdater.h"

#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

MySidMapUpdater::MySidMapUpdater(
    const SwitchIdScopeResolver* resolver,
    const MySidTable& mySidTable)
    : resolver_(resolver), mySidTable_(mySidTable) {}

std::shared_ptr<SwitchState> MySidMapUpdater::operator()(
    const std::shared_ptr<SwitchState>& state) {
  auto newMySids = createUpdatedMySidMap(mySidTable_, state->getMySids());
  if (!newMySids) {
    return state;
  }

  auto nextState = state;
  SwitchState::modify(&nextState);
  nextState->resetMySids(newMySids);
  return nextState;
}

std::shared_ptr<MultiSwitchMySidMap> MySidMapUpdater::createUpdatedMySidMap(
    const MySidTable& ribMySidTable,
    const std::shared_ptr<MultiSwitchMySidMap>& currentMySids) {
  bool updated = false;
  auto newMySids = std::make_shared<MultiSwitchMySidMap>();

  for (const auto& [cidr, ribMySid] : ribMySidTable) {
    auto id = ribMySid->getID();
    auto existingMySid = currentMySids->getNodeIf(id);
    std::shared_ptr<MySid> mySidToAdd;
    if (existingMySid) {
      if (existingMySid == ribMySid || *existingMySid == *ribMySid) {
        mySidToAdd = existingMySid;
      } else {
        mySidToAdd = ribMySid;
        updated = true;
      }
    } else {
      mySidToAdd = ribMySid;
      updated = true;
    }
    newMySids->addNode(mySidToAdd, resolver_->scope(mySidToAdd));
  }

  // Check for deleted entries
  for (const auto& miter : std::as_const(*currentMySids)) {
    for (const auto& [key, existingMySid] : std::as_const(*miter.second)) {
      if (!newMySids->getNodeIf(key)) {
        updated = true;
        break;
      }
    }
  }

  return updated ? newMySids : nullptr;
}

} // namespace facebook::fboss
