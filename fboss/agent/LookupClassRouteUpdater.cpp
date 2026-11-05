/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fb303/ServiceData.h>

#include "fboss/agent/LookupClassRouteUpdater.h"

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/NeighborTableDeltaCallbackGenerator.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

/*
 * TODO get rid of this flag once configs have been updated to
 * not pass in this cmdline flag
 */
DEFINE_bool(
    queue_per_host_route_fix,
    true,
    "Enable route entry (ip-per-task/VIP) portion of Queue-per-host fix");

namespace {
constexpr auto kQphMultiNextHopCounter = "qph.multinexthop.route";
} // namespace

namespace facebook::fboss {

// Helper methods

void LookupClassRouteUpdater::reAddAllRoutes(const StateDelta& stateDelta) {
  auto addRoute = [&stateDelta, this](RouterID rid, const auto& route) {
    if (!route->getClassID().has_value()) {
      processRouteAdded(stateDelta, rid, route);
    }
  };
  if (!vlan2SubnetsCache_.empty()) {
    forAllRoutes(stateDelta.newState(), addRoute);
  }
}

bool LookupClassRouteUpdater::vlanHasOtherPortsWithClassIDs(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    const std::shared_ptr<Port>& removedPort) {
  for (auto& [id, portInfo] : vlan->getPortsInfo()) {
    auto portID = PortID(id);
    std::ignore = portInfo;
    auto port = switchState->getPorts()->getNodeIf(portID);

    if (portID != removedPort->getID() &&
        port->getLookupClassesToDistributeTrafficOn().size() != 0) {
      return true;
    }
  }

  return false;
}

void LookupClassRouteUpdater::removeNextHopsForSubnet(
    const StateDelta& stateDelta,
    const folly::CIDRNetwork& subnet,
    const std::shared_ptr<Vlan>& vlan) {
  auto& [ipAddress, mask] = subnet;

  auto it = nextHopAndVlan2Prefixes_.begin();
  while (it != nextHopAndVlan2Prefixes_.end()) {
    const auto& [nextHop, vlanID] = it->first;

    // Element pointed by the iterator would be deleted from
    // nextHopAndVlan2Prefixes_ as part of processNeighborRemoved processing.
    // Thus, advance the iterator to next element.
    ++it;

    if (vlanID == vlan->getID() && nextHop.inSubnet(ipAddress, mask)) {
      if (nextHop.isV6()) {
        auto ndpEntry = getNeighborTableForVlan<NdpTable>(
                            stateDelta.newState(), vlan->getID())
                            ->getEntryIf(nextHop.asV6());
        if (ndpEntry) {
          processNeighborRemoved(stateDelta, vlan->getID(), ndpEntry);
        }
      } else if (nextHop.isV4()) {
        auto arpEntry = getNeighborTableForVlan<ArpTable>(
                            stateDelta.newState(), vlan->getID())
                            ->getEntryIf(nextHop.asV4());
        if (arpEntry) {
          processNeighborRemoved(stateDelta, vlan->getID(), arpEntry);
        }
      }
    }
  }
}

std::optional<cfg::AclLookupClass>
LookupClassRouteUpdater::getClassIDForLinkLocal(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::IPAddressV6& ipAddressV6) {
  CHECK(ipAddressV6.isLinkLocal());

  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return std::nullopt;
  }

  /*
   * Link local neighbors don't have classID, thus classID for a route whose
   * nexthop is link local is derived from MAC address corresponding to the
   * link local address.
   * Try extracting MAC for LL from IP itself. If it fails, fallback to
   * looking up MAC from corresponding NDP entry
   */
  auto mac = ipAddressV6.getMacAddressFromLinkLocal();
  if (!mac) {
    auto ndpEntry = getNeighborTableForVlan<NdpTable>(switchState, vlanID)
                        ->getNodeIf(ipAddressV6.str());
    if (ndpEntry) {
      mac = ndpEntry->getMac();
    }
  }
  if (mac) {
    auto macEntry = vlan->getMacTable()->getMacIf(*mac);
    return macEntry ? macEntry->getClassID() : std::nullopt;
  }
  return std::nullopt;
}

std::optional<cfg::AclLookupClass>
LookupClassRouteUpdater::getClassIDForNeighbor(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::IPAddress& ipAddress) {
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return std::nullopt;
  }

  if (ipAddress.isV6() && ipAddress.asV6().isLinkLocal()) {
    return getClassIDForLinkLocal(switchState, vlanID, ipAddress.asV6());
  }

  if (ipAddress.isV6()) {
    auto ndpEntry = getNeighborTableForVlan<NdpTable>(switchState, vlanID)
                        ->getEntryIf(ipAddress.asV6());
    return ndpEntry ? ndpEntry->getClassID() : std::nullopt;
  } else if (ipAddress.isV4()) {
    auto arpEntry = getNeighborTableForVlan<ArpTable>(switchState, vlanID)
                        ->getEntryIf(ipAddress.asV4());
    return arpEntry ? arpEntry->getClassID() : std::nullopt;
  }

  return std::nullopt;
}

// Methods for dealing with vlan2SubnetsCache_

bool LookupClassRouteUpdater::belongsToSubnetInCache(
    VlanID vlanID,
    const folly::IPAddress& ipToSearch) {
  auto it = vlan2SubnetsCache_.find(vlanID);
  if (it != vlan2SubnetsCache_.end()) {
    auto subnetsCache = it->second;
    for (const auto& [ipAddress, mask] : subnetsCache) {
      if (ipToSearch.inSubnet(ipAddress, mask)) {
        return true;
      }
    }
  }

  return false;
}

void LookupClassRouteUpdater::updateSubnetsCache(
    const StateDelta& stateDelta,
    std::shared_ptr<Port> port,
    bool reAddAllRoutesEnabled) {
  auto& newState = stateDelta.newState();

  bool subnetCacheUpdated = false;
  for (const auto& [vlanID, vlanInfo] : port->getVlans()) {
    std::ignore = vlanInfo;
    auto vlan = newState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    auto& subnetsCache = vlan2SubnetsCache_[vlanID];
    auto interface =
        newState->getInterfaces()->getNodeIf(vlan->getInterfaceID());
    if (interface) {
      for (auto iter : std::as_const(*interface->getAddresses())) {
        auto address =
            std::make_pair(folly::IPAddress(iter.first), iter.second->cref());
        subnetCacheUpdated = subnetsCache.insert(address).second;
      }
    }
  }
  if (subnetCacheUpdated && reAddAllRoutesEnabled) {
    /*
     * When a new subnet is added to the cache, the nextHops of existing
     * routes may become eligible for caching in
     * nextHopAndVlan2Prefixes_. Furthermore, such a nextHop may have
     * classID associated with it, and in that case, the corresponding
     * route could inherit that classID. Thus, re-add all the routes.
     */
    reAddAllRoutes(stateDelta);
  }
}

