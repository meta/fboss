/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/TunManager.h"

extern "C" {
#include <linux/if.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>
#include <netlink/route/rule.h>
#include <sys/ioctl.h>
}

#include <fb303/ServiceData.h>
#include <folly/MapUtil.h>
#include <folly/lang/CString.h>
#include <folly/logging/xlog.h>

#include <exception>
#include <unordered_map>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/NlError.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SysError.h"
#include "fboss/agent/TunIntf.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

#include <boost/container/flat_set.hpp>

namespace {
const int kDefaultMtu = 1500;
// Todo manually test
const int maxRouteTableId = 4096;
const int minRouteTableId = 1;

} // namespace

namespace facebook::fboss {

using folly::IPAddress;

// enable_1to1_intf_route_table_mapping feature depends on
// cleanup_probed_kernel_data if enabling on existing platforms
DEFINE_bool(
    enable_1to1_intf_route_table_mapping,
    false,
    "Enable 1 to 1 interface id to route table id mapping");

TunManager::TunManager(SwSwitch* sw, FbossEventBase* evb) : sw_(sw), evb_(evb) {
  DCHECK(sw) << "NULL pointer to SwSwitch.";
  DCHECK(evb) << "NULL pointer to EventBase";

  sock_ = nl_socket_alloc();
  if (!sock_) {
    throw FbossError("failed to allocate libnl socket");
  }
  auto error = nl_connect(sock_, NETLINK_ROUTE);
  nlCheckError(error, "failed to connect netlink socket to NETLINK_ROUTE");
}

void TunManager::stopProcessing() {
  if (observingState_) {
    sw_->unregisterStateObserver(this);
  }
  // Run stop() on the EventBase thread to synchronize with any in-flight
  // TunIntf::handlerReady callbacks. unregisterHandler() prevents new
  // dispatches but does NOT cancel an already-running callback. By
  // scheduling stop() on the evb, we guarantee it runs after any pending
  // handlerReady invocation completes, closing the race window between
  // stopProcessing() and SwSwitch::stop() tearing down members.
  if (evb_->isRunning()) {
    evb_->runImmediatelyOrRunInFbossEventBaseThreadAndWait([this] {
      std::lock_guard<std::mutex> lock(mutex_);
      stop();
    });
  } else {
    std::lock_guard<std::mutex> lock(mutex_);
    stop();
  }
  nl_close(sock_);
  nl_socket_free(sock_);
}

TunManager::~TunManager() {}

void TunManager::startObservingUpdates() {
  sw_->registerStateObserver(this, "TunManager");
  observingState_ = true;
}

void TunManager::stateUpdated(const StateDelta& delta) {
  // TODO(aeckert): t15067879 We currently compare the entire
  // interface map instead of using the iterator in this delta because
  // some of the interfaces may get get probed from hardware before
  // they are in the SwitchState. It would be nicer if we did a little
  // more work at startup to sync the state, perhaps updating the
  // SwitchState with the probed interfaces. This would allow us to
  // reuse the iterator in the delta for more readable code and also
  // not have to worry about waiting to listen to updates until the
  // SwSwitch is in the configured state. t4155406 should also help
  // with that.

  auto state = delta.newState();
  evb_->runInFbossEventBaseThread([this, state]() { this->sync(state); });
}

bool TunManager::sendPacketToHost(
    InterfaceID dstIfID,
    std::unique_ptr<RxPacket> pkt) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = intfs_.find(dstIfID);
  if (iter == intfs_.end()) {
    // the Interface ID has been deleted, make a log, and skip the pkt
    XLOG(DBG4) << "Dropping a packet for unknown interface " << dstIfID;
    return false;
  }
  return iter->second->sendPacketToHost(std::move(pkt));
}

void TunManager::addExistingIntf(const std::string& ifName, int ifIndex) {
  InterfaceID ifID = utility::getIDFromTunIntfName(ifName);
  auto ret = intfs_.emplace(ifID, nullptr);
  if (!ret.second) {
    throw FbossError("Duplicate interface ", ifName);
  }

  SCOPE_FAIL {
    intfs_.erase(ret.first);
  };
  ret.first->second.reset(new TunIntf(
      sw_, evb_, ifID, getInterfaceType(ifID), ifIndex, getInterfaceMtu(ifID)));
}

void TunManager::addNewIntf(
    InterfaceID ifID,
    bool isUp,
    const Interface::Addresses& addrs) {
  auto ret = intfs_.emplace(ifID, nullptr);
  if (!ret.second) {
    throw FbossError("Duplicate interface for interface ", ifID);
  }

  SCOPE_FAIL {
    intfs_.erase(ret.first);
  };
  auto intf = std::make_unique<TunIntf>(
      sw_,
      evb_,
      ifID,
      getInterfaceType(ifID),
      isUp,
      addrs,
      getInterfaceMtu(ifID));

  SCOPE_FAIL {
    intf->setDelete();
  };
  const auto ifName = intf->getName();
  auto ifIndex = intf->getIfIndex();

  // bring up the interface so that we can add the default route in next step
  setIntfStatus(ifName, ifIndex, isUp);

  // create a new route table for this InterfaceID
  addRouteTable(ifID, ifIndex);

  // add all addresses
  for (const auto& addr : addrs) {
    addTunAddress(ifID, ifName, ifIndex, addr.first, addr.second);
  }

  // If interface is created in DOWN state, program unreachable routes
  // to prevent traffic from falling back to OOB via eth0.
  if (!isUp) {
    addUnreachableRoutesForIntf(ifID, addrs);
  }

  // Store it in local map on success
  ret.first->second = std::move(intf);
}

void TunManager::removeIntf(InterfaceID ifID) {
  auto iter = intfs_.find(ifID);
  if (iter == intfs_.end()) {
    throw FbossError("Cannot find interface ", ifID, " for deleting.");
  }

  // Remove any unreachable routes for this interface (if it was down)
  removeUnreachableRoutesForIntf(ifID);

  // Remove all source routing rules attached to this interface. Addresses
  // and routes are automatically removed once interface is deteled.
  auto& intf = iter->second;
  for (auto const& addr : intf->getAddresses()) {
    addRemoveSourceRouteRule(ifID, addr.first, false);
  }

  // Remove the route table and associated rule
  removeRouteTable(ifID, intf->getIfIndex());
  intf->setDelete();
  intfs_.erase(iter);
}

void TunManager::addProbedAddr(
    int ifIndex,
    const IPAddress& addr,
    uint8_t mask) {
  for (auto& intf : intfs_) {
    if (intf.second->getIfIndex() == ifIndex) {
      XLOG(DBG2) << "Adding probed address: " << addr.str() << "/"
                 << static_cast<int>(mask) << " @ index " << ifIndex;
      intf.second->addAddress(addr, mask);
      return;
    }
  }
  // This function is called for all interface addresses discovered from
  // the host. Since we only create TunIntf object for TUN interfaces,
  // it is normal we cannot find the interface matching the ifIndex provided.
  XLOG(DBG3) << "Cannot find interface @ index " << ifIndex
             << " for probed address " << addr.str() << "/"
             << static_cast<int>(mask);
}

