// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/SwitchIdScopeResolver.h"
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwitchInfoUtils.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/MirrorOnDropReport.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/SflowCollector.h"
#include "fboss/agent/state/Srv6Tunnel.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/SystemPort.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VxlanTunnel.h"

namespace {

std::string switchInfoToSysPortRangesStr(
    const std::map<int64_t, facebook::fboss::cfg::SwitchInfo>&
        switchIdToSwitchInfo) {
  std::vector<std::string> entries;
  for (const auto& [id, info] : switchIdToSwitchInfo) {
    std::vector<std::string> globalRanges;
    for (const auto& range : *info.systemPortRanges()->systemPortRanges()) {
      globalRanges.push_back(
          "[" + std::to_string(*range.minimum()) + ", " +
          std::to_string(*range.maximum()) + "]");
    }
    std::vector<std::string> localRanges;
    for (const auto& range :
         *info.localSystemPortRanges()->systemPortRanges()) {
      localRanges.push_back(
          "[" + std::to_string(*range.minimum()) + ", " +
          std::to_string(*range.maximum()) + "]");
    }
    entries.push_back(
        "switchId=" + std::to_string(id) + " global={" +
        folly::join(", ", globalRanges) + "} local={" +
        folly::join(", ", localRanges) + "}");
  }
  return folly::join("; ", entries);
}

} // namespace