// Methods for handling port updates

void LookupClassRouteUpdater::processPortAdded(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& addedPort,
    bool reAddAllRoutesEnabled) {
  CHECK(addedPort);

  if (addedPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    /*
     *  Only downlink ports connecting to MH-NIC have lookupClasses
     *  configured. For all the other ports, no-op.
     */
    return;
  }

  updateSubnetsCache(stateDelta, addedPort, reAddAllRoutesEnabled);
}

void LookupClassRouteUpdater::processPortRemovedForVlan(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& removedPort,
    VlanID vlanID) {
  auto& newState = stateDelta.newState();

  auto vlanIter = vlan2SubnetsCache_.find(vlanID);
  if (vlanIter == vlan2SubnetsCache_.end()) {
    return;
  }

  auto vlan = newState->getVlans()->getNodeIf(vlanID);
  if (!vlan || vlanHasOtherPortsWithClassIDs(newState, vlan, removedPort)) {
    return;
  }

  auto interface = newState->getInterfaces()->getNodeIf(vlan->getInterfaceID());
  if (!interface) {
    return;
  }

  auto& subnetsCache = vlan2SubnetsCache_[vlanID];
  for (auto iter : std::as_const(*interface->getAddresses())) {
    std::pair<folly::IPAddress, uint8_t> address(
        folly::IPAddress(iter.first), iter.second->ref());
    removeNextHopsForSubnet(stateDelta, address, vlan);
    subnetsCache.erase(address);
  }
}

void LookupClassRouteUpdater::processPortRemoved(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& removedPort) {
  CHECK(removedPort);

  if (removedPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    /*
     *  Only downlink ports connecting to MH-NIC have lookupClasses
     *  configured. For all the other ports, no-op.
     */
    return;
  }

  for (const auto& [vlanID, vlanInfo] : removedPort->getVlans()) {
    std::ignore = vlanInfo;
    processPortRemovedForVlan(stateDelta, removedPort, vlanID);
  }
}

void LookupClassRouteUpdater::processPortChanged(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  CHECK(oldPort && newPort);
  CHECK_EQ(oldPort->getID(), newPort->getID());

  if (oldPort->getLookupClassesToDistributeTrafficOn().size() == 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() != 0) {
    // enable queue-per-host for this port
    processPortAdded(stateDelta, newPort, true /* re-all all routes */);
  } else if (
      oldPort->getLookupClassesToDistributeTrafficOn().size() != 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    // disable queue-per-host for this port
    processPortRemoved(stateDelta, oldPort);
  } else if (
      oldPort->getLookupClassesToDistributeTrafficOn().size() != 0 &&
      newPort->getLookupClassesToDistributeTrafficOn().size() != 0) {
    // queue-per-host remains enabled, but port's VLAN membership changed, readd
    if (oldPort->getVlans() != newPort->getVlans()) {
      processPortRemoved(stateDelta, oldPort);
      processPortAdded(stateDelta, newPort, true /* re-all all routes */);
    }
  }
}

void LookupClassRouteUpdater::processPortUpdates(const StateDelta& stateDelta) {
  for (const auto& delta : stateDelta.getPortsDelta()) {
    auto oldPort = delta.getOld();
    auto newPort = delta.getNew();

    if (!oldPort && newPort) {
      // processRouteUpdates is invoked after processPortAdd,
      // thus, we don't need to re-add all the routes.
      processPortAdded(
          stateDelta, newPort, false /* don't re-add all routes */);
    } else if (oldPort && !newPort) {
      processPortRemoved(stateDelta, oldPort);
    } else {
      processPortChanged(stateDelta, oldPort, newPort);
    }
  }
}

// Methods for handling interface updates

void LookupClassRouteUpdater::processInterfaceAdded(
    const StateDelta& stateDelta,
    const std::shared_ptr<Interface>& addedInterface) {
  CHECK(addedInterface);
  // TODO(Chenab): Support port router interface
  if (addedInterface->getType() != cfg::InterfaceType::VLAN) {
    // TODO: Start handling interface types for PORT once we
    // we support class IDs on VOQ switches
    return;
  }

  auto switchState = stateDelta.newState();

  auto vlanID = addedInterface->getVlanID();
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return;
  }

  for (auto& [id, portInfo] : vlan->getPortsInfo()) {
    PortID portID(id);
    std::ignore = portInfo;
    auto port = switchState->getPorts()->getNodeIf(portID);
    if (!port) {
      XLOG(FATAL) << "Port " << portID << " not found in state for vlan "
                  << vlanID;
    }
    // routes are re-added once outside the for loop
    processPortAdded(stateDelta, port, false /* don't re-add all routes */);
  }

  /*
   * processMacAddrsToBlockUpdates would not cache subnet corresponding to newly
   * added blocked mac address if there is no interface for that subnet.
   * Thus, when an interface is added, process blocked mac address list again.
   */
  for ([[maybe_unused]] const auto& [_, switchSettings] :
       std::as_const(*switchState->getSwitchSettings())) {
    for (const auto& settingsIter : *(switchSettings->getMacAddrsToBlock())) {
      auto blockedVlanID =
          VlanID(settingsIter->cref<switch_state_tags::macAddrToBlockVlanID>()
                     ->toThrift());
      auto blockedNeighborMac = folly::MacAddress(
          settingsIter->cref<switch_state_tags::macAddrToBlockAddr>()
              ->toThrift());
      if (blockedVlanID != vlanID) {
        continue;
      }

      std::vector<folly::IPAddress> neighborIPAddr;
      for (auto iter :
           std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<
                         folly::IPAddressV4>(switchState, vlan))) {
        auto neighborEntry = iter.second;
        if (neighborEntry->getMac() == blockedNeighborMac) {
          neighborIPAddr.emplace_back(neighborEntry->getIP());
        }
      }

      for (auto iter :
           std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<
                         folly::IPAddressV6>(switchState, vlan))) {
        auto neighborEntry = iter.second;
        if (neighborEntry->getMac() == blockedNeighborMac &&
            !isNoHostRoute(neighborEntry)) {
          neighborIPAddr.emplace_back(neighborEntry->getIP());
        }
      }

      for (auto iter : std::as_const(*addedInterface->getAddresses())) {
        std::pair<folly::IPAddress, uint8_t> address(
            folly::IPAddress(iter.first), iter.second->ref());
        for (auto& neighborIP : neighborIPAddr) {
          if (neighborIP.inSubnet(address.first, address.second)) {
            auto& subnetsCache = vlan2SubnetsCache_[vlanID];
            subnetsCache.insert(address);
            break;
          }
        }
      }
    }
  }

  reAddAllRoutes(stateDelta);
}

