/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <iterator>

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/RouteUpdateWrapper.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/utils/NeighborTestUtils.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>

using boost::container::flat_map;
using boost::container::flat_set;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace {

using namespace facebook::fboss;

std::optional<AggregatePortID> getAggPortID(
    const std::shared_ptr<SwitchState>& inputState,
    const PortID& portId) {
  for (const auto& [_, aggPorts] :
       std::as_const(*inputState->getAggregatePorts())) {
    for (auto idAndAggPort : std::as_const(*aggPorts)) {
      if (idAndAggPort.second->isMemberPort(portId)) {
        return idAndAggPort.second->getID();
      }
    }
  }
  return std::nullopt;
}

flat_map<InterfaceID, folly::CIDRNetwork> computeInterface2Subnet(
    const std::shared_ptr<SwitchState>& inputState,
    bool v6) {
  boost::container::flat_map<InterfaceID, folly::CIDRNetwork> intf2Network;
  auto getIntf2Network = [&](const auto& interfaces) {
    for (const auto& [_, intfMap] : std::as_const(*interfaces)) {
      for (auto iter : std::as_const(*intfMap)) {
        const auto& intf = iter.second;
        for (const auto& cidrStr : intf->getAddressesCopy()) {
          auto subnet = folly::IPAddress::createNetwork(cidrStr.first.str());
          if (!v6 && subnet.first.isV4()) {
            intf2Network[intf->getID()] = subnet;
          } else if (v6 && subnet.first.isV6() && !subnet.first.isLinkLocal()) {
            intf2Network[intf->getID()] = subnet;
          }
        }
      }
    }
  };
  getIntf2Network(inputState->getInterfaces());
  getIntf2Network(inputState->getRemoteInterfaces());
  return intf2Network;
}
} // namespace