void TunManager::setIntfStatus(
    const std::string& ifName,
    int ifIndex,
    bool status) {
  /**
   * NOTE: Why use `ioctl` instead of `netlink` ?
   *
   * netlink's `rtnl_link_change` API was in-effective. Internally netlink
   * tries to pass a message with RTM_NEWLINK which some old kernel couldn't
   * process. After messing my head around with netlink for few hours I decided
   * to use `ioctl` which was much easy and straight-forward operation.
   */

  // Prepare socket
  auto sockFd = socket(PF_INET, SOCK_DGRAM, 0);
  sysCheckError(sockFd, "Failed to open socket");
  SCOPE_EXIT {
    close(sockFd);
  };

  // Prepare request
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  folly::strlcpy(ifr.ifr_name, ifName.c_str(), IFNAMSIZ);

  // Get existing flags
  int error = ioctl(sockFd, SIOCGIFFLAGS, static_cast<void*>(&ifr));
  sysCheckError(error, "Failed to get existing interface flags of ", ifName);

  // Mutate flags
  if (status) {
    ifr.ifr_flags |= IFF_UP;
  } else {
    ifr.ifr_flags &= ~IFF_UP;
  }

  // Set flags
  error = ioctl(sockFd, SIOCSIFFLAGS, static_cast<void*>(&ifr));
  sysCheckError(error, "Failed to set interface flags on ", ifName);
  XLOG(DBG2) << "Brought " << (status ? "up" : "down") << " interface "
             << ifName << " @ index " << ifIndex;
}

int TunManager::getTableId(InterfaceID ifID) const {
  int tableId;
  switch (sw_->getSwitchInfoTable().l3SwitchType()) {
    case cfg::SwitchType::NPU:
      tableId = getTableIdForNpu(ifID);
      break;
    case cfg::SwitchType::VOQ:
      tableId = getTableIdForVoq(ifID);
      break;
    case cfg::SwitchType::PHY:
    case cfg::SwitchType::FABRIC:
      CHECK(false);
      throw FbossError("No system port range in SwitchSettings for VOQ switch");
  }

  if (!FLAGS_enable_1to1_intf_route_table_mapping) {
    // Sanity checks. Generated ID must be in range [1-253]
    CHECK_GE(tableId, 1);
    CHECK_LE(tableId, 253);
  } else {
    // Sanity checks. Generated ID must be in range  [minRouteTableId,
    // maxRouteTableId]
    CHECK_GE(tableId, minRouteTableId);
    CHECK_LE(tableId, maxRouteTableId);
  }

  return tableId;
}

std::unordered_map<InterfaceID, int> TunManager::buildIfIdToTableIdMap(
    std::shared_ptr<SwitchState> state) const {
  std::unordered_map<InterfaceID, int> ifIdToTableId;
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (const auto& iter : std::as_const(*intfMap)) {
      const auto& intf = iter.second;
      auto ifId = intf->getID();
      auto tableId = getTableId(ifId);
      ifIdToTableId[ifId] = tableId;
      XLOG(DBG2) << "Created mapping from state: ifId " << ifId
                 << " -> tableId " << tableId;
    }
  }
  return ifIdToTableId;
}

std::unordered_map<int, int>
TunManager::buildIfIndexToTableIdMapFromProbedRoutes() const {
  std::unordered_map<int, int> ifIndexToTableId;
  for (const auto& probedRoute : probedRoutes_) {
    if (probedRoute.ifIndex > 0) {
      ifIndexToTableId[probedRoute.ifIndex] = probedRoute.tableId;
      XLOG(DBG2) << "Created mapping: ifIndex " << probedRoute.ifIndex
                 << " -> tableId " << probedRoute.tableId;
    } else {
      XLOG(DBG2) << "Skipping route & source-rule cleanup for table "
                 << probedRoute.tableId << " (no ifindex)";
    }
  }
  return ifIndexToTableId;
}

std::unordered_map<int, int> TunManager::buildIfIndexToTableIdMapFromRules()
    const {
  std::unordered_map<int, int> ifIndexToTableId;

  // For each interface, find matching source rules to determine table ID
  for (const auto& intf : intfs_) {
    auto ifIndex = intf.second->getIfIndex();
    auto ifID = intf.first;
    const auto& addresses = intf.second->getAddresses();

    bool foundMapping = false;

    // Look for source rules matching this interface's addresses
    for (const auto& addr : addresses) {
      if (addr.first.isLinkLocal()) {
        continue; // Skip link-local addresses
      }

      for (const auto& rule : probedRules_) {
        // Parse the rule source address to extract just the IP part
        auto ipaddr = IPAddress::createNetwork(rule.srcAddr, -1, false).first;
        std::string ruleAddrOnly = ipaddr.str();

        // Compare the IP address parts
        if (ruleAddrOnly == addr.first.str()) {
          ifIndexToTableId[ifIndex] = rule.tableId;
          XLOG(DBG2) << "Mapped ifIndex " << ifIndex << " to tableId "
                     << rule.tableId << " via address " << addr.first.str()
                     << " (rule: " << rule.srcAddr << ")";
          foundMapping = true;
          break; // Found mapping for this interface
        }
      }

      if (foundMapping) {
        break; // Found mapping for this interface
      }
    }

    if (!foundMapping) {
      XLOG(DBG2) << "No source rule found for interface " << ifID
                 << " @ ifIndex " << ifIndex;
    }
  }

  return ifIndexToTableId;
}

std::unordered_map<int, int> TunManager::buildIfIndextoTableMapFromProbedData()
    const {
  // Build a map of interface index to table ID from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMapFromProbedRoutes();

  // Augment ifIndexToTableId with buildIfIndexToTableIdMapFromRules
  auto ifIndexToTableIdFromRules = buildIfIndexToTableIdMapFromRules();
  for (const auto& [ifIndex, tableId] : ifIndexToTableIdFromRules) {
    // Add whatever was not there from probed routes
    if (ifIndexToTableId.find(ifIndex) == ifIndexToTableId.end()) {
      ifIndexToTableId[ifIndex] = tableId;
      XLOG(DBG2) << "Augmented mapping from source rules: ifIndex " << ifIndex
                 << " -> tableId " << tableId;
    }
  }

  return ifIndexToTableId;
}

std::unordered_map<InterfaceID, int> TunManager::buildProbedIfIdToTableIdMap()
    const {
  std::unordered_map<InterfaceID, int> probedIfIdToTableId;

  // Build a map of interface index to table ID from probed data
  auto ifIndexToTableId = buildIfIndextoTableMapFromProbedData();

  // Map interface IDs to table IDs using probed data
  for (const auto& intf : intfs_) {
    auto ifId = intf.first; // InterfaceID from map key
    auto ifIndex = intf.second->getIfIndex();

    // Get table ID from probed routes instead of computing new one
    auto tableIdIter = ifIndexToTableId.find(ifIndex);
    if (tableIdIter != ifIndexToTableId.end()) {
      auto tableId = tableIdIter->second;
      probedIfIdToTableId[ifId] = tableId;
      XLOG(DBG2) << "Created mapping from probed interfaces: ifId " << ifId
                 << " -> tableId " << tableId;
    } else {
      XLOG(DBG2) << "No probed table ID found for interface " << ifId
                 << " @ ifIndex " << ifIndex;
    }
  }
  return probedIfIdToTableId;
}

bool TunManager::requiresProbedDataCleanup(
    const std::unordered_map<InterfaceID, int>& stateMap,
    const std::unordered_map<InterfaceID, int>& probedMap) const {
  bool mapsEqual = (stateMap == probedMap);

  if (!mapsEqual) {
    XLOG(DBG2) << "Interface mappings differ - state has " << stateMap.size()
               << " entries, probed has " << probedMap.size() << " entries";
    return true;
  }

  XLOG(DBG2)
      << "Interface ID to table ID mappings are identical, skipping cleanup";
  return false;
}

