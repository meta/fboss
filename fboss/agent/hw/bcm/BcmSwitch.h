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

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <boost/container/flat_map.hpp>
#include <folly/Synchronized.h>
#include <folly/executors/FunctionScheduler.h>
#include <folly/json/dynamic.h>
#include <gtest/gtest_prod.h>

#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmRxPacket.h"
#include "fboss/agent/hw/bcm/types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/highfreq_types.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"

extern "C" {
#include <bcm/cosq.h>
#include <bcm/error.h>
#include <bcm/l2.h>
#ifdef INCLUDE_PKTIO
#include <bcm/pktio.h>
#include <bcm/pktio_defs.h>
#endif
#include <bcm/port.h>
#include <bcm/rx.h>
#include <bcm/types.h>
}

DECLARE_bool(flexports);
DECLARE_bool(force_init_fp);
DECLARE_bool(flowletSwitchingEnable);

namespace facebook::fboss {

class AclEntry;
class AggregatePort;
class ArpEntry;
class BcmAclTable;
class BcmControlPlane;
class BcmCosManager;
class BcmEgress;
class BcmEgressManager;
class BcmHostKey;
class BcmHostTable;
class BcmIntf;
class BcmIntfTable;
class BcmL3NextHop;
class BcmLabeledHostKey;
class BcmMplsNextHop;
class BcmMultiPathNextHopTable;
class BcmMultiPathNextHopStatsManager;
class BcmNeighborTable;
template <class K, class V>
class BcmNextHopTable;
class BcmPortTable;
class BcmQosPolicyTable;
class BcmRouteTable;
class BcmRouteCounterTableBase;
class BcmRxPacket;
class BcmStatUpdater;
class BcmSwitchEventCallback;
class BcmTeFlowTable;
class BcmTrunkTable;
class BcmUnit;
class BcmWarmBootCache;
class BcmWarmBootHelper;
class BcmRtag7LoadBalancer;
class BcmSflowExporterTable;
class LoadBalancer;
class PacketTraceInfo;
class SflowCollector;
class MockRxPacket;
class Interface;
class Port;
class PortStats;
class QosPolicy;
class Vlan;
class VlanMap;
class Mirror;
class BcmMirror;
class BcmMirrorTable;
class ControlPlane;
class BcmBstStatsMgr;
class BcmLabelMap;
class BcmSwitchSettings;
class BcmMacTable;
class PortQueue;
class BcmQcmManager;
class BcmPtpTcMgr;
class BcmEgressQueueFlexCounterManager;
class TeFlowEntry;
class UnsupportedFeatureManager;
class BcmUdfManager;
class UdfPacketMatcher;
class UdfGroup;
class MultiSwitchSettings;
class SwitchSettings;

/*
 * Virtual interface to BcmSwitch, primarily for mocking/testing
 */
class BcmSwitchIf : public HwSwitch {
 public:
  using HwSwitch::HwSwitch;
  /*
   * Flush internal tables w/o but remain attached to ASIC/Unit
   */
  virtual void resetTables() = 0;

  virtual BcmPlatform* getPlatform() const override = 0;

  virtual std::unique_ptr<PacketTraceInfo> getPacketTrace(
      std::unique_ptr<MockRxPacket> pkt) const = 0;

  virtual bool isRxThreadRunning() const = 0;

  virtual int getUnit() const = 0;

  virtual const BcmPortTable* getPortTable() const = 0;
  virtual BcmPortTable* writablePortTable() const = 0;

  virtual const BcmIntfTable* getIntfTable() const = 0;

  virtual const BcmHostTable* getHostTable() const = 0;

  virtual const BcmEgressManager* getEgressManager() const = 0;

  virtual const BcmNeighborTable* getNeighborTable() const = 0;

  virtual const BcmNextHopTable<BcmHostKey, BcmL3NextHop>* getL3NextHopTable()
      const = 0;

  virtual BcmNextHopTable<BcmHostKey, BcmL3NextHop>* writableL3NextHopTable()
      const = 0;

  virtual const BcmNextHopTable<BcmLabeledHostKey, BcmMplsNextHop>*
  getMplsNextHopTable() const = 0;

  virtual BcmNextHopTable<BcmLabeledHostKey, BcmMplsNextHop>*
  writableMplsNextHopTable() const = 0;

  virtual const BcmMultiPathNextHopTable* getMultiPathNextHopTable() const = 0;

  virtual BcmMultiPathNextHopTable* writableMultiPathNextHopTable() const = 0;

  virtual const BcmMultiPathNextHopStatsManager*
  getMultiPathNextHopStatsManager() const = 0;

  virtual BcmMultiPathNextHopStatsManager*
  writableMultiPathNextHopStatsManager() const = 0;

  virtual const BcmAclTable* getAclTable() const = 0;

  virtual const BcmQosPolicyTable* getQosPolicyTable() const = 0;

  virtual const BcmTeFlowTable* getTeFlowTable() const = 0;

  virtual const BcmUdfManager* getUdfMgr() const = 0;

  virtual const BcmTrunkTable* getTrunkTable() const = 0;

  virtual BcmStatUpdater* getStatUpdater() const = 0;

  virtual BcmControlPlane* getControlPlane() const = 0;

  virtual bcm_if_t getDropEgressId() const = 0;

  virtual bcm_if_t getToCPUEgressId() const = 0;

  virtual BcmCosManager* getCosMgr() const = 0;

  virtual BcmHostTable* writableHostTable() const = 0;

