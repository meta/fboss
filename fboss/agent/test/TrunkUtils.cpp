/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/TrunkUtils.h"

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss::utility {

cfg::AggregatePortMember makePortMember(int32_t port, cfg::LacpPortRate rate) {
  static auto constexpr kAggPriority = 32768;
  cfg::AggregatePortMember aggMember;
  *aggMember.memberPortID() = port;
  *aggMember.priority() = kAggPriority;
  aggMember.rate() = rate;
  return aggMember;
}

namespace {
// A VLAN left with no ports must be dropped; otherwise it diverges between cold
// boot and warmboot.
void removeUnreferencedVlans(
    cfg::SwitchConfig* config,
    const std::set<int32_t>& memberOldVlans,
    int32_t aggVlan) {
  // TODO:remove this once cisco fixes the LAG vlan removal issue
  bool isCiscoAsic = false;
  for (const auto& [_, switchInfo] :
       *config->switchSettings()->switchIdToSwitchInfo()) {
    auto asicType = *switchInfo.asicType();
    if (asicType == cfg::AsicType::ASIC_TYPE_EBRO ||
        asicType == cfg::AsicType::ASIC_TYPE_YUBA ||
        asicType == cfg::AsicType::ASIC_TYPE_P200 ||
        asicType == cfg::AsicType::ASIC_TYPE_GARONNE ||
        asicType == cfg::AsicType::ASIC_TYPE_G202X) {
      isCiscoAsic = true;
      break;
    }
  }
  if (isCiscoAsic) {
    return;
  }

  std::set<int32_t> stillReferenced;
  for (const auto& vlanPort : *config->vlanPorts()) {
    stillReferenced.insert(vlanPort.vlanID().value());
  }

  for (auto oldVlan : memberOldVlans) {
    if (oldVlan == aggVlan || stillReferenced.contains(oldVlan) ||
        (oldVlan == *config->defaultVlan())) {
      continue;
    }

    auto& vlans = *config->vlans();
    vlans.erase(
        std::remove_if(
            vlans.begin(),
            vlans.end(),
            [&](const auto& v) { return v.id().value() == oldVlan; }),
        vlans.end());

    auto& intfs = *config->interfaces();
    intfs.erase(
        std::remove_if(
            intfs.begin(),
            intfs.end(),
            [&](const auto& i) {
              return i.type().value() == cfg::InterfaceType::VLAN &&
                  i.vlanID().value() == oldVlan;
            }),
        intfs.end());
  }
}
} // namespace

void addAggPort(
    int key,
    const std::vector<int32_t>& ports,
    cfg::SwitchConfig* config,
    cfg::LacpPortRate rate,
    double minLinkPercentage,
    cfg::AggregatePortType aggregatePortType,
    std::optional<std::string> aggPortName) {
  // Create agg port with requisite members
  static constexpr auto kAggPortName = "AGG";
  cfg::AggregatePort aggPort;
  aggPort.key() = key;
  if (aggPortName.has_value()) {
    aggPort.name() = *aggPortName;
    aggPort.description() = *aggPortName;
  } else {
    aggPort.name() = folly::to<std::string>(kAggPortName, "-", key);
    aggPort.description() = kAggPortName;
  }
  aggPort.minimumCapacity()->set_linkPercentage(minLinkPercentage);
  aggPort.aggregatePortType() = aggregatePortType;
  for (auto port : ports) {
    aggPort.memberPorts()->push_back(makePortMember(port, rate));
  }
  config->aggregatePorts()->push_back(aggPort);
  // Set VLAN for all members to be the same
  std::set<uint32_t> memberPorts(ports.begin(), ports.end());
  std::optional<int32_t> aggVlan;
  std::set<int32_t> memberOldVlans;
  for (auto& vlanPort : *config->vlanPorts()) {
    if (memberPorts.contains(vlanPort.logicalPort().value())) {
      int32_t oldVlan = vlanPort.vlanID().value();
      memberOldVlans.insert(oldVlan);
      if (!aggVlan) {
        aggVlan = oldVlan;
      }
      vlanPort.vlanID() = *aggVlan;
    }
  }

  if (aggVlan.has_value()) {
    removeUnreferencedVlans(config, memberOldVlans, *aggVlan);
  }

  if (aggregatePortType == cfg::AggregatePortType::LAG_PORT) {
    // Set ingress VLAN for all members to be the same
    for (auto& port : *config->ports()) {
      if (memberPorts.contains(port.logicalID().value())) {
        if (!aggVlan) {
          throw FbossError("No VLAN found for aggregate port in addAggPort");
        }
        port.ingressVlan() = *aggVlan;
      }
    }
  }
}

void addAggPortWithRouterInterface(
    int key,
    const std::vector<int32_t>& ports,
    cfg::SwitchConfig* config,
    cfg::LacpPortRate rate,
    double minLinkPercentage) {
  static constexpr auto kAggPortName = "AGG";
  cfg::AggregatePort aggPort;
  aggPort.key() = key;
  aggPort.name() = folly::to<std::string>(kAggPortName, "-", key);
  aggPort.description() = kAggPortName;
  aggPort.minimumCapacity()->set_linkPercentage(minLinkPercentage);
  aggPort.aggregatePortType() = cfg::AggregatePortType::LAG_PORT;
  for (auto port : ports) {
    aggPort.memberPorts()->push_back(makePortMember(port, rate));
  }
  config->aggregatePorts()->push_back(aggPort);

  std::set<int32_t> memberPorts(ports.begin(), ports.end());
  auto& interfaces = *config->interfaces();
  auto isMemberIntf = [&memberPorts](const auto& intf) {
    return intf.portID().has_value() && memberPorts.contains(*intf.portID());
  };

  auto firstMemberIntf =
      std::find_if(interfaces.begin(), interfaces.end(), isMemberIntf);
  if (firstMemberIntf == interfaces.end()) {
    throw FbossError(
        "No port router interface found for members of aggregate port ", key);
  }
  // Rebind the first member's interface to the aggregate, keeping its id, mac
  // and addresses, then drop the rest. Both in one pass, so no member port is
  // ever left holding a router interface of its own.
  firstMemberIntf->portID().reset();
  firstMemberIntf->aggregatePortID() = key;
  auto aggIntfID = *firstMemberIntf->intfID();

  interfaces.erase(
      std::remove_if(
          interfaces.begin(),
          interfaces.end(),
          [&isMemberIntf, aggIntfID](const auto& intf) {
            return *intf.intfID() != aggIntfID && isMemberIntf(intf);
          }),
      interfaces.end());
}

std::shared_ptr<SwitchState> enableTrunkPorts(
    std::shared_ptr<SwitchState> curState) {
  auto newState{curState};
  for (const auto& [_, aggPorts] :
       std::as_const(*newState->getAggregatePorts())) {
    for (const auto& idAndAggPort : std::as_const(*aggPorts)) {
      auto aggPort = idAndAggPort.second->modify(&newState);
      for (auto subPort : aggPort->sortedSubports()) {
        aggPort->setForwardingState(
            subPort.portID, AggregatePort::Forwarding::ENABLED);
      }
    }
  }
  return newState;
}

std::shared_ptr<SwitchState> disableTrunkPorts(
    std::shared_ptr<SwitchState> curState) {
  auto newState{curState};
  for (const auto& [_, aggPorts] :
       std::as_const(*newState->getAggregatePorts())) {
    for (const auto& idAndAggPort : std::as_const(*aggPorts)) {
      auto aggPort = idAndAggPort.second->modify(&newState);
      for (auto subPort : aggPort->sortedSubports()) {
        aggPort->setForwardingState(
            subPort.portID, AggregatePort::Forwarding::DISABLED);
      }
    }
  }
  return newState;
}

std::shared_ptr<SwitchState> setTrunkMinLinkCount(
    std::shared_ptr<SwitchState> curState,
    uint8_t minlinks) {
  auto newState{curState};
  for (const auto& [_, aggPorts] :
       std::as_const(*newState->getAggregatePorts())) {
    for (const auto& idAndAggPort : std::as_const(*aggPorts)) {
      auto aggPort = idAndAggPort.second->modify(&newState);
      aggPort->setMinimumLinkCount(minlinks);
    }
  }
  return newState;
}

std::shared_ptr<SwitchState> disableTrunkPort(
    std::shared_ptr<SwitchState> curState,
    const AggregatePortID& aggId,
    const facebook::fboss::PortID& portId) {
  auto newState{curState};
  auto aggPortOld = newState->getAggregatePorts()->getNodeIf(aggId);
  auto aggPort = aggPortOld->modify(&newState);
  aggPort->setForwardingState(portId, AggregatePort::Forwarding::DISABLED);
  return newState;
}

} // namespace facebook::fboss::utility