int TunManager::getTableIdForNpu(InterfaceID ifID) const {
  int tableId = static_cast<int>(ifID);
  // Start from TH6, table id could be larger than 256, directly use interface
  // id as table id
  if (FLAGS_enable_1to1_intf_route_table_mapping) {
    if (tableId == 0 || tableId == 253 || tableId == 254 || tableId == 255) {
      throw FbossError(
          "Route table ID 0, 253, 254, 255 are reserved in kernel usage");
    }
    return tableId;
  }
  // Kernel only supports up to 256 tables. The last few are used by kernel
  // as main, default, and local. IDs 0, 254 and 255 are not available. So we
  // use range 1-253 for our usecase.

  // Hacky. Need better solution but works for now. Our InterfaceID are
  // Type-1: 2000, 2001, 2002, 2003 ...
  // Type-2: 4000, 4001, 4002, 4003, 4004, ...
  // Type-3: 10, 11, 12, 13 (Virtual Interfaces)
  if (ifID >= InterfaceID(4000)) { // 4000, 4001, 4002, 4003 ...
    tableId = ifID - 4000 + 201; // 201, 202, 203, ...
  } else if (ifID >= InterfaceID(3000)) { // 3000, 3001, ...
    // <RGSW, FUJI> in OLR has 96 upinks, hence we cannot use 4000
    tableId = ifID - 3000 + 101; // 101, 102, 103, ....
  } else if (ifID >= InterfaceID(2000)) { // 2000, 2001, ...
    tableId = ifID - 2000 + 1; // 1, 2, 3, ....
  } else { // 10, 11, 12, 13 ...
    tableId = 250 - (ifID - 10); // 250, 249, 248, ...
  }

  return tableId;
}

int TunManager::getTableIdForVoq(InterfaceID ifID) const {
  // Kernel only supports up to 256 tables. The last few are used by kernel
  // as main, default, and local. IDs 0, 254 and 255 are not available. So we
  // use range 1-253 for our usecase.
  //
  // VOQ systems use port based RIFs.
  // Port based RIF IDs are assigned starting minimum system port range of the
  // first switch. Thus, map ifID to 1-253 with ifID - firstSwitchSysPortMin
  // In practice, [firstSwitchSysPortMin, lastSwitchSysPortUsed] range is <<
  // 253. Moreover, getTableID asserts that the computed ID is <= 253

  const auto& switchIdToSwitchInfo =
      utility::getFirstNodeIf(sw_->getState()->getSwitchSettings())
          ->getSwitchIdToSwitchInfo();
  auto platform = sw_->getPlatformType();
  if (isDualStage3Q2QMode()) {
    if (switchIdToSwitchInfo.size() > 1) {
      throw FbossError(
          "No Tun intf support for - multi asic support for 2 stage scale setup");
    }
    auto intf = sw_->getState()->getInterfaces()->getNode(ifID);
    auto constexpr kLocalIntfTableStart = 1;
    auto constexpr kLower16KIntfTableStart = 25;
    auto constexpr kHigher16KIntfTableStart = 125;
    if (intf->getScope() == cfg::Scope::LOCAL) {
      auto tableId = kLocalIntfTableStart + ifID;
      CHECK(
          tableId >= kLocalIntfTableStart && tableId < kLower16KIntfTableStart)
          << "Local interface IDs must be in range 1-24";
      return tableId;
    }
    auto sysPortRange = getCoveringSysPortRange(ifID, switchIdToSwitchInfo);
    auto offset = ifID - *sysPortRange.minimum();
    if (*sysPortRange.minimum() < 16 * 1024) {
      // lower 16K range
      auto tableId = kLower16KIntfTableStart + offset;
      CHECK(
          tableId >= kLower16KIntfTableStart &&
          tableId < kHigher16KIntfTableStart)
          << " Lower 16K range RIF table IDs should be within ["
          << kLocalIntfTableStart << ", " << kHigher16KIntfTableStart << ")";
      return tableId;
    } else {
      // Higher 16K range
      auto tableId = kHigher16KIntfTableStart + offset;
      CHECK(
          tableId >= kHigher16KIntfTableStart &&
          tableId < kHigher16KIntfTableStart + 100)
          << " Higher 16K range RIF table IDs should be within ["
          << kHigher16KIntfTableStart << ", " << kHigher16KIntfTableStart + 100
          << ")";
      return tableId;
    }
  } else if (
      platform == PlatformType::PLATFORM_JANGA800BIC ||
      FLAGS_dsf_single_stage_r192_f40_e32 ||
      FLAGS_dsf_single_stage_r128_f40_e16_uniform_local_offset ||
      // TODO(daiweix): remove FLAGS_hyper_port after
      // FLAGS_dsf_single_stage_r128_f40_e16_uniform_local_offset is set for
      // hyper port
      FLAGS_hyper_port) {
    auto intf = sw_->getState()->getInterfaces()->getNode(ifID);
    auto constexpr kLocalIntfTableStart = 1;
    auto constexpr kGlobalIntfTableStart = 200;
    if (intf->getScope() == cfg::Scope::LOCAL) {
      if (FLAGS_dsf_single_stage_r128_f40_e16_uniform_local_offset) {
        auto constexpr kLocalSystemPortIdStart = 6058;
        return kLocalIntfTableStart + ifID - kLocalSystemPortIdStart;
      } else {
        return kLocalIntfTableStart + ifID;
      }
    } else {
      auto firstSwitchSysPortRange =
          getFirstSwitchSystemPortIdRange(switchIdToSwitchInfo);
      return kGlobalIntfTableStart + ifID - *firstSwitchSysPortRange.minimum();
    }
  }
  auto firstSwitchSysPortRange =
      getFirstSwitchSystemPortIdRange(switchIdToSwitchInfo);
  return ifID - *firstSwitchSysPortRange.minimum();
}

int TunManager::getInterfaceMtu(InterfaceID ifID) const {
  auto interface = sw_->getState()->getInterfaces()->getNodeIf(ifID);
  return interface ? interface->getMtu() : kDefaultMtu;
}

cfg::InterfaceType TunManager::getInterfaceType(InterfaceID ifID) const {
  auto interface = sw_->getState()->getInterfaces()->getNodeIf(ifID);
  return interface ? interface->getType() : cfg::InterfaceType::VLAN;
}

void TunManager::addRemoveRouteTable(InterfaceID ifID, int ifIndex, bool add) {
  addRemoveRouteTable(getTableId(ifID), ifIndex, add, ifID);
}

void TunManager::addRemoveRouteTable(
    int tableId,
    int ifIndex,
    bool add,
    std::optional<InterfaceID> ifID) {
  // We just store default routes (one for IPv4 and one for IPv6) in each
  // route table.
  const folly::IPAddress addrs[] = {
      IPAddress{"0.0.0.0"}, // v4 default
      IPAddress{"::0"}, // v6 default
  };

  for (const auto& addr : addrs) {
    auto route = rtnl_route_alloc();
    SCOPE_EXIT {
      rtnl_route_put(route);
    };

    auto error = rtnl_route_set_family(route, addr.family());
    nlCheckError(error, "Failed to set family to", addr.family());

    rtnl_route_set_table(route, tableId);
    rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
    rtnl_route_set_protocol(route, RTPROT_FBOSS);

    error = rtnl_route_set_type(route, RTN_UNICAST);
    nlCheckError(error, "Failed to set type to ", RTN_UNICAST);

    auto destaddr = nl_addr_build(
        addr.family(),
        const_cast<unsigned char*>(addr.bytes()),
        addr.byteCount());
    if (!destaddr) {
      throw FbossError("Failed to build destination address for ", addr);
    }
    SCOPE_EXIT {
      nl_addr_put(destaddr);
    };

    nl_addr_set_prefixlen(destaddr, 0);
    error = rtnl_route_set_dst(route, destaddr);
    nlCheckError(error, "Failed to set destination route to ", addr);

    auto nexthop = rtnl_route_nh_alloc();
    if (!nexthop) {
      throw FbossError("Failed to allocate nexthop");
    }

    rtnl_route_nh_set_ifindex(nexthop, ifIndex);
    rtnl_route_add_nexthop(route, nexthop);

    if (add) {
      error = rtnl_route_add(sock_, route, NLM_F_REPLACE);
    } else {
      error = rtnl_route_delete(sock_, route, 0);
    }
    /**
     * Disable: Because of some weird reason this CHECK fails while deleting
     * v4 default route. However route actually gets wiped off from Linux
     * routing table.
    nlCheckError(error, "Failed to ", add ? "add" : "remove",
                  " default route ", addr, " @ index ", ifIndex,
                  " in table ", tableId, " for interface ", ifID,
                  ". ErrorCode: ", error);
      */
    if (error < 0) {
      XLOG(WARNING) << "Failed to " << (add ? "add" : "remove")
                    << " default route " << addr << " @index " << ifIndex
                    << ". ErrorCode: " << error;
    }
    XLOG(DBG2) << (add ? "Added" : "Removed") << " default route " << addr
               << " @ index " << ifIndex << " in table " << tableId
               << (ifID.has_value() ? " for interface " + std::to_string(*ifID)
                                    : "");
  }
}