void LookupClassRouteUpdater::processInterfaceRemoved(
    const StateDelta& stateDelta,
    const std::shared_ptr<Interface>& removedInterface) {
  CHECK(removedInterface);
  // TODO(Chenab): Support port router interface
  if (removedInterface->getType() != cfg::InterfaceType::VLAN) {
    // TODO: Start handling interface types for PORT once we
    // we support class IDs on VOQ switches
    return;
  }

  auto switchState = stateDelta.newState();

  auto vlanID = removedInterface->getVlanID();
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return;
  }

  for (auto& [id, portInfo] : vlan->getPortsInfo()) {
    PortID portID(id);
    std::ignore = portInfo;
    auto port = switchState->getPorts()->getNodeIf(portID);
    /*
     * Subnets for an interface could be cached in following cases:
     *   - port has non-empty lookup class list,
     *   - blocked neighbor IP
     *
     * However, when an interface is removed, subnets cached due to the
     * interface are removed regardless.
     *
     * When an interface is added, processInterfaceAdded processes the
     * switchState for ports with non-empty lookup class list as well as
     * blocked neighbor IP to reopulate the subnet cache.
     */
    processPortRemovedForVlan(stateDelta, port, vlanID);
  }
}

void LookupClassRouteUpdater::processInterfaceChanged(
    const StateDelta& stateDelta,
    const std::shared_ptr<Interface>& oldInterface,
    const std::shared_ptr<Interface>& newInterface) {
  CHECK(oldInterface && newInterface);
  CHECK_EQ(oldInterface->getID(), newInterface->getID());

  processInterfaceRemoved(stateDelta, oldInterface);
  processInterfaceAdded(stateDelta, newInterface);
}

void LookupClassRouteUpdater::processInterfaceUpdates(
    const StateDelta& stateDelta) {
  for (const auto& delta : stateDelta.getIntfsDelta()) {
    auto oldInterface = delta.getOld();
    auto newInterface = delta.getNew();

    if (!oldInterface && newInterface) {
      processInterfaceAdded(stateDelta, newInterface);
    } else if (oldInterface && !newInterface) {
      processInterfaceRemoved(stateDelta, oldInterface);
    } else {
      processInterfaceChanged(stateDelta, oldInterface, newInterface);
    }
  }
}

// Methods for handling neighbor updates

bool LookupClassRouteUpdater::isNeighborReachable(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::IPAddressV6& neighborIP) {
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return false;
  }

  auto linkLocalEntry =
      NeighborTableDeltaCallbackGenerator::getTable<folly::IPAddressV6>(
          switchState, vlan)
          ->getEntryIf(neighborIP);

  return linkLocalEntry && linkLocalEntry->isReachable();
}

/*
 * Link local neighbors don't have classID, thus classID for a route whose
 * nexthop is link local is derived from MAC address corresponding to the
 * link local address.
 *
 * There are two cases here:
 *
 *  1. LinkLocal is resolved, then MAC gets classID.
 *      processNeighborAdded<folly::MacAddress> assigns classID to Route(s)
 *
 *  2. MAC gets classID, then LinkLocal is resolved.
 *      processNeighborAdded<folly::IPAddressV6> assigns classID to Route(s)
 */
template <typename AddedNeighborT>
void LookupClassRouteUpdater::processNeighborAdded(
    const StateDelta& stateDelta,
    VlanID vlanID,
    const std::shared_ptr<AddedNeighborT>& addedNeighbor) {
  CHECK(addedNeighbor);

  auto newState = stateDelta.newState();
  folly::IPAddress addedNeighborIP;
  std::optional<cfg::AclLookupClass> neighborClassID;
  if constexpr (std::is_same_v<AddedNeighborT, MacEntry>) {
    addedNeighborIP = folly::IPAddressV6(
        folly::IPAddressV6::LinkLocalTag::LINK_LOCAL, addedNeighbor->getMac());
    if (!isNeighborReachable(newState, vlanID, addedNeighborIP.asV6())) {
      return;
    }
    // LinkLocal is resolved, then MAC gets classID
    neighborClassID = addedNeighbor->getClassID();
  } else {
    addedNeighborIP = addedNeighbor->getIP();
    if (addedNeighborIP.isV6() && addedNeighborIP.isLinkLocal()) {
      // MAC gets classID, then LinkLocal is resolved
      neighborClassID = getClassIDForLinkLocal(
          stateDelta.newState(), vlanID, addedNeighborIP.asV6());
    } else {
      neighborClassID = addedNeighbor->getClassID();
    }
  }

  if (!belongsToSubnetInCache(vlanID, addedNeighborIP)) {
    return;
  }

  // If the neighbor is nextHop for a route that is already processed, the
  // neighbor would be present in the cache.
  auto& [withClassIDPrefixes, withoutClassIDPrefixes] =
      nextHopAndVlan2Prefixes_[std::make_pair(addedNeighborIP, vlanID)];

  if (!neighborClassID.has_value()) {
    return;
  }

  /*
   * Check if the added neighbor would make a route become QPH multiple next
   * hop, since neighbor is already associated with a class ID.
   */
  addPrefixesWithMultiNextHops(
      withoutClassIDPrefixes, addedNeighborIP, newState, vlanID);

  /*
   * For every route with *this* nexthop, if the route does not have a
   * classID associated with it, then assign *this* nexthop's classID.
   */
  std::vector<RidAndCidr> toBeUpdatedPrefixes;
  std::set_difference(
      withoutClassIDPrefixes.begin(),
      withoutClassIDPrefixes.end(),
      allPrefixesWithClassID_.begin(),
      allPrefixesWithClassID_.end(),
      std::inserter(toBeUpdatedPrefixes, toBeUpdatedPrefixes.end()));

  auto routeClassID = neighborClassID.value();

  for (const auto& ridAndCidr : toBeUpdatedPrefixes) {
    withoutClassIDPrefixes.erase(ridAndCidr);
    withClassIDPrefixes.insert(ridAndCidr);
    allPrefixesWithClassID_.insert(ridAndCidr);
    toUpdateRoutesAndClassIDs_.emplace_back(ridAndCidr, routeClassID);
  }
}

