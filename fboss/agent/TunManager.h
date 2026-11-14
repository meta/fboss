/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/StateObserver.h"
#include "fboss/agent/TunIntfBase.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/types.h"

#include <set>
#include <unordered_map>

#include <boost/container/flat_map.hpp>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/IPAddress.h>

extern "C" {
#include <netlink/object.h>
#include <netlink/socket.h>
}

namespace facebook::fboss {

class InterfaceMap;
class RxPacket;
class SwSwitch;
class TunIntf;

class TunManager : public StateObserver {
 public:
  TunManager(SwSwitch* sw, FbossEventBase* evb);
  ~TunManager() override;

#ifdef TUNMANAGER_ROUTE_PROCESSOR_FRIEND_TESTS
  TUNMANAGER_ROUTE_PROCESSOR_FRIEND_TESTS
#endif

#ifdef TUNMANAGER_RULE_PROCESSOR_FRIEND_TESTS
  TUNMANAGER_RULE_PROCESSOR_FRIEND_TESTS
#endif

#ifdef TUNMANAGER_INTERFACE_STATUS_FRIEND_TESTS
  TUNMANAGER_INTERFACE_STATUS_FRIEND_TESTS
#endif

#ifdef AGENT_TUNNEL_MGR_FRIEND_TESTS
  AGENT_TUNNEL_MGR_FRIEND_TESTS
#endif

#ifdef HW_TUN_MANAGER_BENCHMARK_FRIEND_TESTS
  HW_TUN_MANAGER_BENCHMARK_FRIEND_TESTS
#endif

  /**
   * Update the intfs_ map based on the given state update. This
   * overrides the StateObserver stateUpdated api, which is always
   * guaranteed to be called from the update thread.
   */
  void stateUpdated(const StateDelta& delta) override;

  /**
   * Send a packet to host.
   * This function can be called from any thread.
   *
   * @return true The packet is sent to host
   *         false The packet is dropped due to errors
   */
  virtual bool sendPacketToHost(
      InterfaceID dstIfID,
      std::unique_ptr<RxPacket> pkt);

  /**
   * Performs probe procedure to read existing TUN interface info from the host
   * and sync the new SwitchState
   *
   * This should really be only called externally once, after config is applied.
   * After that all updates should come via the stateUpdated calls.
   *
   * SwSwitch calls this API when initial configuration is applied on agent
   * restart.
   */
  virtual void sync(std::shared_ptr<SwitchState> state);

  void forceInitialSync();

  void forceInitialSyncBlocking();

  /**
   * This should be called externally only after initial sync has been
   * performed.
   *
   * SwSwitch calls this API after calling initial sync when initial
   * configuration is applied.
   */
  virtual void startObservingUpdates();

  /*
   * Probe linux for tun interfaces already configured.
   *
   * May be called from any thread. The probe runs on evb_'s thread, because
   * it registers TunIntf handlers on evb_ and libevent is not thread safe.
   * Blocks until the probe has finished and rethrows any error it raised.
   */
  virtual void probe();

  /*
   * Perform initial cleanup of probed data if required
   */
  void performInitialCleanup(std::shared_ptr<SwitchState> state);

  void stopProcessing();

  /**
   * Get interface status for a given interface
   */
  bool getIntfStatus(std::shared_ptr<SwitchState> state, InterfaceID ifID);

  bool isValidNlSocket();

 private:
  /**
   * @brief Probed route from the kernel.
   */
  struct ProbedRoute {
    /*< Address family (AF_INET for IPv4, AF_INET6 for IPv6) */
    int family{};
    /*< Routing table identifier */
    int tableId{};
    /*< Destination address/network as string */
    std::string destination;
    /*< Network interface index */
    int ifIndex{};

    /**
     * @brief Constructor for initializing family, tableId, and destination.
     *
     * @param family Address family (AF_INET for IPv4, AF_INET6 for IPv6)
     * @param tableId Routing table identifier
     * @param destination Destination address/network as string
     */
    ProbedRoute(int family, int tableId, const std::string& destination)
        : family(family),
          tableId(tableId),
          destination(destination),
          ifIndex(0) {}