void TunManager::addRemoveUnreachableRoute(
    const folly::CIDRNetwork& prefix,
    bool add) {
  auto route = rtnl_route_alloc();
  SCOPE_EXIT {
    rtnl_route_put(route);
  };

  auto family = prefix.first.family();
  auto error = rtnl_route_set_family(route, family);
  nlCheckError(error, "Failed to set family to ", family);

  rtnl_route_set_table(route, RT_TABLE_MAIN);
  rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
  rtnl_route_set_protocol(route, RTPROT_FBOSS);

  error = rtnl_route_set_type(route, RTN_UNREACHABLE);
  nlCheckError(error, "Failed to set type to RTN_UNREACHABLE");

  auto destaddr = nl_addr_build(
      family,
      const_cast<unsigned char*>(prefix.first.bytes()),
      prefix.first.byteCount());
  if (!destaddr) {
    throw FbossError("Failed to build destination address for ", prefix.first);
  }
  SCOPE_EXIT {
    nl_addr_put(destaddr);
  };

  nl_addr_set_prefixlen(destaddr, prefix.second);
  error = rtnl_route_set_dst(route, destaddr);
  nlCheckError(
      error,
      "Failed to set destination for unreachable route to ",
      prefix.first,
      "/",
      static_cast<int>(prefix.second));

  if (add) {
    error = rtnl_route_add(sock_, route, NLM_F_REPLACE);
  } else {
    error = rtnl_route_delete(sock_, route, 0);
  }
  if (error < 0) {
    XLOG(ERR) << "Failed to " << (add ? "add" : "remove")
              << " unreachable route " << prefix.first << "/"
              << static_cast<int>(prefix.second)
              << " in main table. ErrorCode: " << error;
  } else {
    XLOG(DBG2) << (add ? "Added" : "Removed") << " unreachable route "
               << prefix.first << "/" << static_cast<int>(prefix.second)
               << " in main table";
  }
}

void TunManager::addUnreachableRoutesForIntf(
    InterfaceID ifID,
    const Interface::Addresses& addrs) {
  for (const auto& [addr, mask] : addrs) {
    if (addr.isLinkLocal()) {
      continue;
    }
    auto networkAddr = addr.mask(mask);
    folly::CIDRNetwork prefix{networkAddr, mask};
    addRemoveUnreachableRoute(prefix, true);
    unreachableRoutes_[ifID].insert(prefix);
  }
  XLOG(DBG2) << "Programmed unreachable routes for down interface " << ifID;
}

void TunManager::removeUnreachableRoutesForIntf(InterfaceID ifID) {
  auto it = unreachableRoutes_.find(ifID);
  if (it == unreachableRoutes_.end()) {
    return;
  }
  for (const auto& prefix : it->second) {
    addRemoveUnreachableRoute(prefix, false);
  }
  unreachableRoutes_.erase(it);
  XLOG(DBG2) << "Removed unreachable routes for interface " << ifID;
}

void TunManager::addRemoveSourceRouteRule(
    InterfaceID ifID,
    const folly::IPAddress& addr,
    bool add) {
  addRemoveSourceRouteRule(getTableId(ifID), addr, add, ifID);
}

void TunManager::addRemoveSourceRouteRule(
    int tableId,
    const folly::IPAddress& addr,
    bool add,
    std::optional<InterfaceID> ifID) {
  // We should not add source routing rule for link-local addresses because
  // they can be re-used across interfaces.
  if (addr.isLinkLocal()) {
    XLOG(DBG2) << "Ignoring source routing rule for link-local address "
               << addr;
    return;
  }

  auto rule = rtnl_rule_alloc();
  SCOPE_EXIT {
    rtnl_rule_put(rule);
  };

  rtnl_rule_set_family(rule, addr.family());
  rtnl_rule_set_table(rule, tableId);
  rtnl_rule_set_action(rule, FR_ACT_TO_TBL);

  auto sourceaddr = nl_addr_build(
      addr.family(),
      const_cast<unsigned char*>(addr.bytes()),
      addr.byteCount());
  if (!sourceaddr) {
    throw FbossError("Failed to build destination address for ", addr);
  }
  SCOPE_EXIT {
    nl_addr_put(sourceaddr);
  };

  nl_addr_set_prefixlen(sourceaddr, addr.bitCount());
  auto error = rtnl_rule_set_src(rule, sourceaddr);
  nlCheckError(error, "Failed to set destination route to ", addr);

  if (add) {
    error = rtnl_rule_add(sock_, rule, NLM_F_REPLACE);
  } else {
    error = rtnl_rule_delete(sock_, rule, 0);
  }
  // There are scenarios where the rule has already been deleted and we try
  // to delete it again. In that case, we can ignore the error.
  if (!add && (error == -NLE_OBJ_NOTFOUND)) {
    XLOG(WARNING) << "Rule not existing for address " << addr
                  << " to lookup table " << tableId
                  << (ifID.has_value()
                          ? " for interface " + std::to_string(*ifID)
                          : "");
  } else {
    nlCheckError(
        error,
        "Failed to ",
        add ? "add" : "remove",
        " rule for address ",
        addr,
        " to lookup table ",
        tableId,
        ifID.has_value() ? " for interface " + std::to_string(*ifID) : "");
    XLOG(DBG2) << (add ? "Added" : "Removed") << " rule for address " << addr
               << " to lookup table " << tableId
               << (ifID.has_value() ? " for interface " + std::to_string(*ifID)
                                    : "");
  }
}

void TunManager::addRemoveTunAddress(
    const std::string& ifName,
    uint32_t ifIndex,
    const folly::IPAddress& addr,
    uint8_t mask,
    bool add) {
  auto tunaddr = rtnl_addr_alloc();
  if (!tunaddr) {
    throw FbossError("Failed to allocate address");
  }
  SCOPE_EXIT {
    rtnl_addr_put(tunaddr);
  };

  rtnl_addr_set_family(tunaddr, addr.family());
  auto localaddr = nl_addr_build(
      addr.family(),
      const_cast<unsigned char*>(addr.bytes()),
      addr.byteCount());
  if (!localaddr) {
    throw FbossError("Failed to build destination address for ", addr);
  }
  SCOPE_EXIT {
    nl_addr_put(localaddr);
  };

  auto error = rtnl_addr_set_local(tunaddr, localaddr);
  nlCheckError(error, "Failed to set local address to ", addr);

  rtnl_addr_set_prefixlen(tunaddr, mask);
  rtnl_addr_set_ifindex(tunaddr, ifIndex);

  if (add) {
    /**
     * When you bring down interface some routes are purged but some still
     * stay there (I tested from command line and v6 routes were gone but v4
     * were there). To be on safe side, when we bring up interface we always
     * add addresses and routes for that interface with REPLACE flag
     * overriding existing ones if any.
     */
    error = rtnl_addr_add(sock_, tunaddr, NLM_F_REPLACE);
  } else {
    error = rtnl_addr_delete(sock_, tunaddr, 0);
  }
  nlCheckError(
      error,
      "Failed to ",
      add ? "add" : "remove",
      " address ",
      addr,
      "/",
      static_cast<int>(mask),
      " to interface ",
      ifName,
      " @ index ",
      ifIndex);
  XLOG(DBG2) << (add ? "Added" : "Removed") << " address " << addr.str() << "/"
             << static_cast<int>(mask) << " on interface " << ifName
             << " @ index " << ifIndex;
}