template <typename RemovedNeighborT>
void LookupClassRouteUpdater::processNeighborRemoved(
    const StateDelta& stateDelta,
    VlanID vlanID,
    const std::shared_ptr<RemovedNeighborT>& removedNeighbor) {
  CHECK(removedNeighbor);

  folly::IPAddress removedNeighborIP;
  if constexpr (std::is_same_v<RemovedNeighborT, MacEntry>) {
    /*
     * Link local neighbors don't have classID, thus classID for a route whose
     * nexthop is link local is derived from MAC address corresponding to the
     * link local address.
     * Thus, when a MAC is removed, process classID change for route whose
     * nexthop is link local corresponding to the MAC being removed.
     */
    removedNeighborIP = folly::IPAddressV6(
        folly::IPAddressV6::LinkLocalTag::LINK_LOCAL,
        removedNeighbor->getMac());
  } else {
    removedNeighborIP = removedNeighbor->getIP();
  }

  if (!belongsToSubnetInCache(vlanID, removedNeighborIP)) {
    return;
  }

  auto it =
      nextHopAndVlan2Prefixes_.find(std::make_pair(removedNeighborIP, vlanID));
  if (it == nextHopAndVlan2Prefixes_.end()) {
    return;
  }

  auto& [withClassIDPrefixes, withoutClassIDPrefixes] = it->second;

  if (withClassIDPrefixes.empty() && withoutClassIDPrefixes.empty()) {
    // neighbor being removed is not a nexthop for any route
    nextHopAndVlan2Prefixes_.erase(it);
    return;
  }

  auto& newState = stateDelta.newState();

  // Updating prefixesWithMultiNextHops_ to see if there's any route with
  // multiple next hop becomes single next hop. Routes with classId will be
  // handled in addRouteAndFindClassID.
  removePrefixesWithMultiNextHops(withoutClassIDPrefixes, removedNeighborIP);

  auto iter = withClassIDPrefixes.begin();
  while (iter != withClassIDPrefixes.end()) {
    auto ridAndCidr = *iter;
    // erase the current iterator, and advance
    iter = withClassIDPrefixes.erase(iter);
    withoutClassIDPrefixes.insert(ridAndCidr);
    allPrefixesWithClassID_.erase(ridAndCidr);

    auto& [rid, cidr] = ridAndCidr;
    std::optional<cfg::AclLookupClass> routeClassID{std::nullopt};

    /*
     * Reuse addRouteAndFindClassID to find another nexthop (if any) that has
     * classID. Note that stateDelta.newState() may still contain the neighbor
     * with classID - if LookupClassUpdater hasn't processed it yet. So the
     * neighbor being removed is omitted explicitly from the computatation by
     * passing to addRouteAndFindClassID.
     */
    if (cidr.first.isV6()) {
      auto route = findRoute<folly::IPAddressV6>(rid, cidr, newState);
      if (route) {
        routeClassID = addRouteAndFindClassID(
            stateDelta, rid, route, std::make_pair(removedNeighborIP, vlanID));
      }
    } else {
      auto route = findRoute<folly::IPAddressV4>(rid, cidr, newState);
      if (route) {
        routeClassID = addRouteAndFindClassID(
            stateDelta, rid, route, std::make_pair(removedNeighborIP, vlanID));
      }
    }

    toUpdateRoutesAndClassIDs_.emplace_back(ridAndCidr, routeClassID);
  }
}

template <typename ChangedNeighborT>
void LookupClassRouteUpdater::processNeighborChanged(
    const StateDelta& stateDelta,
    VlanID vlanID,
    const std::shared_ptr<ChangedNeighborT>& oldNeighbor,
    const std::shared_ptr<ChangedNeighborT>& newNeighbor) {
  CHECK(oldNeighbor && newNeighbor);

  if (!oldNeighbor->getClassID().has_value() &&
      !newNeighbor->getClassID().has_value()) {
    return;
  } else if (
      !oldNeighbor->getClassID().has_value() &&
      newNeighbor->getClassID().has_value()) {
    processNeighborAdded(stateDelta, vlanID, newNeighbor);
  } else if (
      oldNeighbor->getClassID().has_value() &&
      !newNeighbor->getClassID().has_value()) {
    processNeighborRemoved(stateDelta, vlanID, oldNeighbor);
  } else if (
      oldNeighbor->getClassID().has_value() &&
      newNeighbor->getClassID().has_value()) {
    if (oldNeighbor->getClassID().value() !=
        newNeighbor->getClassID().value()) {
      processNeighborRemoved(stateDelta, vlanID, oldNeighbor);
      processNeighborAdded(stateDelta, vlanID, newNeighbor);
    }
  }
}

// VOQ switches don't use VLANs. Thus, neighbor tables are being migrated
// from VLANs to Interfaces.
// However, LookupClassUpdater heavily uses VLANs and is enabled only for NPU
// switches today.
// Thus, this function works as follows:
// - Compute neighbor table delta for VLAN neighbor tables or Interface
//   neighbor tables depending on the feature flag.
// - For the old/new interface, derive corresponding VLAN ID.
//   There is always 1:1 between interface and VLAN for NPU switches.
// Use the VLAN ID to pass to other methods in LookupClassUpdater.
template <typename AddrT, typename MapDeltaT>
void LookupClassRouteUpdater::processNeighborUpdates(
    const StateDelta& stateDelta,
    const MapDeltaT& mapDelta) {
  for (const auto& mapDeltaEntry : mapDelta) {
    auto newVlanOrIntf = mapDeltaEntry.getNew();
    if (!newVlanOrIntf) {
      auto oldVlanOrIntf = mapDeltaEntry.getOld();

      std::shared_ptr<Vlan> oldVlan;
      if constexpr (std::is_same_v<MapDeltaT, MultiSwitchInterfaceMapDelta>) {
        // Lookup VLAN corresponding to the interface
        oldVlan = stateDelta.oldState()->getVlans()->getNodeIf(
            oldVlanOrIntf->getVlanID());
      } else {
        oldVlan = oldVlanOrIntf;
      }

      for (auto iter :
           std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
               stateDelta.oldState(), oldVlan))) {
        auto entry = iter.second;
        processNeighborRemoved(stateDelta, oldVlan->getID(), entry);
      }

      continue;
    }

    VlanID vlanID;
    if constexpr (std::is_same_v<MapDeltaT, MultiSwitchInterfaceMapDelta>) {
      // Lookup VLAN corresponding to the interface
      vlanID = newVlanOrIntf->getVlanID();
    } else {
      vlanID = newVlanOrIntf->getID();
    }

    for (const auto& delta :
         NeighborTableDeltaCallbackGenerator::getTableDelta<AddrT>(
             mapDeltaEntry)) {
      auto oldNeighbor = delta.getOld();
      auto newNeighbor = delta.getNew();

      /*
       * At this point in time, queue-per-host fix is needed (and thus
       * supported) for physical link only.
       */
      if ((oldNeighbor && !oldNeighbor->getPort().isPhysicalPort()) ||
          (newNeighbor && !newNeighbor->getPort().isPhysicalPort())) {
        continue;
      }

      if (!oldNeighbor) {
        processNeighborAdded(stateDelta, vlanID, newNeighbor);
      } else if (!newNeighbor) {
        processNeighborRemoved(stateDelta, vlanID, oldNeighbor);
      } else {
        processNeighborChanged(stateDelta, vlanID, oldNeighbor, newNeighbor);
      }
    }
  }
}