  virtual BcmEgressManager* writableEgressManager() const = 0;

  virtual BcmAclTable* writableAclTable() const = 0;

  virtual BcmTeFlowTable* writableTeFlowTable() const = 0;

  virtual BcmWarmBootCache* getWarmBootCache() const = 0;

  virtual const BcmRouteTable* routeTable() const = 0;

  virtual BcmRouteTable* writableRouteTable() const = 0;

  virtual const BcmRouteCounterTableBase* routeCounterTable() const = 0;

  virtual BcmRouteCounterTableBase* writableRouteCounterTable() const = 0;

  virtual const BcmMirrorTable* getBcmMirrorTable() const = 0;

  virtual BcmMirrorTable* writableBcmMirrorTable() const = 0;

  virtual BcmLabelMap* writableLabelMap() const = 0;

  virtual std::string gatherSdkState() const = 0;

  virtual void dumpState(const std::string& path) const = 0;

  virtual const BcmSwitchSettings* getSwitchSettings() const = 0;
};

/*
 * BcmSwitch is a HwSwitch implementation for systems that use a single
 * Broadcom ASIC.
 */
class BcmSwitch : public BcmSwitchIf {
 public:
  using HwSwitch::FeaturesDesired;
  using HwSwitch::stateChanged;
  /*
   * Construct a new BcmSwitch.
   *
   * With this constructor, BcmSwitch will fully own the BCM SDK.
   * When init() is called, it will initialize the SDK, then find and
   * initialize the first switching ASIC.
   */
  explicit BcmSwitch(
      BcmPlatform* platform,
      uint32_t featuresDesired =
          (FeaturesDesired::PACKET_RX_DESIRED |
           FeaturesDesired::LINKSCAN_DESIRED));

  ~BcmSwitch() override;

  /*
   * Flush tables tracking ASIC state. Without releasing the ASIC/unit for
   * destruction/detaching. Primarily used for testing, where we flush state
   * and then try to recreate it by mimicking a warmboot sequence
   */
  void resetTables() override;

  /*
   * Initialize the BcmSwitch.
   */
  HwInitResult initImpl(
      Callback* callback,
      BootType bootType,
      bool failHwCallsOnWarmboot) override;

  /*
   * Minimal initialization of the BcmSwitch. This is used for BcmReplayTest.
   */
  void minimalInit();

  void runBcmScript(const std::string& filename) const;

  void unregisterCallbacks() override;

  BcmPlatform* getPlatform() const override {
    return platform_;
  }
  BcmMmuState getMmuState() const {
    return mmuState_;
  }
  uint64_t getMMUCellBytes() const {
    return mmuCellBytes_;
  }
  uint64_t getMMUBufferBytes() const {
    return mmuBufferBytes_;
  }

  std::unique_ptr<TxPacket> allocatePacket(uint32_t size) const override;
  bool sendPacketSwitchedAsync(std::unique_ptr<TxPacket> pkt) noexcept override;
  bool sendPacketOutOfPortAsync(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt) noexcept override;

  bool sendPacketSwitchedSync(std::unique_ptr<TxPacket> pkt) noexcept override;
  bool sendPacketOutOfPortSync(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt) noexcept override;
  std::unique_ptr<PacketTraceInfo> getPacketTrace(
      std::unique_ptr<MockRxPacket> pkt) const override;

  bool isRxThreadRunning() const override;

  int getUnit() const override {
    return unit_;
  }

  static bcm_vrf_t getBcmVrfId(RouterID routerId) {
    static_assert(
        sizeof(bcm_vrf_t) == sizeof(RouterID),
        "Size of bcm_vrf_t must equal to size of RouterID");
    return static_cast<bcm_vrf_t>(routerId);
  }
  static RouterID getRouterId(bcm_vrf_t vrf) {
    return RouterID(vrf);
  }
  static bcm_if_t getBcmIntfId(InterfaceID intfId) {
    static_assert(
        sizeof(bcm_if_t) == sizeof(InterfaceID),
        "Size of bcm_if_t must equal to size of InterfaceID");
    return static_cast<bcm_if_t>(intfId);
  }
  static VlanID getVlanId(bcm_vlan_t vlan) {
    return VlanID(vlan);
  }
  const BcmPortTable* getPortTable() const override {
    return portTable_.get();
  }
  BcmPortTable* writablePortTable() const override {
    return portTable_.get();
  }
  const BcmIntfTable* getIntfTable() const override {
    return intfTable_.get();
  }
  const BcmHostTable* getHostTable() const override {
    return hostTable_.get();
  }
  const BcmEgressManager* getEgressManager() const override {
    return egressManager_.get();
  }
  const BcmNeighborTable* getNeighborTable() const override {
    return neighborTable_.get();
  }
  const BcmNextHopTable<BcmHostKey, BcmL3NextHop>* getL3NextHopTable()
      const override {
    return l3NextHopTable_.get();
  }
  BcmNextHopTable<BcmHostKey, BcmL3NextHop>* writableL3NextHopTable()
      const override {
    return l3NextHopTable_.get();
  }
  const BcmNextHopTable<BcmLabeledHostKey, BcmMplsNextHop>*
  getMplsNextHopTable() const override {
    return mplsNextHopTable_.get();
  }
  BcmNextHopTable<BcmLabeledHostKey, BcmMplsNextHop>* writableMplsNextHopTable()
      const override {
    return mplsNextHopTable_.get();
  }