namespace facebook::fboss::utility {

boost::container::flat_set<PortDescriptor> getSingleVlanOrRoutedCabledPorts(
    const SwSwitch* sw) {
  boost::container::flat_set<PortDescriptor> ports;

  if (!sw->getSwitchInfoTable().haveL3Switches()) {
    XLOG(INFO) << "No L3 switches found, skipping single vlan/routed ports";
    return {};
  }
  /*
   * VOQ switches do not have any vlan ports. They have only routed ports.
   * So, we need to get the routed ports which are of type INTERFACE_PORT.
   */
  if (sw->getSwitchInfoTable().l3SwitchType() == cfg::SwitchType::VOQ) {
    for (const auto& portMap : std::as_const(*sw->getState()->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if (port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
            port.second->getPortType() == cfg::PortType::HYPER_PORT) {
          ports.insert(PortDescriptor{PortID(port.second->getID())});
        }
      }
    }
  } else {
    for (const auto& intfTable :
         std::as_const(*sw->getState()->getInterfaces())) {
      for (const auto& intf : std::as_const(*intfTable.second)) {
        if (intf.second->getType() == cfg::InterfaceType::PORT) {
          auto port =
              sw->getState()->getPorts()->getNodeIf(intf.second->getPortID());
          if (port->getPortType() != cfg::PortType::INTERFACE_PORT) {
            continue;
          }
          ports.insert(PortDescriptor{port->getID()});
        } else if (intf.second->getType() == cfg::InterfaceType::VLAN) {
          auto vlan =
              sw->getState()->getVlans()->getNode(intf.second->getVlanID());
          auto memberPorts = vlan->getPortsInfo();
          if (memberPorts.size() == 1) {
            ports.insert(PortDescriptor{PortID(memberPorts.begin()->first)});
          }
        }
      }
    }
  }
  return ports;
}

template <typename AddrT, typename NextHopT>
BaseEcmpSetupHelper<AddrT, NextHopT>::BaseEcmpSetupHelper(
    bool needL2EntryForNeighbor)
    : needL2EntryForNeighbor_(needL2EntryForNeighbor) {}

template <typename AddrT, typename NextHopT>
flat_map<PortDescriptor, InterfaceID>
BaseEcmpSetupHelper<AddrT, NextHopT>::computePortDesc2Interface(
    const std::shared_ptr<SwitchState>& inputState,
    const std::set<cfg::PortType>& portTypes) const {
  boost::container::flat_map<PortDescriptor, InterfaceID> portDesc2Interface;
  HwSwitchMatcher matcher(
      std::unordered_set<SwitchID>{SwitchID(FLAGS_switch_id_for_testing)});
  std::set<PortID> portIds;
  auto portMap = inputState->getPorts()->getMapNodeIf(matcher);
  if (portMap) {
    for (const auto& port : std::as_const(*portMap)) {
      if (portTypes.find(port.second->getPortType()) != portTypes.end()) {
        portIds.insert(port.second->getID());
      }
    }
  }
  for (const auto& portId : portIds) {
    PortDescriptor portDesc = PortDescriptor(portId);
    auto aggId = getAggPortID(inputState, portId);
    if (aggId) {
      portDesc = PortDescriptor(*aggId);
    }

    if (auto intf = getInterface(portDesc, inputState)) {
      portDesc2Interface.insert(std::make_pair(portDesc, *intf));
    }
  }
  // Local SystemPorts (scoped to NPU under test)
  auto sysPortMap = inputState->getSystemPorts()->getMapNodeIf(matcher);
  if (sysPortMap) {
    for (const auto& systemPort : std::as_const(*sysPortMap)) {
      PortDescriptor portDesc = PortDescriptor(systemPort.second->getID());
      if (auto intf = getInterface(portDesc, inputState)) {
        portDesc2Interface.insert(std::make_pair(portDesc, *intf));
      }
    }
  }
  // Remote SystemPorts (from other DSF nodes, not filtered by local switch ID)
  for (const auto& remoteSysPortMap :
       std::as_const(*inputState->getRemoteSystemPorts())) {
    for (const auto& systemPort : std::as_const(*remoteSysPortMap.second)) {
      PortDescriptor portDesc = PortDescriptor(systemPort.second->getID());
      if (auto intf = getInterface(portDesc, inputState)) {
        portDesc2Interface.insert(std::make_pair(portDesc, *intf));
      }
    }
  }
  return portDesc2Interface;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const boost::container::flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  return resolveNextHopsImpl(
      inputState, portDescs, true, useLinkLocal, encapIdx);
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::unresolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const boost::container::flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal) const {
  return resolveNextHopsImpl(
      inputState, portDescs, false, useLinkLocal, std::nullopt);
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::resolveNextHopsImpl(
    const std::shared_ptr<SwitchState>& inputState,
    const boost::container::flat_set<PortDescriptor>& portDescs,
    bool resolve,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  auto outputState{inputState};
  for (auto nhop : nhops_) {
    if (portDescs.find(nhop.portDesc) != portDescs.end()) {
      outputState = resolve
          ? resolveNextHop(outputState, nhop, useLinkLocal, encapIdx)
          : unresolveNextHop(outputState, nhop, useLinkLocal);
    }
  }
  return outputState;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::resolveVlanRifNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    const std::shared_ptr<Interface>& intf,
    bool useLinkLocal) const {
  using NeighborTableT = typename std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;

  auto outputState{inputState->clone()};

  NeighborTableT* nbrTable;
  nbrTable = intf->template getNeighborEntryTable<AddrT>()->modify(
      intf->getID(), &outputState);

  auto nhopIp = useLinkLocal ? nhop.linkLocalNhopIp.value() : nhop.ip;
  auto existingEntry = nbrTable->getEntryIf(nhopIp);
  if (existingEntry) {
    nbrTable->updateEntry(
        nhopIp,
        nhop.mac,
        nhop.portDesc,
        intf->getID(),
        NeighborState::REACHABLE);
    if (needL2EntryForNeighbor()) {
      outputState = NeighborTestUtils::updateMacEntryForUpdatedNbrEntry(
          outputState,
          intf->getVlanID(),
          existingEntry,
          nbrTable->getEntryIf(nhopIp));
    }
  } else {
    nbrTable->addEntry(nhopIp, nhop.mac, nhop.portDesc, intf->getID());
    if (needL2EntryForNeighbor()) {
      outputState = NeighborTestUtils::addMacEntryForNewNbrEntry(
          outputState, intf->getVlanID(), nbrTable->getEntryIf(nhopIp));
    }
  }

  return outputState;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::resolvePortRifNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    const std::shared_ptr<Interface>& intf,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  auto outputState{inputState->clone()};
  auto nbrTable = intf->getNeighborEntryTable<AddrT>()->toThrift();
  auto nhopIp = useLinkLocal ? nhop.linkLocalNhopIp.value() : nhop.ip;
  bool isLocal =
      inputState->getInterfaces()->getNodeIf(intf->getID()) != nullptr;
  state::NeighborEntryFields nbr;
  nbr.mac() = nhop.mac.toString();
  nbr.interfaceId() = static_cast<int>(intf->getID());
  nbr.ipaddress() = nhopIp.str();
  nbr.portId() = nhop.portDesc.toThrift();
  nbr.state() = state::NeighborState::Reachable;
  if (encapIdx) {
    nbr.encapIndex() = *encapIdx;
  }
  nbr.isLocal() = isLocal;
  nbrTable[*nbr.ipaddress()] = nbr;
  auto origIntf = isLocal
      ? outputState->getInterfaces()->getNode(intf->getID())
      : outputState->getRemoteInterfaces()->getNode(intf->getID());
  auto interface = origIntf->modify(&outputState);
  interface->setNeighborEntryTable<AddrT>(nbrTable);
  return outputState;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::unresolveVlanRifNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    const std::shared_ptr<Interface>& intf,
    bool useLinkLocal) const {
  using NeighborTableT = typename std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;

  auto outputState{inputState->clone()};

  NeighborTableT* nbrTable;
  nbrTable = intf->template getNeighborEntryTable<AddrT>()->modify(
      intf->getID(), &outputState);

  auto nhopIp = useLinkLocal ? nhop.linkLocalNhopIp.value() : nhop.ip;
  auto entry = nbrTable->getEntryIf(nhopIp);
  if (entry) {
    nbrTable->removeEntry(nhopIp);
    if (needL2EntryForNeighbor()) {
      outputState = NeighborTestUtils::pruneMacEntryForDelNbrEntry(
          outputState, intf->getVlanID(), entry);
    }
  }
  return outputState;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::unresolvePortRifNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    const std::shared_ptr<Interface>& intf,
    bool useLinkLocal) const {
  auto outputState{inputState->clone()};
  auto nbrTable = intf->getNeighborEntryTable<AddrT>()->toThrift();
  auto nhopIp = useLinkLocal ? nhop.linkLocalNhopIp.value() : nhop.ip;
  nbrTable.erase(nhopIp.str());
  auto origIntf = outputState->getInterfaces()->getNodeIf(intf->getID())
      ? outputState->getInterfaces()->getNode(intf->getID())
      : outputState->getRemoteInterfaces()->getNode(intf->getID());
  auto interface = origIntf->modify(&outputState);
  interface->setNeighborEntryTable<AddrT>(nbrTable);
  return outputState;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::resolveNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  auto intfID = portDesc2Interface_.find(nhop.portDesc)->second;
  auto intf = inputState->getInterfaces()->getNodeIf(intfID);
  if (intf == nullptr) {
    intf = inputState->getRemoteInterfaces()->getNodeIf(intfID);
  }
  switch (intf->getType()) {
    case cfg::InterfaceType::VLAN:
      CHECK(!encapIdx.has_value())
          << " Encap index not supported for VLAN rifs";
      return resolveVlanRifNextHop(inputState, nhop, intf, useLinkLocal);
    case cfg::InterfaceType::SYSTEM_PORT:
    case cfg::InterfaceType::PORT:
      return resolvePortRifNextHop(
          inputState, nhop, intf, useLinkLocal, encapIdx);
      break;
  }
  CHECK(false) << " Unhandled interface type: ";
  return nullptr;
}

template <typename AddrT, typename NextHopT>
std::shared_ptr<SwitchState>
BaseEcmpSetupHelper<AddrT, NextHopT>::unresolveNextHop(
    const std::shared_ptr<SwitchState>& inputState,
    const NextHopT& nhop,
    bool useLinkLocal) const {
  auto intfID = portDesc2Interface_.find(nhop.portDesc)->second;
  auto intf = inputState->getInterfaces()->getNodeIf(intfID);
  if (intf == nullptr) {
    intf = inputState->getRemoteInterfaces()->getNodeIf(intfID);
  }
  switch (intf->getType()) {
    case cfg::InterfaceType::VLAN:
      return unresolveVlanRifNextHop(inputState, nhop, intf, useLinkLocal);
    case cfg::InterfaceType::SYSTEM_PORT:
    case cfg::InterfaceType::PORT:
      return unresolvePortRifNextHop(inputState, nhop, intf, useLinkLocal);
      break;
  }
  CHECK(false) << " Unhandled interface type: ";
  return nullptr;
}

template <typename AddrT, typename NextHopT>
std::vector<PortDescriptor> BaseEcmpSetupHelper<AddrT, NextHopT>::ecmpPortDescs(
    int width) const {
  std::vector<PortDescriptor> portDescs;
  for (auto i = 0; i < width; ++i) {
    portDescs.push_back(nhops_[i].portDesc);
  }
  return portDescs;
}

template <typename AddrT, typename NextHopT>
std::optional<InterfaceID> BaseEcmpSetupHelper<AddrT, NextHopT>::getInterface(
    const PortDescriptor& port,
    const std::shared_ptr<SwitchState>& state) const {
  if (auto vlan = getVlan(port, state)) {
    auto intf =
        state->getInterfaces()->getNodeIf(InterfaceID(static_cast<int>(*vlan)));
    if (!intf) {
      // No interface config for this vlan
      return std::nullopt;
    }
    CHECK(intf->getType() == cfg::InterfaceType::VLAN);
    CHECK(intf->getVlanID() == *vlan);
    return intf->getID();
  } else if (port.isPhysicalPort()) {
    auto sysPortRanges =
        state->getAssociatedSystemPortRangesIf(port.phyPortID());
    if (!sysPortRanges.systemPortRanges()->empty()) {
      // Look for port RIF
      auto sysPortId = getSystemPortID(
          port.phyPortID(),
          state,
          state->getAssociatedSwitchID(port.phyPortID()));
      if (auto intf = state->getInterfaces()->getNodeIf(
              InterfaceID(static_cast<int>(sysPortId)))) {
        return intf->getID();
      }
    }
    for (const auto& intfs : std::as_const(*state->getInterfaces())) {
      for (const auto& intf : std::as_const(*intfs.second)) {
        if (intf.second->getType() != cfg::InterfaceType::PORT) {
          continue;
        }
        if (intf.second->getPortIDf() == port.phyPortID()) {
          return intf.second->getID();
        }
      }
    }
  } else if (port.isAggregatePort()) {
    // An interface bound to the aggregate port itself. Platforms that put
    // their aggregates in a vlan resolve above, via getVlan().
    for (const auto& intfs : std::as_const(*state->getInterfaces())) {
      for (const auto& intf : std::as_const(*intfs.second)) {
        if (intf.second->getType() != cfg::InterfaceType::PORT) {
          continue;
        }
        if (intf.second->getAggregatePortIDf() == port.aggPortID()) {
          return intf.second->getID();
        }
      }
    }
  } else if (port.isSystemPort()) {
    // Find systemPort on both local and remote interfaces
    if (auto intf = state->getInterfaces()->getNodeIf(port.intID())) {
      return intf->getID();
    }
    if (auto intf = state->getRemoteInterfaces()->getNodeIf(port.intID())) {
      return intf->getID();
    }
  }
  return std::nullopt;
}
template <typename AddrT, typename NextHopT>
std::optional<VlanID> BaseEcmpSetupHelper<AddrT, NextHopT>::getVlan(
    const PortDescriptor& port,
    const std::shared_ptr<SwitchState>& state) const {
  // Get physical port and extract first vlan from it. We assume
  // port is always part of a single vlan
  auto getPhysicalPortId = [&port, &state]() -> std::optional<PortID> {
    std::optional<PortID> portId;
    switch (port.type()) {
      case PortDescriptor::PortType::PHYSICAL:
        portId = port.phyPortID();
        break;
      case PortDescriptor::PortType::AGGREGATE: {
        auto aggPort = state->getAggregatePorts()->getNodeIf(port.aggPortID());
        if (!aggPort) {
          return std::nullopt;
        }
        portId = aggPort->sortedSubports().begin()->portID;
      } break;

      case PortDescriptor::PortType::SYSTEM_PORT:
        break;
    }
    return portId;
  };
  if (auto phyPortId = getPhysicalPortId()) {
    auto phyPort = state->getPorts()->getNodeIf(*phyPortId);
    for (const auto& vlanMember : phyPort->getVlans()) {
      if (vlanMember.first == VlanID(0)) {
        continue;
      }
      return vlanMember.first;
    }
  }
  return std::nullopt;
}

template <typename IPAddrT>
EcmpSetupTargetedPorts<IPAddrT>::EcmpSetupTargetedPorts(
    const std::shared_ptr<SwitchState>& inputState,
    bool needL2EntryForNeighbor,
    std::optional<folly::MacAddress> nextHopMac,
    RouterID routerId,
    bool forProdConfig,
    const std::set<cfg::PortType>& portTypes)
    : BaseEcmpSetupHelper<IPAddrT, EcmpNextHopT>(needL2EntryForNeighbor),
      routerId_(routerId) {
  computeNextHops(inputState, nextHopMac, forProdConfig, portTypes);
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::computeNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    std::optional<folly::MacAddress> nextHopMac,
    bool forProdConfig,
    const std::set<cfg::PortType>& portTypes) {
  BaseEcmpSetupHelperT::portDesc2Interface_ =
      BaseEcmpSetupHelperT::computePortDesc2Interface(inputState, portTypes);
  auto intf2Subnet =
      computeInterface2Subnet(inputState, BaseEcmpSetupHelperT::kIsV6);
  int offset = 0;
  // Locally administered, unicast MACs
  auto baseMac = folly::MacAddress("06:00:00:00:00:00").u64HBO();
  for (const auto& portDescAndInterface :
       BaseEcmpSetupHelperT::portDesc2Interface_) {
    auto intf = portDescAndInterface.second;
    auto ipAddrStr = intf2Subnet[intf].first.str();
    if (ipAddrStr.empty()) {
      // Ignore intfs without ip addresses
      continue;
    }
    auto subnetIp = IPAddrT(ipAddrStr);
    auto bytes = subnetIp.toByteArray();
    int lastOctet = bytes[bytes.size() - 1];
    offset++;
    if (forProdConfig) {
      // Flip last bit to compute next hop IP because prod configs
      // can have uplink interfaces with /127 subnets.
      lastOctet ^= 1;
    } else {
      // Add a offset to compute next in subnet next hop IP.
      // Essentially for l3 intf with subnet X, we
      // would compute next hops by incrementing last octet
      // of subnet.
      lastOctet = (lastOctet + offset) % 255;
    }
    // Fail if we go >255 at the last octet
    CHECK_GE(255, lastOctet);
    bytes[bytes.size() - 1] = static_cast<uint8_t>(lastOctet);
    BaseEcmpSetupHelperT::nhops_.push_back(EcmpNextHopT(
        IPAddrT(bytes),
        portDescAndInterface.first,
        nextHopMac ? MacAddress::fromHBO(nextHopMac.value().u64HBO())
                   : MacAddress::fromHBO(baseMac + offset),
        intf));
  }
}

template <typename IPAddrT>
typename EcmpSetupTargetedPorts<IPAddrT>::EcmpNextHopT
EcmpSetupTargetedPorts<IPAddrT>::nhop(PortDescriptor portDesc) const {
  auto it = std::find_if(
      BaseEcmpSetupHelperT::nhops_.begin(),
      BaseEcmpSetupHelperT::nhops_.end(),
      [portDesc](const EcmpNextHopT& nh) { return nh.portDesc == portDesc; });
  if (it == BaseEcmpSetupHelperT::nhops_.end()) {
    throw FbossError("Could not find a nhop for: ", portDesc);
  }
  return *it;
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::addRoutesToUpdater(
    RouteUpdateWrapper* updater,
    const flat_set<PortDescriptor>& portDescriptors,
    const std::vector<RouteT>& prefixes,
    const std::vector<NextHopWeight>& weights,
    const std::optional<RouteCounterID>& counterID,
    const std::optional<bool>& disableTTLDecrement) const {
  if (prefixes.empty()) {
    return;
  }
  std::vector<NextHopWeight> hopWeights;
  if (!weights.size()) {
    for (auto i = 0; i < portDescriptors.size(); ++i) {
      hopWeights.push_back(ECMP_WEIGHT);
    }
  } else {
    hopWeights = weights;
  }

  CHECK_EQ(portDescriptors.size(), hopWeights.size());
  RouteNextHopSet nhops;
  {
    auto i = 0;
    for (const auto& portDescriptor : portDescriptors) {
      nhops.emplace(UnresolvedNextHop(
          BaseEcmpSetupHelperT::ip(portDescriptor),
          hopWeights[i++],
          std::nullopt,
          disableTTLDecrement));
    }
  }
  for (const auto& prefix : prefixes) {
    updater->addRoute(
        routerId_,
        folly::IPAddress(prefix.network()),
        prefix.mask(),
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP, counterID));
  }
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::programRoutes(
    RouteUpdateWrapper* updater,
    const flat_set<PortDescriptor>& portDescriptors,
    const std::vector<RouteT>& prefixes,
    const std::vector<NextHopWeight>& weights,
    std::optional<RouteCounterID> counterID,
    const std::optional<bool>& disableTTLDecrement) const {
  addRoutesToUpdater(
      updater,
      portDescriptors,
      prefixes,
      weights,
      counterID,
      disableTTLDecrement);
  updater->program();
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::programRoutes(
    RouteUpdateWrapper* updater,
    const std::vector<flat_set<PortDescriptor>>& portDescriptors,
    const std::vector<RouteT>& prefixes,
    const std::vector<std::vector<NextHopWeight>>& weights,
    std::optional<RouteCounterID> counterID,
    const std::optional<bool>& disableTTLDecrement) const {
  std::vector<std::vector<NextHopWeight>> hopWeights = weights;
  if (!weights.size()) {
    for (int i = 0; i < portDescriptors.size(); i++) {
      hopWeights.emplace_back();
    }
  }
  CHECK_EQ(portDescriptors.size(), hopWeights.size());
  CHECK_EQ(portDescriptors.size(), prefixes.size());
  for (int i = 0; i < portDescriptors.size(); i++) {
    addRoutesToUpdater(
        updater,
        portDescriptors[i],
        {prefixes[i]},
        hopWeights[i],
        counterID,
        disableTTLDecrement);
  }
  updater->program();
}

template <typename IPAddrT>
RouteNextHopSet EcmpSetupTargetedPorts<IPAddrT>::setupMplsNexthops(
    const flat_set<PortDescriptor>& portDescriptors,
    std::map<PortDescriptor, LabelForwardingAction::LabelStack>& stacks,
    LabelForwardingAction::LabelForwardingType labelActionType,
    const std::vector<NextHopWeight>& weights) const {
  RouteNextHopSet nhops;
  std::vector<NextHopWeight> hopWeights;
  if (!weights.size()) {
    for (auto i = 0; i < portDescriptors.size(); ++i) {
      hopWeights.push_back(ECMP_WEIGHT);
    }
  } else {
    hopWeights = weights;
  }

  CHECK_EQ(portDescriptors.size(), hopWeights.size());
  auto i = 0;
  for (const auto& portDescriptor : portDescriptors) {
    auto itr = stacks.find(portDescriptor);
    if (itr != stacks.end() && !itr->second.empty()) {
      if (labelActionType == LabelForwardingAction::LabelForwardingType::PUSH) {
        nhops.emplace(UnresolvedNextHop(
            BaseEcmpSetupHelperT::ip(portDescriptor),
            hopWeights[i++],
            LabelForwardingAction(labelActionType, itr->second)));
      } else {
        CHECK(
            labelActionType ==
            LabelForwardingAction::LabelForwardingType::SWAP);
        nhops.emplace(UnresolvedNextHop(
            BaseEcmpSetupHelperT::ip(portDescriptor),
            hopWeights[i++],
            LabelForwardingAction(labelActionType, itr->second[0])));
      }
    } else if (
        labelActionType == LabelForwardingAction::LabelForwardingType::PHP ||
        labelActionType ==
            LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP) {
      nhops.emplace(UnresolvedNextHop(
          BaseEcmpSetupHelperT::ip(portDescriptor),
          hopWeights[i++],
          LabelForwardingAction(labelActionType)));
    } else {
      nhops.emplace(UnresolvedNextHop(
          BaseEcmpSetupHelperT::ip(portDescriptor), hopWeights[i++]));
    }
  }

  return nhops;
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::programMplsRoutes(
    std::unique_ptr<RouteUpdateWrapper> updater,
    const flat_set<PortDescriptor>& portDescriptors,
    std::map<PortDescriptor, LabelForwardingAction::LabelStack> stacks,
    const std::vector<LabelID>& labels,
    LabelForwardingAction::LabelForwardingType labelActionType,
    const std::vector<NextHopWeight>& weights,
    std::optional<RouteCounterID> /* counterID */) const {
  if (labels.empty()) {
    return;
  }
  auto nhops =
      setupMplsNexthops(portDescriptors, stacks, labelActionType, weights);
  for (const auto& label : labels) {
    MplsRoute route;
    route.topLabel() = label;
    route.nextHops() = util::fromRouteNextHopSet(nhops);
    updater->addRoute(ClientID::BGPD, route);
  }
  updater->program();
}
template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::programIp2MplsRoutes(
    std::unique_ptr<RouteUpdateWrapper> updater,
    const boost::container::flat_set<PortDescriptor>& portDescriptors,
    std::map<PortDescriptor, LabelForwardingAction::LabelStack> stacks,
    const std::vector<RouteT>& prefixes,
    const std::vector<NextHopWeight>& weights) const {
  std::vector<NextHopWeight> hopWeights;
  std::vector<NextHopWeight> forwardingActions;

  auto nhops = setupMplsNexthops(
      portDescriptors,
      stacks,
      LabelForwardingAction::LabelForwardingType::PUSH,
      weights);
  for (const auto& prefix : prefixes) {
    updater->addRoute(
        routerId_,
        folly::IPAddress(prefix.network()),
        prefix.mask(),
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
  }
  updater->program();
}

template <typename IPAddrT>
void EcmpSetupTargetedPorts<IPAddrT>::unprogramRoutes(
    RouteUpdateWrapper* wrapper,
    const std::vector<RouteT>& prefixes) const {
  for (const auto& prefix : prefixes) {
    wrapper->delRoute(
        routerId_,
        folly::IPAddress(prefix.network()),
        prefix.mask(),
        ClientID::BGPD);
  }
  wrapper->program();
}

template <typename IPAddrT>
flat_set<PortDescriptor> EcmpSetupAnyNPorts<IPAddrT>::getPortDescs(
    int width) const {
  flat_set<PortDescriptor> portDescs;
  for (size_t w = 0; w < width; ++w) {
    portDescs.insert(getNextHops().at(w).portDesc);
  }
  return portDescs;
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> EcmpSetupAnyNPorts<IPAddrT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    size_t numNextHops,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  return ecmpSetupTargetedPorts_.resolveNextHops(
      inputState, getPortDescs(numNextHops), useLinkLocal, encapIdx);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> EcmpSetupAnyNPorts<IPAddrT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal,
    std::optional<int64_t> encapIdx) const {
  return ecmpSetupTargetedPorts_.resolveNextHops(
      inputState, portDescs, useLinkLocal, encapIdx);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> EcmpSetupAnyNPorts<IPAddrT>::unresolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal) const {
  return ecmpSetupTargetedPorts_.unresolveNextHops(
      inputState, portDescs, useLinkLocal);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> EcmpSetupAnyNPorts<IPAddrT>::unresolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    size_t numNextHops,
    bool useLinkLocal) const {
  return ecmpSetupTargetedPorts_.unresolveNextHops(
      inputState, getPortDescs(numNextHops), useLinkLocal);
}

template <typename IPAddrT>
void EcmpSetupAnyNPorts<IPAddrT>::programRoutes(
    RouteUpdateWrapper* updater,
    size_t width,
    const std::vector<RouteT>& prefixes,
    const std::vector<NextHopWeight>& weights,
    const std::optional<bool>& disableTTLDecrement) const {
  // Use default route prefix when caller passes empty prefixes
  const auto& effectivePrefixes =
      prefixes.empty() ? std::vector<RouteT>{{IPAddrT(), 0}} : prefixes;
  ecmpSetupTargetedPorts_.programRoutes(
      updater,
      getPortDescs(width),
      effectivePrefixes,
      weights,
      std::nullopt,
      disableTTLDecrement);
}

template <typename IPAddrT>
void EcmpSetupAnyNPorts<IPAddrT>::programRoutes(
    RouteUpdateWrapper* updater,
    const flat_set<PortDescriptor>& portDescs,
    const std::vector<RouteT>& prefixes,
    const std::vector<NextHopWeight>& weights,
    const std::optional<bool>& disableTTLDecrement) const {
  ecmpSetupTargetedPorts_.programRoutes(
      updater, portDescs, prefixes, weights, std::nullopt, disableTTLDecrement);
}

template <typename IPAddrT>
void EcmpSetupAnyNPorts<IPAddrT>::programIp2MplsRoutes(
    std::unique_ptr<RouteUpdateWrapper> updater,
    size_t width,
    const std::vector<RouteT>& prefixes,
    std::vector<LabelForwardingAction::LabelStack> stacks,
    const std::vector<NextHopWeight>& weights) const {
  auto ports = getPortDescs(width);
  std::map<PortDescriptor, LabelForwardingAction::LabelStack> port2Stack;
  auto i = 0;
  for (auto port : ports) {
    i = (i % stacks.size());
    port2Stack.emplace(port, stacks[i++]);
  }
  ecmpSetupTargetedPorts_.programIp2MplsRoutes(
      std::move(updater), ports, port2Stack, prefixes, weights);
}

template <typename IPAddrT>
void EcmpSetupAnyNPorts<IPAddrT>::unprogramRoutes(
    RouteUpdateWrapper* wrapper,
    const std::vector<RouteT>& prefixes) const {
  ecmpSetupTargetedPorts_.unprogramRoutes(wrapper, prefixes);
}

template <typename IPAddrT>
std::vector<PortDescriptor> EcmpSetupAnyNPorts<IPAddrT>::ecmpPortDescs(
    int width) const {
  return ecmpSetupTargetedPorts_.ecmpPortDescs(width);
}

template <typename IPAddrT>
void MplsEcmpSetupTargetedPorts<IPAddrT>::setupECMPForwarding(
    const std::shared_ptr<SwitchState>& inputState,
    std::unique_ptr<RouteUpdateWrapper> updater,
    const boost::container::flat_set<PortDescriptor>& portDescriptors,
    const std::vector<NextHopWeight>& weights) const {
  std::vector<NextHopWeight> hopWeights;
  if (!weights.size()) {
    for (auto i = 0; i < portDescriptors.size(); ++i) {
      hopWeights.push_back(ECMP_WEIGHT);
    }
  } else {
    hopWeights = weights;
  }

  CHECK_EQ(portDescriptors.size(), hopWeights.size());
  auto outputState{inputState->clone()};
  LabelNextHopSet nhops;
  {
    auto i = 0;
    for (const auto& portDescriptor : portDescriptors) {
      auto iter =
          BaseEcmpSetupHelperT::portDesc2Interface_.find(portDescriptor);
      auto intfId = iter->second;
      auto nexthop = nhop(portDescriptor);
      nhops.emplace(
          LabelNextHop(nexthop.ip, intfId, hopWeights[i++], nexthop.action));
    }
  }

  MplsRoute route;
  route.topLabel() = topLabel_.value();
  route.nextHops() = util::fromRouteNextHopSet(nhops);
  updater->addRoute(ClientID(0), route);
  updater->program();
}

template <typename IPAddrT>
void MplsEcmpSetupTargetedPorts<IPAddrT>::computeNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    std::optional<folly::MacAddress> nextHopMac,
    bool forProdConfig,
    const std::set<cfg::PortType>& portTypes) {
  BaseEcmpSetupHelperT::portDesc2Interface_ =
      BaseEcmpSetupHelperT::computePortDesc2Interface(inputState, portTypes);
  auto intf2Subnet =
      computeInterface2Subnet(inputState, BaseEcmpSetupHelperT::kIsV6);
  int offset = 0;
  auto baseMac = folly::MacAddress("06:00:00:00:00:00").u64HBO();
  for (const auto& portDescAndInterface :
       BaseEcmpSetupHelperT::portDesc2Interface_) {
    auto intf = portDescAndInterface.second;
    auto subnetIp = IPAddrT(intf2Subnet[intf].first.str());
    auto bytes = subnetIp.toByteArray();
    int lastOctet = bytes[bytes.size() - 1];
    offset++;
    if (forProdConfig) {
      // Flip last bit to compute next hop IP because prod configs
      // can have uplink interfaces with /127 subnets.
      lastOctet ^= 1;
    } else {
      // Add a offset to compute next in subnet next hop IP.
      // Essentially for l3 intf with subnet X, we
      // would compute next hops by incrementing last octet
      // of subnet.
      lastOctet = (lastOctet + offset) % 255;
    }
    // Fail if we go to 255 at the last octet
    CHECK_GT(255, lastOctet);
    bytes[bytes.size() - 1] = static_cast<uint8_t>(lastOctet);
    BaseEcmpSetupHelperT::nhops_.push_back(
        EcmpMplsNextHop<IPAddrT>(
            IPAddrT(bytes),
            portDescAndInterface.first,
            nextHopMac ? MacAddress::fromHBO(nextHopMac.value().u64HBO())
                       : MacAddress::fromHBO(baseMac + offset),
            intf,
            getLabelForwardingAction(portDescAndInterface.first)));
  }
}

template <typename IPAddrT>
EcmpMplsNextHop<IPAddrT> MplsEcmpSetupTargetedPorts<IPAddrT>::nhop(
    PortDescriptor portDesc) const {
  auto it = std::find_if(
      BaseEcmpSetupHelperT::nhops_.begin(),
      BaseEcmpSetupHelperT::nhops_.end(),
      [portDesc](const auto& nh) { return nh.portDesc == portDesc; });
  if (it == BaseEcmpSetupHelperT::nhops_.end()) {
    throw FbossError("Could not find a nhop for: ", portDesc);
  }
  return *it;
}

template <typename IPAddrT>
LabelForwardingAction
MplsEcmpSetupTargetedPorts<IPAddrT>::getLabelForwardingAction(
    PortDescriptor port) const {
  auto label = static_cast<int32_t>(port.intID());

  LabelForwardingAction::LabelStack pushStack;
  for (auto i = 1; i <= 3; i++) {
    pushStack.push_back(label * 10 + i);
  }

  switch (actionType_) {
    case LabelForwardingAction::LabelForwardingType::PUSH:
      return LabelForwardingAction(actionType_, std::move(pushStack));

    case LabelForwardingAction::LabelForwardingType::SWAP:
      return LabelForwardingAction(actionType_, pushStack[0]);
    case LabelForwardingAction::LabelForwardingType::PHP:
    case LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP:
    case LabelForwardingAction::LabelForwardingType::NOOP:
      return LabelForwardingAction(actionType_);
  }
  return LabelForwardingAction(
      LabelForwardingAction::LabelForwardingType::NOOP);
}

template <typename IPAddrT>
flat_set<PortDescriptor> MplsEcmpSetupAnyNPorts<IPAddrT>::getPortDescs(
    int width) const {
  flat_set<PortDescriptor> portDescs;
  for (size_t w = 0; w < width; ++w) {
    portDescs.insert(getNextHops().at(w).portDesc);
  }
  return portDescs;
}

template <typename IPAddrT>
std::vector<PortDescriptor> MplsEcmpSetupAnyNPorts<IPAddrT>::ecmpPortDescs(
    int width) const {
  return mplsEcmpSetupTargetedPorts_.ecmpPortDescs(width);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> MplsEcmpSetupAnyNPorts<IPAddrT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    size_t numNextHops,
    bool useLinkLocal) const {
  return mplsEcmpSetupTargetedPorts_.resolveNextHops(
      inputState, getPortDescs(numNextHops), useLinkLocal);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> MplsEcmpSetupAnyNPorts<IPAddrT>::unresolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    size_t numNextHops,
    bool useLinkLocal) const {
  return mplsEcmpSetupTargetedPorts_.unresolveNextHops(
      inputState, getPortDescs(numNextHops), useLinkLocal);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> MplsEcmpSetupAnyNPorts<IPAddrT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const boost::container::flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal) const {
  return mplsEcmpSetupTargetedPorts_.resolveNextHops(
      inputState, portDescs, useLinkLocal);
}

template <typename IPAddrT>
std::shared_ptr<SwitchState> MplsEcmpSetupAnyNPorts<IPAddrT>::unresolveNextHops(
    const std::shared_ptr<SwitchState>& inputState,
    const boost::container::flat_set<PortDescriptor>& portDescs,
    bool useLinkLocal) const {
  return mplsEcmpSetupTargetedPorts_.unresolveNextHops(
      inputState, portDescs, useLinkLocal);
}

template <typename IPAddrT>
void MplsEcmpSetupAnyNPorts<IPAddrT>::setupECMPForwarding(
    const std::shared_ptr<SwitchState>& inputState,
    std::unique_ptr<RouteUpdateWrapper> updater,
    size_t width,
    const std::vector<NextHopWeight>& weights) const {
  mplsEcmpSetupTargetedPorts_.setupECMPForwarding(
      inputState, std::move(updater), getPortDescs(width), weights);
}

template class BaseEcmpSetupHelper<
    folly::IPAddressV4,
    EcmpNextHop<folly::IPAddressV4>>;
template class BaseEcmpSetupHelper<
    folly::IPAddressV6,
    EcmpNextHop<folly::IPAddressV6>>;

template class BaseEcmpSetupHelper<
    folly::IPAddressV4,
    EcmpMplsNextHop<folly::IPAddressV4>>;
template class BaseEcmpSetupHelper<
    folly::IPAddressV6,
    EcmpMplsNextHop<folly::IPAddressV6>>;

template class EcmpSetupTargetedPorts<folly::IPAddressV4>;
template class EcmpSetupTargetedPorts<folly::IPAddressV6>;
template class EcmpSetupAnyNPorts<folly::IPAddressV4>;
template class EcmpSetupAnyNPorts<folly::IPAddressV6>;

template class MplsEcmpSetupTargetedPorts<folly::IPAddressV4>;
template class MplsEcmpSetupTargetedPorts<folly::IPAddressV6>;
template class MplsEcmpSetupAnyNPorts<folly::IPAddressV4>;
template class MplsEcmpSetupAnyNPorts<folly::IPAddressV6>;

} // namespace facebook::fboss::utility