template <typename RouteT>
bool LookupClassRouteUpdater::addRouteToMultiNextHopMap(
    const std::shared_ptr<SwitchState>& newState,
    const std::shared_ptr<RouteT>& route,
    std::optional<std::pair<folly::IPAddress, VlanID>> addedNeighborIPandVlan,
    const RidAndCidr& ridAndCidr) {
  for (const auto& nextHop : getNextHops(newState, route->getForwardInfo())) {
    if (nextHop.resolvedInNextLevel()) {
      continue;
    }
    auto vlanID =
        newState->getInterfaces()->getNodeIf(nextHop.intf())->getVlanID();
    if (!belongsToSubnetInCache(vlanID, nextHop.addr())) {
      continue;
    }

    const auto& [addedNeighborIP, addedNeighborVlan] =
        addedNeighborIPandVlan.value();
    if (nextHop.addr() == addedNeighborIP && vlanID == addedNeighborVlan) {
      continue;
    }

    auto neighborClassID =
        getClassIDForNeighbor(newState, vlanID, nextHop.addr());
    if (neighborClassID.has_value() && neighborClassID == route->getClassID()) {
      // Insert this route to the multi-nexthop map.
      prefixesWithMultiNextHops_[ridAndCidr] = {
          addedNeighborIP, nextHop.addr()};
      return true;
    }
  }
  return false;
}

// Methods for handling route updates

template <typename RouteT>
std::optional<cfg::AclLookupClass>
LookupClassRouteUpdater::addRouteAndFindClassID(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& addedRoute,
    std::optional<std::pair<folly::IPAddress, VlanID>> nextHopAndVlanToOmit) {
  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{
          addedRoute->prefix().network(), addedRoute->prefix().mask()});

  auto& newState = stateDelta.newState();
  std::optional<cfg::AclLookupClass> routeClassID{std::nullopt};
  std::set<folly::IPAddress> neighborsWithClassId;
  for (const auto& nextHop :
       getNextHops(newState, addedRoute->getForwardInfo())) {
    if (nextHop.resolvedInNextLevel()) {
      continue;
    }
    auto vlanID =
        newState->getInterfaces()->getNodeIf(nextHop.intf())->getVlanID();
    if (!belongsToSubnetInCache(vlanID, nextHop.addr())) {
      continue;
    }

    if (nextHopAndVlanToOmit.has_value()) {
      const auto& [nextHopToOmit, vlanIDToOmit] = nextHopAndVlanToOmit.value();
      if (nextHop.addr() == nextHopToOmit && vlanID == vlanIDToOmit) {
        continue;
      }
    }

    auto neighborClassID =
        getClassIDForNeighbor(newState, vlanID, nextHop.addr());
    if (neighborClassID.has_value()) {
      neighborsWithClassId.insert(nextHop.addr());
    }

    /*
     * The nextHopAndVlan may already be cached if:
     *   - it is also nextHop for some other route that was previously added.
     *   - during processNeighborAdded
     * retrieve previously cached entry, and if absent, create new entry.
     */
    auto& [withClassIDPrefixes, withoutClassIDPrefixes] =
        nextHopAndVlan2Prefixes_[std::make_pair(nextHop.addr(), vlanID)];

    /*
     * In the current implementation, route inherits classID of the 'first'
     * nexthop that has classID. This could be revised in future if necessary.
     */
    if (!routeClassID.has_value() && neighborClassID.has_value()) {
      routeClassID = neighborClassID;
      withClassIDPrefixes.insert(ridAndCidr);
      withoutClassIDPrefixes.erase(ridAndCidr);
    } else {
      /*
       * Refer to detailed comment in agent/LookupClassRouteUpdater.h
       * Route inherits classID of one of its reachable next hops...
       */
      if (neighborClassID.has_value()) {
        XLOG(DBG2) << "Queue-per-host can break for Route " << addedRoute->str()
                   << " when traffic chooses vlan: " << vlanID
                   << " nexthop: " << nextHop.addr();
      }

      withoutClassIDPrefixes.insert(ridAndCidr);
      withClassIDPrefixes.erase(ridAndCidr);
    }
  }

  if (routeClassID.has_value()) {
    allPrefixesWithClassID_.insert(ridAndCidr);
  } else {
    allPrefixesWithClassID_.erase(ridAndCidr);
  }

  /*
   * Update prefixesWithMultiNextHops_ to keep track of routes with multiple
   * nexthop.
   */
  updatePrefixesWithMultiNextHops(neighborsWithClassId, ridAndCidr);
  return routeClassID;
}

void LookupClassRouteUpdater::updatePrefixesWithMultiNextHops(
    const std::set<folly::IPAddress>& neighborsWithClassId,
    const RidAndCidr& ridAndCidr) {
  if (neighborsWithClassId.size() > 1) {
    // The route has multiple nexthops pointing to different class IDs - add
    // into the map if not already.
    auto ret = prefixesWithMultiNextHops_.insert(
        std::pair<RidAndCidr, std::set<folly::IPAddress>>(
            ridAndCidr, neighborsWithClassId));
    if (ret.second) {
      fb303::fbData->setCounter(
          SwitchStats::kCounterPrefix + kQphMultiNextHopCounter,
          prefixesWithMultiNextHops_.size());
      XLOG(DBG2) << "Number of routes with QPH multiple next hops: "
                 << prefixesWithMultiNextHops_.size();
    }
  } else {
    // Remove route from the set if the route no longer has multiple nexthops.
    auto ret = prefixesWithMultiNextHops_.erase(ridAndCidr);
    if (ret) {
      fb303::fbData->setCounter(
          SwitchStats::kCounterPrefix + kQphMultiNextHopCounter,
          prefixesWithMultiNextHops_.size());
      XLOG(DBG2) << "Number of routes with QPH multiple next hops: "
                 << prefixesWithMultiNextHops_.size();
    }
  }
}

void LookupClassRouteUpdater::removePrefixesWithMultiNextHops(
    const std::set<RidAndCidr>& withoutClassIDPrefixes,
    const folly::IPAddress& removedNeighborIP) {
  for (const auto& ridAndCidr : withoutClassIDPrefixes) {
    if (prefixesWithMultiNextHops_.find(ridAndCidr) !=
        prefixesWithMultiNextHops_.end()) {
      auto ret =
          prefixesWithMultiNextHops_[ridAndCidr].erase(removedNeighborIP);
      if (ret && prefixesWithMultiNextHops_[ridAndCidr].size() <= 1) {
        prefixesWithMultiNextHops_.erase(ridAndCidr);
        fb303::fbData->setCounter(
            SwitchStats::kCounterPrefix + kQphMultiNextHopCounter,
            prefixesWithMultiNextHops_.size());
        XLOG(DBG2) << "Number of routes with QPH multiple next hops: "
                   << prefixesWithMultiNextHops_.size();
      }
    }
  }
}