  const BcmMultiPathNextHopTable* getMultiPathNextHopTable() const override {
    return multiPathNextHopTable_.get();
  }
  BcmMultiPathNextHopTable* writableMultiPathNextHopTable() const override {
    return multiPathNextHopTable_.get();
  }

  const BcmMultiPathNextHopStatsManager* getMultiPathNextHopStatsManager()
      const override {
    return multiPathNextHopStatsManager_.get();
  }
  BcmMultiPathNextHopStatsManager* writableMultiPathNextHopStatsManager()
      const override {
    return multiPathNextHopStatsManager_.get();
  }

  const BcmQosPolicyTable* getQosPolicyTable() const override {
    return qosPolicyTable_.get();
  }
  const BcmAclTable* getAclTable() const override {
    return aclTable_.get();
  }
  BcmStatUpdater* getStatUpdater() const override {
    return bcmStatUpdater_.get();
  }
  const BcmTeFlowTable* getTeFlowTable() const override {
    return teFlowTable_.get();
  }
  const BcmTrunkTable* getTrunkTable() const override {
    return trunkTable_.get();
  }

  const BcmSwitchSettings* getSwitchSettings() const override {
    return switchSettings_.get();
  }

  bool isPortUp(PortID port) const override;
  bool portExists(PortID port) const override;

  bcm_if_t getDropEgressId() const override;
  bcm_if_t getToCPUEgressId() const override;

  BcmControlPlane* getControlPlane() const override {
    return controlPlane_.get();
  }

  /*
   * BcmSwitch state as folly::dynamic
   * For now we only dump Host table.
   */
  folly::dynamic toFollyDynamic() const override;

  folly::F14FastMap<std::string, HwPortStats> getPortStats() const override;
  // TODO: wire up BcmTrunkTable::getHwTrunkStats() to populate trunk stats
  std::map<std::string, HwTrunkStats> getTrunkStats() const override {
    return {};
  }
  folly::F14FastMap<std::string, HwRouterInterfaceStats>
  getRouterInterfaceStats() const override {
    return {};
  }
  std::map<std::string, HwSysPortStats> getSysPortStats() const override {
    return {};
  }

  FabricReachabilityStats getFabricReachabilityStats() const override {
    return {};
  }

  CpuPortStats getCpuPortStats() const override;

  uint64_t getDeviceWatermarkBytes() const override;

  TeFlowStats getTeFlowStats() const override;
  HwSwitchDropStats getSwitchDropStats() const override {
    // No HwSwitchDropStats collected
    return HwSwitchDropStats{};
  }

  HwSwitchWatermarkStats getSwitchWatermarkStats() const override;
  HwSwitchPipelineStats getSwitchPipelineStats() const override;
  HwSwitchTemperatureStats getSwitchTemperatureStats() const override;
  HwFlowletStats getHwFlowletStats() const override;
  HwSwitchHardResetStats getHwSwitchHardResetStats() const override;

  HwResourceStats getResourceStats() const override;
  HwSwitchCounterStats getHwSwitchCounterStats() const override;
  std::map<int, cfg::PortState> getSysPortShelState() const override {
    return {};
  }

  std::vector<EcmpDetails> getAllEcmpDetails() const override;

  cfg::SwitchingMode getFwdSwitchingMode(const RouteNextHopEntry&) override;
  /*
   * Wrapper functions to register and unregister a BCM event callbacks.  These
   * just forward the call.
   */
  std::shared_ptr<BcmSwitchEventCallback> registerSwitchEventCallback(
      bcm_switch_event_t eventID,
      std::shared_ptr<BcmSwitchEventCallback> callback);
  void unregisterSwitchEventCallback(bcm_switch_event_t eventID);

  BcmCosManager* getCosMgr() const override {
    return cosManager_.get();
  };

  void fetchL2Table(std::vector<L2EntryThrift>* l2Table, bool sdk = false)
      const override;

  BcmHostTable* writableHostTable() const override {
    return hostTable_.get();
  }
  BcmEgressManager* writableEgressManager() const override {
    return egressManager_.get();
  }
  BcmAclTable* writableAclTable() const override {
    return aclTable_.get();
  }
  BcmTeFlowTable* writableTeFlowTable() const override {
    return teFlowTable_.get();
  }
  BcmWarmBootCache* getWarmBootCache() const override {
    return warmBootCache_.get();
  }

  BcmRouteTable* writableRouteTable() const override {
    return routeTable_.get();
  }
  const BcmRouteTable* routeTable() const override {
    return routeTable_.get();
  }

  const BcmRouteCounterTableBase* routeCounterTable() const override {
    return routeCounterTable_.get();
  }

  BcmRouteCounterTableBase* writableRouteCounterTable() const override {
    return routeCounterTable_.get();
  }

  const BcmMirrorTable* getBcmMirrorTable() const override {
    return mirrorTable_.get();
  }
  BcmMirrorTable* writableBcmMirrorTable() const override {
    return mirrorTable_.get();
  }

  BcmLabelMap* writableLabelMap() const override {
    return labelMap_.get();
  }

  std::string gatherSdkState() const override;

  /**
   * Log the hardware state for the switch
   */
  void dumpState(const std::string& path) const override;

  /**
   * Log the hardware state for the switch
   */
  void dumpDebugState(const std::string& path) const override {
    dumpState(path);
  }

  /**
   * Generate seed for load balancer
   */
  uint32_t generateDeterministicSeed(
      LoadBalancerID loadBalancerID,
      folly::MacAddress mac) const override;
  /**
   * Log the hardware state for the switch
   */
  std::string listObjects(const std::vector<HwObjectType>& types, bool cached)
      const override;

