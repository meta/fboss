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

#include <boost/noncopyable.hpp>
#include <fb303/ThreadCachedServiceData.h>
#include <fb303/detail/QuantileStatWrappers.h>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <chrono>
#include "fboss/agent/AggregatePortStats.h"
#include "fboss/agent/InterfaceStats.h"
#include "fboss/agent/PortStats.h"
#include "fboss/agent/gen-cpp2/agent_stats_types.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/ThriftCallDurationLogger.h"

namespace facebook::fboss {

class PortStats;

using PortStatsMap =
    folly::ConcurrentHashMap<PortID, std::unique_ptr<PortStats>>;
using AggregatePortStatsMap = folly::
    ConcurrentHashMap<AggregatePortID, std::unique_ptr<AggregatePortStats>>;
using InterfaceStatsMap =
    folly::ConcurrentHashMap<InterfaceID, std::unique_ptr<InterfaceStats>>;

class SwitchStats : public boost::noncopyable, public ThriftCallDurationLogger {
 public:
  // Method to update the tunnelInterfacePacketDrop counter
  void updateTxBufferLimitExceededDrops() {
    txBufferLimitExceedDrop_.addValue(1);
  }

  /*
   * The prefix to use for our counter names
   */
  static std::string kCounterPrefix;

  explicit SwitchStats(int numSwitches);

  /*
   * Return the PortStats object for the given PortID.
   */
  PortStats* FOLLY_NULLABLE port(PortID portID);

  AggregatePortStats* FOLLY_NULLABLE
  aggregatePort(AggregatePortID aggregatePortID);

  InterfaceStats* FOLLY_NULLABLE intf(InterfaceID intfID);

  /*
   * Getters.
   */
  PortStatsMap* getPortStats() {
    return &ports_;
  }
  const PortStatsMap* getPortStats() const {
    return &ports_;
  }

  // Create a PortStats object for the given PortID
  PortStats* createPortStats(PortID portID, std::string portName);
  AggregatePortStats* createAggregatePortStats(
      AggregatePortID id,
      std::string name);

  void deletePortStats(PortID portID) {
    ports_.erase(portID);
  }

  InterfaceStatsMap* getInterfaceStats() {
    return &intfIDToStats_;
  }

  const InterfaceStatsMap* getInterfaceStats() const {
    return &intfIDToStats_;
  }

  InterfaceStats* createInterfaceStats(
      InterfaceID intfID,
      std::string intfName);

  void deleteInterfaceStats(InterfaceID intfID) {
    intfIDToStats_.erase(intfID);
  }

  /*
   * Change the name of a port. This will also reset its counters.
   */
  PortStats* updatePortName(PortID portID, const std::string& portName);

  void trappedPkt() {
    trapPkts_.addValue(1);
  }
  void pktDropped() {
    trapPktDrops_.addValue(1);
  }
  void pktBogus() {
    trapPktBogus_.addValue(1);
    trapPktDrops_.addValue(1);
  }
  void pktError() {
    trapPktErrors_.addValue(1);
    trapPktDrops_.addValue(1);
  }
  void pktUnhandled() {
    trapPktUnhandled_.addValue(1);
    trapPktDrops_.addValue(1);
  }
  void pktToHost(uint32_t bytes) {
    trapPktToHost_.addValue(1);
    trapPktToHostBytes_.addValue(bytes);
  }
  void pktFromHost(uint32_t bytes) {
    pktFromHost_.addValue(1);
    pktFromHostBytes_.addValue(bytes);
  }