    /**
     * @brief Default constructor.
     */
    ProbedRoute() = default;
  };

  /**
   * @brief Probed rule from the kernel.
   */
  struct ProbedRule {
    /*< Address family (AF_INET for IPv4, AF_INET6 for IPv6) */
    int family{};
    /*< Routing table identifier */
    int tableId{};
    /*< Source address as string */
    std::string srcAddr;

    /**
     * @brief Constructor for initializing family, tableId, and srcAddr.
     *
     * @param family Address family (AF_INET for IPv4, AF_INET6 for IPv6)
     * @param tableId Routing table identifier
     * @param srcAddr Source address as string
     */
    ProbedRule(int family, int tableId, const std::string& srcAddr)
        : family(family), tableId(tableId), srcAddr(srcAddr) {}

    /**
     * @brief Default constructor.
     */
    ProbedRule() = default;
  };

  // no copy to assign
  TunManager(const TunManager&) = delete;
  TunManager& operator=(const TunManager&) = delete;

  /**
   * start/stop packet forwarding on all TUN interfaces
   */
  void stop() const;
  void start() const;

  /**
   * Add a TUN interface. It can happen two ways
   * 1. During probe process when we discover existing Tun interface on linux
   * 2. When we want to create a new TUN interface in linux
   */
  void addExistingIntf(const std::string& name, int ifIndex);
  void
  addNewIntf(InterfaceID ifID, bool isUp, const Interface::Addresses& addrs);

  // Remove an existing TUN interface
  void removeIntf(InterfaceID ifID);

  // A tun interface was changed, update the addresses accordingly
  void updateIntf(InterfaceID ifID, const Interface::Addresses& addrs);

  /**
   * Bring UP/DOWN interfaces by mutating admin status in Linux
   */
  void setIntfStatus(const std::string& ifName, int ifIndex, bool status);

  /**
   * Add/remove a route table.
   *
   * On Host we create a routing table for every switch interface which has
   * v4/v6 default route with corresponding Tun interface as nexthop. Everything
   * Forwarded to that routing table will comes out of Tun interface and it
   * will eventually go out of corresponding switch interface.
   */
  void addRemoveRouteTable(InterfaceID ifID, int ifIndex, bool add);
  virtual void addRemoveRouteTable(
      int tableId,
      int ifIndex,
      bool add,
      std::optional<InterfaceID> ifID = std::nullopt);
  void addRouteTable(InterfaceID ifID, int ifIndex) {
    addRemoveRouteTable(ifID, ifIndex, true);
  }
  void removeRouteTable(InterfaceID ifID, int ifIndex) {
    addRemoveRouteTable(ifID, ifIndex, false);
  }

  /**
   * Add/remove an RTN_UNREACHABLE route in the main routing table for a
   * connected subnet. When a front-panel interface goes down, these routes
   * prevent traffic destined to the interface's subnets from falling back
   * to the management interface (eth0) via the default route.
   */
  void addRemoveUnreachableRoute(const folly::CIDRNetwork& prefix, bool add);

  /**
   * Program unreachable routes for all connected subnets of a down interface.
   */
  void addUnreachableRoutesForIntf(
      InterfaceID ifID,
      const Interface::Addresses& addrs);

  /**
   * Remove all unreachable routes previously programmed for an interface.
   */
  void removeUnreachableRoutesForIntf(InterfaceID ifID);

  /**
   * Creates a tableId for given interface.
   */
  int getTableId(InterfaceID ifID) const;

  /**
   * Build a mapping from interface ID to table ID from SwitchState.
   */
  std::unordered_map<InterfaceID, int> buildIfIdToTableIdMap(
      std::shared_ptr<SwitchState> state) const;

  /**
   * Build a mapping from interface index to table ID from probed routes.
   */
  std::unordered_map<int, int> buildIfIndexToTableIdMapFromProbedRoutes() const;