void TunManager::addTunAddress(
    InterfaceID ifID,
    const std::string& ifName,
    uint32_t ifIndex,
    folly::IPAddress addr,
    uint8_t mask) {
  addRemoveSourceRouteRule(ifID, addr, true);
  SCOPE_FAIL {
    try {
      addRemoveSourceRouteRule(ifID, addr, false);
    } catch (const std::exception&) {
      XLOG(ERR) << "Failed to removed partially added source rule on "
                << "interface " << ifName;
    }
  };
  addRemoveTunAddress(ifName, ifIndex, addr, mask, true);
}

void TunManager::removeTunAddress(
    InterfaceID ifID,
    const std::string& ifName,
    uint32_t ifIndex,
    folly::IPAddress addr,
    uint8_t mask) {
  addRemoveSourceRouteRule(ifID, addr, false);
  SCOPE_FAIL {
    try {
      addRemoveSourceRouteRule(ifID, addr, true);
    } catch (const std::exception&) {
      XLOG(ERR) << "Failed to add partially added source rule on "
                << "interface " << ifName;
    }
  };
  addRemoveTunAddress(ifName, ifIndex, addr, mask, false);
}

void TunManager::start() const {
  for (const auto& intf : intfs_) {
    intf.second->start();
  }
}

void TunManager::stop() const {
  for (const auto& intf : intfs_) {
    intf.second->stop();
  }
}

void TunManager::linkProcessor(struct nl_object* obj, void* data) {
  struct rtnl_link* link = reinterpret_cast<struct rtnl_link*>(obj);

  // Get name of an interface
  const auto name = rtnl_link_get_name(link);
  if (!name) {
    throw FbossError(
        "Device @ index ",
        rtnl_link_get_ifindex(link),
        " does not have a name");
  }

  // Only add interface if it is a Tun interface
  if (!utility::isTunIntfName(name)) {
    XLOG(DBG3) << "Ignore interface " << name
               << " because it is not a tun interface";
    return;
  }

  XLOG(DBG2) << "Adding TUN interface: " << name << " @ index "
             << rtnl_link_get_ifindex(link);

  static_cast<TunManager*>(data)->addExistingIntf(
      std::string(name), rtnl_link_get_ifindex(link));
}

void TunManager::addressProcessor(struct nl_object* obj, void* data) {
  struct rtnl_addr* addr = reinterpret_cast<struct rtnl_addr*>(obj);

  // Validate family
  auto family = rtnl_addr_get_family(addr);
  if (family != AF_INET && family != AF_INET6) {
    XLOG(DBG3) << "Skip address from device @ index "
               << rtnl_addr_get_ifindex(addr)
               << " because of its address family " << family;
    return;
  }

  // Validate address
  auto localaddr = rtnl_addr_get_local(addr);
  if (!localaddr) {
    XLOG(DBG3) << "Skip address from device @ index "
               << rtnl_addr_get_ifindex(addr)
               << " because of it does not have a local address ";
    return;
  }

  // Convert rtnl_addr to string representation
  char buf[INET6_ADDRSTRLEN];
  nl_addr2str(localaddr, buf, sizeof(buf));
  if (!*buf) {
    XLOG(DBG3) << "Device @ index " << rtnl_addr_get_ifindex(addr)
               << " does not have an address at family " << family;
  }

  auto ipaddr = IPAddress::createNetwork(buf, -1, false).first;
  static_cast<TunManager*>(data)->addProbedAddr(
      rtnl_addr_get_ifindex(addr), ipaddr, nl_addr_get_prefixlen(localaddr));
}

void TunManager::performInitialCleanup(std::shared_ptr<SwitchState> state) {
  // Build a map of interface ID to table ID from state interfaces
  auto ifIdToTableId = this->buildIfIdToTableIdMap(state);

  // Build a map of interface ID to table ID from probed interfaces
  auto probedIfIdToTableId = this->buildProbedIfIdToTableIdMap();

  // Only delete probed data if there's a difference between the maps
  if (requiresProbedDataCleanup(ifIdToTableId, probedIfIdToTableId)) {
    deleteAllProbedData();
    probedStateCleanedUp_ = true;
    sw_->stats()->probedStateCleanedUp();
  }
  initialCleanupDone_ = true;
}

void TunManager::probe() {
  bool probeRan = false;
  std::exception_ptr probeError;
  evb_->runImmediatelyOrRunInFbossEventBaseThreadAndWait([&]() {
    probeRan = true;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      doProbe(lock);
    } catch (...) {
      probeError = std::current_exception();
    }
  });
  if (probeError) {
    std::rethrow_exception(probeError);
  }
  if (!probeRan) {
    XLOG(WARN) << "TUN probe was not scheduled on the EventBase, deferring "
               << "it to the first sync()";
  }
}

void TunManager::doProbe(std::lock_guard<std::mutex>& /* lock */) {
  evb_->checkIsInEventBaseThread();
  XLOG(DBG2) << "Starting to probe() for linux interfaces...";

  const auto startTs = std::chrono::steady_clock::now();
  SCOPE_EXIT {
    const auto endTs = std::chrono::steady_clock::now();
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTs - startTs);
    XLOG(DBG2) << "Probing of linux state took " << elapsedMs.count()
               << "ms. success=" << (probeDone_ ? "true" : "false");
  };

  CHECK(!probeDone_); // Callers must check for probeDone before calling
  stop(); // stop all interfaces
  intfs_.clear(); // clear all interface info

  // get links
  struct nl_cache* cache;
  auto error = rtnl_link_alloc_cache(sock_, AF_UNSPEC, &cache);
  nlCheckError(error, "Cannot get links from Kernel");
  SCOPE_EXIT {
    nl_cache_free(cache);
  };
  nl_cache_foreach(cache, &TunManager::linkProcessor, this);

  // get addresses
  struct nl_cache* addressCache;
  error = rtnl_addr_alloc_cache(sock_, &addressCache);
  nlCheckError(error, "Cannot get addresses from Kernel");
  SCOPE_EXIT {
    nl_cache_free(addressCache);
  };
  nl_cache_foreach(addressCache, &TunManager::addressProcessor, this);

  if (FLAGS_cleanup_probed_kernel_data) {
    // Get routes
    struct nl_cache* routeCache;
    error = rtnl_route_alloc_cache(sock_, AF_UNSPEC, 0, &routeCache);
    nlCheckError(error, "Cannot get routes from Kernel");
    SCOPE_EXIT {
      nl_cache_free(routeCache);
    };
    nl_cache_foreach(routeCache, &TunManager::routeProcessor, this);

    // Get source routing rules
    struct nl_cache* ruleCache;
    error = rtnl_rule_alloc_cache(sock_, AF_UNSPEC, &ruleCache);
    nlCheckError(error, "Cannot get rules from Kernel");
    SCOPE_EXIT {
      nl_cache_free(ruleCache);
    };
    nl_cache_foreach(ruleCache, &TunManager::ruleProcessor, this);
  }

  start();
  probeDone_ = true;
}

/**
 * Delete all probed data from kernel including routes, addresses, rules and
 * tunnel interfaces.
 */