  /**
   * Set the aging timer for MAC address aging
   */
  void setMacAging(std::chrono::seconds agingInterval);

  /*
   * Handle a fatal crash.
   */
  void exitFatal() const override;

  phy::FecMode getPortFECMode(PortID port) const override;

  cfg::PortSpeed getPortMaxSpeed(PortID port) const override;

  bool isValidStateUpdate(const StateDelta& delta) const override;

  bcm_gport_t getCpuGPort() const;
  /*
   * Calls linkStateChanged. Invoked by linkscan thread
   * on BCM ASIC as well as explicitly by tests.
   */
  static void
  linkscanCallback(int unit, bcm_port_t port, bcm_port_info_t* info);

  /*
   * Invoked by BCM SDK when a PFC watchdog deadlock recovery event
   * is initiated, used for logging and tracking such events!
   */
  static int pfcDeadlockRecoveryEventCallback(
      int unit,
      bcm_port_t port,
      bcm_cos_queue_t cosq,
      bcm_cosq_pfc_deadlock_recovery_event_t recovery_state,
      void* userdata);

  BootType getBootType() const override {
    return bootType_;
  }

  BcmBstStatsMgr* getBstStatsMgr() const {
    return bstStatsMgr_.get();
  }

  BcmQcmManager* getBcmQcmMgr() const {
    return qcmManager_.get();
  }

  BcmPtpTcMgr* getPtpTcMgr() const {
    return ptpTcMgr_.get();
  }

  const BcmUdfManager* getUdfMgr() const override {
    return udfManager_.get();
  }

  BcmEgressQueueFlexCounterManager* getBcmEgressQueueFlexCounterManager()
      const {
    return queueFlexCounterMgr_.get();
  }

  /**
   * Runs a diag cmd on the corresponding unit
   */
  void printDiagCmd(const std::string& cmd) const override;

  /*
   * Clear statistics for a list of ports.
   */
  void clearPortStats(
      const std::unique_ptr<std::vector<int32_t>>& ports) override;

  std::vector<phy::PrbsLaneStats> getPortAsicPrbsStats(PortID portId) override;
  void clearPortAsicPrbsStats(PortID portId) override;

  std::vector<prbs::PrbsPolynomial> getPortPrbsPolynomials(
      int32_t /* portId */) override;
  prbs::InterfacePrbsState getPortPrbsState(PortID portId) override;

  void clearInterfacePhyCounters(
      const std::unique_ptr<std::vector<int32_t>>& ports) override;

  /*
   * Friend tests. We want the abilty to test private methods
   * without comprimising encapsulation for code generally.
   * To that end make tests friends, but no one else
   */
  FRIEND_TEST(BcmTest, fpNoMissingOrQsetChangedGrpsPostInit);

  /*
   * Private callback called by the Broadcom API. Dispatches to
   * BcmSwitch::l2AddrUpdteReceived
   */
  static void l2LearningCallback(
      int unit,
      bcm_l2_addr_t* l2Addr,
      int operation,
      void* userData);

  /*
   * Private callback called by BcmSwitch::l2AddrCallBack. Dispatches to
   * callback_->l2LearningUpdateReceived
   */
  void l2LearningUpdateReceived(bcm_l2_addr_t* l2Addr, int operation) noexcept;

  /*
   * Callback handlers for bcm_l2_traverse that generate L2 table update
   * callbacks from software. These are used to maintain SwitchState's MAC
   * Table across warmboot, L2LearningMode changes etc.
   */
  static int addL2TableCb(int unit, bcm_l2_addr_t* l2Addr, void* userData);
  static int
  addL2TableCbForPendingOnly(int unit, bcm_l2_addr_t* l2Addr, void* userData);
  static int deleteL2TableCb(int unit, bcm_l2_addr_t* l2Addr, void* userData);

  bool isPortEnabled(PortID port) const;

  bool useHSDK() const;

  bool usePKTIO() const;

  std::map<PortID, phy::PhyInfo> updateAllPhyInfoImpl() override;
  const std::map<PortID, FabricEndpoint>& getFabricConnectivity()
      const override {
    static const std::map<PortID, FabricEndpoint> kEmpty;
    return kEmpty;
  }
  std::vector<PortID> getSwitchReachability(
      SwitchID /* switchId */) const override {
    return {};
  }

  void syncLinkStates() override;
  void syncPortLinkState(PortID port) override;

  // no concept of link active states in BcmSwitch
  void syncLinkActiveStates() override {}
  // no (fabric) link connectivity concept in BcmSwitch
  void syncLinkConnectivity() override {}
  // no switch reachability in BcmSwitch
  void syncSwitchReachability() override {}

  AclStats getAclStats() const override;

  std::shared_ptr<SwitchState> reconstructSwitchState() const override;

  void injectSwitchReachabilityChangeNotification() override {}

  bool getArsExhaustionStatus() override;

  std::vector<FirmwareInfo> getAllFirmwareInfo() const override {
    return {};
  }

  void startHighFrequencyStatsThread(
      const HighFrequencyStatsCollectionConfig& config);
  void stopHighFrequencyStatsThread();
  void getHighFrequencyTimeseriesStats(
      std::vector<HwHighFrequencyStats>& stats,
      const std::unique_ptr<GetHighFrequencyStatsOptions>& options) const;