  /**
   * Build a mapping from interface index to table ID from source rules.
   */
  std::unordered_map<int, int> buildIfIndexToTableIdMapFromRules() const;

  /**
   * Build a mapping from interface index to table ID from probed data.
   *
   * Combines data from probed routes and rules to create a comprehensive
   * mapping of interface indices to routing table IDs.
   */
  std::unordered_map<int, int> buildIfIndextoTableMapFromProbedData() const;

  /**
   * Build a mapping from interface ID to table ID from probed interfaces.
   */
  std::unordered_map<InterfaceID, int> buildProbedIfIdToTableIdMap() const;

  /**
   * Check if probed data cleanup is required by comparing interface mappings.
   *
   * @param stateMap Interface ID to table ID mapping from SwitchState
   * @param probedMap Interface ID to table ID mapping from probed interfaces
   * @return true if cleanup is required (mappings differ), false otherwise
   */
  bool requiresProbedDataCleanup(
      const std::unordered_map<InterfaceID, int>& stateMap,
      const std::unordered_map<InterfaceID, int>& probedMap) const;

  int getTableIdForNpu(InterfaceID ifID) const;
  int getTableIdForVoq(InterfaceID ifID) const;

  /**
   * Add/remove an IP rule for source routing based on a given address
   *
   * The default route uses the management interface as the egress interface.
   * Source routing is used to force the packets with source IP address matching
   * one the front panel port IP addresses to be sent through the corresponding
   * front panel ports instead of the management interface.
   */
  void addRemoveSourceRouteRule(
      InterfaceID ifID,
      const folly::IPAddress& addr,
      bool add);
  virtual void addRemoveSourceRouteRule(
      int tableId,
      const folly::IPAddress& addr,
      bool add,
      std::optional<InterfaceID> ifID = std::nullopt);

  /**
   * Add/Remove an address to/from a TUN interface on the host
   */
  virtual void addRemoveTunAddress(
      const std::string& ifName,
      uint32_t ifIndex,
      const folly::IPAddress& addr,
      uint8_t mask,
      bool add);

  /**
   * Add/Remove address as well source-routing-rule for TUN interface on host.
   */
  void addTunAddress(
      InterfaceID ifID,
      const std::string& ifName,
      uint32_t ifIndex,
      folly::IPAddress addr,
      uint8_t mask);
  void removeTunAddress(
      InterfaceID ifID,
      const std::string& ifName,
      uint32_t ifIndex,
      folly::IPAddress addr,
      uint8_t mask);

  /**
   * Netlink callback for processing and storing links
   */
  static void linkProcessor(struct nl_object* obj, void* data);

  /**
   * Netlink callback for processing and storing addresses
   */
  static void addressProcessor(struct nl_object* obj, void* data);

  /**
   * Netlink callback for processing routes read from kernel.
   *
   * Process routes discovered during kernel probing and stores
   * them for later cleanup. It filters routes based on address family
   * (IPv4/IPv6 only), table ID (1-253 range), and extracts destination,
   * nexthop, and interface information.
   *
   * @param obj Netlink route object to process
   * @param data Pointer to TunManager instance for storing probed routes
   */
  static void routeProcessor(struct nl_object* obj, void* data);

  /**
   * Netlink callback for processing source routing rules read from kernel.
   *
   * Process source routing rules discovered during kernel probing and stores
   * them for later cleanup. It filters rules based on address family
   * (IPv4/IPv6 only), table ID (1-253 range), and extracts source address
   * and table information.
   *
   * @param obj Netlink rule object to process
   * @param data Pointer to TunManager instance for storing probed rules
   */
  static void ruleProcessor(struct nl_object* obj, void* data);

  /**
   * Delete probed routes from kernel routing tables.
   *
   * Removes default routes (0.0.0.0/0 and ::/0) that were discovered
   * during kernel probing.
   *
   * @param ifIndexToTableId Map from interface index to routing table ID
   */
  void deleteProbedRoutes(const std::unordered_map<int, int>& ifIndexToTableId);

