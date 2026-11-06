/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/Port.h"

#include <folly/Conv.h>
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/state/NodeBase-defs.h"

using apache::thrift::TEnumTraits;
using folly::to;
using std::string;

namespace facebook::fboss {

state::RxSak PortFields::rxSakToThrift(
    const PortFields::MKASakKey& sakKey,
    const mka::MKASak& sak) {
  state::MKASakKey sakKeyThrift;
  *sakKeyThrift.sci() = sakKey.sci;
  *sakKeyThrift.associationNum() = sakKey.associationNum;
  state::RxSak rxSakThrift;
  *rxSakThrift.sakKey() = sakKeyThrift;
  *rxSakThrift.sak() = sak;
  return rxSakThrift;
}

std::pair<PortFields::MKASakKey, mka::MKASak> PortFields::rxSakFromThrift(
    state::RxSak rxSak) {
  PortFields::MKASakKey sakKey{
      *rxSak.sakKey()->sci(), *rxSak.sakKey()->associationNum()};
  return std::make_pair(sakKey, *rxSak.sak());
}

state::VlanInfo PortFields::VlanInfo::toThrift() const {
  state::VlanInfo vlanThrift;
  *vlanThrift.tagged() = tagged;
  *vlanThrift.priorityTagged() = priorityTagged;
  return vlanThrift;
}

// static
PortFields::VlanInfo PortFields::VlanInfo::fromThrift(
    const state::VlanInfo& vlanThrift) {
  return VlanInfo(*vlanThrift.tagged(), *vlanThrift.priorityTagged());
}

Port* Port::modify(std::shared_ptr<SwitchState>* state) {
  if (!isPublished()) {
    CHECK(!(*state)->isPublished());
    return this;
  }

  MultiSwitchPortMap* ports = (*state)->getPorts()->modify(state);
  const auto scope = ports->getNodeAndScope(getID()).second;
  auto newPort = clone();
  auto* ptr = newPort.get();
  ports->updateNode(std::move(newPort), scope);
  return ptr;
}

Port::Port(PortID id, const std::string& name) {
  set<switch_state_tags::portId>(id);
  set<switch_state_tags::portName>(name);
}

InterfaceID Port::getInterfaceID() const {
  // On VOQ/Fabric switches, port and interface have 1:1 relation.
  // For non VOQ/Fabric switches, in practice, a port is always part of a
  // single VLAN (and thus single interface).
  auto intfs = getInterfaceIDs();
  CHECK_EQ(intfs.size(), 1) << "Invalid interfaces for port " << getName();
  return InterfaceID(intfs.at(0));
}

void Port::setActiveErrors(const std::set<PortError>& errors) {
  set<switch_state_tags::activeErrors>(
      std::vector<PortError>(errors.begin(), errors.end()));
}

void Port::addError(PortError error) {
  auto& errors = safe_cref<switch_state_tags::activeErrors>()->impl();
  if (std::find(errors.cbegin(), errors.cend(), error) != errors.end()) {
    return;
  }
  auto portErrors = getActiveErrors();
  portErrors.push_back(error);
  set<switch_state_tags::activeErrors>(portErrors);
}

void Port::removeError(PortError error) {
  auto errors = getActiveErrors();
  std::erase(errors, error);
  set<switch_state_tags::activeErrors>(errors);
}

bool llrConfigChanged(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  if (oldPort->getLlrConfigName() != newPort->getLlrConfigName()) {
    return true;
  }
  auto oldLlr = oldPort->getLlrConfig();
  auto newLlr = newPort->getLlrConfig();
  if (oldLlr.has_value() != newLlr.has_value()) {
    return true;
  }
  return oldLlr.has_value() && *oldLlr.value() != *newLlr.value();
}

template struct ThriftStructNode<Port, state::PortFields>;

} // namespace facebook::fboss