 private:
  enum Flags : uint32_t {
    RX_REGISTERED = 0x01,
    LINKSCAN_REGISTERED = 0x02,
    PFC_WATCHDOG_REGISTERED = 0x04
  };
  enum DeltaType { ADDED, REMOVED, CHANGED };

  // Forbidden copy constructor and assignment operator
  BcmSwitch(BcmSwitch const&) = delete;
  BcmSwitch& operator=(BcmSwitch const&) = delete;
  BcmSwitch(BcmSwitch&&) = delete;
  BcmSwitch& operator=(BcmSwitch&&) = delete;

  /*
   * gracefulExit performs the requisite cleanup
   * for doing a warm boot next time around if
   * supported. Also signal our ability to do a
   * warm boot by creating warm boot file.
   *
   * Requires getting the lock since we don't want
   * state changes while we are calling cleanup
   * shutdown apis in the BCM sdk.
   */
  void gracefulExitImpl() override;
  /*
   * Handle SwitchRunState changes
   */
  void switchRunStateChangedImpl(SwitchRunState newState) override;

  /*
   * Update all statistics.
   */
  void updateStatsImpl() override;

  /*
   * Get default state switch is in on a cold boot
   */
  std::shared_ptr<SwitchState> getColdBootSwitchState() const;

  void setupToCpuEgress();

  std::unique_ptr<BcmRxPacket> createRxPacket(BcmPacketT bcmPacket);

  void changeDefaultVlan(VlanID oldId, VlanID newId);

  void processChangedVlan(
      const std::shared_ptr<Vlan>& oldVlan,
      const std::shared_ptr<Vlan>& newVlan);
  void processAddedVlan(const std::shared_ptr<Vlan>& vlan);
  void preprocessRemovedVlan(const std::shared_ptr<Vlan>& vlan);
  void processRemovedVlan(const std::shared_ptr<Vlan>& vlan);

  void processChangedIntf(
      const std::shared_ptr<Interface>& oldIntf,
      const std::shared_ptr<Interface>& newIntf);
  void processAddedIntf(const std::shared_ptr<Interface>& intf);
  void processRemovedIntf(const std::shared_ptr<Interface>& intf);

  template <typename MapDeltaT>
  void processNeighborDelta(
      const MapDeltaT& mapDelta,
      std::shared_ptr<SwitchState>* appliedState,
      DeltaType optype);
  template <typename MapDeltaT, typename AddrT>
  void processNeighborTableDelta(
      const MapDeltaT& mapDelta,
      std::shared_ptr<SwitchState>* appliedState,
      DeltaType optype);

  template <typename DELTA, typename ParentClassT>
  void processNeighborEntryDelta(
      const DELTA& delta,
      std::shared_ptr<SwitchState>* appliedState);
  template <typename NeighborEntryT>
  void processAddedNeighborEntry(const NeighborEntryT* addedEntry);
  template <typename NeighborEntryT>
  void processChangedNeighborEntry(
      const NeighborEntryT* oldEntry,
      const NeighborEntryT* newEntry);
  template <typename NeighborEntryT>
  void processRemovedNeighborEntry(const NeighborEntryT* removedEntry);

  template <typename NeighborEntryT>
  void processAddedAndChangedNeighbor(
      const BcmHostKey& key,
      const BcmIntf* intf,
      const NeighborEntryT* entry);

  void processDisabledPorts(const StateDelta& delta);
  void processEnabledPorts(const StateDelta& delta);
  void processAddedPorts(const StateDelta& delta);
  void processChangedPorts(const StateDelta& delta);
  void pickupLinkStatusChanges(const StateDelta& delta);
  void reconfigurePortGroups(const StateDelta& delta);

  template <typename RouteT>
  void processChangedRoute(
      const RouterID& id,
      const std::shared_ptr<RouteT>& oldRoute,
      const std::shared_ptr<RouteT>& newRoute);
  template <typename RouteT>
  void processAddedRoute(
      const RouterID& id,
      const std::shared_ptr<RouteT>& route);
  template <typename RouteT>
  void processRemovedRoute(
      const RouterID& id,
      const std::shared_ptr<RouteT>& route);
  void processRemovedRoutes(const StateDelta& delta);
  void processAddedChangedRoutes(
      const StateDelta& delta,
      std::shared_ptr<SwitchState>* appliedState);
  template <typename AddrT>
  void processRouteTableDelta(
      const StateDelta& delta,
      std::shared_ptr<SwitchState>* appliedState);
  template <typename AddrT>
  bool isRouteUpdateValid(const StateDelta& delta) const;

  void processQosChanges(const StateDelta& delta);

  void processAclChanges(const StateDelta& delta);
  void processChangedAcl(
      const std::shared_ptr<AclEntry>& oldAcl,
      const std::shared_ptr<AclEntry>& newAcl);
  void processAddedAcl(const std::shared_ptr<AclEntry>& acl);
  void processRemovedAcl(const std::shared_ptr<AclEntry>& acl);
  bool hasValidAclMatcher(const std::shared_ptr<AclEntry>& acl) const;

  void processTeFlowChanges(
      const StateDelta& delta,
      std::shared_ptr<SwitchState>* appliedState);
  void processChangedTeFlow(
      const std::shared_ptr<TeFlowEntry>& oldTeFlow,
      const std::shared_ptr<TeFlowEntry>& newTeFlow);
  void processAddedTeFlow(const std::shared_ptr<TeFlowEntry>& teFlow);
  void processRemovedTeFlow(const std::shared_ptr<TeFlowEntry>& teFlow);