void LookupClassRouteUpdater::addPrefixesWithMultiNextHops(
    const std::set<RidAndCidr>& withoutClassIDPrefixes,
    const folly::IPAddress& addedNeighborIP,
    const std::shared_ptr<SwitchState>& newState,
    VlanID vlanID) {
  bool prefixesWithMultiNextHopsAdded = false;
  for (const auto& ridAndCidr : withoutClassIDPrefixes) {
    // Route already has multiple next hops - add neighbor to the set.
    if (prefixesWithMultiNextHops_.find(ridAndCidr) !=
        prefixesWithMultiNextHops_.end()) {
      prefixesWithMultiNextHops_[ridAndCidr].insert(addedNeighborIP);
    } else {
      // If route has classId, with the addition of neighbor class ID, it will
      // have multiple next hops with classID.
      auto& [rid, cidr] = ridAndCidr;
      std::set<folly::IPAddress> nextHopsWithClassId{addedNeighborIP};
      if (cidr.first.isV6()) {
        auto route = findRoute<folly::IPAddressV6>(rid, cidr, newState);
        if (route->getClassID().has_value()) {
          prefixesWithMultiNextHopsAdded |= addRouteToMultiNextHopMap(
              newState,
              route,
              std::make_pair(addedNeighborIP, vlanID),
              ridAndCidr);
        }
      } else {
        auto route = findRoute<folly::IPAddressV4>(rid, cidr, newState);
        if (route->getClassID().has_value()) {
          prefixesWithMultiNextHopsAdded |= addRouteToMultiNextHopMap(
              newState,
              route,
              std::make_pair(addedNeighborIP, vlanID),
              ridAndCidr);
        }
      }
    }
  }
  if (prefixesWithMultiNextHopsAdded) {
    fb303::fbData->setCounter(
        SwitchStats::kCounterPrefix + kQphMultiNextHopCounter,
        prefixesWithMultiNextHops_.size());
    XLOG(DBG2) << "Number of routes with QPH multiple next hops: "
               << prefixesWithMultiNextHops_.size();
  }
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteAdded(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& addedRoute) {
  CHECK(addedRoute);

  /*
   * Non-resolved routes are not programmed in HW
   * routes to CPU have no nextHops, and can't get classID.
   */
  if (!addedRoute->isResolved() || addedRoute->isToCPU()) {
    return;
  }

  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{
          addedRoute->prefix().network(), addedRoute->prefix().mask()});
  auto routeClassID =
      addRouteAndFindClassID(stateDelta, rid, addedRoute, std::nullopt);

  if (routeClassID.has_value()) {
    toUpdateRoutesAndClassIDs_.push_back(
        std::make_pair(ridAndCidr, routeClassID));
  }
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteRemoved(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& removedRoute) {
  CHECK(removedRoute);

  /*
   * Non-resolved routes are not programmed in HW
   * routes to CPU (have no nextHops), and can't get classID.
   */
  if (!removedRoute->isResolved() || removedRoute->isToCPU()) {
    return;
  }

  // ClassID is associated with (and refCnt'ed for) MAC and ARP/NDP neighbor.
  // Route simply inherits classID of its nexthop, so we need not release
  // classID here. Furthermore, the route is already removed, so we don't need
  // to schedule a state update either. Just remove the route from local data
  // structures.

  auto ridAndCidr = std::make_pair(
      rid,
      folly::CIDRNetwork{
          removedRoute->prefix().network(), removedRoute->prefix().mask()});

  auto routeClassID = removedRoute->getClassID();
  auto& oldState = stateDelta.oldState();
  auto& newState = stateDelta.newState();
  for (const auto& nextHop :
       getNextHops(oldState, removedRoute->getForwardInfo())) {
    if (nextHop.resolvedInNextLevel()) {
      continue;
    }
    auto vlanID =
        newState->getInterfaces()->getNodeIf(nextHop.intf())->getVlanID();
    if (!belongsToSubnetInCache(vlanID, nextHop.addr())) {
      continue;
    }

    auto it =
        nextHopAndVlan2Prefixes_.find(std::make_pair(nextHop.addr(), vlanID));
    CHECK(it != nextHopAndVlan2Prefixes_.end());
    auto& [withClassIDPrefixes, withoutClassIDPrefixes] = it->second;

    // The prefix has to be in either of the sets.
    auto numErased = withClassIDPrefixes.erase(ridAndCidr) +
        withoutClassIDPrefixes.erase(ridAndCidr);
    CHECK_EQ(numErased, 1);

    if (withClassIDPrefixes.empty() && withoutClassIDPrefixes.empty()) {
      // if this was the only route this entry was NextHop for, and there is no
      // neighbor corresponding to this NextHop, erase it.
      auto vlan = newState->getVlans()->getNodeIf(vlanID);
      if (vlan) {
        if (nextHop.addr().isV6()) {
          auto ndpEntry = getNeighborTableForVlan<NdpTable>(newState, vlanID)
                              ->getEntryIf(nextHop.addr().asV6());
          if (!ndpEntry) {
            nextHopAndVlan2Prefixes_.erase(it);
          }
        } else if (nextHop.addr().isV4()) {
          auto arpEntry = getNeighborTableForVlan<ArpTable>(newState, vlanID)
                              ->getEntryIf(nextHop.addr().asV4());
          if (!arpEntry) {
            nextHopAndVlan2Prefixes_.erase(it);
          }
        }
      }
    }
  }

  if (routeClassID.has_value()) {
    auto numErasedFromAllPrefixes = allPrefixesWithClassID_.erase(ridAndCidr);
    // LE because this maybe and an update due to neighbor going away. In which
    // case we would clear inherited class ID on route due to neighbor and prune
    // from allPrefixesWithClassID_ during neighbor removal. Then we would get
    // another update due to change route (some classId->none). Which should be
    // a noop.
    CHECK_LE(numErasedFromAllPrefixes, 1);
  }
}

template <typename RouteT>
void LookupClassRouteUpdater::processRouteChanged(
    const StateDelta& stateDelta,
    RouterID rid,
    const std::shared_ptr<RouteT>& oldRoute,
    const std::shared_ptr<RouteT>& newRoute) {
  CHECK(oldRoute);
  CHECK(newRoute);

  if (!oldRoute->isResolved() && !newRoute->isResolved()) {
    return;
  } else if (!oldRoute->isResolved() && newRoute->isResolved()) {
    processRouteAdded(stateDelta, rid, newRoute);
  } else if (oldRoute->isResolved() && !newRoute->isResolved()) {
    processRouteRemoved(stateDelta, rid, oldRoute);
  } else if (oldRoute->isResolved() && newRoute->isResolved()) {
    /*
     * If the list of nexthops changes, a route may lose the nexthop it
     * inherited classID from. In that case, we need to find another reachable
     * nexthop for the route.
     *
     * This could be implemented by std::set_difference of getNextHopSet().
     * However, it is easier to remove the route and add it again.
     * processRouteRemoved does not schedule state update, so the only
     * additional overhead of this approach is some local computation.
     */
    if ((getNextHops(stateDelta.oldState(), oldRoute->getForwardInfo()) !=
         getNextHops(stateDelta.newState(), newRoute->getForwardInfo())) ||
        (oldRoute->getClassID() != newRoute->getClassID())) {
      processRouteRemoved(stateDelta, rid, oldRoute);
      processRouteAdded(stateDelta, rid, newRoute);
    }
  }
}

template <typename AddrT>
void LookupClassRouteUpdater::processRouteUpdates(
    const StateDelta& stateDelta) {
  auto changedFn =
      [&stateDelta, this](
          RouterID rid, const auto& oldRoute, const auto& newRoute) {
        processRouteChanged(stateDelta, rid, oldRoute, newRoute);
      };
  auto addedFn = [&stateDelta, this](RouterID rid, const auto& newRoute) {
    processRouteAdded(stateDelta, rid, newRoute);
  };
  auto removedFn = [&stateDelta, this](RouterID rid, const auto& oldRoute) {
    processRouteRemoved(stateDelta, rid, oldRoute);
  };
  forEachChangedRoute<AddrT>(stateDelta, changedFn, addedFn, removedFn);
}

// Methods for scheduling state updates

void LookupClassRouteUpdater::updateClassIDsForRoutes(
    const std::vector<RouteAndClassID>& routesAndClassIDs) const {
  std::unordered_map<
      std::pair<RouterID, std::optional<cfg::AclLookupClass>>,
      std::vector<folly::CIDRNetwork>>
      ridClassId2Prefixes;
  for (const auto& [ridAndCidr, classID] : routesAndClassIDs) {
    auto& [rid, cidr] = ridAndCidr;
    ridClassId2Prefixes[std::make_pair(rid, classID)].emplace_back(cidr);
  }
  auto updater = sw_->getRouteUpdater();
  for (const auto& [ridAndClassId, prefixes] : ridClassId2Prefixes) {
    updater.programClassID(
        ridAndClassId.first, prefixes, ridAndClassId.second, true /*async*/);
  }
}

// Methods for blocked neighbor processing
std::optional<folly::CIDRNetwork>
LookupClassRouteUpdater::getInterfaceSubnetForIPIf(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::IPAddress& ipAddress) const {
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return std::nullopt;
  }

  auto interface =
      switchState->getInterfaces()->getNodeIf(vlan->getInterfaceID());
  if (interface) {
    for (auto iter : std::as_const(*interface->getAddresses())) {
      std::pair<folly::IPAddress, uint8_t> address(
          folly::IPAddress(iter.first), iter.second->ref());
      if (ipAddress.inSubnet(address.first, address.second)) {
        return std::make_pair(address.first, address.second);
      }
    }
  }

  return std::nullopt;
}

