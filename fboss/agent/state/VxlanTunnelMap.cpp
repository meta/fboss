// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/state/VxlanTunnelMap.h"

#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/VxlanTunnel.h"

namespace facebook::fboss {

VxlanTunnelMap::VxlanTunnelMap() = default;

VxlanTunnelMap::~VxlanTunnelMap() = default;

void VxlanTunnelMap::addTunnel(const std::shared_ptr<VxlanTunnel>& tunnel) {
  addNode(tunnel);
}

void VxlanTunnelMap::updateTunnel(const std::shared_ptr<VxlanTunnel>& tunnel) {
  updateNode(tunnel);
}

void VxlanTunnelMap::removeTunnel(std::string id) {
  removeNodeIf(id);
}

MultiSwitchVxlanTunnelMap* MultiSwitchVxlanTunnelMap::modify(
    std::shared_ptr<SwitchState>* state) {
  return SwitchState::modify<switch_state_tags::vxlanTunnelMaps>(state);
}

template struct ThriftMapNode<VxlanTunnelMap, VxlanTunnelMapTraits>;
} // namespace facebook::fboss