  void arpPkt() {
    trapPktArp_.addValue(1);
  }
  void arpUnsupported() {
    arpUnsupported_.addValue(1);
    trapPktDrops_.addValue(1);
  }
  void arpNotMine() {
    arpNotMine_.addValue(1);
    trapPktDrops_.addValue(1);
  }
  void arpRequestRx() {
    arpRequestsRx_.addValue(1);
  }
  void arpRequestTx() {
    arpRequestsTx_.addValue(1);
  }
  void arpReplyRx() {
    arpRepliesRx_.addValue(1);
  }
  void arpReplyTx() {
    arpRepliesTx_.addValue(1);
  }
  void arpBadOp() {
    arpBadOp_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void ipv6NdpPkt() {
    trapPktNdp_.addValue(1);
  }
  void ipv6NdpBad() {
    ipv6NdpBad_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void dhcpV4Pkt() {
    dhcpV4Pkt_.addValue(1);
  }

  void dhcpV6Pkt() {
    dhcpV6Pkt_.addValue(1);
  }

  void ipv4Rx() {
    ipv4Rx_.addValue(1);
  }
  void ipv4TooSmall() {
    ipv4TooSmall_.addValue(1);
  }
  void ipv4WrongVer() {
    ipv4WrongVer_.addValue(1);
  }
  void ipv4Nexthop() {
    ipv4Nexthop_.addValue(1);
  }
  void ipv4Mine() {
    ipv4Mine_.addValue(1);
  }
  void ipv4NoArp() {
    ipv4NoArp_.addValue(1);
  }
  void ipv4TtlExceeded() {
    ipv4TtlExceeded_.addValue(1);
  }
  void ipv4Ttl1Mine() {
    ipv4Ttl1Mine_.addValue(1);
  }

  void ipv6HopExceeded() {
    ipv6HopExceeded_.addValue(1);
  }
  void ipv6HopLimit1Mine() {
    ipv6HopLimit1Mine_.addValue(1);
  }

  void mplsTtlExceeded() {
    mplsTtlExceeded_.addValue(1);
  }

  void srv6DecapMySidToMe() {
    srv6DecapMySidToMe_.addValue(1);
  }

  void srv6NonLastSegmentDecapDrop() {
    srv6NonLastSegmentDecapDrop_.addValue(1);
  }

  void udpTooSmall() {
    udpTooSmall_.addValue(1);
  }

  void dhcpV4BadPkt() {
    dhcpV4BadPkt_.addValue(1);
    dhcpV4DropPkt_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void dhcpV4DropPkt() {
    dhcpV4DropPkt_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void dhcpV6BadPkt() {
    dhcpV6BadPkt_.addValue(1);
    dhcpV6DropPkt_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void dhcpV6DropPkt() {
    dhcpV6DropPkt_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void addRouteV4() {
    addRouteV4_.addValue(1);
  }
  void addRouteV6() {
    addRouteV6_.addValue(1);
  }
  void delRouteV4() {
    delRouteV4_.addValue(1);
  }
  void delRouteV6() {
    delRouteV6_.addValue(1);
  }

  void addRoutesV4(uint64_t routeCount) {
    addRouteV4_.addValue(routeCount);
  }
  void addRoutesV6(uint64_t routeCount) {
    addRouteV6_.addValue(routeCount);
  }
  void delRoutesV4(uint64_t routeCount) {
    delRouteV4_.addValue(routeCount);
  }
  void delRoutesV6(uint64_t routeCount) {
    delRouteV6_.addValue(routeCount);
  }

  void ribResolutionCyclesDetected(uint64_t count) {
    ribResolutionCyclesDetected_.addValue(count);
  }

  void ipv4DstLookupFailure() {
    dstLookupFailureV4_.addValue(1);
    dstLookupFailure_.addValue(1);
  }

  void ipv6DstLookupFailure() {
    dstLookupFailureV6_.addValue(1);
    dstLookupFailure_.addValue(1);
  }
  void ipv6NdpNotMine() {
    ipv6NdpNotMine_.addValue(1);
    trapPktDrops_.addValue(1);
  }

  void stateUpdate(std::chrono::microseconds us) {
    updateState_.addValue(us.count());
  }

  void ribRouteProgrammingTimeUs(std::chrono::microseconds us) {
    ribRouteProgrammingTimeUs_.addValue(us.count());
  }

  void cpuLatencyUs(double latencyUs) {
    cpuLatencyUs_.addValue(latencyUs);
  }

  void bgHeartbeatDelay(int delay) {
    bgHeartbeatDelay_.addValue(delay);
  }

  void updHeartbeatDelay(int delay) {
    updHeartbeatDelay_.addValue(delay);
  }

  void packetTxHeartbeatDelay(int value) {
    packetTxHeartbeatDelay_.addValue(value);
  }

  void lacpHeartbeatDelay(int value) {
    lacpHeartbeatDelay_.addValue(value);
  }

  void neighborCacheHeartbeatDelay(int value) {
    neighborCacheHeartbeatDelay_.addValue(value);
  }

  void packetRxHeartbeatDelay(int delay) {
    packetRxHeartbeatDelay_.addValue(delay);
  }

  void dsfSubReconnectThreadHeartbeatDelay(int delay) {
    dsfSubReconnectThreadHeartbeatDelay_.addValue(delay);
  }

  void dsfSubStreamThreadHeartbeatDelay(int delay) {
    dsfSubStreamThreadHeartbeatDelay_.addValue(delay);
  }

  void bgEventBacklog(int value) {
    bgEventBacklog_.addValue(value);
  }

  void updEventBacklog(int value) {
    updEventBacklog_.addValue(value);
  }

  void lacpEventBacklog(int value) {
    lacpEventBacklog_.addValue(value);
  }

  void packetTxEventBacklog(int value) {
    packetTxEventBacklog_.addValue(value);
  }

  void neighborCacheEventBacklog(int value) {
    neighborCacheEventBacklog_.addValue(value);
  }

  void dsfSubReconnectThreadEventBacklog(int value) {
    dsfSubReconnectThreadEventBacklog_.addValue(value);
  }

  void dsfSubStreamThreadEventBacklog(int value) {
    dsfSubStreamThreadEventBacklog_.addValue(value);
  }

  void maxNumOfPhysicalHostsPerQueue(int value) {
    maxNumOfPhysicalHostsPerQueue_.addValue(value);
  }

  void pendingStateUpdateCount(int value) {
    pendingStateUpdateCount_.addValue(value);
  }

  void thriftRequestCompletionTimeMs(std::chrono::milliseconds ms) override {
    thriftRequestCompletionTimeMs_.addValue(ms.count());
  }

  void dsfSubscriptionServeDelayMs(int64_t delayMs) {
    dsfSubscriptionServeDelayWatermark_.addValue(delayMs);
  }

  void linkStateChange() {
    linkStateChange_.addValue(1);
  }

  void linkFault(int64_t increment) {
    linkFault_.addValue(increment);
  }

  void linkActiveStateChange() {
    linkActiveStateChange_.addValue(1);
  }

  void switchReachabilityChangeProcessed() {
    switchReachabilityChangeProcessed_.addValue(1);
  }

  int64_t getSwitchReachabilityChangeProcessed() {
    return getCumulativeValue(switchReachabilityChangeProcessed_);
  }

  void pcapDistFailure() {
    pcapDistFailure_.incrementValue(1);
  }

  void pktTooBig() {
    trapPktTooBig_.addValue(1);
  }

  void LldpRecvdPkt() {
    LldpRecvdPkt_.addValue(1);
  }
  void LldpBadPkt() {
    LldpBadPkt_.addValue(1);
  }
  void LldpValidateMisMatch() {
    LldpValidateMisMatch_.addValue(1);
  }
  void LldpNeighborsSize(int value) {
    LldpNeighborsSize_.addValue(value);
  }
  void LacpRxTimeouts() {
    LacpRxTimeouts_.addValue(1);
  }
  void LacpMismatchPduTeardown() {
    LacpMismatchPduTeardown_.addValue(1);
  }

  void MkPduRecvdPkt() {
    MkPduRecvdPkts_.addValue(1);
  }
  void MkPduSendPkt() {
    MkPduSendPkts_.addValue(1);
  }
  void MkPduSendFailure() {
    MkPduSendFailure_.addValue(1);
  }
  void MkPduPortNotRegistered() {
    MkPduPortNotRegistered_.addValue(1);
  }
  void MkPduInterval(double value, std::string_view port) {
    mkPduInterval_.addValue(value, port);
  }
  void MKAServiceSendFailue() {
    MKAServiceSendFailure_.addValue(1);
  }
  void MKAServiceSendSuccess() {
    MKAServiceSendSuccess_.addValue(1);
  }
  void MKAServiceRecvSuccess() {
    MKAServiceRecvSuccess_.addValue(1);
  }
  void PfcDeadlockDetectionCount() {
    pfcDeadlockDetectionCount_.addValue(1);
  }
  void PfcDeadlockRecoveryCount() {
    pfcDeadlockRecoveryCount_.addValue(1);
  }
  void ThreadHeartbeatMissCount() {
    threadHeartbeatMissCount_.addValue(1);
  }

  void localSystemPort(int value) {
    localSystemPort_.incrementValue(value);
  }
  void remoteSystemPort(int value) {
    remoteSystemPort_.incrementValue(value);
  }
  void localRifs(int value) {
    localRifs_.incrementValue(value);
  }
  void remoteRifs(int value) {
    remoteRifs_.incrementValue(value);
  }
  void localResolvedNdp(int value) {
    localResolvedNdp_.incrementValue(value);
  }
  void remoteResolvedNdp(int value) {
    remoteResolvedNdp_.incrementValue(value);
  }
  void localResolvedArp(int value) {
    localResolvedArp_.incrementValue(value);
  }
  void remoteResolvedArp(int value) {
    remoteResolvedArp_.incrementValue(value);
  }
  void failedDsfSubscription(const std::string& peerName, int value);

  void fsdbPublishQueueLength(uint64_t value) {
    fsdbPublishQueueLength_.addValue(value);
  }

  void fillAgentStats(AgentStats& agentStats) const;
  void fillFabricReachabilityStats(
      FabricReachabilityStats& fabricReachabilityStats) const;

  void warmBoot() {
    warmBoot_.addValue(1);
  }

  void coldBoot() {
    coldBoot_.addValue(1);
  }

  void warmBootFromHwSwitch() {
    warmBootFromHwSwitch_.addValue(1);
  }

  void probedStateCleanedUp() {
    probedStateCleanupStatus_.incrementValue(1);
  }

  int64_t getProbedStateCleanupStatus() const {
    return getCumulativeValue(probedStateCleanupStatus_, false);
  }

  void multiSwitchStatus(bool enabled) {
    multiSwitchStatus_.addValue(enabled ? 1 : 0);
  }

  void macTableUpdateFailure() {
    macTableUpdateFailure_.addValue(1);
  }

  int getMacTableUpdateFailure() const {
    return getCumulativeValue(macTableUpdateFailure_);
  }

  void fwDrainedWithHighNumActiveFabricLinks() {
    fwDrainedWithHighNumActiveFabricLinks_.addValue(1);
  }

  void neighborTableUpdateFailure() {
    neighborTableUpdateFailure_.addValue(1);
  }

  int getNeighborTableUpdateFailure() const {
    return getCumulativeValue(neighborTableUpdateFailure_);
  }

  void hiPriPktsReceived() {
    hiPriPktsReceived_.addValue(1);
  }

  void midPriPktsReceived() {
    midPriPktsReceived_.addValue(1);
  }

  void loPriPktsReceived() {
    loPriPktsReceived_.addValue(1);
  }

  void midPriPktsDropped() {
    midPriPktsDropped_.addValue(1);
  }

  void loPriPktsDropped() {
    loPriPktsDropped_.addValue(1);
  }

  void resourceAccountantRejectedUpdates() {
    resourceAccountantRejectedUpdates_.addValue(1);
  }

  void routeProgrammingUpdateAttempts() {
    routeProgrammingUpdateAttempts_.addValue(1);
  }

  int64_t getRouteProgrammingUpdateAttempts() const {
    return getCumulativeValue(routeProgrammingUpdateAttempts_);
  }

  void routeProgrammingUpdateFailures() {
    routeProgrammingUpdateFailures_.addValue(1);
  }
  int64_t getRouteProgrammingUpdateFailures() const {
    return getCumulativeValue(routeProgrammingUpdateFailures_);
  }

  void switchConfiguredMs(uint64_t ms) {
    switchConfiguredMs_.addValue(ms);
  }
  void setFabricOverdrainPct(int16_t switchIndex, int16_t overdrainPct);

  void setDrainState(int16_t switchIndex, cfg::SwitchDrainState drainState);

  void setActivePortsWithoutSwitchReachability(
      int16_t switchIndex,
      int numPorts);

  void setInactivePortsWithSwitchReachability(
      int16_t switchIndex,
      int numPorts);

  void setNumActiveFabricLinksEligibleForMinLink(
      int32_t virtualDeviceId,
      int32_t numLinks);

  void setNumActivePortsPerVirtualDevice(
      int32_t virtualDeviceId,
      int32_t numActivePorts);

  void hwAgentConnectionStatus(int switchIndex, bool connected) {
    CHECK_LT(switchIndex, hwAgentConnectionStatus_.size());
    hwAgentConnectionStatus_[switchIndex].incrementValue(connected ? 1 : -1);
  }

  void hwAgentUpdateTimeout(int switchIndex) {
    CHECK_LT(switchIndex, hwAgentUpdateTimeouts_.size());
    hwAgentUpdateTimeouts_[switchIndex].addValue(1);
  }

  void hwAgentStatsEventSinkConnectionStatus(int switchIndex, bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex].statsEventSinkDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex].setStatsEventSinkStatus(
        connected);
  }

  void hwAgentLinkEventSinkConnectionStatus(int switchIndex, bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex].linkEventSinkDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex].setLinkEventSinkStatus(
        connected);
  }

  void hwAgentFdbEventSinkConnectionStatus(int switchIndex, bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex].fdbEventSinkDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex].setFdbEventSinkStatus(connected);
  }