  void processAggregatePortChanges(const StateDelta& delta);
  void processChangedAggregatePort(
      const std::shared_ptr<AggregatePort>& oldAggPort,
      const std::shared_ptr<AggregatePort>& newAggPort);
  void processAddedAggregatePort(const std::shared_ptr<AggregatePort>& aggPort);
  void processRemovedAggregatePort(
      const std::shared_ptr<AggregatePort>& aggPort);

  void processPortFlowletConfigAdd(const StateDelta& delta);

  void processLoadBalancerChanges(const StateDelta& delta);
  void processChangedLoadBalancer(
      const std::shared_ptr<LoadBalancer>& oldLoadBalancer,
      const std::shared_ptr<LoadBalancer>& newLoadBalancer);
  void processAddedLoadBalancer(
      const std::shared_ptr<LoadBalancer>& loadBalancer);
  void processRemovedLoadBalancer(
      const std::shared_ptr<LoadBalancer>& loadBalancer);

  void processUdfAdd(const StateDelta& delta);
  void processUdfRemove(const StateDelta& delta);
  void processAddedUdfPacketMatcher(
      const std::shared_ptr<UdfPacketMatcher>& udfPacketMatcher);
  void processAddedUdfGroup(const std::shared_ptr<UdfGroup>& udfGroup);
  void processRemovedUdfPacketMatcher(
      const std::shared_ptr<UdfPacketMatcher>& udfPacketMatcher);
  void processRemovedUdfGroup(const std::shared_ptr<UdfGroup>& udfGroup);

  // The following function will modify the object. In particular, the return
  // state will be published (to indicate what has actually been applied). If
  // everything from delta was successfully applied, then the "new" state in
  // delta will be published.
  //
  // Lock has to be performed in the function.
  std::shared_ptr<SwitchState> stateChangedImpl(
      const std::vector<StateDelta>& delta,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior)
      override;
  std::shared_ptr<SwitchState> stateChangedImplLocked(
      const StateDelta& delta,
      const std::lock_guard<std::mutex>& lock);

  void processSflowSamplingRateChanges(const StateDelta& delta);
  void processSflowCollectorChanges(const StateDelta& delta);
  void processChangedSflowCollector(
      const std::shared_ptr<SflowCollector>& oldCollector,
      const std::shared_ptr<SflowCollector>& newCollector);
  void processAddedSflowCollector(
      const std::shared_ptr<SflowCollector>& collector);
  void processRemovedSflowCollector(
      const std::shared_ptr<SflowCollector>& collector);

  void processControlPlaneChanges(const StateDelta& delta);

  void processMirrorChanges(const StateDelta& delta);

  void processAddedLabelForwardingEntry(
      const std::shared_ptr<Route<LabelID>>& addedEntry);
  void processRemovedLabelForwardingEntry(
      const std::shared_ptr<Route<LabelID>>& deletedEntry);
  void processChangedLabelForwardingEntry(
      const std::shared_ptr<Route<LabelID>>& oldEntry,
      const std::shared_ptr<Route<LabelID>>& newEntry);
  void processChangedLabelForwardingInformationBase(const StateDelta& delta);
  bool isValidLabelForwardingEntry(const Route<LabelID>* entry) const;

  void processSwitchSettingsChanged(const StateDelta& delta);
  void processSwitchSettingsEntryChanged(
      const std::shared_ptr<SwitchSettings>& oldSwitchSettings,
      const std::shared_ptr<SwitchSettings>& newSwitchSettings,
      const StateDelta& delta);

  void processFlowletSwitchingConfigChanges(const StateDelta& delta);
  void processEcmpForArsChanges(const StateDelta& delta);

  void processMacTableChanges(const StateDelta& delta);

  void processDynamicEgressLoadExponentChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicQueueExponentChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicQueueMinThresholdBytesChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicQueueMaxThresholdBytesChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicSampleRateChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicEgressMinThresholdBytesChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicEgressMaxThresholdBytesChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void processDynamicPhysicalQueueExponentChanged(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitching,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitching);

  void setEgressEcmpEtherType(uint32_t etherTypeEligiblity);

  void setEcmpDynamicRandomSeed(int ecmpRandomSeed);

  /*
   * linkStateChangedHwNotLocked is in the call chain started by link scan
   * thread while invoking our link state handler. Link scan thread
   * holds its internal lock for this unit here (LC_LOCK(unit) - BcmUnitLock
   * from here on), while calling this. This in turn means we can't acquire
   * BcmSwitch::lock_ in this call back else we risk getting into a deadlock.
   * Consider, we want to apply a change to h/w and BcmSwitch::stateChanged
   * function gets called. stateChanged then acquires BcmSwitch::lock_ and calls
   * into Bcm code. At this point imagine a port comes up and linkscan
   * handler notices this and acquires BcmUnitLock and invokes our handler.
   * We would now try to acquire BcmSwitch::lock_ but block since update
   * thread holds that. Meanwhile BCM function called from update thread would
   * try to acquire BcmUnitLock and block since the link scan thread is
   * holding that lock.
   * Back traces from deadlocked process here
   * https://phabricator.fb.com/P20042479
   */
  void linkStateChangedHwNotLocked(
      bcm_port_t port,
      bool up,
      phy::LinkFaultStatus iPhyFaultStatus);

