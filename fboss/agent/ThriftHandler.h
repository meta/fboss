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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/fb303/cpp/FacebookBase2.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"

#include <folly/String.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp/server/TServerEventHandler.h>
#include <thrift/lib/cpp2/async/DuplexChannel.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

namespace facebook::fboss {

class AggregatePort;
class Port;
class SwSwitch;
class Vlan;
class SwitchState;
class AclEntry;
class LinkNeighbor;

class ThriftHandler : virtual public FbossCtrlSvIf,
                      public fb303::FacebookBase2 {
 public:
  template <typename T>
  using ThriftCallback = apache::thrift::HandlerCallbackPtr<T>;
  using TConnectionContext = apache::thrift::server::TConnectionContext;

  using Address = network::thrift::Address;
  using BinaryAddress = network::thrift::BinaryAddress;
  using EventBase = FbossEventBase;
  using Addresses = std::vector<Address>;
  using BinaryAddresses = std::vector<BinaryAddress>;

  explicit ThriftHandler(SwSwitch* sw);
  ~ThriftHandler() override;

  fb303::cpp2::fb_status getStatus() override;

  void async_tm_getStatus(ThriftCallback<fb303::cpp2::fb_status> cb) override;

  void flushCountersNow() override;

  void addUnicastRoute(int16_t client, std::unique_ptr<UnicastRoute> route)
      override;
  void deleteUnicastRoute(int16_t client, std::unique_ptr<IpPrefix> prefix)
      override;
  void addUnicastRoutes(
      int16_t client,
      std::unique_ptr<std::vector<UnicastRoute>> routes) override;
  void deleteUnicastRoutes(
      int16_t client,
      std::unique_ptr<std::vector<IpPrefix>> prefixes) override;
  void syncFib(
      int16_t client,
      std::unique_ptr<std::vector<UnicastRoute>> routes) override;

  void addUnicastRouteInVrf(
      int16_t client,
      std::unique_ptr<UnicastRoute> route,
      int32_t vrf) override;
  void deleteUnicastRouteInVrf(
      int16_t client,
      std::unique_ptr<IpPrefix> prefix,
      int32_t vrf) override;
  void addUnicastRoutesInVrf(
      int16_t client,
      std::unique_ptr<std::vector<UnicastRoute>> routes,
      int32_t vrf) override;
  void deleteUnicastRoutesInVrf(
      int16_t client,
      std::unique_ptr<std::vector<IpPrefix>> prefixes,
      int32_t vrf) override;
  void syncFibInVrf(
      int16_t client,
      std::unique_ptr<std::vector<UnicastRoute>> routes,
      int32_t vrf) override;

  /* MySid entries */
  void addMySidEntries(
      std::unique_ptr<std::vector<MySidEntry>> mySidEntries) override;
  void deleteMySidEntries(
      std::unique_ptr<std::vector<IpPrefix>> prefixes) override;
  void getMySidEntries(std::vector<MySidEntry>& entries) override;

  /* MPLS routes */
  void addMplsRoutes(
      int16_t clientId,
      std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) override;

  void deleteMplsRoutes(
      int16_t client,
      std::unique_ptr<std::vector<int32_t>> topLabels) override;
  void syncMplsFib(
      int16_t client,
      std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) override;
  void getMplsRouteTableByClient(
      std::vector<MplsRoute>& mplsRoutes,
      int16_t clientId) override;

  void getAllMplsRouteDetails(
      std::vector<MplsRouteDetails>& mplsRouteDetails) override;

  void getMplsRouteDetails(
      MplsRouteDetails& mplsRouteDetail,
      MplsLabel topLabel) override;

  // The thrift default only applies on the wire; spell it out again here so
  // in-process callers that do not care can omit it.
  void addNamedNextHopGroups(
      std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
      bool combineDuplicatedNextHops = false) override;
  void addOrUpdateNamedNextHopGroups(
      std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
      bool combineDuplicatedNextHops = false) override;
  void deleteNamedNextHopGroups(
      std::unique_ptr<std::vector<std::string>> names) override;
  void getNextHopGroups(
      std::vector<NextHopGroup>& result,
      bool replicateWeightedNexthops = false) override;
  void getNamedNextHopGroups(
      std::vector<NextHopGroup>& result,
      std::unique_ptr<std::vector<std::string>> names,
      bool replicateWeightedNexthops = false) override;

  void addPolicies(std::unique_ptr<std::vector<Policy>> policies) override;
  void deletePolicies(
      std::unique_ptr<std::vector<std::string>> policyNames) override;
  void getPolicies(
      std::vector<Policy>& result,
      std::unique_ptr<std::vector<std::string>> policyNames) override;
  void getAllPolicies(std::vector<Policy>& result) override;

  SwSwitch* getSw() const {
    return sw_;
  }

  void sendPkt(
      int32_t port,
      int32_t vlan,
      std::unique_ptr<fbstring> data,
      int32_t numOfPkts,
      int32_t intervalInMs) override;
  void sendPktHex(
      int32_t port,
      int32_t vlan,
      std::unique_ptr<fbstring> hex,
      int32_t numOfPkts,
      int32_t intervalInMs) override;

  void txPkt(
      int32_t port,
      std::unique_ptr<fbstring> data,
      int32_t numOfPkts,
      int32_t intervalInMs) override;
  void txPktL2(
      std::unique_ptr<fbstring> data,
      int32_t numOfPkts,
      int32_t intervalInMs) override;
  void setupL3TrafficLoops(
      std::unique_ptr<std::vector<BinaryAddress>> qualificationAddresses,
      std::unique_ptr<std::vector<std::string>> portNames,
      std::unique_ptr<std::vector<BinaryAddress>> peerNextHops,
      std::unique_ptr<std::vector<std::string>> expectedPeerMacs,
      std::unique_ptr<std::vector<int32_t>> timeoutSeconds) override;
  void extendL3TrafficLoops(
      std::unique_ptr<std::vector<std::string>> portNames,
      int32_t timeoutSeconds) override;
  void teardownL3TrafficLoops(
      std::unique_ptr<std::vector<std::string>> portNames) override;
  void getL3TrafficLoopStatus(
      std::map<std::string, L3TrafficLoopStatus>& statuses,
      std::unique_ptr<std::vector<std::string>> portNames) override;
  void txPktL3(
      std::unique_ptr<fbstring> payload,
      int32_t numOfPkts,
      int32_t intervalInMs) override;

  int32_t flushNeighborEntry(std::unique_ptr<BinaryAddress> ip, int32_t vlan)
      override;

  int32_t flushNeighborEntries(
      std::unique_ptr<std::vector<IfAndIP>> entries) override;

  void getVlanAddresses(Addresses& addrs, int32_t vlan) override;
  void getVlanAddressesByName(
      Addresses& addrs,
      const std::unique_ptr<std::string> vlan) override;
  void getVlanBinaryAddresses(BinaryAddresses& addrs, int32_t vlan) override;
  void getVlanBinaryAddressesByName(
      BinaryAddresses& addrs,
      const std::unique_ptr<std::string> vlan) override;
  /* Returns the Ip Route for the address */
  void getIpRoute(
      UnicastRoute& route,
      std::unique_ptr<Address> addr,
      int32_t vrfId) override;
  void getIpRouteDetails(
      RouteDetails& route,
      std::unique_ptr<Address> addr,
      int32_t vrfId) override;
  void getAllInterfaces(
      std::map<int32_t, InterfaceDetail>& interfaces) override;
  void getInterfaceList(std::vector<std::string>& interfaceList) override;

  void getRouteTable(std::vector<UnicastRoute>& routeTable) override;
  void getRouteTableByClient(
      std::vector<UnicastRoute>& routeTable,
      int16_t clientId) override;
  void getRouteTableDetails(std::vector<RouteDetails>& routeTable) override;

  void getRouteTableSize(RouteCount& routeCount) override;

  void getPortStatus(
      std::map<int32_t, PortStatus>& status,
      std::unique_ptr<std::vector<int32_t>> ports) override;
  void setPortState(int32_t portId, bool enable) override;
  void setPortDrainState(int32_t portId, bool drain) override;
  void setPortLoopbackMode(int32_t portId, PortLoopbackMode mode) override;
  void getAllPortLoopbackMode(
      std::map<int32_t, PortLoopbackMode>& port2LbMode) override;

  void programInternalPhyPorts(
      std::map<int32_t, cfg::PortProfileID>& programmedPorts,
      std::unique_ptr<TransceiverInfo> transceiver,
      bool force) override;

  void clearPortPrbsStats(int32_t portId, phy::PortComponent component)
      override;
  void getPortPrbsStats(
      phy::PrbsStats& prbsStats,
      int32_t portId,
      phy::PortComponent component) override;
  void setPortPrbs(
      int32_t portId,
      phy::PortComponent component,
      bool enable,
      int32_t polynominal) override;
  void getSupportedPrbsPolynomials(
      std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;
  void getInterfacePrbsState(
      prbs::InterfacePrbsState& prbsState,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;
  void setInterfacePrbs(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component,
      std::unique_ptr<prbs::InterfacePrbsState> state) override;
  void getInterfacePrbsStats(
      phy::PrbsStats& response,
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;
  void clearInterfacePrbsStats(
      std::unique_ptr<std::string> portName,
      phy::PortComponent component) override;
  void setInterfacesPrbs(
      std::unique_ptr<std::vector<std::string>> portNames,
      phy::PortComponent component,
      std::unique_ptr<prbs::InterfacePrbsState> state) override;
  void addAdjacencyFrr(
      std::unique_ptr<FrrProtectedObject> protectedObject,
      std::unique_ptr<std::vector<NextHopThrift>> backupNextHops) override;
  void deleteAdjacencyFrr(
      std::unique_ptr<FrrProtectedObject> protectedObject) override;
  void setInterfaceTxRx(
      std::vector<phy::TxRxEnableResponse>& txRxEnableResponse,
      std::unique_ptr<std::vector<phy::TxRxEnableRequest>> txRxEnableRequests)
      override;
  void getInterfaceDetail(
      InterfaceDetail& interfaceDetails,
      int32_t interfaceId) override;
  void getVrfs(std::vector<VrfInfo>& vrfs) override;
  void getPortInfo(PortInfoThrift& portInfo, int32_t portId) override;
  void getAllPortInfo(std::map<int32_t, PortInfoThrift>& portInfo) override;
  void clearPortStats(std::unique_ptr<std::vector<int32_t>> ports) override;
  void clearAllPortStats() override;
  void getPortStats(PortInfoThrift& portInfo, int32_t portId) override;
  void getAllPortStats(std::map<int32_t, PortInfoThrift>& portInfo) override;
  void getRunningConfig(std::string& configStr) override;
  void getArpTable(std::vector<ArpEntryThrift>& arpTable) override;
  void getL2Table(std::vector<L2EntryThrift>& l2Table) override;
  void getAclTable(std::vector<AclEntryThrift>& AclTable) override;
  void getAclTableGroup(AclTableThrift& aclTableEntry) override;
  void getAggregatePort(
      AggregatePortThrift& aggregatePortThrift,
      int32_t aggregatePortIDThrift) override;
  void getAggregatePortTable(
      std::vector<AggregatePortThrift>& aggregatePortsThrift) override;
  void getNdpTable(std::vector<NdpEntryThrift>& arpTable) override;
  void getLacpPartnerPair(LacpPartnerPair& lacpPartnerPair, int32_t portID)
      override;
  void getAllLacpPartnerPairs(
      std::vector<LacpPartnerPair>& lacpPartnerPairs) override;

  /* returns the product information */
  void getProductInfo(ProductInfo& productInfo) override;

  BootType getBootType() override;

  void getLldpNeighbors(std::vector<LinkNeighborThrift>& results) override;

  void startPktCapture(std::unique_ptr<CaptureInfo> info) override;
  void stopPktCapture(std::unique_ptr<std::string> name) override;
  void stopAllPktCaptures() override;

  void startLoggingRouteUpdates(
      std::unique_ptr<RouteUpdateLoggingInfo> info) override;
  void stopLoggingRouteUpdates(
      std::unique_ptr<IpPrefix> prefix,
      std::unique_ptr<std::string> identifier) override;
  void stopLoggingAnyRouteUpdates(
      std::unique_ptr<std::string> identifier) override;
  void getRouteUpdateLoggingTrackedPrefixes(
      std::vector<RouteUpdateLoggingInfo>& infos) override;

  void startLoggingMplsRouteUpdates(
      std::unique_ptr<MplsRouteUpdateLoggingInfo> info) override;
  void stopLoggingMplsRouteUpdates(
      std::unique_ptr<MplsRouteUpdateLoggingInfo> info) override;
  void stopLoggingAnyMplsRouteUpdates(
      std::unique_ptr<std::string> identifier) override;
  void getMplsRouteUpdateLoggingTrackedLabels(
      std::vector<MplsRouteUpdateLoggingInfo>& infos) override;

  void getRouteCounterBytes(
      std::map<std::string, std::int64_t>& routeCounters,
      std::unique_ptr<std::vector<std::string>> counters) override;
  void getAllRouteCounterBytes(
      std::map<std::string, std::int64_t>& routeCounters) override;

  void getTeFlowTableDetails(std::vector<TeFlowDetails>& flowTable) override;
  void getFabricConnectivity(
      std::map<std::string, FabricEndpoint>& connectivity) override;
  void getFabricReachability(
      std::map<std::string, FabricEndpoint>& reachability) override;
  void getSwitchReachability(
      std::map<std::string, std::vector<std::string>>& reachabilityMatrix,
      std::unique_ptr<std::vector<std::string>> switchNames) override;
  void getDsfNodes(std::map<int64_t, cfg::DsfNode>& dsfNodes) override;
  void getDsfSubscriptions(
      std::vector<FsdbSubscriptionThrift>& subscriptions) override;
  void getDsfSubscriptionClientId(std::string& ret) override;
  void getDsfSessions(std::vector<DsfSessionThrift>& dsfSessions) override;
  void getSystemPorts(std::map<int64_t, SystemPortThrift>& sysPorts) override;
  void getSysPortStats(
      std::map<std::string, HwSysPortStats>& hwSysPortStats) override;
  void getCpuPortStats(CpuPortStats& hwCpuPortStats) override;
  void getAllCpuPortStats(std::map<int, CpuPortStats>& hwCpuPortStats) override;
  void getHwPortStats(std::map<std::string, HwPortStats>& hwPortStats) override;
  void getRouteCounters(
      std::map<std::string, HwSwitchCounter>& routeCounters) override;
  void getHwRouterInterfaceStats(
      std::map<std::string, HwRouterInterfaceStats>& hwRouterInterfaceStats)
      override;
  void getFabricReachabilityStats(
      FabricReachabilityStats& fabricReachabilityStats) override;
  void getAllEcmpDetails(std::vector<EcmpDetails>& ecmpDetails) override;
  void getHwAgentConnectionStatus(
      std::map<int16_t, HwAgentEventSyncStatus>& hwAgentSyncStatusMap) override;
  void getSwitchIndicesForInterfaces(
      std::map<int16_t, std::vector<std::string>>& switchIndicesForInterfaces,
      std::unique_ptr<std::vector<std::string>> interfaces) override;

  /*
   * Thrift handler for keepalive messages.  It's a no-op, but prevents the
   * server from hitting an idle timeout while it's still publishing samples.
   *
   * @param[in]    callback    The callback for after we finish processing the
   *                           request.
   */
  void async_tm_keepalive(ThriftCallback<void> callback) override {
    callback->done();
  }

  /*
   * Indicate a change in the parent ThriftServer's idle timeout.  NOT a thrift
   * call.  This must be called before any client calls the getIdleTimeout()
   * Thrift function or it will throw an FbossError.  It is not always set
   * because sometimes we want to create a ThriftHandler without a ThriftServer
   * (e.g., during unit tests).
   *
   * @param[in]   timeout      The idle timeout in seconds.
   */
  void setIdleTimeout(const int32_t timeout) {
    thriftIdleTimeout_ = timeout;
  }

  /*
   * Thrift call to get the server's idle timeout.  Used by duplex clients to
   * configure keepalive intervals. If the timeout is unset of <0 (invalid) this
   * call throws an FbossError.
   *
   * @return    The idle timeout in seconds.
   */
  int32_t getIdleTimeout() override;

  /**
   * Thrift call to force reload the config from config file flag. This is
   * useful if we change the config file while the agent is running, and wish
   * to update its config to most recent version.
   */
  void reloadConfig() override;

  /*
   * Thrift call to validate a candidate config (config file contents) against
   * the running agent without applying it. Fills result.errors with the reasons
   * reloadConfig() would reject it. Throws FbossError only if the validation
   * could not be run.
   */
  void validateConfig(
      thrift::ConfigValidationResult& result,
      std::unique_ptr<std::string> config,
      thrift::ConfigApplyMethod applyMethod) override;

  /*
   * Get last time(ms since epoch) of the config is applied.
   * NOTE: If no config has ever been applied, the default timestamp is 0.
   * TODO(joseph5wu) Will deprecate such api and use getConfigAppliedInfo()
   * instead
   */
  int64_t getLastConfigAppliedInMs() override;

  /*
   * Get config applied information, which includes last config applied time(ms)
   * and last coldboot config applied time(ms).
   */
  void getConfigAppliedInfo(ConfigAppliedInfo& configAppliedInfo) override;

  /**
   * Serialize live running switch state at the path pointer by thrift path
   */
  void getCurrentStateJSON(std::string& ret, std::unique_ptr<std::string> path)
      override;

  /*
   * Get live serialized switch state for provided paths
   */
  void getCurrentStateJSONForPaths(
      std::map<std::string, std::string>& pathToState,
      std::unique_ptr<std::vector<std::string>> paths) override;

  /*
   * Apply every json Patch to specified path.
   * json Patch must be a valid JSON object string.
   */
  void patchCurrentStateJSONForPaths(
      std::unique_ptr<std::map<std::string, std::string>> pathToJsonPatch)
      override;

  SwitchRunState getSwitchRunState() override;

  void setSSLPolicy(apache::thrift::SSLPolicy sslPolicy) {
    sslPolicy_ = sslPolicy;
  }

  SSLType getSSLPolicy() override;

  void setExternalLedState(int32_t portNum, PortLedExternalState ledState)
      override;

  void getHwDebugDump(std::string& out) override;
  void setSdkRegDumpEnabled(bool enabled) override;
  void listHwObjects(
      std::string& out,
      std::unique_ptr<std::vector<HwObjectType>> hwObjects,
      bool cached) override;

  void getPlatformMapping(cfg::PlatformMapping& ret) override;

  void getBlockedNeighbors(
      std::vector<cfg::Neighbor>& blockedNeighbors) override;
  void setNeighborsToBlock(
      std::unique_ptr<std::vector<cfg::Neighbor>> neighborsToBlock) override;

  void getMacAddrsToBlock(
      std::vector<cfg::MacAndVlan>& blockedMacAddrs) override;
  void setMacAddrsToBlock(
      std::unique_ptr<std::vector<cfg::MacAndVlan>> macAddrsToBlock) override;

  void getMyMacs(std::vector<cfg::MacAndVlan>& myMacs) override;

  void publishLinkSnapshots(
      std::unique_ptr<std::vector<std::string>> portNames) override;

  void getInterfacePhyInfo(
      std::map<std::string, phy::PhyInfo>& phyInfos,
      std::unique_ptr<std::vector<std::string>> portNames) override;
  void getAllInterfacePhyInfo(
      std::map<std::string, phy::PhyInfo>& phyInfos) override;
  bool isSwitchDrained() override;
  void getActualSwitchDrainState(
      std::map<int64_t, cfg::SwitchDrainState>& switchId2ActualSwitchDrainState)
      override;
  void getMultiSwitchRunState(MultiSwitchRunState& runState) override;
  void getAllFabricLinkMonitoringStats(
      std::map<int32_t, FabricLinkMonPortStats>& stats) override;
  void getFabricMonitoringDetails(
      std::vector<FabricMonitoringDetail>& details) override;

 protected:
  void addMplsRoutesImpl(
      std::shared_ptr<SwitchState>* state,
      ClientID clientId,
      const std::vector<MplsRoute>& mplsRoutes) const;
  void addMplsRibRoutes(
      int16_t clientId,
      std::unique_ptr<std::vector<MplsRoute>> mplsRoutes,
      bool sync) const;
  void deleteMplsRibRoutes(
      int16_t clientId,
      std::unique_ptr<std::vector<MplsLabel>> mplsRoutes) const;
  void getPortStatusImpl(
      std::map<int32_t, PortStatus>& statusMap,
      const std::unique_ptr<std::vector<int32_t>>& ports) const;

  void ensureConfigured(folly::StringPiece function) const;
  void ensureConfigured() const {
    // This version of ensureConfigured() won't log
    ensureConfigured(folly::StringPiece(nullptr, nullptr));
  }
  void ensureVoqOrFabric(folly::StringPiece function) const;

 private:
  struct L3TrafficLoopStateStore;
  void setupL3TrafficLoopImpl(
      const BinaryAddress& qualificationAddress,
      const std::string& portName,
      const BinaryAddress& peerNextHop,
      const std::string& expectedPeerMac,
      int32_t timeoutSeconds);
  void teardownL3TrafficLoopImpl(const std::string& portName);

  void ensureNPU(folly::StringPiece function) const;
  void ensureNotFabric(folly::StringPiece function) const;
  void updateUnicastRoutesImpl(
      int32_t vrf,
      int16_t client,
      const std::unique_ptr<std::vector<UnicastRoute>>& routes,
      const std::string& updType,
      bool sync);
  void addNamedNextHopGroupsImpl(
      folly::StringPiece function,
      std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
      bool combineDuplicatedNextHops);

  void buildFabricMonitoringLookupMaps(
      const cfg::SwitchConfig& config,
      const std::shared_ptr<SwitchState>& swState,
      cfg::SwitchType switchType,
      std::map<std::string, SwitchID>& switchNameToSwitchIds,
      std::map<SwitchID, std::string>& switchIdToSystemPort);

  int determineVirtualDevice(
      const std::shared_ptr<Port>& swPort,
      const cfg::SwitchConfig& config,
      cfg::SwitchType switchType,
      const std::map<std::string, SwitchID>& switchNameToSwitchIds,
      const cfg::PortNeighbor& neighbor);

  void populateFabricPortDetail(
      const std::shared_ptr<Port>& swPort,
      const cfg::SwitchConfig& config,
      cfg::SwitchType switchType,
      const std::map<std::string, SwitchID>& switchNameToSwitchIds,
      const std::map<SwitchID, std::string>& switchIdToSystemPort,
      FabricMonitoringDetail& detail);

  void fillPortStats(PortInfoThrift& portInfo, int numPortQs = 0);

  Vlan* getVlan(int32_t vlanId);
  Vlan* getVlan(const std::string& vlanName);
  template <typename ADDR_TYPE, typename ADDR_CONVERTER>
  void getVlanAddresses(
      const Vlan* vlan,
      std::vector<ADDR_TYPE>& addrs,
      ADDR_CONVERTER& converter);
  // Forbidden copy constructor and assignment operator
  ThriftHandler(ThriftHandler const&) = delete;
  ThriftHandler& operator=(ThriftHandler const&) = delete;
  ThriftHandler(ThriftHandler&&) = delete;
  ThriftHandler& operator=(ThriftHandler&&) = delete;

  template <typename Result>
  void fail(const ThriftCallback<Result>& callback, const std::exception& ex) {
    FbossError error(folly::exceptionStr(ex));
    callback->exception(error);
  }
  template <typename AddressT, typename NeighborThriftT>
  void addRemoteNeighbors(
      const std::shared_ptr<SwitchState> state,
      std::vector<NeighborThriftT>& nbrs) const;

  void getSwitchIdToSwitchInfo(
      std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) override;

  /*
   * A pointer to the SwSwitch.  We don't own this.
   * It's the main program's responsibility to ensure that the SwSwitch exists
   * for the lifetime of the ThriftHandler.
   */
  SwSwitch* sw_;
  std::unique_ptr<L3TrafficLoopStateStore> l3TrafficLoopState_;

  int thriftIdleTimeout_{};
  std::vector<const TConnectionContext*> brokenClients_;

  apache::thrift::SSLPolicy sslPolicy_{apache::thrift::SSLPolicy::DISABLED};

  std::unordered_set<uint16_t> syncedFibClients_;
};

} // namespace facebook::fboss
