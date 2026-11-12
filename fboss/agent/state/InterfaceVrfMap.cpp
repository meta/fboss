// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "fboss/agent/state/InterfaceVrfMap.h"

#include "fboss/agent/state/InterfaceVrf.h"
#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

MultiSwitchInterfaceVrfMap* MultiSwitchInterfaceVrfMap::modify(
    std::shared_ptr<SwitchState>* state) {
  return SwitchState::modify<switch_state_tags::interfaceVrfMaps>(state);
}

RouterID MultiSwitchInterfaceVrfMap::getVrf(InterfaceID intf) const {
  auto entry = getNodeIf(intf);
  return entry ? entry->getVrf() : RouterID(0);
}

template struct ThriftMapNode<InterfaceVrfMap, InterfaceVrfMapTraits>;

} // namespace facebook::fboss