  /*
   * For any actions that require a lock or might need to
   * be delayed, we do not perform those in linkStateChanged.
   * Acquiring locks in the link scan call back call chain
   * can lead to deadlocks (see comments above linkStateChangedHwNotLocked).
   * While waiting in the link scan call back can cause subsequent
   * link up/down notifications to be delayed, leading to packet
   * loss in case of back to back port up/down events.
   *
   */
  void linkStateChanged(PortID port, bool up, cfg::PortType portType);

  /*
   * Private callback called by the Broadcom API. Dispatches to
   * BcmSwitch::packetReceived.
   */
  static bcm_rx_t packetRxSdkCallback(int unit, bcm_pkt_t* pkt, void* cookie);

#ifdef INCLUDE_PKTIO
  /*
   * Private callback called by the Broadcom API. Dispatches to
   * BcmSwitch::packetReceived.
   */
  static bcm_pktio_rx_t
  pktioPacketRxSdkCallback(int unit, bcm_pktio_pkt_t* pkt, void* cookie);
#endif

  /*
   * Private callback called by BcmSwitch::packetRxCallback. Dispatches to
   * callback_->packetReceived.
   */
  int packetReceived(BcmPacketT& bcmPacket) noexcept;

  /*
   * Update global statistics.
   */
  void updateGlobalStats();

  /*
   * Drop IPv6 Router Advertisements.
   */
  void dropIPv6RAs();

  /*
   * Drop DHCP packets that are sent to us.
   * stopBufferStatCollection();
   */
  void dropDhcpPackets();

  /*
   * Process packets for which L3 MTU check fails
   */
  void setL3MtuFailPackets();

  /**
   * Copy IPv6 link local multicast packets to CPU
   */
  void copyIPv6LinkLocalMcastPackets();

  /*
   * Create ACL group
   */
  void createAclGroup(
      const std::optional<std::set<bcm_udf_id_t>>& udfIds = std::nullopt);
  /*
   * Create slow protocols MAC group
   */
  void createSlowProtocolsGroup();
  /*
   * During warm boot, check for missing FP Groups or FP groups
   * for which QSETs changed.
   */
  bool haveMissingOrQSetChangedFPGroups() const;
  void setupFPGroups();

  /*
   * Forces a linkscan pass on the provided ports.
   */
  void forceLinkscanOn(bcm_pbmp_t ports);

  /*
   * Configure rate limiting of packets sent to the CPU.
   */
  void configureRxRateLimiting();

  /*
   * Stop linkscan thread. This should only be done on shutdown.
   */
  void stopLinkscanThread();

  /*
   * Handle a potential sFlow trapped packet.
   *
   * Returns true if it is only an sFlow sample.
   * Returns false if it is there for another reason as well.
   */
  bool handleSflowPacket(BcmPacketT& bcmPacket) noexcept;

  void initFieldProcessor() const;

  void initMirrorModule() const;

  void initMplsModule() const;

  /**
   * Setup COS manager
   */
  void setupCos();

  /*
   * Setup linkscan unless its explicitly disabled via featuresDesired_ flag
   */
  void setupLinkscan();
  /*
   * Setup packet RX unless its explicitly disabled via featuresDesired_ flag
   */
  void setupPacketRx();

  /*
   * Check if state, speed update for this port port would
   * be permissible in the hardware.
   * Right now we check for valid speed and port state update
   * given the constraints of lanes on the physical
   * port group/QSFP. More checks can be added as needed.
   */
  bool isValidPortUpdate(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort,
      const std::shared_ptr<SwitchState>& newState) const;

  /* check that given port queue traffic classes are valid */
  bool isValidPortQueueUpdate(
      const std::vector<std::shared_ptr<PortQueue>>& oldPortQueueConfig,
      const std::vector<std::shared_ptr<PortQueue>>& newPortQueueConfig) const;

  /* check that feasible qos policy is associated with port */
  bool isValidPortQosPolicyUpdate(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort,
      const std::shared_ptr<SwitchState>& newState) const;

  void exportDeviceBufferUsage();

