/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

DECLARE_bool(enable_acl_table_group);

namespace {
const std::string kTunIntfPrefix = "fboss";
} // anonymous namespace

namespace facebook::fboss::utility {

std::shared_ptr<AclEntry> getAclEntryByName(
    const std::shared_ptr<SwitchState> state,
    const std::string& aclName) {
  if (FLAGS_enable_acl_table_group) {
    auto aclMap = state->getAclsForTable(
        cfg::AclStage::INGRESS,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
    return aclMap ? aclMap->getNodeIf(aclName) : nullptr;
  }
  return state->getAcl(aclName);
}

std::optional<std::string> getAclTableNameForEntry(
    const std::shared_ptr<SwitchState> state,
    const std::string& aclEntryId) {
  if (!FLAGS_enable_acl_table_group) {
    return cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  }

  auto aclTableGroups = state->getAclTableGroups();
  if (!aclTableGroups) {
    return std::nullopt;
  }

  for (const auto& groupMap : std::as_const(*aclTableGroups)) {
    for (const auto& [stage, group] : std::as_const(*groupMap.second)) {
      for (const auto& [tableName, table] :
           std::as_const(*group->getAclTableMap())) {
        if (table->getAclMap()->getNodeIf(aclEntryId)) {
          return tableName;
        }
      }
    }
  }
  return std::nullopt;
}

bool isTunIntfName(std::string const& ifName) {
  return ifName.find(kTunIntfPrefix) == 0;
}

std::string createTunIntfName(InterfaceID ifID) {
  return fmt::format("{}{}", kTunIntfPrefix, folly::to<std::string>(ifID));
}

InterfaceID getIDFromTunIntfName(std::string const& ifName) {
  if (!isTunIntfName(ifName)) {
    throw FbossError(ifName, " is not a valid tun interface");
  }

  return InterfaceID(atoi(ifName.substr(kTunIntfPrefix.size()).c_str()));
}

folly::MacAddress getInterfaceMac(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan) {
  return state->getInterfaces()->getInterfaceInVlan(vlan)->getMac();
}
folly::MacAddress getInterfaceMac(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID intf) {
  return state->getInterfaces()->getNode(intf)->getMac();
}

folly::MacAddress getMacForFirstInterfaceWithPorts(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId) {
  auto intfID = firstInterfaceIDWithPorts(state, switchId);
  return getInterfaceMac(state, intfID);
}

std::optional<InterfaceID> firstInterfaceIDWithPortsIf(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId,
    std::optional<cfg::Scope> scope) {
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>{switchId});
  auto intfMap = state->getInterfaces()->getMapNodeIf(matcher);
  if (intfMap) {
    for (const auto& [intfID, intf] : std::as_const(*intfMap)) {
      if (intf->isVirtual()) {
        continue;
      }
      if (scope.has_value() && intf->getScope() != scope.value()) {
        continue;
      }
      return InterfaceID(intfID);
    }
  }
  return std::nullopt;
}

InterfaceID firstInterfaceIDWithPorts(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId,
    std::optional<cfg::Scope> scope) {
  auto intfID = firstInterfaceIDWithPortsIf(state, switchId, scope);
  if (!intfID.has_value()) {
    throw FbossError(
        "No interface found in state for switchId: ",
        static_cast<int64_t>(switchId));
  }
  return intfID.value();
}

std::vector<folly::IPAddress> getIntfAddrs(
    const std::shared_ptr<SwitchState>& state,
    const InterfaceID& intf) {
  std::vector<folly::IPAddress> addrs;
  auto intfNode = state->getInterfaces()->getNode(intf);
  for (const auto& addr : std::as_const(*intfNode->getAddresses())) {
    addrs.emplace_back(addr.first);
  }
  return addrs;
}

std::vector<folly::IPAddressV4> getIntfAddrsV4(
    const std::shared_ptr<SwitchState>& state,
    const InterfaceID& intf) {
  std::vector<folly::IPAddressV4> addrs;

  for (auto addr : getIntfAddrs(state, intf)) {
    if (addr.isV4()) {
      addrs.emplace_back(addr.asV4());
    }
  }
  return addrs;
}

std::vector<folly::IPAddressV6> getIntfAddrsV6(
    const std::shared_ptr<SwitchState>& state,
    const InterfaceID& intf) {
  std::vector<folly::IPAddressV6> addrs;

  for (auto addr : getIntfAddrs(state, intf)) {
    if (addr.isV6()) {
      addrs.emplace_back(addr.asV6());
    }
  }
  return addrs;
}

std::shared_ptr<Interface> firstInterfaceWithPorts(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId) {
  auto intfID = utility::firstInterfaceIDWithPorts(state, switchId);
  return state->getInterfaces()->getNodeIf(intfID);
}

std::vector<StateDelta> computeReversedDeltas(
    const std::vector<fsdb::OperDelta>& operDeltas,
    const std::shared_ptr<SwitchState>& currentState,
    const std::shared_ptr<SwitchState>& intermediateState) {
  std::vector<StateDelta> stateDeltas;
  std::vector<StateDelta> reversedStateDeltas;

  // Get a temporary StateDelta vector from operDelta vector
  stateDeltas.reserve(operDeltas.size());
  auto oldState = currentState;
  for (const auto& delta : operDeltas) {
    stateDeltas.emplace_back(oldState, delta);
    oldState = stateDeltas.back().newState();
  }

  // Most often, there is just 1 delta
  // if intermediateState is currentState, then failure is in 1st delta
  // directly goto this initial state
  //
  // If failure is in the middle of the deltas
  // Generate delta in reverse order up to the intermediate state delta
  if (intermediateState != currentState) {
    for (const auto& delta : stateDeltas) {
      reversedStateDeltas.emplace(
          reversedStateDeltas.begin(), delta.newState(), delta.oldState());
      // perform a deep comparison
      if (*delta.newState() == *intermediateState) {
        break;
      }
    }
  }

  return reversedStateDeltas;
}

} // namespace facebook::fboss::utility