namespace facebook::fboss {

SwitchIdScopeResolver::SwitchIdScopeResolver(
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo)
    : switchIdToSwitchInfo_(switchIdToSwitchInfo) {
  auto voqSwitchIds = getSwitchIdsOfType(cfg::SwitchType::VOQ);
  auto npuSwitchIds = getSwitchIdsOfType(cfg::SwitchType::NPU);
  if (voqSwitchIds.size() && npuSwitchIds.size()) {
    throw FbossError(
        " Only one of "
        "voq, npu switch types can be present in a chassis");
  }
  if (voqSwitchIds.size() || npuSwitchIds.size()) {
    l3SwitchMatcher_ = std::make_unique<HwSwitchMatcher>(
        voqSwitchIds.size() ? voqSwitchIds : npuSwitchIds);
  }
  if (voqSwitchIds.size()) {
    voqSwitchMatcher_ = std::make_unique<HwSwitchMatcher>(voqSwitchIds);
  }
  std::unordered_set<SwitchID> allSwitchIds;
  for (const auto& switchIdAndInfo : switchIdToSwitchInfo_) {
    allSwitchIds.insert(SwitchID(switchIdAndInfo.first));
  }
  if (allSwitchIds.size()) {
    allSwitchMatcher_ = std::make_unique<HwSwitchMatcher>(allSwitchIds);
  }
}

std::unordered_set<SwitchID> SwitchIdScopeResolver::getSwitchIdsOfType(
    cfg::SwitchType type) const {
  std::unordered_set<SwitchID> ids;
  for (const auto& switchIdAndInfo : switchIdToSwitchInfo_) {
    if (switchIdAndInfo.second.switchType() == type) {
      ids.insert(SwitchID(switchIdAndInfo.first));
    }
  }
  return ids;
}

void SwitchIdScopeResolver::checkL3() const {
  if (!l3SwitchMatcher_) {
    throw FbossError(" One or more l3 switchIds must be set to get l3 scope");
  }
}
const HwSwitchMatcher& SwitchIdScopeResolver::l3SwitchMatcher() const {
  checkL3();
  return *l3SwitchMatcher_;
}

const HwSwitchMatcher& SwitchIdScopeResolver::allSwitchMatcher() const {
  if (!allSwitchMatcher_) {
    throw FbossError(
        "One or more all switchIds must be set to get allSwitch scope");
  }
  return *allSwitchMatcher_;
}

void SwitchIdScopeResolver::checkVoq() const {
  if (!voqSwitchMatcher_) {
    throw FbossError(" One or more voq switchIds must be set to get voq scope");
  }
}
const HwSwitchMatcher& SwitchIdScopeResolver::voqSwitchMatcher() const {
  checkVoq();
  return *voqSwitchMatcher_;
}

const HwSwitchMatcher& SwitchIdScopeResolver::scope(
    const std::shared_ptr<ControlPlane>& /*c*/) const {
  // ControlPlane (CPU port) is supported for L3 switches (VOQ/NPU) and FABRIC
  // switches. For L3 switches, use l3SwitchMatcher and for FABRIC switches
  //  use allSwitchMatcher
  if (l3SwitchMatcher_) {
    return *l3SwitchMatcher_;
  }
  // Non l3 switches with CPU ports
  return allSwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(PortID portId) const {
  for (const auto& switchIdAndSwitchInfo : switchIdToSwitchInfo_) {
    auto switchInfo = switchIdAndSwitchInfo.second;
    if (static_cast<int64_t>(portId) >=
            *switchIdAndSwitchInfo.second.portIdRange()->minimum() &&
        static_cast<int64_t>(portId) <=
            *switchIdAndSwitchInfo.second.portIdRange()->maximum()) {
      return HwSwitchMatcher(
          std::unordered_set<SwitchID>(
              {SwitchID(switchIdAndSwitchInfo.first)}));
    }
  }
  throw FbossError("No switch found for port ", portId);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::vector<PortID>& portIds) const {
  std::unordered_set<SwitchID> switchIds;
  for (const auto& portId : portIds) {
    auto portSwitchIds = scope(portId).switchIds();
    switchIds.insert(portSwitchIds.begin(), portSwitchIds.end());
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Port>& port) const {
  return scope(port->getID());
}

HwSwitchMatcher SwitchIdScopeResolver::scope(const cfg::Port& port) const {
  return scope(PortID(*port.logicalID()));
}

// ACLs are scoped to all L3 switches unless they match on a qualifier tied to a
// specific NPU. Today that is srcPort; add other NPU-specific ACL qualifiers
// here as they need per-switch scoping.
// Port 0 is the CPU port which is shared across all NPUs — ACLs referencing it
// apply to all L3 switches.
HwSwitchMatcher SwitchIdScopeResolver::scope(const cfg::AclEntry& acl) const {
  if (auto srcPort = acl.srcPort()) {
    if (*srcPort != 0) {
      return scope(PortID(*srcPort));
    }
  }
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<AclEntry>& acl) const {
  if (!acl) {
    return l3SwitchMatcher();
  }
  if (auto srcPort = acl->getSrcPort()) {
    if (*srcPort != 0) {
      return scope(PortID(*srcPort));
    }
  }
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::AggregatePort& aggPort) const {
  checkL3();
  std::unordered_set<SwitchID> switchIds;
  for (const auto& subport : *aggPort.memberPorts()) {
    auto subPortSwitchIds = scope(PortID(*subport.memberPortID())).switchIds();
    switchIds.insert(subPortSwitchIds.begin(), subPortSwitchIds.end());
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<AggregatePort>& aggPort) const {
  checkL3();
  std::unordered_set<SwitchID> switchIds;
  for (const auto& subport : aggPort->sortedSubports()) {
    auto portId = subport.portID;
    auto subPortSwitchIds = scope(portId).switchIds();
    switchIds.insert(subPortSwitchIds.begin(), subPortSwitchIds.end());
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(SystemPortID sysPortId) const {
  for (const auto& [id, info] : switchIdToSwitchInfo_) {
    if (withinRange(*info.systemPortRanges(), sysPortId)) {
      return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(id)}));
    } else if (withinRange(*info.localSystemPortRanges(), sysPortId)) {
      return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(id)}));
    }
  }

  XLOG(ERR) << "SystemPortID " << static_cast<int64_t>(sysPortId)
            << " NOT within any range: "
            << switchInfoToSysPortRangesStr(switchIdToSwitchInfo_);
  return voqSwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<SystemPort>& sysPort) const {
  return scope(sysPort->getID());
}

const HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Vlan>& vlan) const {
  // TODO - restrict vlan scope to L3 switches
  // Currently we create pseudo vlans on fabric switches
  if (vlan->getPortsInfo().empty()) {
    // interfaceID is 0 when no cfg::Interface claims this VLAN - a pseudo or
    // default vlan, which is never programmed as a RIF.
    const auto intfID = static_cast<int32_t>(vlan->getInterfaceID());
    if (intfID != 0) {
      for (const auto& [switchId, switchInfo] : switchIdToSwitchInfo_) {
        if (switchInfo.loopbackIntfId().has_value() &&
            *switchInfo.loopbackIntfId() == intfID) {
          return HwSwitchMatcher(
              std::unordered_set<SwitchID>({SwitchID(switchId)}));
        }
      }
    }

    // Nothing names an owner. Prefer the l3 switch over an arbitrary pick:
    // allSwitchMatcher() includes fabric switches, which never own a RIF.
    if (l3SwitchMatcher_) {
      if (l3SwitchMatcher_->switchIds().size() == 1) {
        return *l3SwitchMatcher_;
      }
      // Several l3 switches and a real RIF: the platform must say which ASIC
      // owns it, otherwise we would program it on the wrong one. A VLAN with
      // no interface (intfID 0) is not a RIF and is allowed through.
      CHECK_EQ(intfID, 0)
          << "Portless interface " << intfID << " on vlan "
          << static_cast<int32_t>(vlan->getID())
          << " has no SwitchInfo::loopbackIntfId, but this platform has "
          << l3SwitchMatcher_->switchIds().size() << " l3 switches";
    }

    return HwSwitchMatcher(
        std::unordered_set<SwitchID>(
            {*allSwitchMatcher().switchIds().begin()}));
  }
  std::unordered_set<SwitchID> switchIds;
  for (const auto& port : vlan->getPortsInfo()) {
    auto portSwitchIds = scope(PortID(port.first)).switchIds();
    switchIds.insert(portSwitchIds.begin(), portSwitchIds.end());
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<AclTableGroup>& /*aclTableGroup*/) const {
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::AclTableGroup& /*aclTableGroup*/) const {
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Interface>& intf,
    const std::shared_ptr<SwitchState>& state) const {
  switch (intf->getType()) {
    case cfg::InterfaceType::SYSTEM_PORT:
      return scope(SystemPortID(static_cast<int64_t>(intf->getID())));
    case cfg::InterfaceType::VLAN:
      return scope(state->getVlans()->getNode(intf->getVlanID()));
    case cfg::InterfaceType::PORT:
      // A port router interface is bound to either a physical port or an
      // aggregate port. Interface's setters keep the two mutually exclusive.
      if (auto aggPortID = intf->getAggregatePortIDf()) {
        return scope(state->getAggregatePorts()->getNode(*aggPortID));
      }
      return scope(intf->getPortID());
  }
  throw FbossError(
      "Unexpected interface type: ", static_cast<int>(intf->getType()));
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Interface>& intf,
    const cfg::SwitchConfig& cfg) const {
  return scope(intf->getType(), intf->getID(), cfg);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::InterfaceType& type,
    const InterfaceID& interfaceId,
    const cfg::SwitchConfig& cfg) const {
  switch (type) {
    case cfg::InterfaceType::SYSTEM_PORT:
      return scope(SystemPortID(static_cast<int64_t>(interfaceId)));
    case cfg::InterfaceType::VLAN: {
      std::optional<int> vlanId;
      for (const auto& intf : *cfg.interfaces()) {
        if (intf.intfID() == static_cast<int>(interfaceId)) {
          vlanId = *intf.vlanID();
        }
      }
      if (!vlanId) {
        throw FbossError(
            "vlan not set for vlan router interface  : ", interfaceId);
      }
      if (*vlanId == *cfg.defaultVlan()) {
        return l3SwitchMatcher();
      }
      auto vitr = std::find_if(
          cfg.vlans()->cbegin(),
          cfg.vlans()->cend(),
          [vlanId](const auto& vlan) { return vlan.id() == *vlanId; });
      if (vitr == cfg.vlans()->cend()) {
        throw FbossError("No vlan found for : ", *vlanId);
      }
      Vlan::MemberPorts vlanMembers;
      for (const auto& vlanPort : *cfg.vlanPorts()) {
        if (vlanPort.vlanID() == *vlanId) {
          state::VlanInfo vlanInfo;
          *vlanInfo.tagged() = *vlanPort.emitTags();
          *vlanInfo.priorityTagged() = *vlanPort.emitPriorityTags();
          vlanMembers.emplace(*vlanPort.logicalPort(), vlanInfo);
        }
      }
      // cfg::Vlan::intfID is optional and coop does not set it, so a Vlan
      // built from config reports interface 0. Populate it from the id we
      // were handed, as ApplyThriftConfig does for state Vlans.
      auto vlan = std::make_shared<Vlan>(&*vitr, vlanMembers);
      vlan->setInterfaceID(interfaceId);
      return scope(vlan);
    }
    case cfg::InterfaceType::PORT: {
      auto itr = std::find_if(
          cfg.interfaces()->cbegin(),
          cfg.interfaces()->cend(),
          [interfaceId](const auto& intf) {
            return InterfaceID(*intf.intfID()) == interfaceId;
          });
      if (itr == cfg.interfaces()->cend()) {
        throw FbossError("No interface found for : ", interfaceId);
      }
      // A port router interface is bound to either a physical port or an
      // aggregate port, never both and never neither. This is the earliest
      // point at which the config is inspected for that binding, so reject a
      // malformed one here rather than letting it fail further down.
      auto aggPortID = itr->aggregatePortID();
      if (itr->portID().has_value() == aggPortID.has_value()) {
        throw FbossError(
            "Port router interface ",
            interfaceId,
            " must set exactly one of portID and aggregatePortID");
      }
      if (aggPortID) {
        auto aitr = std::find_if(
            cfg.aggregatePorts()->cbegin(),
            cfg.aggregatePorts()->cend(),
            [aggPortID](const auto& aggPort) {
              return *aggPort.key() == *aggPortID;
            });
        if (aitr == cfg.aggregatePorts()->cend()) {
          throw FbossError("No aggregate port found for : ", interfaceId);
        }
        return scope(*aitr);
      }
      auto pitr = std::find_if(
          cfg.ports()->cbegin(), cfg.ports()->cend(), [itr](const auto& port) {
            return *port.logicalID() == *(itr->portID());
          });
      if (pitr == cfg.ports()->cend()) {
        throw FbossError("No port found for : ", interfaceId);
      }
      const auto& port = *pitr;
      return scope(PortID(*port.logicalID()));
    }
  }
  throw FbossError("Unexpected interface type: ", static_cast<int>(type));
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<ForwardingInformationBaseContainer>& /*fibs*/) const {
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<FibInfo>& /*fibInfo*/) const {
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<LabelForwardingEntry>& /*entry*/) const {
  return l3SwitchMatcher();
}

const HwSwitchMatcher& SwitchIdScopeResolver::scope(
    const std::shared_ptr<SflowCollector>& /*entry*/) const {
  return l3SwitchMatcher();
}

const HwSwitchMatcher& SwitchIdScopeResolver::scope(
    const std::shared_ptr<SwitchSettings>& /*s*/) const {
  throw FbossError("Scope is per HwSwitch, implemented during config apply");
}

const HwSwitchMatcher& SwitchIdScopeResolver::scope(
    const cfg::SflowCollector& /*entry*/) const {
  return l3SwitchMatcher();
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<SwitchState>& state,
    const boost::container::flat_set<PortDescriptor>& ports) const {
  std::unordered_set<SwitchID> switchIds;

  for (auto port : ports) {
    auto matcher = scope(state, port);
    for (auto switchId : matcher.switchIds()) {
      switchIds.insert(switchId);
    }
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<SwitchState>& state,
    const PortDescriptor& portDesc) const {
  switch (portDesc.type()) {
    case PortDescriptor::PortType::PHYSICAL:
      return scope(state, portDesc.phyPortID());

    case PortDescriptor::PortType::AGGREGATE:
      return scope(state, portDesc.aggPortID());

    case PortDescriptor::PortType::SYSTEM_PORT:
      return scope(portDesc.sysPortID());
  }
  throw FbossError("unknown port type");
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<SwitchState>& state,
    const PortID& portId) const {
  auto port = state->getPorts()->getNode(portId);
  return scope(port);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<SwitchState>& state,
    const AggregatePortID& aggPortId) const {
  auto aggPport = state->getAggregatePorts()->getNode(aggPortId);
  return scope(aggPport);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(cfg::SwitchType type) const {
  std::unordered_set<SwitchID> switchIds;
  for (const auto& entry : switchIdToSwitchInfo_) {
    if (entry.second.switchType().value() != type) {
      continue;
    }
    switchIds.insert(SwitchID(entry.first));
  }
  if (switchIds.size() == 0) {
    throw FbossError(
        "No switches of type ", apache::thrift::util::enumNameSafe(type));
  }
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Mirror>& mirror) const {
  std::unordered_set<SwitchID> switchIds;
  switchIds.insert(SwitchID(mirror->getSwitchId()));
  return HwSwitchMatcher(switchIds);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::MirrorOnDropReport& report) const {
  return scope(PortID(folly::copy(report.mirrorPortId().value())));
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<MirrorOnDropReport>& report) const {
  auto mirrorPortId = report->getMirrorPortId();
  if (!mirrorPortId.has_value()) {
    throw FbossError(
        "MirrorOnDropReport ",
        report->getID(),
        " has no mirror port, so it names no owning switch");
  }
  return scope(mirrorPortId.value());
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::Srv6Tunnel& tunnel,
    const cfg::SwitchConfig& cfg) const {
  if (*tunnel.tunnelType() == TunnelType::SRV6_DECAP) {
    // A decap tunnel has no underlay interface; scope it to all L3 switches.
    return l3SwitchMatcher();
  }
  auto intfId = InterfaceID(*tunnel.underlayIntfID());
  for (const auto& intf : *cfg.interfaces()) {
    if (InterfaceID(*intf.intfID()) == intfId) {
      return scope(*intf.type(), intfId, cfg);
    }
  }
  throw FbossError(
      "No interface found for Srv6Tunnel underlay interface: ", intfId);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Srv6Tunnel>& tunnel,
    const std::shared_ptr<SwitchState>& state) const {
  if (tunnel->getType() == TunnelType::SRV6_DECAP) {
    // A decap tunnel has no underlay interface; scope it to all L3 switches.
    return l3SwitchMatcher();
  }
  auto intfId = tunnel->getUnderlayIntfId();
  auto intf = state->getInterfaces()->getNode(intfId);
  return scope(intf, state);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<Srv6Tunnel>& tunnel,
    const cfg::SwitchConfig& cfg) const {
  if (tunnel->getType() == TunnelType::SRV6_DECAP) {
    // A decap tunnel has no underlay interface; scope it to all L3 switches.
    return l3SwitchMatcher();
  }
  auto intfId = tunnel->getUnderlayIntfId();
  for (const auto& intf : *cfg.interfaces()) {
    if (InterfaceID(*intf.intfID()) == intfId) {
      return scope(*intf.type(), intfId, cfg);
    }
  }
  throw FbossError(
      "No interface found for Srv6Tunnel underlay interface: ", intfId);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const cfg::VxLanTunnel& tunnel,
    const cfg::SwitchConfig& cfg) const {
  auto intfId = InterfaceID(*tunnel.underlayIntfID());
  for (const auto& intf : *cfg.interfaces()) {
    if (InterfaceID(*intf.intfID()) == intfId) {
      return scope(*intf.type(), intfId, cfg);
    }
  }
  throw FbossError(
      "No interface found for VxlanTunnel underlay interface: ", intfId);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<VxlanTunnel>& tunnel,
    const std::shared_ptr<SwitchState>& state) const {
  auto intfId = tunnel->getUnderlayIntfId();
  auto intf = state->getInterfaces()->getNode(intfId);
  return scope(intf, state);
}

HwSwitchMatcher SwitchIdScopeResolver::scope(
    const std::shared_ptr<VxlanTunnel>& tunnel,
    const cfg::SwitchConfig& cfg) const {
  auto intfId = tunnel->getUnderlayIntfId();
  for (const auto& intf : *cfg.interfaces()) {
    if (InterfaceID(*intf.intfID()) == intfId) {
      return scope(*intf.type(), intfId, cfg);
    }
  }
  throw FbossError(
      "No interface found for VxlanTunnel underlay interface: ", intfId);
}

} // namespace facebook::fboss