void TunManager::deleteAllProbedData() {
  XLOG(INFO) << "Starting to delete all probed data from kernel";

  // Build a map of interface index to table ID from probed data
  auto ifIndexToTableId = buildIfIndextoTableMapFromProbedData();

  deleteProbedRoutes(ifIndexToTableId);
  deleteProbedAddressesAndRules(ifIndexToTableId);
  deleteProbedInterfaces();

  XLOG(INFO) << "Finished deleting all probed data from kernel";
}

boost::container::flat_map<InterfaceID, bool> TunManager::getInterfaceStatus(
    std::shared_ptr<SwitchState> state) {
  boost::container::flat_map<InterfaceID, bool> statusMap;

  // Derive interface status from all ports
  const auto& portMaps = state->getPorts();

  // Declare all virtual or state_sync disabled interfaces as up
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (auto iter : std::as_const(*intfMap)) {
      const auto& intf = iter.second;
      if (intf->isVirtual() || intf->isStateSyncDisabled()) {
        statusMap.emplace(intf->getID(), true);
      } else if (intf->getType() == cfg::InterfaceType::PORT) {
        // An aggregate port router interface follows the aggregate: it is up
        // while at least minimumLinkCount members are forwarding. Below that
        // LACP puts every member in standby, so the aggregate carries no
        // traffic even though the member links are still up.
        if (auto aggregatePortID = intf->getAggregatePortIDf()) {
          auto aggPort =
              state->getAggregatePorts()->getNodeIf(*aggregatePortID);
          if (!aggPort) {
            XLOG(ERR) << "Aggregate port interface " << intf->getID()
                      << " does not have aggregate port in state.";
            continue;
          }
          statusMap.emplace(intf->getID(), aggPort->isUp());
          continue;
        }
        auto port = state->getPorts()->getNodeIf(intf->getPortID());
        if (!port) {
          XLOG(ERR) << "Port interface " << intf->getID()
                    << " does not have port in state.";
          continue;
        }
        statusMap.emplace(intf->getID(), port->isPortUp());
      }
    }
  }

  auto vlanMap = state->getVlans();
  for (const auto& portMap : std::as_const(*portMaps)) {
    for (const auto& port : std::as_const(*portMap.second)) {
      bool isPortUp = port.second->isPortUp();
      for (const auto& vlanIDToInfo : port.second->getVlans()) {
        auto vlan = vlanMap->getNodeIf(vlanIDToInfo.first);
        if (!vlan) {
          if (vlanIDToInfo.first == VlanID(0)) {
            continue;
          }
          XLOG(ERR) << "Vlan " << vlanIDToInfo.first << " not found in state.";
          continue;
        }

        auto intfID = vlan->getInterfaceID();
        statusMap[intfID] |= isPortUp; // NOTE: We are applying `OR` operator
      } // for vlanIDToInfo
    } // for portIDToObj
  }

  return statusMap;
}

bool TunManager::isValidNlSocket() {
  return sock_ ? true : false;
}

bool TunManager::getIntfStatus(
    std::shared_ptr<SwitchState> state,
    InterfaceID ifID) {
  // Get interface status map
  auto intfStatusMap = getInterfaceStatus(state);
  return folly::get_default(intfStatusMap, ifID, false);
}

void TunManager::performInitialSyncIfNeeded() {
  if (numSyncs_ == 0) {
    // no syncs occurred yet. Force initial sync. The initial sync is done
    // with applied state, and subsequent sync's will also be done with
    // the applied states.
    sync(sw_->getState());
  }
}

void TunManager::forceInitialSync() {
  evb_->runInFbossEventBaseThread([this]() { performInitialSyncIfNeeded(); });
}

void TunManager::forceInitialSyncBlocking() {
  XLOG(DBG2) << "Starting forceInitialSyncBlocking()...";

  StopWatch stopWatch(std::string("forceInitialSyncBlocking"), false);
  SCOPE_EXIT {
    XLOG(DBG2) << "forceInitialSyncBlocking() took "
               << static_cast<int>(stopWatch.msecsElapsed().count())
               << "ms. cleanup_done="
               << (initialCleanupDone_ ? "true" : "false")
               << " probed_state_cleaned_up="
               << (probedStateCleanedUp_ ? "true" : "false");
  };

  evb_->runInFbossEventBaseThreadAndWait(
      [this]() { performInitialSyncIfNeeded(); });
}

void TunManager::sync(std::shared_ptr<SwitchState> state) {
  CHECK(evb_->isInEventBaseThread());
  using Addresses = Interface::Addresses;
  using ConstAddressesIter = Addresses::const_iterator;
  using IntfInfo = std::pair<bool /* status */, Addresses>;
  using IntfToAddrsMap = boost::container::flat_map<InterfaceID, IntfInfo>;
  using ConstIntfToAddrsMapIter = IntfToAddrsMap::const_iterator;

  // Get interface status.
  auto intfStatusMap = getInterfaceStatus(state);

  // prepare new addresses
  IntfToAddrsMap newIntfToInfo;
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (auto iter : std::as_const(*intfMap)) {
      const auto& intf = iter.second;
      auto addrs = intf->getAddressesCopy();

      // Ideally all interfaces should be present in intfStatusMap as either
      // interface will be virtual or will have at least one port. Keeping
      // default status of interface to be DOWN in case if interface is not
      // virtual and is not associated with any physical port
      const auto status =
          folly::get_default(intfStatusMap, intf->getID(), false);

      newIntfToInfo[intf->getID()] = {status, addrs};
    }
  }

  // Hold mutex while changing interfaces
  std::lock_guard<std::mutex> lock(mutex_);
  if (!probeDone_) {
    doProbe(lock);
  }
  if (FLAGS_cleanup_probed_kernel_data) {
    if (!initialCleanupDone_) {
      performInitialCleanup(state);
    } else {
      XLOG(DBG2) << "Initial cleanup already completed, skipping cleanup";
    }
  }

  // prepare old addresses
  IntfToAddrsMap oldIntfToInfo;
  for (const auto& intf : intfs_) {
    const auto& addrs = intf.second->getAddresses();
    bool status = intf.second->getStatus();
    oldIntfToInfo[intf.first] = {status, addrs};

    // Change MTU if it has altered
    auto interface = state->getInterfaces()->getNodeIf(intf.first);
    if (interface && interface->getMtu() != intf.second->getMtu()) {
      intf.second->setMtu(interface->getMtu());
    }
  }

  // Callback function for removing addresses for a particular interface
  auto removeChangedAddress = [this](
                                  InterfaceID ifID,
                                  const std::string& ifName,
                                  int ifIndex,
                                  const Addresses& oldAddrs,
                                  const Addresses& newAddrs) {
    applyChanges(
        oldAddrs,
        newAddrs,
        [&](ConstAddressesIter& oldIter, ConstAddressesIter& newIter) {
          if (oldIter->second == newIter->second) {
            // addresses and masks are both same
            return;
          }
          removeTunAddress(
              ifID, ifName, ifIndex, oldIter->first, oldIter->second);
        },
        [&](ConstAddressesIter& /*newIter*/) {},
        [&](ConstAddressesIter& oldIter) {
          removeTunAddress(
              ifID, ifName, ifIndex, oldIter->first, oldIter->second);
        });
  };

  // Callback function for adding addresses for a particular interface
  auto addChangedAddress = [this](
                               InterfaceID ifID,
                               const std::string& ifName,
                               int ifIndex,
                               const Addresses& oldAddrs,
                               const Addresses& newAddrs) {
    applyChanges(
        oldAddrs,
        newAddrs,
        [&](ConstAddressesIter& oldIter, ConstAddressesIter& newIter) {
          if (oldIter->second == newIter->second) {
            // addresses and masks are both same
            return;
          }
          addTunAddress(ifID, ifName, ifIndex, newIter->first, newIter->second);
        },
        [&](ConstAddressesIter& newIter) {
          addTunAddress(ifID, ifName, ifIndex, newIter->first, newIter->second);
        },
        [&](ConstAddressesIter& oldIter) {

        });
  };

  // Remove addresses(kernel entries) for all interfaces before we add new
  // addresses.
  applyChanges(
      oldIntfToInfo,
      newIntfToInfo,
      [&](ConstIntfToAddrsMapIter& oldIter, ConstIntfToAddrsMapIter& newIter) {
        auto newStatus = newIter->second.first;
        const auto& oldAddrs = oldIter->second.second;
        const auto& newAddrs = newIter->second.second;

        // Interface must exists
        const auto& intf = intfs_.at(oldIter->first);
        auto ifID = intf->getInterfaceID();
        int ifIndex = intf->getIfIndex();
        const auto& ifName = intf->getName();

        // IPv6 throws exception when interface is down, so applying changes
        // only if interface is up
        if (newStatus) {
          removeChangedAddress(ifID, ifName, ifIndex, oldAddrs, newAddrs);
        }
      },
      [&](ConstIntfToAddrsMapIter& /*newIter*/) {},
      [&](ConstIntfToAddrsMapIter& /*oldIter*/) {});

  // Add addresses(kernel entries) for all interfaces before we add new
  // addresses.
  applyChanges(
      oldIntfToInfo,
      newIntfToInfo,
      [&](ConstIntfToAddrsMapIter& oldIter, ConstIntfToAddrsMapIter& newIter) {
        auto oldStatus = oldIter->second.first;
        auto newStatus = newIter->second.first;
        const auto& oldAddrs = oldIter->second.second;
        const auto& newAddrs = newIter->second.second;

        // Interface must exists
        const auto& intf = intfs_.at(oldIter->first);
        auto ifID = intf->getInterfaceID();
        int ifIndex = intf->getIfIndex();
        const auto& ifName = intf->getName();

        // mutate intf status and addresses
        intf->setStatus(newStatus);
        intf->setAddresses(newAddrs);

        // Update interface status
        if (oldStatus ^ newStatus) { // old and new status is different
          setIntfStatus(ifName, ifIndex, newStatus);
        }

        // Interface going DOWN: program unreachable routes to prevent
        // control-plane traffic from falling back to OOB via eth0.
        if (oldStatus && !newStatus) {
          addUnreachableRoutesForIntf(ifID, oldAddrs);
        }

        // We need to add route-table and tun-addresses if interface is
        // brought up recently.
        if (!oldStatus && newStatus) {
          removeUnreachableRoutesForIntf(ifID);
          addRouteTable(ifID, ifIndex);
          // Remove old source route rules to avoid duplicates
          for (const auto& addr : oldAddrs) {
            addRemoveSourceRouteRule(ifID, addr.first, false);
          }
          // Add new addresses
          for (const auto& addr : newAddrs) {
            addTunAddress(ifID, ifName, ifIndex, addr.first, addr.second);
          }
        }

        // Update interface addresses only if interface is up currently
        // We would like to process address change whenever current state of
        // interface is UP. We should not try to add addresses when
        // interface is down as it can throw exception for v6 address.
        if (newStatus) {
          addChangedAddress(ifID, ifName, ifIndex, oldAddrs, newAddrs);
        }
      },
      [&](ConstIntfToAddrsMapIter& newIter) {
        auto& statusAddr = newIter->second;
        addNewIntf(newIter->first, statusAddr.first, statusAddr.second);
      },
      [&](ConstIntfToAddrsMapIter& oldIter) { removeIntf(oldIter->first); });

  start();

  // track number of times sync is called
  ++numSyncs_;
}

