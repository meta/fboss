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
#include "fboss/agent/HwSwitchHandler.h"
#include "fboss/agent/L2LearnEventObserver.h"
#include "fboss/agent/MultiHwSwitchHandler.h"
#include "fboss/agent/MultiSwitchFb303Stats.h"
#include "fboss/agent/PacketObserver.h"
#include "fboss/agent/StateDeltaLogger.h"
#include "fboss/agent/SwRxPacket.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchInfoTable.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/agent_info_types.h"
#include "fboss/agent/gen-cpp2/agent_stats_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_reachability_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/single/MonolithicHwSwitchHandler.h"
#include "fboss/agent/state/StateUpdate.h"
#include "fboss/agent/types.h"
#include "fboss/lib/HwWriteBehavior.h"
#include "fboss/lib/ThreadHeartbeat.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

#include <folly/IntrusiveList.h>
#include <folly/Range.h>
#include <folly/SpinLock.h>
#include <folly/ThreadLocal.h>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <optional>

#if FOLLY_HAS_COROUTINES
#include <folly/coro/BoundedQueue.h>
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

DECLARE_bool(rx_sw_priority);

namespace facebook::fboss {

struct AgentConfig;
class ArpHandler;
class InterfaceStats;
class IPv4Handler;
class IPv6Handler;
class LinkAggregationManager;
class LldpManager;
class MPLSHandler;
class PktCaptureManager;
class PlatformMapping;
class PlatformProductInfo;
class Port;
class PortDescriptor;
class PortStats;
class PortUpdateHandler;
class RxPacket;
class SwitchState;
class SwitchStats;
class SwitchIdScopeResolver;
class StateDelta;
class NeighborUpdater;
class PacketLogger;
class RouteUpdateLogger;
class StateObserver;
class TunManager;
class MirrorManager;
class TamManager;
class PhySnapshotManager;
class AclNexthopHandler;
class LookupClassUpdater;
class LookupClassRouteUpdater;
class MacTableManager;
class ResolvedNexthopMonitor;
class ResolvedNexthopProbeScheduler;
class MySidNeighborObserver;
class StaticL2ForNeighborObserver;
class MKAServiceManager;
class PacketStreamHandler;
template <typename AddressT>
class Route;
class Interface;
class FsdbSyncer;
class TeFlowNexthopHandler;
class DsfSubscriber;
class HwAsicTable;
class MultiHwSwitchHandler;
class MonolithicHwSwitchHandler;
class SwitchStatsObserver;
class MultiSwitchPacketStreamMap;
class SwSwitchWarmBootHelper;
class AgentDirectoryUtil;
class HwSwitchThriftClientTable;
class ResourceAccountant;
class RemoteNeighborUpdater;
class EcmpResourceManager;
class ShelManager;
class PbrAclManager;
class FabricLinkMonitoringManager;
class CpuLatencyManager;
class StateUpdateValidator;

inline static const int kHiPriorityBufferSize{1000};
inline static const int kMidPriorityBufferSize{1000};
inline static const int kLowPriorityBufferSize{1000};

namespace fsdb {
enum class FsdbSubscriptionState;
}

enum class SwitchFlags : int {
  DEFAULT = 0,
  ENABLE_TUN = 1,
  ENABLE_LLDP = 2,
  PUBLISH_STATS = 4,
  ENABLE_LACP = 8,
  ENABLE_MACSEC = 16,
  ENABLE_FABRIC_LINK_MONITORING = 32,
};

inline SwitchFlags operator|(SwitchFlags lhs, SwitchFlags rhs) {
  using BackingType = std::underlying_type_t<SwitchFlags>;
  return static_cast<SwitchFlags>(
      static_cast<BackingType>(lhs) | static_cast<BackingType>(rhs));
}

inline SwitchFlags& operator|=(SwitchFlags& lhs, SwitchFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

inline bool operator&(SwitchFlags lhs, SwitchFlags rhs) {
  using BackingType = std::underlying_type_t<SwitchFlags>;
  return (static_cast<BackingType>(lhs) & static_cast<BackingType>(rhs)) != 0;
}

/*
 * A software representation of a switch.
 *
 * This represents an entire switch in the network (as opposed to a single
 * switch ASIC).
 *
 * SwSwitch is the primary entry point into the FBOSS controller.  It provides
 * all of the hardware-independent logic for switching and routing packets.  It
 * must be used in conjunction with a HwSwitch object, which provides an
 * interface to the switch hardware.
 */
class SwSwitch : public HwSwitchCallback {
 public:
  using StateUpdateFn = std::function<std::shared_ptr<SwitchState>(
      const std::shared_ptr<SwitchState>&)>;

  using StateUpdatedCallback = std::function<void(const StateDelta&)>;

  using AllThreadsSwitchStats =
      folly::ThreadLocalPtr<SwitchStats, SwSwitch>::Accessor;

  SwSwitch(
      HwSwitchHandlerInitFn hwSwitchHandlerInitFn,
      const AgentDirectoryUtil* agentDirUtil,
      bool supportsAddRemovePort,
      const AgentConfig* config,
      const std::shared_ptr<SwitchState>& initialState = nullptr);

  /*
   * Needed for mock platforms that do cannot initialize platform mapping
   * based on fruid file
   */
  SwSwitch(
      HwSwitchHandlerInitFn hwSwitchHandlerInitFn,
      std::unique_ptr<PlatformMapping> platformMapping,
      const AgentDirectoryUtil* agentDirUtil,
      bool supportsAddRemovePort,
      const AgentConfig* config);

  /* used in tests */
  SwSwitch(
      HwSwitchHandlerInitFn hwSwitchHandlerInitFn,
      std::unique_ptr<PlatformMapping> platformMapping,
      const AgentDirectoryUtil* agentDirUtil,
      bool supportsAddRemovePort,
      const AgentConfig* config,
      const std::shared_ptr<SwitchState>& initialState = nullptr);

  ~SwSwitch() override;

  MultiHwSwitchHandler* getHwSwitchHandler() {
    return multiHwSwitchHandler_.get();
  }

  const MultiHwSwitchHandler* getHwSwitchHandler() const {
    return multiHwSwitchHandler_.get();
  }

  const PlatformMapping* getPlatformMapping() const {
    return platformMapping_.get();
  }
  PlatformMapping* getPlatformMapping() {
    return platformMapping_.get();
  }

  TunManager* getTunManager() {
    return tunMgr_.get();
  }
  const SwitchIdScopeResolver* getScopeResolver() const {
    return scopeResolver_.get();
  }

  HwSwitchThriftClientTable* getHwSwitchThriftClientTable() const {
    return hwSwitchThriftClientTable_.get();
  }

  /*
   * Initialize the switch.
   *
   * This initializes the underlying hardware, and populates the current
   * SwitchState to reflect the hardware state.
   *
   * Note that this function is generally slow, and may take many seconds to
   * complete.
   *
   * Param TunManager can be passed to init method. It allows
   * using any custom TunManager instead of default (useful in testing). This
   * will be used only when ENABLE_TUN is present in flags param. Pass nullptr
   * if you don't want custom behaviour for TunManager (default one will be
   * created).
   *
   * Param optional SwitchFlags has has the following flags defined:
   *
   * ENABLE_TUN: enables interface sync to system.
   * ENABLE_LLDP: enables periodically sending LLDP packets
   * PUBLISH_STATS: if set, we will publish the boot type (graceful or
   *                   otherwise) after we initialize the hardware.
   * DEFAULT: None of the above flags are set.
   *
   */
  void init(
      std::unique_ptr<TunManager> tunMgr,
      HwSwitchInitFn hwSwitchInitFn,
      SwitchFlags flags = SwitchFlags::DEFAULT);

  // can be used in the tests, where a test orchestrating ensemble can be
  // injected between HwSwitch and SwSwitch an ensemble must dispatch events to
  // SwSwitch as soon as it receives.
  void init(
      HwSwitchCallback* callback,
      std::unique_ptr<TunManager> tunMgr,
      HwSwitchInitFn hwSwitchInitFn,
      const HwWriteBehavior& hwWriteBehavior,
      SwitchFlags flags = SwitchFlags::DEFAULT);

  void init(
      const HwWriteBehavior& hwWriteBehavior,
      SwitchFlags flags = SwitchFlags::DEFAULT);

  bool isFullyInitialized() const;

  bool isInitialized() const;

  bool isFullyConfigured() const;

  bool isConfigured() const;

  bool isExiting() const;

  void updateLldpStats();

  void publishStatsToFsdb();

  void publishIPhyStatesToFsdb(const std::map<PortID, phy::PhyInfo>& phyInfo);

  AgentStats fillFsdbStats();

  void updateStats();

  state::WarmbootState gracefulExitState() const;

  state::SwitchState updateOverrideEcmpSwitchingMode(
      state::WarmbootState* warmbootState) const;

  /*
   * Get a pointer to the current switch state.
   *
   * There are actually two states, applied and desired. In absence of hw
   * update failures, these states should be the same. However when they
   * do differ, return appliedState to the callers, since that's what
   * we applied to HW.
   */
  std::shared_ptr<SwitchState> getState() const {
    return getAppliedState();
  }
  /**
   * Schedule an update to the switch state.
   *
   *  @param  update
   *          The update to be enqueued
   *  @return bool whether the update was queued or not
   * This schedules the specified StateUpdate to be invoked in the update
   * thread in order to update the SwitchState.
   *
   */
  bool updateState(std::unique_ptr<StateUpdate> update);

  /**
   * Schedule an update to the switch state.
   *
   * @param name  A name to identify the source of this update.  This is
   *              primarily used for logging and debugging purposes.
   * @param fn    The function that will prepare the new SwitchState.
   * @return bool whether the update was queued or not
   * The StateUpdateFn takes a single argument -- the current SwitchState
   * object to modify.  It should return a new SwitchState object, or null if
   * it decides that no update needs to be performed.
   *
   * Note that the update function will not be called immediately--it will be
   * invoked later from the update thread.  Therefore if you supply a lambda
   * with bound arguments, make sure that any bound arguments will still be
   * valid later when the function is invoked.  (e.g., Don't capture local
   * variables from your current call frame by reference.)
   *
   * The StateUpdateFn must not throw any exceptions.
   *
   * The update thread may choose to batch updates in some cases--if it has
   * multiple update functions to run it may run them all at once and only
   * send a single update notification to the HwSwitch and other update
   * subscribers.  Therefore the StateUpdateFn may be called with an
   * unpublished SwitchState in some cases.
   */
  bool updateState(folly::StringPiece name, StateUpdateFn fn);

  /**
   * Schedule an update to the switch state.
   *
   * @param name  A name to identify the source of this update.  This is
   *              primarily used for logging and debugging purposes.
   * @param fn    The function that will prepare the new SwitchState.
   *
   * A version of updateState that prevents the update handling queue from
   * coalescing this update with later updates. This should rarely be used,
   * but can be used when there is an update that MUST be seen by the hw
   * implementation, even if the inverse update is immediately applied.
   */
  void updateStateNoCoalescing(folly::StringPiece name, StateUpdateFn fn);

  /*
   * A version of updateState() that doesn't return until the update has been
   * applied.
   *
   * This should only be called in situations where it is safe to block the
   * current thread until the operation completes.
   *
   */
  void updateStateBlocking(folly::StringPiece name, StateUpdateFn fn);

  /*
   * A version of updateState() that reports back failures in applying state
   * to Hw. Such updates are marked as blocking, non coalescing and
   * transactional (if HwSwitch supports transactions). This will guarantee that
   * if desired state update could not be applied FbossHwUpdateError exception
   * will be thrown in the caller's thread.
   *
   * Note though that its upto the HwSwitch implementation to decide which state
   * update failures it can protect against. For things HwSwitch does not
   * protect against it may just fail hard,
   *
   */
  void updateStateWithHwFailureProtection(
      folly::StringPiece name,
      StateUpdateFn fn,
      std::optional<StateDeltaApplication> deltaApplicationBehavior =
          std::nullopt);

  /**
   * Apply config from the config file (specified in 'config' flag).
   *
   *  @param  reason
   *          What is the reson for applying config. This will be printed for
   *          logging purposes.
   */
  void applyConfig(const std::string& reason, bool reload = false);
  /**
   * Apply passed in config - used primarily by tests.
   *
   *  @param  reason
   *          What is the reson for applying config. This will be printed for
   *          logging purposes.
   */
  void applyConfig(
      const std::string& reason,
      const cfg::SwitchConfig& newConfig);
  /**
   * Dry-run newConfig against the current switch state. Runs the same config
   * to state translation and state update validation as applyConfig(), but
   * never publishes the resulting state, programs routes or records the
   * config. Throws FbossError if the config would be rejected.
   *
   * applyMethod is how the config will be applied. A change that cannot be
   * made on a running agent is accepted if applyMethod is disruptive enough
   * for it, and otherwise throws RestartRequiredError with the method it
   * needs. Validation stops at the first such change, so checks that would
   * have run after it do not run.
   */
  void validateConfig(
      const cfg::SwitchConfig& newConfig,
      thrift::ConfigApplyMethod applyMethod);

  /*
   * Get config applied information which include last config applied time(ms).
   * NOTE: If no config has ever been applied, the default timestamp is 0.
   */
  ConfigAppliedInfo getConfigAppliedInfo() const {
    return *configAppliedInfo_.rlock();
  }

  /*
   * Registers an observer of all state updates. An observer will be notified of
   * all state updates that occur and all classes that care about state updates
   * should register using this api.
   *
   * The only required method for observers is stateUpdated and observers can
   * count on this always being called from the update thread.
   */
  void registerStateObserver(StateObserver* observer, const std::string& name)
      override;
  void unregisterStateObserver(StateObserver* observer) override;

  /*
   * Signal to the switch that initial config is applied.
   * The switch may then use this to start certain functions
   * which make sense only after the initial config has been
   * applied. As an example it makes sense to start the packet
   * receive only after applying the initial config, else in
   * case of a warm boot this causes us to receive packets tagged
   * with a vlan which software switch state does not even know
   * about. OTOH in case of a cold boot it causes host entries
   * to be created in the wrong (default VLAN).
   */
  void initialConfigApplied(
      const std::chrono::steady_clock::time_point& startTime);

  std::shared_ptr<SwitchState> reconcileRemoteInterfaceRoutesOnWarmboot(
      const std::shared_ptr<SwitchState>& state);

  /*
   * Get the SwitchStats for the current thread.
   *
   * This object should only be used from the current thread.  It should never
   * be stored and used in other threads.
   */
  SwitchStats* stats() {
    SwitchStats* s = stats_.get();
    if (s) {
      return s;
    }
    return createSwitchStats();
  }

  /**
   * Get all SwitchStats for all threads
   */
  AllThreadsSwitchStats getAllThreadsSwitchStats() {
    return stats_.accessAllThreads();
  }

  /*
   * Construct and destroy a client to dump packets to the packet distribution
   * service.
   */
  void constructPushClient(uint16_t port);

  void destroyPushClient();

  /*
   * Send a kill message to the distribution process.
   */
  void killDistributionProcess();

  /*
   * Check if the passed in state is valid.
   * For now we just check for the new port speeds being valid.
   * This could be extended as needed
   */
  bool isValidStateUpdate(const StateDelta& delta, SwitchStats* stats = nullptr)
      const;

  /*
   * Same check, but leaves the resource accounting untouched. For callers that
   * validate inside an update function, since applyUpdate validates again.
   */
  bool isValidStateUpdateDryRun(
      const StateDelta& delta,
      SwitchStats* stats = nullptr) const;

  /*
   * Get the PortStats for the specified port.
   *
   * Note that this returns a thread-local object specific to the current
   * thread.
   */
  PortStats* portStats(PortID port);

  /*
   * Get the InterfaceStats for the specified interface.
   *
   * Note that this returns a thread-local object specific to the current
   * thread.
   */
  InterfaceStats* interfaceStats(InterfaceID intf);

  /*
   * Get Internal PhyInfo
   */
  std::map<PortID, const phy::PhyInfo> getIPhyInfo(
      std::vector<PortID>& portIDs);

  /*
   * Get PortStatus for all the ports.
   */
  std::map<int32_t, PortStatus> getPortStatus();

  /*
   * Get PortStatus of the specified port.
   */
  PortStatus getPortStatus(PortID port);

  /*
   * Get Product Information.
   */
  void getProductInfo(ProductInfo& productInfo) const;

  /*
   * Get Platform Type information.
   */
  PlatformType getPlatformType() const;

  /*
   * Get Platform data supportsAddRemovePort
   */
  bool getPlatformSupportsAddRemovePort() const;

  /*
   * Get the PortStats for the ingress port of this packet.
   */
  PortStats* portStats(const RxPacket* pkt);
  PortStats* portStats(const std::unique_ptr<RxPacket>& pkt) {
    return portStats(pkt.get());
  }

  /*
   * Get the EventBase for the background thread
   */
  FbossEventBase* getBackgroundEvb() {
    return &backgroundEventBase_;
  }

  /*
   * Get the EventBase over which LacpController and LacpMachines should execute
   */
  FbossEventBase* getLacpEvb() {
    return &lacpEventBase_;
  }

  /*
   * Get the EventBase for the update thread
   */
  FbossEventBase* getUpdateEvb() {
    return &updateEventBase_;
  }

  /*
   * Get the EventBase for Arp/Ndp Cache
   */
  FbossEventBase* getNeighborCacheEvb() {
    return &neighborCacheEventBase_;
  }

  /**
   * Do the packet received callback, and throw exception if there is an error
   * in the handling of packet.
   *
   * This is usually not called for regular packets. Useful for testing. For
   * normal calls, see packetReceived() callback from HwSwitch.
   */
  void packetReceivedThrowExceptionOnError(std::unique_ptr<RxPacket> pkt);

  // HwSwitchCallback methods
  void packetReceived(std::unique_ptr<RxPacket> pkt) noexcept override;
  void linkStateChanged(
      PortID port,
      bool up,
      cfg::PortType portType,
      std::optional<phy::LinkFaultStatus> iPhyFaultStatus = std::nullopt,
      std::optional<AggregatePortID> aggPortId = std::nullopt) override;
  void linkActiveStateChangedOrFwIsolated(
      const std::map<PortID, bool>& port2IsActive,
      bool fwIsolated,
      const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate) override;
  void linkAdminStateChangedByFw(
      const std::vector<int32_t>& fwDisabledPortIds) override;
  void linkConnectivityChanged(
      const std::map<PortID, multiswitch::FabricConnectivityDelta>&
          port2OldAndNewConnectivity) override;
  void switchReachabilityChanged(
      const SwitchID switchId,
      const std::map<SwitchID, std::set<PortID>>& switchReachabilityInfo)
      override;
  void pfcWatchdogStateChanged(
      const PortID& portId,
      const bool deadlockDetected) override;
  void l2LearningUpdateReceived(
      L2Entry l2Entry,
      L2EntryUpdateType l2EntryUpdateType) override;
  void exitFatal() const noexcept override;

  uint32_t getEthernetHeaderSize(bool tagged) const;

  /*
   * Allocate a new TxPacket.
   */
  std::unique_ptr<TxPacket> allocatePacket(uint32_t size) const;

  /**
   * Allocate a TxPacket, which is used to send out through HW
   *
   * The caller of the function provides the minimum size of space needed
   * in the packet to store L3 packet. The function adds L2 header size and
   * also makes sure packet size meeting minimum packet size.
   *
   * The IOBuf returned through the packet will have some headroom reserved
   * already which can be used to write L2 header. The caller is expected
   * to write the L3 contents starting from writableTail().
   *
   * @param l3Len L3 packet size
   & @param tagged VLAN tagged packet
   * @return the unique pointer to a tx packet
   */
  std::unique_ptr<TxPacket> allocateL3TxPacket(uint32_t l3Len, bool tagged);

  /**
   * All FBOSS Network Control packets should use this API to send out
   */
  bool sendNetworkControlPacketAsync(
      std::unique_ptr<TxPacket> pkt,
      std::optional<PortDescriptor> portDescriptor) noexcept;

  /*
   * Pipeline bypass if portDescriptor it set.
   * Pipeline lookup otherwise.
   *
   * VOQ switches will use pipeline bypass (no VLANs, no bcast domain).
   * NPU switches will use pipeline bypass or pipeline lookup.
   *
   * Egress queue to send the packet out from can be set for pipeline bypass.
   */
  bool sendPacketAsync(
      std::unique_ptr<TxPacket> pkt,
      std::optional<PortDescriptor> portDescriptor = std::nullopt,
      std::optional<uint8_t> queueId = std::nullopt) noexcept;

  bool sendPacketOutOfPortAsync(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt) noexcept;

  bool sendPacketOutOfPortAsync(
      std::unique_ptr<TxPacket> pkt,
      AggregatePortID aggPortID,
      std::optional<uint8_t> queue = std::nullopt) noexcept;

  bool sendPacketOutOfPortSyncForPktType(
      std::unique_ptr<TxPacket> pkt,
      const PortID& portID,
      PacketType packetType) noexcept;

  /*
   * Send a packet to HwSwitch using thrift stream
   */
  bool sendPacketOutViaThriftStream(
      std::unique_ptr<TxPacket> pkt,
      SwitchID switchId,
      std::optional<PortID> portID,
      std::optional<uint8_t> queue = std::nullopt,
      std::optional<PacketType> packetType = std::nullopt) noexcept;
  /*
   * Send a packet, using switching logic to send it out the correct port(s)
   * for the specified VLAN and destination MAC.
   */
  // TODO Migrate all callsites to explicitly pass switchIDs
  bool sendPacketSwitchedAsync(
      std::unique_ptr<TxPacket> pkt,
      const LocalSwitchIDs& switchIds = {}) noexcept;

  /**
   * Send out L3 packet through HW
   *
   * The L3 packet is supposed to be stored starting from pkt->buf()->data(),
   * whose length is provided through pkt->buf()->length().
   *
   * The caller of the function has to make sure the IOBuf in the packet has
   * enough headroom (EthHdr::SIZE) to store the L2 header. Also the
   * IOBuf is bigger enough to hold a minimum size of packet (68). The packet
   * allocated by allocateL3TxPacket() is guaranteed for the above requirements.
   * If any of the above reuqirements is not met, the packet will be dropped.
   *
   * The function will prepend the L2 header to the L3 packet before it is
   * sent out. All (ipv6) link-local unicast and multicast packets are forwarded
   * onto VLAN corresponding to Host interface from which it is received.
   * Link-local communication doesn't go through L3 table lookups, hence it will
   * work regardless of L3 lookup tables state.
   *
   * @param pkt The packet that has L3 packet stored to send out
   */
  void sendL3Packet(std::unique_ptr<TxPacket> pkt, InterfaceID ifID) noexcept;

  /**
   * method to send out a packet from HW to host.
   *
   * @return true The packet is sent to host
   *         false The packet is dropped due to errors
   */
  bool sendPacketToHost(InterfaceID dstIfID, std::unique_ptr<RxPacket> pkt);

  /**
   * Get the ArpHandler object.
   *
   * The ArpHandler returned is owned by the SwSwitch, and is only valid as
   * long as the SwSwitch object.
   */
  ArpHandler* getArpHandler() {
    return arp_.get();
  }

  /**
   * Get the IPv6Handler object.
   *
   * The IPv6Handler returned is owned by the SwSwitch, and is only valid as
   * long as the SwSwitch object.
   */
  IPv6Handler* getIPv6Handler() {
    return ipv6_.get();
  }

  /**
   * Get the NeighborUpdater object.
   *
   * The NeighborUpdater returned is owned by the SwSwitch, and is only valid as
   * long as the SwSwitch object.
   */
  NeighborUpdater* getNeighborUpdater() {
    return nUpdater_.get();
  }

  /*
   * Get the PktCaptureManager object.
   */
  PktCaptureManager* getCaptureMgr() {
    return pcapMgr_.get();
  }

  PacketObservers* getPacketObservers() {
    return pktObservers_.get();
  }

  L2LearnEventObservers* getL2LearnEventObservers() {
    return l2LearnEventObservers_.get();
  }

  /*
   * Get the LldpManager object
   */
  LldpManager* getLldpMgr() {
    return lldpManager_.get();
  }
#if FOLLY_HAS_COROUTINES
  /*
   *
   */
  MKAServiceManager* getMKAServiceMgr() {
    return mkaServiceManager_.get();
  }

  PacketStreamHandler* getPacketStreamHandler() const {
    return packetStreamHandler_;
  }
#endif
  void setPacketStreamHandler(PacketStreamHandler* handler);
  /*
   * Get the PacketLogger object
   */
  PacketLogger* getPacketLogger() {
    return packetLogger_.get();
  }

  /*
   * Get the RouteUpdateLogger object
   */
  RouteUpdateLogger* getRouteUpdateLogger() {
    return routeUpdateLogger_.get();
  }

  LinkAggregationManager* getLagManager() {
    return lagManager_.get();
  }

  ResolvedNexthopMonitor* getResolvedNexthopMonitor() {
    return resolvedNexthopMonitor_.get();
  }

  LookupClassUpdater* getLookupClassUpdater() {
    return lookupClassUpdater_.get();
  }

  LookupClassRouteUpdater* getLookupClassRouteUpdater() {
    return lookupClassRouteUpdater_.get();
  }

  FabricLinkMonitoringManager* getFabricLinkMonitoringManager() {
    return fabricLinkMonitoringManager_.get();
  }

  CpuLatencyManager* getCpuLatencyManager() {
    return cpuLatencyManager_.get();
  }

  const EcmpResourceManager* getEcmpResourceManager() const {
    return ecmpResourceManager_.get();
  }

  /*
   * RIB and switch state need to be kept in sync,
   * so only expose const/non write access to rib
   * All writes to rib must go through SwSwitch or
   * through the well defined RouteUpdateWrapper
   * abstraction
   */
  RoutingInformationBase* getRib() const {
    return rib_.get();
  }

  SwSwitchRouteUpdateWrapper getRouteUpdater() {
    return SwSwitchRouteUpdateWrapper(this, rib_.get());
  }
  const DsfSubscriber* getDsfSubscriber() const {
    return dsfSubscriber_.get();
  }

  /*
   * Gets the flags the SwSwitch was initialized with.
   */
  SwitchFlags getFlags() const {
    return flags_;
  }

  /*
   * Allow hardware to perform any cleanup needed to gracefully restart the
   * agent before we exit application.
   */
  void gracefulExit();

  BootType getBootType() const {
    return bootType_;
  }

  /*
   * Helper function for enabling route update logging
   */
  void logRouteUpdates(
      const std::string& addr,
      uint8_t mask,
      const std::string& identifier);

  void stopLoggingRouteUpdates(const std::string& identifier);

  /*
   * Register a function that will send notifications about the port status.
   * Only one port status listener is supported, and calling this multiple
   * times will overwrite the current listener.
   */
  void registerNeighborListener(
      std::function<void(
          const std::vector<std::string>& added,
          const std::vector<std::string>& deleted)> callback);

  void invokeNeighborListener(
      const std::vector<std::string>& added,
      const std::vector<std::string>& deleted);

  std::string getConfigStr() const;
  cfg::SwitchConfig getConfig() const;
  cfg::AgentConfig getAgentConfig() const;
  const agent_info::AgentInfo& getAgentInfo() const {
    return agentInfo_;
  }

  AdminDistance clientIdToAdminDistance(int clientId) const;

  /*
   * Clear PortStats of the specified port.
   */
  void clearPortStats(const std::unique_ptr<std::vector<int32_t>>& ports);

  std::vector<phy::PrbsLaneStats> getPortAsicPrbsStats(PortID portId);
  void clearPortAsicPrbsStats(PortID portId);

  SwitchRunState getSwitchRunState() const;

  std::vector<prbs::PrbsPolynomial> getPortPrbsPolynomials(PortID /* portId */);
  prbs::InterfacePrbsState getPortPrbsState(PortID /* portId */);

  template <typename AddressT>
  std::shared_ptr<Route<AddressT>> longestMatch(
      std::shared_ptr<SwitchState> state,
      const AddressT& address,
      RouterID vrf);

  ResolvedNexthopProbeScheduler* getResolvedNexthopProbeScheduler() {
    return resolvedNexthopProbeScheduler_.get();
  }

  void setFibSyncTimeForClient(ClientID clientId);

  std::optional<cfg::SdkVersion> getSdkVersion() {
    return sdkVersion_;
  }

  /*
   * Public use only in tests
   */
  void stop(bool isGracefulStop = true, bool revertToMinAlpmState = false);

  void publishPhyInfoSnapshots(PortID portID) const;

  bool sendArpRequestHelper(
      std::shared_ptr<Interface> intf,
      std::shared_ptr<SwitchState> state,
      folly::IPAddressV4 source,
      folly::IPAddressV4 target);

  bool sendNdpSolicitationHelper(
      std::shared_ptr<Interface> intf,
      std::shared_ptr<SwitchState> state,
      const folly::IPAddressV6& target);

  TeFlowStats getTeFlowStats();

  VlanID getVlanIDHelper(
      std::optional<VlanID> vlanID,
      cfg::InterfaceType intfType = cfg::InterfaceType::VLAN) const;

  // Send neighbor solicitation for interfaces with desiredPeerAddressIPv6
  // configured
  void sendNeighborSolicitationForConfiguredInterfaces(
      const std::string& reason,
      const std::optional<folly::IPAddressV6>& targetIP = std::nullopt);

  InterfaceID getInterfaceIDForAggregatePort(
      AggregatePortID aggregatePortID) const;

  void sentArpRequest(
      const std::shared_ptr<Interface>& intf,
      folly::IPAddressV4 ip);

  void sentNeighborSolicitation(
      const std::shared_ptr<Interface>& intf,
      const folly::IPAddressV6& target);

  const SwitchInfoTable& getSwitchInfoTable() const {
    return switchInfoTable_;
  }

  HwAsicTable* getHwAsicTable() const {
    return hwAsicTable_.get();
  }
  bool fsdbStatPublishReady() const;
  bool fsdbStatePublishReady() const;

  // Helper function to clone a new SwitchState to modify the original
  // TransceiverMap if there's a change.
  // This can be removed after deleting qsfp cache
  static std::shared_ptr<SwitchState> modifyTransceivers(
      const std::shared_ptr<SwitchState>& state,
      const std::unordered_map<TransceiverID, TransceiverInfo>& currentTcvrs,
      const PlatformMapping* platformMapping,
      const SwitchIdScopeResolver* scopeResolver);

  folly::MacAddress getLocalMac(SwitchID switchId) const;

  TransceiverIdxThrift getTransceiverIdxThrift(PortID port) const;

  std::optional<uint32_t> getHwLogicalPortId(PortID port) const;

  const AgentDirectoryUtil* getDirUtil() const;

  MultiSwitchPacketStreamMap* getPacketStreamMap() {
    return packetStreamMap_.get();
  }

  void updateDsfSubscriberState(
      const std::string& remoteEndpoint,
      fsdb::FsdbSubscriptionState oldState,
      fsdb::FsdbSubscriptionState newState);

  // used by tests to avoid having to reload config from disk
  void setConfig(std::unique_ptr<AgentConfig> config);

  bool needL2EntryForNeighbor() const;

  SwSwitchWarmBootHelper* getWarmBootHelper() {
    return swSwitchWarmbootHelper_.get();
  }

  void updateHwSwitchStats(
      uint16_t switchIndex,
      multiswitch::HwSwitchStats hwStats);

  // Returns a copy of hwswitch exported stats.
  // To be used only in tests as copy is expensive.
  multiswitch::HwSwitchStats getHwSwitchStatsExpensive(
      uint16_t switchIndex) const;
  std::map<uint16_t, multiswitch::HwSwitchStats> getHwSwitchStatsExpensive()
      const;
  std::map<std::string, HwSwitchCounter> getRouteCounters() const;

  FabricReachabilityStats getFabricReachabilityStats();
  void setPortsDownForSwitch(SwitchID switchId);

  std::map<PortID, HwPortStats> getHwPortStats(
      std::vector<PortID> portId) const;
  std::map<InterfaceID, HwRouterInterfaceStats> getHwRouterInterfaceStats(
      const std::vector<InterfaceID>& intfIds) const;
  void getAllHwSysPortStats(
      std::map<std::string, HwSysPortStats>& hwSysPortStats) const;
  std::map<SystemPortID, HwSysPortStats> getHwSysPortStats(
      const std::vector<SystemPortID>& portId) const;
  void getAllHwPortStats(std::map<std::string, HwPortStats>& hwPortStats) const;
  void getAllCpuPortStats(std::map<int, CpuPortStats>& hwCpuPortStats) const;
  bool isRunModeMultiSwitch() const;
  bool isRunModeMonolithic() const {
    return !isRunModeMultiSwitch();
  }
  MonolithicHwSwitchHandler* getMonolithicHwSwitchHandler() const;
  int16_t getSwitchIndexForInterface(const std::string& interface) const;
  const folly::
      ConcurrentHashMap<std::pair<RouterID, folly::IPAddress>, InterfaceID>&
      getAddrToLocalIntfMap() const {
    return addrToLocalIntf_;
  }
  const std::map<SwitchID, switch_reachability::SwitchReachability>
  getSwitchReachability() const {
    return *hwSwitchReachability_.rlock();
  }
  void rxPacketReceived(std::unique_ptr<SwRxPacket> pkt);

  std::optional<VlanID> getVlanIDForTx(const InterfaceID& ifID) const;

  template <typename VlanOrIntfT>
  std::optional<VlanID> getVlanIDForTx(
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) const;
  bool hasQualifiedConfiguredDesiredPeer(const InterfaceID& intfId);
  const ResourceAccountant* getResourceAccountant() const;

 private:
  void initAgentInfo();
  void createAndProbeTunManager();
  void initializeTunManager(bool useBlocking);

  std::optional<folly::MacAddress> getSourceMac(
      const std::shared_ptr<Interface>& intf) const;
  void updateStateBlockingImpl(
      folly::StringPiece name,
      StateUpdateFn fn,
      int stateUpdateBehavior,
      std::optional<StateDeltaApplication> deltaApplicationBehavior);

  /*
   * Applied state corresponds to what was successfully applied
   * to h/w
   */
  std::shared_ptr<SwitchState> getAppliedState() const {
    std::unique_lock guard(stateLock_);
    return appliedStateDontUseDirectly_;
  }

  using StateUpdateList =
      folly::IntrusiveList<StateUpdate, &StateUpdate::listHook_>;

  // Forbidden copy constructor and assignment operator
  SwSwitch(SwSwitch const&) = delete;
  SwSwitch& operator=(SwSwitch const&) = delete;

  /*
   * Update the current states.
   */
  void setStateInternal(std::shared_ptr<SwitchState> newAppliedState);

  void updatePortInfo();
  void updateRouteStats();
  void updateTeFlowStats();
  void updateFlowletStats();
  void updateFabricLinkMonitoringStats();
  void setSwitchRunState(SwitchRunState desiredState);
  SwitchStats* createSwitchStats();

  PortDescriptor getPortFromPkt(const RxPacket* pkt) const;

  void handlePacket(std::unique_ptr<RxPacket> pkt);

  bool isFabricLinkMonitoringPacket(
      const RxPacket& pkt,
      const std::shared_ptr<SwitchState>& state) const;
  template <typename VlanOrIntfT>
  void handlePacketImpl(
      std::unique_ptr<RxPacket> pkt,
      const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

  void updatePtpTcCounter();
  static void handlePendingUpdatesHelper(SwSwitch* sw);
  void handlePendingUpdates();
  std::pair<std::shared_ptr<SwitchState>, std::shared_ptr<SwitchState>>
  applyUpdate(
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState,
      bool isTransaction,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior);

  void startThreads();
  void stopThreads();
  void threadLoop(folly::StringPiece name, folly::EventBase* eventBase);

  /*
   * Helpers to add/remove state observers. These should only be
   * called from the update thread, if the update thread is running.
   */
  bool stateObserverRegistered(StateObserver* observer);
  void addStateObserver(StateObserver* observer, const std::string& name);
  void removeStateObserver(StateObserver* observer);

  /*
   * File where switch state gets dumped on exit
   */
  std::string getSwitchStateFile() const;

  void dumpBadStateUpdate(
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState) const;

  /*
   * Notifies all the observers that a state update occurred.
   */
  void notifyStateObservers(const StateDelta& delta);

  /*
   * Reconstruct state modifier from initial switch state.
   */
  std::vector<StateDelta> reconstructStateFromManagers(
      const std::shared_ptr<SwitchState>& emptyState,
      const std::shared_ptr<SwitchState>& initialState);

  // On warm boot, applies the RIB to the state if RIB reconstruct merged or
  // backfilled NextHop ids, so the state carries the RIB's ids.
  std::shared_ptr<SwitchState> syncNextHopIDsToFIB(
      const std::shared_ptr<SwitchState>& state);

  void logLinkStateEvent(PortID port, bool up);

  void logSwitchRunStateChange(
      SwitchRunState oldState,
      SwitchRunState newState);

  void onSwitchRunStateChange(SwitchRunState newState);

  uint64_t fsdbPublishQueueLength() const;

  // Sets the counter that tracks port status
  void setPortStatusCounter(PortID port, bool up);
  void setPortActiveStatusCounter(PortID port, bool isActive);

  void updateConfigAppliedInfo();

  std::shared_ptr<SwitchState> stateChanged(
      const StateDelta& delta,
      bool transaction,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior)
      const;

  std::shared_ptr<SwitchState> stateChanged(
      const std::vector<StateDelta>& delta,
      bool transaction,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior)
      const;

  template <typename FsdbFunc>
  void runFsdbSyncFunction(FsdbFunc&& fn);

  void storeWarmBootState(const state::WarmbootState& state);

  std::unique_ptr<AgentConfig> loadConfig();

  void applyConfigImpl(
      const std::string& reason,
      const cfg::SwitchConfig& newConfig);

  void setConfigImpl(std::unique_ptr<AgentConfig> config);

  std::shared_ptr<SwitchState> preInit(
      SwitchFlags flags = SwitchFlags::DEFAULT);

  void postInit();

  void initLldpManager();

  void initFabricLinkMonitoringManager();
  void initCpuLatencyManager();

  void publishBootTypeStats();

  void initThreadHeartbeats();

  void startHeartbeatWatchdog();

  void updateMultiSwitchGlobalFb303Stats();

  void stopHwSwitchHandler();

  void packetRxThread();

  // TODO: To be removed once switchWatermarkStats is available in prod
  HwBufferPoolStats getBufferPoolStatsFromSwitchWatermarkStats();

  void updateAddrToLocalIntf(const StateDelta& delta);

  void validateSwitchReachabilityInformation(
      const SwitchID& switchId,
      const std::map<SwitchID, std::set<PortID>>& switchReachabilityInfo);
#if FOLLY_HAS_COROUTINES
  using BoundedRxPktQueue = folly::coro::BoundedQueue<
      std::unique_ptr<SwRxPacket>,
      false /*SingleProducer*/,
      true /* SingleConsumer*/>;
  class RxPacketHandlerQueues {
   public:
    folly::coro::Task<void> enqueue(
        std::unique_ptr<SwRxPacket> pkt,
        SwitchStats* stats) {
      auto cosQueue = pkt->cosQueue();
      if (!cosQueue || CpuCosQueueId(*cosQueue) == CpuCosQueueId::HIPRI) {
        co_await hiPriQueue_.enqueue(std::move(pkt));
        stats->hiPriPktsReceived();
      } else if (CpuCosQueueId(*cosQueue) == CpuCosQueueId::MIDPRI) {
        midPriQueue_.try_enqueue(std::move(pkt)) ? stats->midPriPktsReceived()
                                                 : stats->midPriPktsDropped();
      } else {
        loPriQueue_.try_enqueue(std::move(pkt)) ? stats->loPriPktsReceived()
                                                : stats->loPriPktsDropped();
      }
      co_return;
    }
    bool hasPacketsToProcess() {
      return hiPriQueue_.size() || midPriQueue_.size() || loPriQueue_.size();
    }
    BoundedRxPktQueue& getHiPriRxPktQueue() {
      return hiPriQueue_;
    }
    BoundedRxPktQueue& getMidPriRxPktQueue() {
      return midPriQueue_;
    }
    BoundedRxPktQueue& getLoPriRxPktQueue() {
      return loPriQueue_;
    }

   private:
    BoundedRxPktQueue hiPriQueue_{kHiPriorityBufferSize};
    BoundedRxPktQueue midPriQueue_{kMidPriorityBufferSize};
    BoundedRxPktQueue loPriQueue_{kLowPriorityBufferSize};
  };
#endif

  std::optional<cfg::SdkVersion> sdkVersion_;
  std::unique_ptr<MultiHwSwitchHandler> multiHwSwitchHandler_;
  const AgentDirectoryUtil* agentDirUtil_;
  bool supportsAddRemovePort_;
  const std::unique_ptr<PlatformProductInfo> platformProductInfo_;
  std::atomic<SwitchRunState> runState_{SwitchRunState::UNINITIALIZED};
  folly::ThreadLocalPtr<SwitchStats, SwSwitch> stats_;
  /**
   * The object to sync the interfaces to the system. This pointer could
   * be nullptr if interface sync is not enabled during init()
   */
  std::unique_ptr<TunManager> tunMgr_;

  /*
   * A list of pending state updates to be applied.
   */
  folly::SpinLock pendingUpdatesLock_;
  StateUpdateList pendingUpdates_;

  /*
   * The current switch state represented as :  appliedState,
   * as in  what is actually applied in the hardware.
   *
   *
   * BEWARE: You generally shouldn't access these states directly, even
   * internally within SwSwitch private methods.  These should only be accessed
   * while holding stateLock_.
   *
   * You almost certainly should call getAppliedState() setStateInternal()
   * instead of directly accessing appliedState
   *
   * This intentionally has an awkward name so people won't forget and try to
   * directly access this pointer.
   */
  std::shared_ptr<SwitchState> appliedStateDontUseDirectly_;
  mutable folly::SpinLock stateLock_;

  /*
   * A thread for performing various background tasks.
   */
  std::unique_ptr<std::thread> backgroundThread_;
  FbossEventBase backgroundEventBase_{"SwSwitchBackgroundEventBase"};
  std::shared_ptr<ThreadHeartbeat> bgThreadHeartbeat_;

  /*
   * A thread for processing packets received from
   * host (linux) that may need to be sent out of
   * ASIC front panel ports
   */
  std::unique_ptr<std::thread> packetTxThread_;
  FbossEventBase packetTxEventBase_{"SwSwitchPacketTxEventBase"};
  std::shared_ptr<ThreadHeartbeat> packetTxThreadHeartbeat_;

  /*
   * A thread for sending packets to the distribution process
   */
  std::shared_ptr<std::thread> pcapDistributionThread_;
  FbossEventBase pcapDistributionEventBase_{
      "SwSwitchPcapDistributionEventBase"};

  /*
   * A thread for processing SwitchState updates.
   */
  std::unique_ptr<std::thread> updateThread_;
  FbossEventBase updateEventBase_{"SwSwitchUpdateEventBase"};
  std::shared_ptr<ThreadHeartbeat> updThreadHeartbeat_;

  /*
   * A thread dedicated to LACP processing.
   */
  std::unique_ptr<std::thread> lacpThread_;
  FbossEventBase lacpEventBase_{"SwSwitchLacpEventBase"};
  std::shared_ptr<ThreadHeartbeat> lacpThreadHeartbeat_;

  /*
   * A thread dedicated to Arp and Ndp cache entry processing.
   */
  std::unique_ptr<std::thread> neighborCacheThread_;
  FbossEventBase neighborCacheEventBase_{"SwSwitchNeighborCacheEventBase"};
  std::shared_ptr<ThreadHeartbeat> neighborCacheThreadHeartbeat_;

  /*
   * A thread for processing rx packets from thrift stream
   */
  std::unique_ptr<std::thread> packetRxThread_;
  folly::EventBase packetRxEventBase_;
  std::shared_ptr<ThreadHeartbeat> packetRxThreadHeartbeat_;

  /*
   * A thread dedicated to monitor above thread heartbeats
   */
  std::unique_ptr<ThreadHeartbeatWatchdog> heartbeatWatchdog_;

  /*
   * A callback for listening to neighbors coming and going.
   */
  std::mutex neighborListenerMutex_;
  std::function<void(
      const std::vector<std::string>& added,
      const std::vector<std::string>& deleted)>
      neighborListener_{nullptr};

  /*
   * The list of classes to notify on a state update. This container should only
   * be accessed/modified from the update thread. This removes the need for
   * locking when we access the container during a state update.
   */
  std::map<StateObserver*, std::string> stateObservers_;
  std::unique_ptr<PacketObservers> pktObservers_;
  std::unique_ptr<L2LearnEventObservers> l2LearnEventObservers_;

  std::unique_ptr<ArpHandler> arp_;
  std::unique_ptr<IPv4Handler> ipv4_;
  std::unique_ptr<IPv6Handler> ipv6_;
  std::unique_ptr<NeighborUpdater> nUpdater_;
  std::unique_ptr<PktCaptureManager> pcapMgr_;
  std::unique_ptr<MirrorManager> mirrorManager_;
  std::unique_ptr<TamManager> tamManager_;
  std::unique_ptr<MPLSHandler> mplsHandler_;
  std::unique_ptr<PacketLogger> packetLogger_;
  std::unique_ptr<RouteUpdateLogger> routeUpdateLogger_;
  std::unique_ptr<StateDeltaLogger> stateDeltaLogger_;
  std::unique_ptr<LinkAggregationManager> lagManager_;
  std::unique_ptr<ResolvedNexthopMonitor> resolvedNexthopMonitor_;
  std::unique_ptr<ResolvedNexthopProbeScheduler> resolvedNexthopProbeScheduler_;
  std::unique_ptr<RoutingInformationBase> rib_{nullptr};

  BootType bootType_{BootType::UNINITIALIZED};
  std::unique_ptr<LldpManager> lldpManager_;
  std::unique_ptr<RemoteNeighborUpdater> remoteNeighborUpdater_;
  std::unique_ptr<PortUpdateHandler> portUpdateHandler_;
  SwitchFlags flags_{SwitchFlags::DEFAULT};

  std::unique_ptr<LookupClassUpdater> lookupClassUpdater_;
  std::unique_ptr<LookupClassRouteUpdater> lookupClassRouteUpdater_;
  std::unique_ptr<StaticL2ForNeighborObserver> staticL2ForNeighborObserver_;
  std::unique_ptr<MySidNeighborObserver> mySidNeighborObserver_;
  std::unique_ptr<MacTableManager> macTableManager_;
#if FOLLY_HAS_COROUTINES
  std::unique_ptr<MKAServiceManager> mkaServiceManager_;
  PacketStreamHandler* packetStreamHandler_{nullptr};
#endif

  static constexpr auto kIphySnapshotIntervalSeconds = 1;

  std::unique_ptr<PhySnapshotManager> phySnapshotManager_;
  std::unique_ptr<AclNexthopHandler> aclNexthopHandler_;
  folly::Synchronized<std::unique_ptr<FsdbSyncer>> fsdbSyncer_;
  std::unique_ptr<TeFlowNexthopHandler> teFlowNextHopHandler_;
  std::unique_ptr<DsfSubscriber> dsfSubscriber_;
  std::shared_ptr<ThreadHeartbeat> dsfSubscriberReconnectThreadHeartbeat_;
  std::shared_ptr<ThreadHeartbeat> dsfSubscriberStreamThreadHeartbeat_;
  SwitchInfoTable switchInfoTable_;
  std::unique_ptr<PlatformMapping> platformMapping_;
  std::unique_ptr<HwAsicTable> hwAsicTable_;
  std::unique_ptr<SwitchIdScopeResolver> scopeResolver_;
  std::unique_ptr<SwitchStatsObserver> switchStatsObserver_;
  std::unique_ptr<EcmpResourceManager> ecmpResourceManager_;
  std::unique_ptr<ShelManager> shelManager_;
  std::unique_ptr<PbrAclManager> pbrAclManager_;
  std::unique_ptr<FabricLinkMonitoringManager> fabricLinkMonitoringManager_;
  bool rxPacketTypeSupported_{false};
  std::unique_ptr<CpuLatencyManager> cpuLatencyManager_;
  std::unique_ptr<StateUpdateValidator> stateUpdateValidator_;

  folly::Synchronized<ConfigAppliedInfo> configAppliedInfo_;
  std::optional<std::chrono::time_point<std::chrono::steady_clock>>
      publishedStatsToFsdbAt_;
  // updateStats() runs every FLAGS_update_stats_interval_s (1s) but phy info is
  // only recollected every FLAGS_update_phy_info_interval_s (10s), so iphy
  // PhyState is published at the collection interval instead of every tick.
  std::optional<std::chrono::time_point<std::chrono::steady_clock>>
      publishedIPhyStatesToFsdbAt_;
  std::unique_ptr<MultiSwitchPacketStreamMap> packetStreamMap_;
  std::unique_ptr<SwSwitchWarmBootHelper> swSwitchWarmbootHelper_;
  std::unique_ptr<HwSwitchThriftClientTable> hwSwitchThriftClientTable_;
  std::unique_ptr<MultiSwitchFb303Stats> multiSwitchFb303Stats_{nullptr};
  std::atomic<std::chrono::time_point<std::chrono::steady_clock>>
      lastPacketRxTime_{std::chrono::steady_clock::time_point::min()};
  folly::Synchronized<std::unique_ptr<AgentConfig>> agentConfig_;
  agent_info::AgentInfo agentInfo_;
  folly::Synchronized<std::map<uint16_t, multiswitch::HwSwitchStats>>
      hwSwitchStats_;
  // Map to lookup local interface address to interface id, for fask look up in
  // rx handling path.
  folly::ConcurrentHashMap<std::pair<RouterID, folly::IPAddress>, InterfaceID>
      addrToLocalIntf_;
  folly::Synchronized<
      std::map<SwitchID, switch_reachability::SwitchReachability>>
      hwSwitchReachability_;
  std::unordered_map<
      SwitchID,
      std::map<SwitchID, std::tuple<std::set<PortID>, uint64_t>>>
      hwReachabilityInfo_;
#if FOLLY_HAS_COROUTINES
  RxPacketHandlerQueues rxPacketHandlerQueues_;
#endif
  std::atomic<bool> packetRxRunning_{false};
  std::condition_variable rxPktThreadCV_;
  std::mutex rxPktMutex_;
};
} // namespace facebook::fboss