bool LookupClassRouteUpdater::isSubnetCachedByLookupClasses(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const folly::CIDRNetwork& addressToSearch) const {
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  if (!vlan) {
    return false;
  }

  auto interface =
      switchState->getInterfaces()->getNodeIf(vlan->getInterfaceID());
  if (!interface) {
    return false;
  }

  bool searchInterfaceAddresses = false;
  for (const auto& portMap : std::as_const(*switchState->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getLookupClassesToDistributeTrafficOn().size() == 0) {
        continue;
      }

      // port is member of vlan for addressToSearch i.e. blocked IP
      auto vlans = port.second->getVlans();
      auto it = vlans.find(vlanID);
      if (it == vlans.end()) {
        continue;
      }

      /*
       * There is a port with non-empty lookupClasses && that port is member of
       * vlan for addressToearch i.e. blocked IP.
       */
      searchInterfaceAddresses = true;
      break;
    }
  }

  if (searchInterfaceAddresses) {
    for (auto iter : std::as_const(*interface->getAddresses())) {
      if (folly::CIDRNetwork(
              folly::IPAddress(iter.first), iter.second->cref()) ==
          addressToSearch) {
        return true;
      }
    }
  }

  return false;
}

template <typename AddrT>
bool LookupClassRouteUpdater::addBlockedNeighborIPtoSubnetCache(
    VlanID vlanID,
    const folly::MacAddress& blockedNeighborMac,
    const std::shared_ptr<SwitchState>& newState) {
  bool subnetCacheUpdated = false;
  auto vlan = newState->getVlans()->getNodeIf(vlanID);
  for (auto iter :
       std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
           newState, vlan))) {
    auto neighborEntry = iter.second;
    if (neighborEntry->getMac() != blockedNeighborMac ||
        isNoHostRoute(neighborEntry)) {
      continue;
    }

    auto neighborIPToBlock = neighborEntry->getIP();
    auto address =
        getInterfaceSubnetForIPIf(newState, vlanID, neighborIPToBlock);
    if (address.has_value()) {
      auto& subnetsCache = vlan2SubnetsCache_[vlanID];
      subnetCacheUpdated |= subnetsCache.insert(address.value()).second;
    }
  }
  return subnetCacheUpdated;
}

template <typename AddrT>
void LookupClassRouteUpdater::removeBlockedNeighborIPfromSubnetCache(
    VlanID vlanID,
    const folly::MacAddress& blockedNeighborMac,
    const StateDelta& stateDelta) {
  auto newState = stateDelta.newState();
  auto vlan = newState->getVlans()->getNodeIf(vlanID);
  for (auto iter :
       std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
           newState, vlan))) {
    auto neighborEntry = iter.second;
    if (neighborEntry->getMac() != blockedNeighborMac ||
        isNoHostRoute(neighborEntry)) {
      continue;
    }

    auto neighborIPToBlock = neighborEntry->getIP();
    auto address =
        getInterfaceSubnetForIPIf(newState, vlanID, neighborIPToBlock);
    /*
     * Remove subnet corresponding to IPs resovled to the unblocked MAC
     * if and only if it would not be cached by other lookup class.
     */
    if (address.has_value() &&
        !isSubnetCachedByLookupClasses(newState, vlanID, address.value())) {
      auto& subnetsCache = vlan2SubnetsCache_[vlanID];
      removeNextHopsForSubnet(stateDelta, address.value(), vlan);
      subnetsCache.erase(address.value());
    }
  }
}