  /**
   * Delete probed addresses and source routing rules from kernel.
   *
   * Removes IP addresses and their associated source routing rules that were
   * discovered during kernel probing.
   *
   * @param ifIndexToTableId Map from interface index to routing table ID
   */
  void deleteProbedAddressesAndRules(
      const std::unordered_map<int, int>& ifIndexToTableId);

  /**
   * Delete probed interfaces from kernel.
   */
  void deleteProbedInterfaces();

  /**
   * Lookup host for existing Tun interfaces and their addresses.
   */
  virtual void doProbe(std::lock_guard<std::mutex>& mutex);

  /**
   * Add an address to a TUN interface during probe process.
   */
  void addProbedAddr(int ifIndex, const folly::IPAddress& addr, uint8_t mask);

  /**
   * Performs initial sync if no syncs have occurred yet.
   */
  void performInitialSyncIfNeeded();

  /**
   * Delete all probed data from kernel including routes, addresses, rules and
   * tunnel interfaces.
   */
  void deleteAllProbedData();

  /**
   * Get MTU of switch interface
   */
  int getInterfaceMtu(InterfaceID ifID) const;

  /**
   * Get the interface type for a given interface ID
   */
  cfg::InterfaceType getInterfaceType(InterfaceID ifID) const;

  /**
   * Get Interface statuses map from a given SwitchState. In switch each
   * Interface/VLAN consists of multiple Ports. We derive state of Interface
   * to be UP if alteast one of the port belonging to that interface is UP.
   */
  static boost::container::flat_map<InterfaceID, bool> getInterfaceStatus(
      std::shared_ptr<SwitchState> state);

  template <
      typename MAPNAME,
      typename CHANGEFN,
      typename ADDFN,
      typename REMOVEFN>
  void applyChanges(
      const MAPNAME& oldMap,
      const MAPNAME& newMap,
      CHANGEFN changeFn,
      ADDFN addFn,
      REMOVEFN removeFn);

  SwSwitch* sw_{nullptr};
  FbossEventBase* evb_{nullptr};

  // Netlink socket for managing interface/addresses in Host/Linux
  nl_sock* sock_{nullptr};

  /**
   * The mutex used to protect `intfs_` which can be used by
   * sync() could manipulate intfs_. Called on the thread that serves evb_.
   * sendPacketToHost() uses intfs_, it can be called from any thread.
   */
  boost::container::flat_map<InterfaceID, std::unique_ptr<TunIntfBase>> intfs_;
  std::mutex mutex_;

  // Whether the manager has registered itself to listen for state updates
  // from sw_
  bool observingState_{false};

  // Initial probe done
  bool probeDone_{false};

  // Initial cleanup done
  bool initialCleanupDone_{false};

  // Was probed state cleaned up?
  bool probedStateCleanedUp_{false};

  uint64_t numSyncs_{0};

  /*< Container to store probed routes from kernel */
  std::vector<ProbedRoute> probedRoutes_;

  /*< Container to store probed rules from kernel */
  std::vector<ProbedRule> probedRules_;

  /**
   * Track unreachable routes programmed for down interfaces.
   * When a front-panel interface goes down, RTN_UNREACHABLE routes are added
   * to the main routing table for the interface's connected subnets. This
   * prevents traffic from falling back to the management interface (eth0).
   * Key: InterfaceID, Value: set of CIDRNetwork prefixes with unreachable
   * routes.
   */
  std::unordered_map<InterfaceID, std::set<folly::CIDRNetwork>>
      unreachableRoutes_;

  enum : uint8_t {
    /**
     * The protocol value used to add the source routing IP rule and the
     * default route in the routing table for source routing.
     * Per rtnetlink.h, any value >= RTPROTO_STATIC(4) to identify us
     */
    RTPROT_FBOSS = 80,
  };
};

} // namespace facebook::fboss