  /*
   * Return true if any of the port's queue names changed, false otherwise.
   */
  bool isPortQueueNameChanged(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  /*
   * Process changes to port queues
   */
  void processChangedPortQueues(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  /*
   * Process enabled port queues
   */
  void processEnabledPortQueues(const std::shared_ptr<Port>& port);

  /*
   * Process changes in port flowlet config
   */
  bool processChangedPortFlowletCfg(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  /*
   * Returns true if a CPU queue name has changed, false otherwise.
   */
  bool isControlPlaneQueueNameChanged(
      const std::shared_ptr<ControlPlane>& oldCPU,
      const std::shared_ptr<ControlPlane>& newCPU);

  /*
   * Process changes to control plane queues
   */
  void processChangedControlPlaneQueues(
      const std::shared_ptr<ControlPlane>& oldCPU,
      const std::shared_ptr<ControlPlane>& newCPU);

  void processChangedRxReasonToQueueEntries(
      const std::shared_ptr<ControlPlane>& oldCPU,
      const std::shared_ptr<ControlPlane>& newCPU);

  /*
   * Returns True if the PG pointed to by port has changed.
   * False otherwise
   */
  bool processChangedPgCfg(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  /*
   * Takes care of PFC watchdog deadlock recovery action
   * programming and callback on deadlock detection, which
   * are per chip configurations.
   */
  void processPfcWatchdogGlobalChanges(const StateDelta& delta);

  /*
   * Disable hotswap setting
   */
  void disableHotSwap() const;

  bool isL2EntryPending(const bcm_l2_addr_t* l2Addr);

  bool processChangedIngressPoolCfg(
      std::optional<BufferPoolFields> oldBufferPoolCfgPtr,
      std::optional<BufferPoolFields> newBufferPoolCfgPtr);

  void processControlPlaneEntryChanged(
      const std::shared_ptr<ControlPlane>& oldCPU,
      const std::shared_ptr<ControlPlane>& newCPU);
  void processControlPlaneEntryAdded(
      const std::shared_ptr<ControlPlane>& newCPU);
  void processControlPlaneEntryRemoved(
      const std::shared_ptr<ControlPlane>& oldCPU);

  void processDefaultAclgroupForUdf(std::set<bcm_udf_id_t>& udfAclIds);
  void initialStateApplied() override;

  void updateHighFrequencyStatsThreadConfig(
      const HighFrequencyStatsCollectionConfig& config);

  HwHighFrequencyStats getHighFrequencyStats();

  /*
   * Member variables
   */
  BcmPlatform* platform_{nullptr};
  Callback* callback_{nullptr};
  int unit_{-1};
  uint32_t flags_{0};
  BcmMmuState mmuState_{BcmMmuState::UNKNOWN};
  uint64_t mmuBufferBytes_{0};
  uint64_t mmuCellBytes_{0};
  std::unique_ptr<BcmWarmBootCache> warmBootCache_;
  std::unique_ptr<BcmPortTable> portTable_;
  std::unique_ptr<BcmEgress> toCPUEgress_;
  std::unique_ptr<BcmIntfTable> intfTable_;
  std::unique_ptr<BcmHostTable> hostTable_;
  std::unique_ptr<BcmEgressManager> egressManager_;
  std::unique_ptr<BcmNeighborTable> neighborTable_;
  std::unique_ptr<BcmNextHopTable<BcmHostKey, BcmL3NextHop>> l3NextHopTable_;
  std::unique_ptr<BcmNextHopTable<BcmLabeledHostKey, BcmMplsNextHop>>
      mplsNextHopTable_;
  std::unique_ptr<BcmMultiPathNextHopTable> multiPathNextHopTable_;
  std::unique_ptr<BcmMultiPathNextHopStatsManager>
      multiPathNextHopStatsManager_;
  std::unique_ptr<BcmLabelMap> labelMap_;
  std::unique_ptr<BcmRouteCounterTableBase> routeCounterTable_;
  std::unique_ptr<BcmRouteTable> routeTable_;
  std::unique_ptr<BcmQosPolicyTable> qosPolicyTable_;
  std::unique_ptr<BcmAclTable> aclTable_;
  std::unique_ptr<BcmStatUpdater> bcmStatUpdater_;
  std::unique_ptr<BcmCosManager> cosManager_;
  std::unique_ptr<BcmTeFlowTable> teFlowTable_;
  std::unique_ptr<BcmTrunkTable> trunkTable_;
  std::unique_ptr<BcmSflowExporterTable> sFlowExporterTable_;
  std::unique_ptr<BcmControlPlane> controlPlane_;
  std::unique_ptr<BcmRtag7LoadBalancer> rtag7LoadBalancer_;
  std::unique_ptr<BcmMirrorTable> mirrorTable_;
  std::unique_ptr<BcmBstStatsMgr> bstStatsMgr_;

  std::unique_ptr<std::thread> linkScanBottomHalfThread_;
  FbossEventBase linkScanBottomHalfEventBase_{"BcmLinkScanBottomHalfEventBase"};

  std::unique_ptr<BcmSwitchSettings> switchSettings_;

  std::unique_ptr<BcmMacTable> macTable_;

  std::unique_ptr<BcmUnit> unitObject_;
  BootType bootType_{BootType::UNINITIALIZED};
  int64_t bstStatsUpdateTime_{0};
  std::unique_ptr<BcmQcmManager> qcmManager_;
  std::unique_ptr<BcmPtpTcMgr> ptpTcMgr_;

  std::unique_ptr<BcmEgressQueueFlexCounterManager> queueFlexCounterMgr_;

  std::unique_ptr<UnsupportedFeatureManager> sysPortMgr_;
  std::unique_ptr<UnsupportedFeatureManager> remoteRifMgr_;
  std::unique_ptr<BcmUdfManager> udfManager_;

  static constexpr std::string_view kHighFreqStatsThreadName_{
      "HighFrequencyStatsThread"};
  static constexpr std::string_view kHighFreqStatsFunctionName_{
      "collectHighFrequencyStats"};
  static HwHighFrequencyStats zeroHighFrequencyStatsTimestamp(
      const HwHighFrequencyStats& stats);
  static bool hasHighFrequencyStatsChanged(
      const HwHighFrequencyStats& a,
      const HwHighFrequencyStats& b);
  static bool hasNonZeroWatermark(const HwHighFrequencyStats& stats);
  void collectHighFrequencyStats();
  HighFrequencyStatsCollectionConfig highFreqStatsThreadConfig_{};
  static constexpr int64_t kHfMinWaitDurationUs_{20000};
  static constexpr int64_t kHfMaxCollectionDurationUs_{10000000};
  std::unique_ptr<folly::FunctionScheduler> highFreqStatsThread_;
  constexpr static int kHighFreqStatsDataMaxSize_{4096};
  constexpr static int kMaxConsecutiveHighFreqStatsCollect_{16};
  folly::Synchronized<std::deque<HwHighFrequencyStats>> highFreqStatsData_{};
  /*
   * Lock to synchronize access to all BCM* data structures
   */
  std::mutex lock_;
};

} // namespace facebook::fboss