// TODO(aeckert): Find a way to reuse the iterator from NodeMapDelta here as
// this basically duplicates that code.
template <
    typename MAPNAME,
    typename CHANGEFN,
    typename ADDFN,
    typename REMOVEFN>
void TunManager::applyChanges(
    const MAPNAME& oldMap,
    const MAPNAME& newMap,
    CHANGEFN changeFn,
    ADDFN addFn,
    REMOVEFN removeFn) {
  auto oldIter = oldMap.begin();
  auto newIter = newMap.begin();
  while (oldIter != oldMap.end() && newIter != newMap.end()) {
    if (oldIter->first < newIter->first) {
      removeFn(oldIter);
      oldIter++;
    } else if (oldIter->first > newIter->first) {
      addFn(newIter);
      newIter++;
    } else {
      changeFn(oldIter, newIter);
      oldIter++;
      newIter++;
    }
  }
  for (; oldIter != oldMap.end(); oldIter++) {
    removeFn(oldIter);
  }
  for (; newIter != newMap.end(); newIter++) {
    addFn(newIter);
  }
}

/**
 * Netlink callback for processing routes read from kernel.
 *
 * Process routes discovered during kernel probing and stores
 * them for later cleanup. It filters routes based on address family
 * (IPv4/IPv6 only), table ID (1-253 range if
 * enable_1to1_intf_route_table_mapping enable [minRouteTableId,
 * maxRouteTableId]), and extracts destination, nexthop, and interface
 * information.
 *
 * @param obj Netlink route object to process
 * @param data Pointer to TunManager instance for storing probed routes
 */
void TunManager::routeProcessor(struct nl_object* obj, void* data) {
  struct rtnl_route* route = reinterpret_cast<struct rtnl_route*>(obj);

  // Get route family
  auto family = rtnl_route_get_family(route);
  if (family != AF_INET && family != AF_INET6) {
    XLOG(DBG2) << "Skip route because of unsupported address family " << family;
    return;
  }

  // Only process routes from table-id 1 through 253
  auto tableId = rtnl_route_get_table(route);
  if (FLAGS_enable_1to1_intf_route_table_mapping) {
    if (tableId < minRouteTableId || tableId > maxRouteTableId) {
      XLOG(DBG2) << "Skip route because table ID " << tableId
                 << " is outside range " << minRouteTableId << ", "
                 << maxRouteTableId;
      return;
    }
    if (tableId == 0 || tableId == 253 || tableId == 254 || tableId == 255) {
      struct nl_addr* dst = rtnl_route_get_dst(route);
      struct nl_addr* src = rtnl_route_get_src(route);
      char srcText[INET6_ADDRSTRLEN];
      char dstText[INET6_ADDRSTRLEN];
      nl_addr2str(dst, dstText, sizeof(dstText));
      nl_addr2str(src, srcText, sizeof(srcText));
      XLOG(ERR)
          << "skip route from route tables reserved for kernel0 253 254 255"
          << "table id: " << static_cast<int>(tableId) << " src: " << srcText
          << " dst: " << dstText;
      return;
    }
  } else {
    if (tableId < 1 || tableId > 253) {
      XLOG(DBG2) << "Skip route because table ID " << tableId
                 << " is outside range [1-253]";
      return;
    }
  }

  // Get destination address
  auto dst = rtnl_route_get_dst(route);
  std::string dstStr;

  if (dst) {
    char dstBuf[INET6_ADDRSTRLEN];
    nl_addr2str(dst, dstBuf, sizeof(dstBuf));
    dstStr = std::string(dstBuf);
  }

  // Check if this is a default route (kernel returns "none" for default routes)
  if (dstStr != "none") {
    // Ignore non-default routes and log them
    XLOG(DBG2) << "Ignoring non-default route to " << dstStr << " in table "
               << tableId << " with protocol "
               << rtnl_route_get_protocol(route);
    return;
  }

  // Convert "none" to proper default route representation
  dstStr = (family == AF_INET) ? "0.0.0.0/0" : "::/0";

  XLOG(DBG2) << "Found default route to " << dstStr << " in table " << tableId
             << " with protocol " << rtnl_route_get_protocol(route);

  // Store route information
  auto* tunManager = static_cast<TunManager*>(data);
  ProbedRoute probedRoute(family, tableId, dstStr);

  // Get nexthop information - only store the first nexthop since we only
  // care about default routes like those created by addRemoveRouteTable(),
  // which are single-nexthop routes.
  auto nexthops = rtnl_route_get_nexthops(route);
  if (nexthops && rtnl_route_get_nnexthops(route) > 0) {
    auto nh = rtnl_route_nexthop_n(route, 0);
    if (nh) {
      auto ifIndex = rtnl_route_nh_get_ifindex(nh);
      probedRoute.ifIndex = ifIndex;
      XLOG(DBG2) << "   dev ifindex " << ifIndex;
    } else {
      // Interface index 0 is never used for a real interface.
      probedRoute.ifIndex = 0;
    }
  } else {
    // No nexthops, store route anyway.
    // Interface index 0 is never used for a real interface.
    probedRoute.ifIndex = 0;
  }

  // Store the route
  tunManager->probedRoutes_.push_back(probedRoute);
}

