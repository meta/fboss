// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/NextHopResolver.h"

#include <folly/logging/xlog.h>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"

namespace {

using facebook::fboss::Interface;
using facebook::fboss::SwitchState;

template <typename AddrT>
using NeighborEntryT =
    typename facebook::fboss::NextHopResolver<AddrT>::NeighborEntryT;

template <typename AddrT>
auto getNeighborEntryTableHelper(
    const std::shared_ptr<SwitchState>& /* state */,
    const std::shared_ptr<Interface>& interface) {
  // Neighbor tables are always on interfaces
  return interface->template getNeighborEntryTable<AddrT>();
}

} // namespace

namespace facebook::fboss {

template <typename AddrT>
NextHopResolver<AddrT>::NextHopResolver(SwSwitch* sw) : sw_(sw) {}

template <typename AddrT>
RouteNextHopEntry::NextHopSet NextHopResolver<AddrT>::resolveNextHops(
    const std::shared_ptr<SwitchState>& state,
    const AddrT& destinationIp,
    const RouterID& routerId) const {
  auto result = sw_->getRib()->getRouteAndNextHops(
      destinationIp, routerId, /*normalized=*/false);
  if (!result || !result->first->isResolved()) {
    return RouteNextHopEntry::NextHopSet();
  }
  return result->second;
}

template <typename AddrT>
std::shared_ptr<NeighborEntryT<AddrT>>
NextHopResolver<AddrT>::resolveNextHopNeighbor(
    const std::shared_ptr<SwitchState>& state,
    const AddrT& destinationIp,
    const NextHop& nexthop) const {
  if (!nexthop.intfID().has_value()) {
    return std::shared_ptr<NeighborEntryT>{nullptr};
  }

  AddrT nextHopIp = getIPAddress<AddrT>(nexthop.addr());
  InterfaceID egressInterface = nexthop.intf();

  auto interface = getInterface(state, egressInterface);
  if (!interface) {
    XLOG(WARNING) << "Interface " << egressInterface
                  << " not found for next hop resolution";
    return std::shared_ptr<NeighborEntryT>{nullptr};
  }

  if (interface->hasAddress(nextHopIp)) {
    // Destination is directly connected - look up destination IP
    return getNeighborEntryTableHelper<AddrT>(state, interface)
        ->getEntryIf(destinationIp);
  } else {
    // Multi-hop - look up the next hop IP
    return getNeighborEntryTableHelper<AddrT>(state, interface)
        ->getEntryIf(nextHopIp);
  }
}

template <typename AddrT>
std::optional<PortDescriptor> NextHopResolver<AddrT>::resolveEgressPort(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<NeighborEntryT>& neighbor) const {
  if (!neighbor || neighbor->zeroPort()) {
    return std::nullopt;
  }

  auto neighborPort = neighbor->getPort();

  switch (neighborPort.type()) {
    case PortDescriptor::PortType::PHYSICAL:
    case PortDescriptor::PortType::SYSTEM_PORT:
      return neighborPort;
    case PortDescriptor::PortType::AGGREGATE: {
      // Pick first forwarding member port from the LAG
      auto aggPort =
          state->getAggregatePorts()->getNodeIf(neighborPort.aggPortID());
      if (!aggPort) {
        XLOG(ERR) << "Resolved to non-existing aggregate port "
                  << neighborPort.aggPortID();
        return std::nullopt;
      }
      auto subportAndFwdStates = aggPort->subportAndFwdState();
      if (subportAndFwdStates.begin() == subportAndFwdStates.end()) {
        return std::nullopt;
      }
      for (const auto& subPortAndFwdState : subportAndFwdStates) {
        if (subPortAndFwdState.second == AggregatePort::Forwarding::ENABLED) {
          return PortDescriptor(subPortAndFwdState.first);
        }
      }
      break;
    }
    default:
      break;
  }

  return std::nullopt;
}

template <typename AddrT>
std::shared_ptr<Interface> NextHopResolver<AddrT>::getInterface(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID interfaceId) const {
  auto interface = state->getInterfaces()->getNodeIf(interfaceId);
  if (!interface && state->getRemoteInterfaces()) {
    XLOG(DBG2) << "Interface: " << interfaceId
               << " not found in local interfaces, "
               << "looking up in remote interfaces";
    interface = state->getRemoteInterfaces()->getNodeIf(interfaceId);
  }
  return interface;
}

template class NextHopResolver<folly::IPAddressV4>;
template class NextHopResolver<folly::IPAddressV6>;

} // namespace facebook::fboss
