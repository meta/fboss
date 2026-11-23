// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "fboss/agent/state/VrfMap.h"

#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vrf.h"

namespace facebook::fboss {

MultiSwitchVrfMap* MultiSwitchVrfMap::modify(
    std::shared_ptr<SwitchState>* state) {
  return SwitchState::modify<switch_state_tags::vrfMaps>(state);
}

template struct ThriftMapNode<VrfMap, VrfMapTraits>;

} // namespace facebook::fboss