/**
 * Netlink callback for processing source routing rules read from kernel.
 * Process source routing rules discovered during kernel probing and stores
 * them for later cleanup. It filters rules based on address family
 * (IPv4/IPv6 only), table ID (1-253 range if
 * enable_1to1_intf_route_table_mapping enable [minRouteTableId,
 * maxRouteTableId]), and extracts source address
 * and table information.
 *
 * @param obj Netlink rule object to process
 * @param data Pointer to TunManager instance for storing probed rules
 */
void TunManager::ruleProcessor(struct nl_object* obj, void* data) {
  struct rtnl_rule* rule = reinterpret_cast<struct rtnl_rule*>(obj);

  // Get rule family
  auto family = rtnl_rule_get_family(rule);
  if (family != AF_INET && family != AF_INET6) {
    XLOG(DBG2) << "Skip rule because of unsupported address family " << family;
    return;
  }

  // Only process rules with table ID in our range [1-253] except
  // enable_1to1_intf_route_table_mapping is true
  auto tableId = rtnl_rule_get_table(rule);
  if (FLAGS_enable_1to1_intf_route_table_mapping) {
    if (tableId < minRouteTableId || tableId > maxRouteTableId) {
      XLOG(DBG2) << "Skip route because table ID " << tableId
                 << " is outside range " << minRouteTableId << ", "
                 << maxRouteTableId;
      return;
    }
    if (tableId == 0 || tableId == 253 || tableId == 254 || tableId == 255) {
      XLOG(DBG2)
          << "skip rules from table ID 0, 253, 254, 255 are reserved in kernel usage";
      return;
    }
  } else {
    if (tableId < 1 || tableId > 253) {
      XLOG(DBG2) << "Skip rule because table ID " << tableId
                 << " is outside range [1-253]";
      return;
    }
  }

  // Get source address
  auto src = rtnl_rule_get_src(rule);
  if (!src) {
    XLOG(DBG2) << "Skip rule because it has no source address";
    return;
  }

  char srcBuf[INET6_ADDRSTRLEN];
  nl_addr2str(src, srcBuf, sizeof(srcBuf));
  std::string srcAddrOnly(srcBuf);

  // Get the prefix length and construct full address string like we create it
  auto prefixLen = nl_addr_get_prefixlen(src);
  std::string srcStr = srcAddrOnly + "/" + std::to_string(prefixLen);

  // Skip link-local addresses (they don't have source rules in our
  // implementation)
  try {
    auto ipaddr = IPAddress::createNetwork(srcAddrOnly, -1, false).first;
    if (ipaddr.isLinkLocal()) {
      XLOG(DBG2) << "Skip rule for link-local address " << srcAddrOnly;
      return;
    }
  } catch (const std::exception& ex) {
    XLOG(DBG2) << "Skip rule due to invalid address " << srcAddrOnly << ": "
               << ex.what();
    return;
  }

  XLOG(DBG2) << "Found source rule: " << srcStr << " -> table " << tableId;

  // Store rule information
  auto* tunManager = static_cast<TunManager*>(data);
  tunManager->probedRules_.emplace_back(family, tableId, srcStr);
}

/**
 * Delete probed routes from kernel routing tables.
 *
 * Removes default routes (0.0.0.0/0 and ::/0) that were discovered
 * during kernel probing.
 *
 * @param ifIndexToTableId Map from interface index to routing table ID
 */
void TunManager::deleteProbedRoutes(
    const std::unordered_map<int, int>& ifIndexToTableId) {
  XLOG(DBG2) << "Deleting probed routes";

  // Directly iterate over the provided map
  for (const auto& [ifIndex, tableId] : ifIndexToTableId) {
    addRemoveRouteTable(tableId, ifIndex, false, std::nullopt);

    XLOG(DBG2) << "Deleted default routes in table " << tableId
               << " for ifIndex " << ifIndex;
  }

  // Clear the probed routes after processing
  probedRoutes_.clear();
}

/**
 * Delete probed addresses and source routing rules from kernel.
 *
 * Removes IP addresses and their associated source routing rules that were
 * discovered during kernel probing.
 *
 * @param ifIndexToTableId Map from interface index to routing table ID
 */
void TunManager::deleteProbedAddressesAndRules(
    const std::unordered_map<int, int>& ifIndexToTableId) {
  XLOG(DBG2) << "Deleting probed addresses and source routing rules";

  // Track which rules were deleted during address cleanup
  std::set<std::pair<int, std::string>> deletedRules;

  for (const auto& intf : intfs_) {
    const auto& addresses = intf.second->getAddresses();
    const auto& ifName = intf.second->getName();
    auto ifIndex = intf.second->getIfIndex();

    // Get table ID from our map instead of calling getTableId
    auto tableIdIter = ifIndexToTableId.find(ifIndex);
    int tableId =
        (tableIdIter != ifIndexToTableId.end()) ? tableIdIter->second : 0;

    for (const auto& addr : addresses) {
      // Delete source routing rule using existing method with explicit table
      // ID
      if (tableId > 0) {
        addRemoveSourceRouteRule(tableId, addr.first, false);
        XLOG(DBG2) << "Deleted source rule for address " << addr.first.str()
                   << " table " << tableId;

        // Track that we deleted this rule (by tableId and address)
        std::string addrStr =
            addr.first.str() + "/" + std::to_string(addr.first.bitCount());
        deletedRules.insert({tableId, addrStr});
      }

      // Delete address directly
      addRemoveTunAddress(ifName, ifIndex, addr.first, addr.second, false);
      XLOG(DBG2) << "Deleted address " << addr.first.str() << "/"
                 << static_cast<int>(addr.second) << " from interface "
                 << ifName;
    }
  }

  // Delete remaining probed rules that weren't already deleted
  for (const auto& probedRule : probedRules_) {
    std::pair<int, std::string> ruleKey = {
        probedRule.tableId, probedRule.srcAddr};

    // If this rule wasn't already deleted during address cleanup, delete it now
    if (deletedRules.find(ruleKey) == deletedRules.end()) {
      // Parse the address from the rule's srcAddr (e.g., "10.0.0.1/32" ->
      // "10.0.0.1")
      auto ipAddr =
          IPAddress::createNetwork(probedRule.srcAddr, -1, false).first;
      addRemoveSourceRouteRule(probedRule.tableId, ipAddr, false);
      XLOG(DBG2) << "Deleted remaining probed rule: " << probedRule.srcAddr
                 << " -> table " << probedRule.tableId;
    }
  }

  // Clear the probed rules after processing
  probedRules_.clear();
}

/**
 * Delete probed interfaces from kernel.
 */
void TunManager::deleteProbedInterfaces() {
  XLOG(INFO) << "Deleting probed interfaces";

  for (const auto& intf : intfs_) {
    intf.second->setDelete();
    XLOG(INFO) << "Marked interface " << intf.first << " for deletion";
  }

  // Clear the interfaces map
  intfs_.clear();
}

} // namespace facebook::fboss