  void hwAgentRxPktEventSinkConnectionStatus(int switchIndex, bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex].rxPktEventSinkDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex].setRxPktEventSinkStatus(
        connected);
  }

  void hwAgentTxPktEventStreamConnectionStatus(
      int switchIndex,
      bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex].txPktEventStreamDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex].setTxPktEventStreamStatus(
        connected);
  }

  void hwAgentSwitchReachabilityChangeEventSinkConnectionStatus(
      int switchIndex,
      bool connected) {
    CHECK_LT(switchIndex, thriftStreamConnectionStatus_.size());
    if (!connected) {
      thriftStreamConnectionStatus_[switchIndex]
          .switchReachabilityChangeEventSinkDisconnected();
    }
    thriftStreamConnectionStatus_[switchIndex]
        .setSwitchReachabilityChangeEventSinkStatus(connected);
  }

  void hwAgentStatsReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].statsEventReceived();
  }

  void hwAgentLinkStatusReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].linkEventReceived();
  }

  void hwAgentFdbEventReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].fdbEventReceived();
  }

  void hwAgentRxPktReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].rxPktEventReceived();
  }

  void hwAgentTxPktSent(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].txPktEventSent();
  }

  void hwAgentRxBadPktReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex].rxBadPktReceived();
  }

  void hwAgentSwitchReachabilityChangeEventReceived(int switchIndex) {
    thriftStreamConnectionStatus_[switchIndex]
        .switchReachabilityChangeEventReceived();
  }

  void dsfSessionGrExpired() {
    dsfGrExpired_.addValue(1);
  }
  int64_t getDsfSessionGrExpired() const {
    return getCumulativeValue(dsfGrExpired_);
  }
  void dsfUpdateFailed() {
    dsfUpdateFailed_.addValue(1);
  }
  int64_t getDsfUpdateFailred() const {
    return getCumulativeValue(dsfUpdateFailed_);
  }
  void warmbootRemoteIntfRoutesInconsistency(int64_t count) {
    warmbootRemoteIntfRoutesInconsistency_.addValue(count);
  }
  int64_t getWarmbootRemoteIntfRoutesInconsistency() const {
    return getCumulativeValue(warmbootRemoteIntfRoutesInconsistency_);
  }
  void warmbootRemoteIntfRoutesReconcileError(int64_t count) {
    warmbootRemoteIntfRoutesReconcileError_.addValue(count);
  }
  int64_t getWarmbootRemoteIntfRoutesReconcileError() const {
    return getCumulativeValue(warmbootRemoteIntfRoutesReconcileError_);
  }

  void switchReachabilityInconsistencyDetected(int16_t switchIndex) {
    CHECK_LT(switchIndex, switchReachabilityInconsistencyDetected_.size());
    switchReachabilityInconsistencyDetected_[switchIndex].addValue(1);
  }
  void setPrimaryEcmpGroupsExhausted(bool exhausted) const;
  void setPrimaryEcmpGroupsCount(uint32_t count) const;
  void setBackupEcmpGroupsCount(uint32_t count) const;
  void setMergedEcmpGroupsCount(uint32_t count) const;
  void setMergedEcmpMemberGroupsCount(uint32_t count) const;
  void primaryEcmpGroupsExhausted() {
    primaryEcmpGroupsExhaustedEvents_.addValue(1);
  }
  int64_t getPrimaryEcmpGroupsExhaustedEvents() const {
    return getCumulativeValue(primaryEcmpGroupsExhaustedEvents_);
  }

  bool getPrimaryEcmpGroupsExhausted() const;
  int64_t getPrimaryEcmpGroupsCount() const;
  int64_t getBackupEcmpGroupsCount() const;
  int64_t getMergedEcmpGroupsCount() const;
  int64_t getMergedEcmpMemberGroupsCount() const;
  void getHwAgentStatus(
      std::map<int16_t, HwAgentEventSyncStatus>& statusMap) const;

  using ThreadLocalStatsMap =
      fb303::ThreadCachedServiceData::ThreadLocalStatsMap;
  using TLTimeseries = fb303::ThreadCachedServiceData::TLTimeseries;
  using TLHistogram = fb303::ThreadCachedServiceData::TLHistogram;
  using TLCounter = fb303::ThreadCachedServiceData::TLCounter;

  // Stats that aggregate into the ServiceData behind map rather than the
  // global one, e.g. to count into a private ServiceData that is never
  // exported.
  explicit SwitchStats(ThreadLocalStatsMap* map, int numSwitches);

 private:
  // Forbidden copy constructor and assignment operator
  SwitchStats(SwitchStats const&) = delete;
  SwitchStats& operator=(SwitchStats const&) = delete;

  void updateFabricOverdrainWatermark(
      int16_t switchIndex,
      int16_t overdrainPct);
  class HwAgentStreamConnectionStatus {
   public:
    explicit HwAgentStreamConnectionStatus(
        ThreadLocalStatsMap* map,
        int16_t switchIndex);
    void setStatsEventSinkStatus(bool connected) {
      statsEventSinkStatus_.incrementValue(connected ? 1 : -1);
    }
    void setLinkEventSinkStatus(bool connected) {
      linkEventSinkStatus_.incrementValue(connected ? 1 : -1);
    }
    void setFdbEventSinkStatus(bool connected) {
      fdbEventSinkStatus_.incrementValue(connected ? 1 : -1);
    }
    void setRxPktEventSinkStatus(bool connected) {
      rxPktEventSinkStatus_.incrementValue(connected ? 1 : -1);
    }
    void setTxPktEventStreamStatus(bool connected) {
      txPktEventStreamStatus_.incrementValue(connected ? 1 : -1);
    }
    void setSwitchReachabilityChangeEventSinkStatus(bool connected) {
      switchReachabilityChangeEventSinkStatus_.incrementValue(
          connected ? 1 : -1);
    }
    void statsEventSinkDisconnected() {
      statsEventSinkDisconnects_.addValue(1);
    }
    void linkEventSinkDisconnected() {
      linkEventSinkDisconnects_.addValue(1);
    }
    void fdbEventSinkDisconnected() {
      fdbEventSinkDisconnects_.addValue(1);
    }
    void rxPktEventSinkDisconnected() {
      rxPktEventSinkDisconnects_.addValue(1);
    }
    void txPktEventStreamDisconnected() {
      txPktEventStreamDisconnects_.addValue(1);
    }
    void switchReachabilityChangeEventSinkDisconnected() {
      switchReachabilityChangeEventSinkDisconnects_.addValue(1);
    }
    int64_t getStatsEventSinkStatus() const {
      return getCumulativeValue(statsEventSinkStatus_, false /*hasSumSuffix*/);
    }
    int64_t getLinkEventSinkStatus() const {
      return getCumulativeValue(linkEventSinkStatus_, false /*hasSumSuffix*/);
    }
    int64_t getFdbEventSinkStatus() const {
      return getCumulativeValue(fdbEventSinkStatus_, false /*hasSumSuffix*/);
    }
    int64_t getRxPktEventSinkStatus() const {
      return getCumulativeValue(rxPktEventSinkStatus_, false /*hasSumSuffix*/);
    }
    int64_t getTxPktEventStreamStatus() const {
      return getCumulativeValue(
          txPktEventStreamStatus_, false /*hasSumSuffix*/);
    }
    int64_t getSwitchReachabilityChangeEventSinkStatus() const {
      return getCumulativeValue(
          switchReachabilityChangeEventSinkStatus_, false /*hasSumSuffix*/);
    }
    void statsEventReceived() {
      statsEventsReceived_.addValue(1);
    }
    void linkEventReceived() {
      linkEventsReceived_.addValue(1);
    }
    void fdbEventReceived() {
      fdbEventsReceived_.addValue(1);
    }
    void rxPktEventReceived() {
      rxPktEventsReceived_.addValue(1);
    }
    void txPktEventSent() {
      txPktEventsSent_.addValue(1);
    }
    void switchReachabilityChangeEventReceived() {
      switchReachabilityChangeEventsReceived_.addValue(1);
    }
    void rxBadPktReceived() {
      rxBadPktReceived_.addValue(1);
    }
    int64_t getStatsEventSinkDisconnectCount() const {
      return getCumulativeValue(statsEventSinkDisconnects_);
    }
    int64_t getLinkEventSinkDisconnectCount() const {
      return getCumulativeValue(linkEventSinkDisconnects_);
    }
    int64_t getFdbEventSinkDisconnectCount() const {
      return getCumulativeValue(fdbEventSinkDisconnects_);
    }
    int64_t getRxPktEventSinkDisconnectCount() const {
      return getCumulativeValue(rxPktEventSinkDisconnects_);
    }
    int64_t getTxPktEventStreamDisconnectCount() const {
      return getCumulativeValue(txPktEventStreamDisconnects_);
    }
    int64_t getSwitchReachabilityChangeEventSinkDisconnectCount() const {
      return getCumulativeValue(switchReachabilityChangeEventSinkDisconnects_);
    }
    int64_t getStatsEventReceivedCount() const {
      return getCumulativeValue(statsEventsReceived_);
    }
    int64_t getLinkEventReceivedCount() const {
      return getCumulativeValue(linkEventsReceived_);
    }
    int64_t getFdbEventReceivedCount() const {
      return getCumulativeValue(fdbEventsReceived_);
    }
    int64_t getRxPktEventReceivedCount() const {
      return getCumulativeValue(rxPktEventsReceived_);
    }
    int64_t getrxBadPktReceivedCount() const {
      return getCumulativeValue(rxBadPktReceived_);
    }
    int64_t getTxPktEventSentCount() const {
      return getCumulativeValue(txPktEventsSent_);
    }
    int64_t getSwitchReachabilityChangeEventReceivedCount() const {
      return getCumulativeValue(switchReachabilityChangeEventsReceived_);
    }

   private:
    TLCounter statsEventSinkStatus_;
    TLCounter linkEventSinkStatus_;
    TLCounter fdbEventSinkStatus_;
    TLCounter rxPktEventSinkStatus_;
    TLCounter txPktEventStreamStatus_;
    TLCounter switchReachabilityChangeEventSinkStatus_;

    TLTimeseries statsEventSinkDisconnects_;
    TLTimeseries linkEventSinkDisconnects_;
    TLTimeseries fdbEventSinkDisconnects_;
    TLTimeseries rxPktEventSinkDisconnects_;
    TLTimeseries txPktEventStreamDisconnects_;
    TLTimeseries switchReachabilityChangeEventSinkDisconnects_;

    TLTimeseries statsEventsReceived_;
    TLTimeseries linkEventsReceived_;
    TLTimeseries fdbEventsReceived_;
    TLTimeseries rxPktEventsReceived_;
    TLTimeseries txPktEventsSent_;
    TLTimeseries switchReachabilityChangeEventsReceived_;
    TLTimeseries rxBadPktReceived_;
  };

  const int numSwitches_;
  // Total number of trapped packets
  TLTimeseries trapPkts_;
  // Number of trapped packets that were intentionally dropped.
  TLTimeseries trapPktDrops_;
  // Malformed packets received
  TLTimeseries trapPktBogus_;
  // Number of times the controller encountered an error trying to process
  // a packet.
  TLTimeseries trapPktErrors_;
  // Trapped packets that the controller didn't know how to handle.
  TLTimeseries trapPktUnhandled_;
  // Trapped packets forwarded to host
  TLTimeseries trapPktToHost_;
  // Trapped packets forwarded to host in bytes
  TLTimeseries trapPktToHostBytes_;
  // Packets sent by host
  TLTimeseries pktFromHost_;
  // Packets sent by host in bytes
  TLTimeseries pktFromHostBytes_;

  // ARP Packets
  TLTimeseries trapPktArp_;
  // ARP packets dropped due to unsupported hardware or protocol settings.
  TLTimeseries arpUnsupported_;
  // ARP packets ignored because we were not the target.
  TLTimeseries arpNotMine_;
  // ARP requests destined for us
  TLTimeseries arpRequestsRx_;
  // ARP replies destined for us
  TLTimeseries arpRepliesRx_;
  // ARP requests sent from us
  TLTimeseries arpRequestsTx_;
  // ARP replies sent from us
  TLTimeseries arpRepliesTx_;
  // ARP packets with an unknown op field
  TLTimeseries arpBadOp_;

  // IPv6 Neighbor Discovery Protocol packets
  TLTimeseries trapPktNdp_;
  TLTimeseries ipv6NdpBad_;
  TLTimeseries ipv6NdpNotMine_;

  // IPv4 Packets
  TLTimeseries ipv4Rx_;
  // IPv4 packets dropped due to smaller packet size
  TLTimeseries ipv4TooSmall_;
  // IPv4 packets dropped due to wrong version number
  TLTimeseries ipv4WrongVer_;
  // IPv4 packets received for resolving nexthop
  TLTimeseries ipv4Nexthop_;
  // IPv4 packets received for myself
  TLTimeseries ipv4Mine_;
  // IPv4 packets received, but not able to send out ARP request
  TLTimeseries ipv4NoArp_;
  // IPv4 TTL exceeded
  TLTimeseries ipv4TtlExceeded_;
  TLTimeseries ipv4Ttl1Mine_;

  // IPv6 hop count exceeded
  TLTimeseries ipv6HopExceeded_;
  // Locally destined packets which arrive with
  // hop limit 1
  TLTimeseries ipv6HopLimit1Mine_;

  // MPLS packets trapped to FBOSS because the top-label TTL expired
  // during forwarding. The received packet carries ingress TTL 1.
  TLTimeseries mplsTtlExceeded_;

  // SRv6 decap: outer header stripped and inner packet re-injected
  TLTimeseries srv6DecapMySidToMe_;
  // SRv6 packet with non-last uSID matching decap MySID
  TLTimeseries srv6NonLastSegmentDecapDrop_;

  // UDP packets dropped due to smaller packet size
  TLTimeseries udpTooSmall_;

  // Total DHCP packets received
  TLTimeseries dhcpV4Pkt_;
  /*
   *  Consolidate DHCP v4 drops under 2 heads
   *  a) Bad dhcp packet receive
   *  b) Catch all dhcp packet drop. This includes a) but also includes
   *  cases where a dhcp relay was not configured for this VLAN
   */
  // DHCPv4 packets dropped due to packet being malformed
  TLTimeseries dhcpV4BadPkt_;
  // DHCPv4 packets dropped
  TLTimeseries dhcpV4DropPkt_;
  // stats for dhcpV6
  TLTimeseries dhcpV6Pkt_;
  TLTimeseries dhcpV6BadPkt_;
  TLTimeseries dhcpV6DropPkt_;

  /**
   * Routes add/delete stats
   *
   * Modification is treated as add.
   */
  TLTimeseries addRouteV4_;
  TLTimeseries addRouteV6_;
  TLTimeseries delRouteV4_;
  TLTimeseries delRouteV6_;

  /**
   * Number of cycles detected during recursive RIB route resolution.
   */
  TLTimeseries ribResolutionCyclesDetected_;

  /**
   * Number of route programming attempts
   */
  TLTimeseries routeProgrammingUpdateAttempts_;
  /**
   * Number of route programming update failures
   */
  TLTimeseries routeProgrammingUpdateFailures_;

  TLTimeseries dstLookupFailureV4_;
  TLTimeseries dstLookupFailureV6_;
  TLTimeseries dstLookupFailure_;

  /**
   * Histogram for time used for SwSwitch::updateState() (in microsecond)
   */
  fb303::detail::QuantileStatWrapper updateState_;

  /**
   * Histogram for RIB route programming time (in microseconds)
   */
  fb303::detail::QuantileStatWrapper ribRouteProgrammingTimeUs_;

  /**
   * Histogram for time used for thrift request completion time (milliseconds)
   */
  fb303::detail::QuantileStatWrapper thriftRequestCompletionTimeMs_;

  /**
   * Histogram for DSF subscription serve delay watermark (milliseconds)
   */
  fb303::detail::QuantileStatWrapper dsfSubscriptionServeDelayWatermark_;

  /**
   * CPU latency probe round-trip time (microseconds).
   */
  fb303::detail::QuantileStatWrapper cpuLatencyUs_;

  /**
   * Background thread heartbeat delay (ms)
   */
  TLHistogram bgHeartbeatDelay_;

  /**
   * Update thread heartbeat delay (ms)
   */
  TLHistogram updHeartbeatDelay_;
  /**
   * Fboss packet Tx thread heartbeat delay (ms)
   */
  TLHistogram packetTxHeartbeatDelay_;
  /**
   * Fboss packet Rx thread heartbeat delay (ms)
   */
  TLHistogram packetRxHeartbeatDelay_;
  /**
   * LACP thread heartbeat delay in milliseconds
   */
  TLHistogram lacpHeartbeatDelay_;
  /**
   * Arp Cache thread heartbeat delay in milliseconds
   */
  TLHistogram neighborCacheHeartbeatDelay_;
  /**
   * DSF Subscriber Reconnect thread heartbeat delay in milliseconds
   */
  TLHistogram dsfSubReconnectThreadHeartbeatDelay_;
  /**
   * DSF Subscriber Stream thread heartbeat delay in milliseconds
   */
  TLHistogram dsfSubStreamThreadHeartbeatDelay_;

  /**
   * Number of events queued in background thread
   */
  TLHistogram bgEventBacklog_;

  /**
   * Number of events queued in update thread
   */
  TLHistogram updEventBacklog_;
  /**
   * Number of events queued in fboss packet TX thread
   */
  TLHistogram packetTxEventBacklog_;

  TLHistogram lacpEventBacklog_;
  /**
   * Number of events queued in fboss Arp Cache thread
   */
  TLHistogram neighborCacheEventBacklog_;
  /**
   * Number of events queued in DSF Subscriber Reconnect thread
   */
  TLHistogram dsfSubReconnectThreadEventBacklog_;
  /**
   * Number of events queued in DSF Subscriber stream serve thread
   */
  TLHistogram dsfSubStreamThreadEventBacklog_;

  /*
   * Maximum number of physical hosts assigned to a port egress queue
   * should be 0 or 1, alert when >=2
   */
  TLHistogram maxNumOfPhysicalHostsPerQueue_;
  /**
   * Number of state updates queued in state update queue
   */
  TLHistogram pendingStateUpdateCount_;

  /**
   * Link state up/down change count
   */
  TLTimeseries linkStateChange_;

  /**
   * Link flaps plus debounce retriggers suppressed by the port debounce
   * hold timers
   */
  TLTimeseries linkFault_;

  /**
   * Link state active/inactive change count
   */
  TLTimeseries linkActiveStateChange_;

  /**
   * Switch reachability change count
   */
  TLTimeseries switchReachabilityChangeProcessed_;

  // Individual port stats objects, indexed by PortID
  PortStatsMap ports_;

  AggregatePortStatsMap aggregatePortIDToStats_;

  InterfaceStatsMap intfIDToStats_;

  // Number of packets dropped by the PCAP distribution service
  TLCounter pcapDistFailure_;

  // Number of packet too big ICMPv6 triggered
  TLTimeseries trapPktTooBig_;

  // Number of LLDP packets.
  TLTimeseries LldpRecvdPkt_;
  // Number of bad LLDP packets.
  TLTimeseries LldpBadPkt_;
  // Number of LLDP packets that did not match configured, expected values.
  TLTimeseries LldpValidateMisMatch_;
  // Number of LLDP Neighbors.
  TLTimeseries LldpNeighborsSize_;

  // Number of LACP Rx timeouts
  TLTimeseries LacpRxTimeouts_;
  // Number of LACP session teardown due to mismatching PDUs
  TLTimeseries LacpMismatchPduTeardown_;
  // Number of MkPdu Received.
  TLTimeseries MkPduRecvdPkts_;
  // Number of MkPdu Send.
  TLTimeseries MkPduSendPkts_;
  // Number of MkPdu Send Failure
  TLTimeseries MkPduSendFailure_;
  // Number of pkt recv when port is not registered.
  TLTimeseries MkPduPortNotRegistered_;
  // MkPdu interval (per port)
  fb303::detail::DynamicQuantileStatWrapper<1> mkPduInterval_;
  // Number of Send Pkt to MkaService Failure.
  TLTimeseries MKAServiceSendFailure_;
  // Number of Pkts Send to MKAService.
  TLTimeseries MKAServiceSendSuccess_;
  // Number of pkts recvd from MkaService.
  TLTimeseries MKAServiceRecvSuccess_;
  // Number of timers pfc deadlock watchodg was hit
  TLTimeseries pfcDeadlockDetectionCount_;
  // Number of timers pfc deadlock recovery hit
  TLTimeseries pfcDeadlockRecoveryCount_;
  // Number of thread heartbeat misses
  TLTimeseries threadHeartbeatMissCount_;

  // Number of system ports in switch state
  TLCounter localSystemPort_;
  // Number of remote system port in switch state
  TLCounter remoteSystemPort_;
  // Number of local Rifs
  TLCounter localRifs_;
  // Number of remote Rifs
  TLCounter remoteRifs_;
  // Number of local resolved NDP
  TLCounter localResolvedNdp_;
  // Number of remote resolved NDP
  TLCounter remoteResolvedNdp_;
  // Number of local resolved ARP
  TLCounter localResolvedArp_;
  // Number of remote resolved ARP
  TLCounter remoteResolvedArp_;
  // Failed Dsf subscriptions
  TLCounter failedDsfSubscription_;
  // Failed Dsf subscriptions by peer SwitchID
  std::map<std::string, TLCounter> failedDsfSubscriptionByPeerSwitchName_;
  TLTimeseries txBufferLimitExceedDrop_;

  TLTimeseries coldBoot_;
  TLTimeseries warmBoot_;
  TLTimeseries warmBootFromHwSwitch_;
  TLCounter probedStateCleanupStatus_;
  TLTimeseries switchConfiguredMs_;
  TLTimeseries dsfGrExpired_;
  TLTimeseries dsfUpdateFailed_;
  TLTimeseries warmbootRemoteIntfRoutesInconsistency_;
  TLTimeseries warmbootRemoteIntfRoutesReconcileError_;
  TLTimeseries hiPriPktsReceived_;
  TLTimeseries midPriPktsReceived_;
  TLTimeseries loPriPktsReceived_;
  TLTimeseries midPriPktsDropped_;
  TLTimeseries loPriPktsDropped_;

  /**
   * Number of updates queued in FSDB publish queue
   */
  TLHistogram fsdbPublishQueueLength_;

  // TODO: delete this once multi_switch becomes default
  TLTimeseries multiSwitchStatus_;
  TLTimeseries neighborTableUpdateFailure_;

  TLTimeseries macTableUpdateFailure_;

  TLTimeseries fwDrainedWithHighNumActiveFabricLinks_;

  /**
   * Number of state updates rejected by resource accountant
   */
  TLTimeseries resourceAccountantRejectedUpdates_;
  /*
   * Number of times primary ecmp groups were exhausted
   * in last interval(.60, .600, .3600)
   */
  TLTimeseries primaryEcmpGroupsExhaustedEvents_;

  std::vector<TLCounter> hwAgentConnectionStatus_;
  std::vector<TLTimeseries> hwAgentUpdateTimeouts_;
  std::vector<HwAgentStreamConnectionStatus> thriftStreamConnectionStatus_;
  std::vector<TLTimeseries> switchReachabilityInconsistencyDetected_;
  std::vector<TLCounter> activePortsWithoutSwitchReachability_;
  std::vector<TLCounter> inactivePortsWithSwitchReachability_;
};

} // namespace facebook::fboss