void LookupClassRouteUpdater::processMacAddrsToBlockAdded(
    const StateDelta& stateDelta,
    const std::vector<std::pair<VlanID, folly::MacAddress>>&
        toBeAddedMacAddrsToBlock) {
  auto newState = stateDelta.newState();

  bool subnetCacheUpdated = false;

  for (const auto& [vlanID, blockedNeighborMac] : toBeAddedMacAddrsToBlock) {
    auto vlan = newState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    subnetCacheUpdated |=
        (addBlockedNeighborIPtoSubnetCache<folly::IPAddressV4>(
             vlanID, blockedNeighborMac, newState) ||
         addBlockedNeighborIPtoSubnetCache<folly::IPAddressV6>(
             vlanID, blockedNeighborMac, newState));
  }

  if (subnetCacheUpdated) {
    /*
     * When a new subnet is added to the cache, the nextHops of existing
     * routes may become eligible for caching in
     * nextHopAndVlan2Prefixes_. Furthermore, such a nextHop may have
     * classID associated with it, and in that case, the corresponding
     * route could inherit that classID. Thus, re-add all the routes.
     */
    reAddAllRoutes(stateDelta);
  }
}

void LookupClassRouteUpdater::processMacAddrsToBlockRemoved(
    const StateDelta& stateDelta,
    const std::vector<std::pair<VlanID, folly::MacAddress>>&
        toBeRemovedMacAddrsToBlock) {
  auto newState = stateDelta.newState();

  for (const auto& [vlanID, blockedNeighborMac] : toBeRemovedMacAddrsToBlock) {
    auto vlan = newState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    removeBlockedNeighborIPfromSubnetCache<folly::IPAddressV4>(
        vlanID, blockedNeighborMac, stateDelta);
    removeBlockedNeighborIPfromSubnetCache<folly::IPAddressV6>(
        vlanID, blockedNeighborMac, stateDelta);
  }
}

void LookupClassRouteUpdater::processMacAddrsToBlockUpdates(
    const StateDelta& stateDelta) {
  auto oldState = stateDelta.oldState();
  auto newState = stateDelta.newState();

  auto oldSwitchSettings = oldState->getSwitchSettings()->size()
      ? oldState->getSwitchSettings()->cbegin()->second
      : std::make_shared<SwitchSettings>();
  auto newSwitchSettings = newState->getSwitchSettings()->size()
      ? newState->getSwitchSettings()->cbegin()->second
      : std::make_shared<SwitchSettings>();

  std::vector<std::pair<VlanID, folly::MacAddress>> oldMacAddrsToBlock(
      oldSwitchSettings->getMacAddrsToBlock_DEPRECATED());

  std::vector<std::pair<VlanID, folly::MacAddress>> newMacAddrsToBlock(
      newSwitchSettings->getMacAddrsToBlock_DEPRECATED());

  sort(oldMacAddrsToBlock.begin(), oldMacAddrsToBlock.end());
  sort(newMacAddrsToBlock.begin(), newMacAddrsToBlock.end());

  if (newMacAddrsToBlock.empty() && oldMacAddrsToBlock.empty()) {
    return;
  }

  std::vector<std::pair<VlanID, folly::MacAddress>> toBeRemovedMacAddrsToBlock;
  std::set_difference(
      oldMacAddrsToBlock.begin(),
      oldMacAddrsToBlock.end(),
      newMacAddrsToBlock.begin(),
      newMacAddrsToBlock.end(),
      std::inserter(
          toBeRemovedMacAddrsToBlock, toBeRemovedMacAddrsToBlock.end()));
  processMacAddrsToBlockRemoved(stateDelta, toBeRemovedMacAddrsToBlock);

  /*
   * It could be insufficient to only process the newly added MAC address.
   * Consider the scenario:
   * 1. MAC is added to the block list
   * 2. Neighbor corresponding to blocked MAC is resolved after MAC address
   *    is added to the block list.
   * Route/neighbor entry would not exist at the time when MAC is added to the
   * block list. To gurantee correctness, always process all currently blocked
   * MAC address.
   */
  processMacAddrsToBlockAdded(stateDelta, newMacAddrsToBlock);
}

void LookupClassRouteUpdater::stateUpdated(const StateDelta& stateDelta) {
  // The current LookupClassRouteUpdater logic relies on VLANs, MAC learning
  // callbacks etc. that are not supported on VOQ/Fabric switches. Thus, run
  // this stateObserver for NPU switches only.
  if (!sw_->getSwitchInfoTable().haveL3Switches() ||
      sw_->getSwitchInfoTable().l3SwitchType() != cfg::SwitchType::NPU) {
    return;
  }

  /*
   * If FLAGS_queue_per_host_route_fix is false:
   *  - if inited_ is false i.e. first call to this state observer, disable
   *  queue-per-host route fix (clear classIDs associated with routes).
   *  - if inited_ is true i.e. subsequent calls to state observer, do nothing.
   */
  if (!inited_) {
    inited_ = true;
  }

  /*
   * If vlan2SubnetsCache_ is updated after routes are added, every update to
   * vlan2SubnetsCache_ must check if the nextHops of previously processed
   * routes now become eligible for addition to nextHopAndVlan2Prefixes_.
   * This would require processing ALL the routes from the switchState, which
   * is expensive. We avoid that by processing port additions before processing
   * route additions (i.e. by calling processPortUpdates before
   * processRouteUpdates).
   */
  processPortUpdates(stateDelta);

  processInterfaceUpdates(stateDelta);

  processMacAddrsToBlockUpdates(stateDelta);

  /*
   * Only RSWs connected to MH-NIC (e.g. Yosemite) need queue-per-host fix, and
   * thus have non-empty vlan2SubnetsCache_ (populated by processPortUpdates).
   * Skip the processing on other setups.
   */
  if (vlan2SubnetsCache_.empty()) {
    return;
  }

  // MAC Tables continue to be part of the VLANs.
  processNeighborUpdates<folly::MacAddress>(
      stateDelta, stateDelta.getVlansDelta());
  processNeighborUpdates<folly::IPAddressV6>(
      stateDelta, stateDelta.getIntfsDelta());
  processNeighborUpdates<folly::IPAddressV4>(
      stateDelta, stateDelta.getIntfsDelta());

  processRouteUpdates<folly::IPAddressV6>(stateDelta);
  processRouteUpdates<folly::IPAddressV4>(stateDelta);
  updateClassIDsForRoutes(toUpdateRoutesAndClassIDs_);
  toUpdateRoutesAndClassIDs_.clear();
}

} // namespace facebook::fboss
