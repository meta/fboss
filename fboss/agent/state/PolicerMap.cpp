// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/state/PolicerMap.h"

#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

MultiSwitchPolicerMap* MultiSwitchPolicerMap::modify(
    std::shared_ptr<SwitchState>* state) {
  return SwitchState::modify<switch_state_tags::policerMaps>(state);
}

template struct ThriftMapNode<PolicerMap, PolicerMapTraits>;

} // namespace facebook::fboss
