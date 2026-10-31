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

#include "fboss/agent/hw/common/PrbsStatsEntry.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/store/SaiObjectWithCounters.h"
#include "fboss/agent/hw/sai/switch/SaiBridgeManager.h"
#include "fboss/agent/hw/sai/switch/SaiBufferManager.h"
#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"
#include "fboss/agent/hw/sai/switch/SaiQosMapManager.h"
#include "fboss/agent/hw/sai/switch/SaiQueueManager.h"
#include "fboss/agent/hw/sai/switch/SaiSamplePacketManager.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"
#include "folly/container/F14Set.h"

#include <gtest/gtest.h>

DECLARE_bool(sai_configure_six_tap);

namespace facebook::fboss {

struct ConcurrentIndices;
struct SaiIngressPriorityGroupHandleAndProfile;
class HwAsic;
class SaiManagerTable;
class SaiPlatform;
class HwPortFb303Stats;
class QosPolicy;
class SaiStore;

using SaiPort = SaiObjectWithCounters<SaiPortTraits>;
using SaiPortSerdes = SaiObject<SaiPortSerdesTraits>;
using SaiPortConnector = SaiObject<SaiPortConnectorTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
using SaiPortLlrProfile = SaiObject<SaiPortLlrProfileTraits>;
#endif
using PrbsStatsTable = std::vector<PrbsStatsEntry>;

/*
 * Cache port mirror data from sw switch
 */
struct SaiPortMirrorInfo {
  std::optional<std::string> ingressMirror;
  std::optional<std::string> egressMirror;
  bool samplingMirror;
  SaiPortMirrorInfo() {}
  SaiPortMirrorInfo(
      std::optional<std::string> ingressMirror,
      std::optional<std::string> egressMirror,
      bool samplingMirror)
      : ingressMirror(ingressMirror),
        egressMirror(egressMirror),
        samplingMirror(samplingMirror) {}
  std::optional<std::string> getIngressMirror() {
    return ingressMirror;
  }
  std::optional<std::string> getEgressMirror() {
    return egressMirror;
  }
  bool isMirrorSampled() {
    return samplingMirror;
  }
};

/*
 * Keep track of port PFC settings
 */
struct SaiPortPfcInfo {
  std::optional<sai_int32_t> pfcMode;
  std::optional<sai_uint8_t> pfcTx;
  std::optional<sai_uint8_t> pfcRx;
  std::optional<sai_uint8_t> pfcTxRx;

  SaiPortPfcInfo(
      std::optional<sai_int32_t> pfcMode = std::nullopt,
      std::optional<sai_uint8_t> pfcTx = std::nullopt,
      std::optional<sai_uint8_t> pfcRx = std::nullopt,
      std::optional<sai_uint8_t> pfcTxRx = std::nullopt)
      : pfcMode(pfcMode), pfcTx(pfcTx), pfcRx(pfcRx), pfcTxRx(pfcTxRx) {}
};

/*
 * For Xphy we create system side port, line side port and a port connector
 * associating these two. The Line side port is used for all subsequent MacSec
 * programming and it is kept in PortHandle's port, the system side Sai port is
 * kept in saiPort and the port connector in connector field.
 */
struct SaiPortHandle {
  ~SaiPortHandle();
  std::shared_ptr<SaiPort> port;
  std::shared_ptr<SaiPort> sysPort;
  std::shared_ptr<SaiPortSerdes> serdes;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  std::shared_ptr<SaiPortLlrProfile> llrProfile;
#endif
  std::shared_ptr<SaiPortSerdes> sysSerdes;
  std::shared_ptr<SaiPortConnector> connector;
  std::shared_ptr<SaiBridgePort> bridgePort;
  std::vector<SaiQueueHandle*> configuredQueues;
  std::shared_ptr<SaiSamplePacket> ingressSamplePacket;
  std::shared_ptr<SaiSamplePacket> egressSamplePacket;
  std::shared_ptr<SaiQosMap> dscpToTcQosMap;
  std::shared_ptr<SaiQosMap> pcpToTcQosMap;
  std::shared_ptr<SaiQosMap> tcToPcpQosMap;
  std::shared_ptr<SaiQosMap> tcToQueueQosMap;
  std::optional<std::string> qosPolicy;
  SaiQueueHandles queues;
  bool prbsEnabled;
  bool txPfcDurationStatsEnabled;
  bool rxPfcDurationStatsEnabled;

  void resetQueues();
  SaiPortMirrorInfo mirrorInfo;
  folly::F14FastMap<
      IngressPriorityGroupID,
      SaiIngressPriorityGroupHandleAndProfile>
      configuredIngressPriorityGroups;
  std::vector<std::shared_ptr<SaiBufferProfileHandle>>
      ingressPortBufferProfiles;
  std::vector<std::shared_ptr<SaiBufferProfileHandle>> egressPortBufferProfiles;
};

class SaiPortManager {
  using Handles = folly::F14FastMap<PortID, std::unique_ptr<SaiPortHandle>>;
  using Stats = folly::F14FastMap<PortID, std::unique_ptr<HwPortFb303Stats>>;

  static constexpr double kSpeedConversionFactor = 1000.;
  static constexpr double kRateConversionFactor = 1024. * 1024. * 1024.;

 public:
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  using ExtOperStatusLatchWarmBootState = std::map<PortID, sai_latch_status_t>;
#endif

  SaiPortManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform,
      ConcurrentIndices* concurrentIndices_);
  ~SaiPortManager();
  void resetQueues();
  PortSaiId addPort(const std::shared_ptr<Port>& swPort);
  void removePort(const std::shared_ptr<Port>& swPort);
  void changePort(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void setIngressAcl(const std::shared_ptr<Port>& swPort);
  void changeIngressAcl(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void replaceIngressAcl(
      AclTableSaiId oldAclTableId,
      AclTableSaiId newAclTableId);
  void resetIngressAcl();

  bool createOnlyAttributeChanged(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  bool createOnlyAttributeChanged(
      const SaiPortTraits::CreateAttributes& oldAttributes,
      const SaiPortTraits::CreateAttributes& newAttributes);

  SaiPortTraits::CreateAttributes attributesFromSwPort(
      const std::shared_ptr<Port>& swPort,
      bool lineSide = false,
      bool basicAttributeOnly = false) const;

  void attributesFromSaiStore(SaiPortTraits::CreateAttributes& attributes);

  SaiPortSerdesTraits::CreateAttributes serdesAttributesFromSwPinConfigs(
      PortSaiId portSaid,
      const std::vector<phy::PinConfig>& pinConfigs,
      const std::shared_ptr<SaiPortSerdes>& serdes,
      bool zeroPreemphasis = false,
      const std::optional<std::string>& customCollection = std::nullopt,
      bool skipSerdesProgramming = false,
      bool txPrecodingEnabled = false,
      bool rxPrecodingEnabled = false);

  const SaiPortHandle* getPortHandle(PortID swId) const;
  SaiPortHandle* getPortHandle(PortID swId);
  void triggerCableLengthMeasurement(const std::vector<PortID>& ports);
  const SaiQueueHandle* getQueueHandle(
      PortID swId,
      const SaiQueueConfig& saiQueueConfig) const;
  SaiQueueHandle* getQueueHandle(
      PortID swId,
      const SaiQueueConfig& saiQueueConfig);
  SaiQueueHandle* getQueueHandle(PortID swId, uint8_t queueId) const;
  std::map<PortID, HwPortStats> getPortStats() const;
  void changeQueue(
      const std::shared_ptr<Port>& swPort,
      const QueueConfig& oldQueueConfig,
      const QueueConfig& newQueueConfig);

  const HwPortFb303Stats* getLastPortStat(PortID port) const;

  std::map<PortID, FabricEndpoint> getFabricConnectivity() const;
  std::optional<FabricEndpoint> getFabricConnectivity(
      const PortID& portId) const;
  std::vector<PortID> getFabricReachabilityForSwitch(
      const SwitchID& switchId) const;
  const Stats& getLastPortStats() const {
    return portStats_;
  }

  cfg::PortSpeed getMaxSpeed(PortID port) const;
  // getSpeed returns the currently configured speed
  cfg::PortSpeed getSpeed(PortID port) const;
  Handles::const_iterator begin() const {
    return handles_.begin();
  }
  Handles::const_iterator end() const {
    return handles_.end();
  }

  void setQosPolicy(PortID portID, const std::optional<std::string>& qosPolicy);
  void setQosPolicy(const std::shared_ptr<QosPolicy>& qosPolicy);
  void clearQosPolicy(PortID portID);
  void clearQosPolicy(const std::shared_ptr<QosPolicy>& qosPolicy);
  void clearQosPolicy();

  void clearArsConfig(PortID portID);
  void clearArsConfig();

  void setTamObject(PortID portId, std::vector<sai_object_id_t> tamObject);
  void resetTamObject(PortID portId);

  std::shared_ptr<MultiSwitchPortMap> reconstructPortsFromStore(
      cfg::SwitchType switchType) const;

  cfg::PortType derivePortTypeOfLogicalPort(PortSaiId portSaiId, PortID portID)
      const;
  std::shared_ptr<Port> swPortFromAttributes(
      SaiPortTraits::CreateAttributes attributees,
      PortSaiId portSaiId,
      cfg::SwitchType switchType) const;

  std::vector<phy::PrbsLaneStats> getPortAsicPrbsStats(PortID portId);
  void clearPortAsicPrbsStats(PortID portId);
  prbs::InterfacePrbsState getPortPrbsState(PortID portId);
  void updatePrbsStats(PortID portId);
  void updateStats(
      PortID portID,
      bool updateWatermarks = false,
      bool updateCableLengths = false);

  // Link up/down debounce retrigger counts are leaba READ_ONLY port attributes
  // (25.5.4210 / 26.2+). Compile-time SDK gate; the per-port read in
  // updateStats is additionally guarded on the port having a debounce hold
  // timer configured.
  static bool isLinkDebounceRetriggerCounterSupported(const HwAsic* asic);

  void updateConnectivityStats(PortID portID);

  void clearStats(PortID portID);
  void clearInterfacePhyCounters(const PortID& portId);

  void programMirrorOnAllPorts(
      const std::string& mirrorName,
      MirrorAction action);

  void addBridgePort(const std::shared_ptr<Port>& port);
  void changeBridgePort(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);

  bool isPortUp(PortID portID) const;

  void setPtpTcEnable(bool enable);
  bool isPtpTcEnabled() const;

  std::vector<sai_port_lane_eye_values_t> getPortEyeValues(
      PortSaiId saiPortId) const;
  std::vector<sai_port_err_status_t> getPortErrStatus(
      PortSaiId saiPortId) const;
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  std::vector<sai_port_frequency_offset_ppm_values_t> getRxPPM(
      PortSaiId saiPortId,
      uint8_t numPmdLanes) const;
  std::vector<sai_port_snr_values_t> getRxSNR(
      const PortSaiId& saiPortId,
      uint8_t numPmdLanes,
      const PortID& portID) const;
#endif
  std::vector<phy::SerdesParameters> getSerdesParameters(
      PortSerdesSaiId serdesSaiPortId,
      const PortID& swPortID,
      uint8_t numPmdLanes) const;
  std::vector<phy::TxSettings> getTxSettings(
      PortSerdesSaiId serdesSaiPortId,
      PortID swPortID,
      uint8_t numPmdLanes) const;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
  std::vector<sai_port_lane_latch_status_t> getRxSignalDetect(
      PortSaiId saiPortId,
      uint8_t numPmdLanes,
      PortID portID) const;
  std::vector<sai_port_lane_latch_status_t> getRxLockStatus(
      PortSaiId saiPortId,
      uint8_t numPmdLanes,
      PortID portID) const;
  std::vector<sai_port_lane_latch_status_t> getFecAlignmentLockStatus(
      PortSaiId saiPortId,
      uint8_t numFecLanes) const;
  std::optional<sai_latch_status_t> getPcsRxLinkStatus(
      PortSaiId saiPortId) const;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  std::optional<sai_latch_status_t> getExtOperStatusLatch(
      PortSaiId saiPortId) const;
  ExtOperStatusLatchWarmBootState snapshotExtOperStatusLatch();
  void updateExtOperStatusLatchAfterWarmBoot(
      const ExtOperStatusLatchWarmBootState& baseline);
#endif
#endif

#if defined(BRCM_PAI_SDK) && SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  phy::Loopback getLoopbackMode(PortSaiId saiPortId) const;
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  std::optional<sai_latch_status_t> getHighCrcErrorRate(
      PortSaiId saiPortId,
      PortID swPort) const;
#endif
  void updateLeakyBucketFb303Counter(PortID portId, int value);
  void updatePmdChangedFb303Counters(
      PortID portId,
      phy::Side side,
      bool signalDetectChanged,
      bool cdrLockChanged);
  void updateLinkFaultChangedFb303Counters(
      PortID portId,
      phy::Side side,
      bool localFaultChanged,
      bool remoteFaultChanged);

  phy::FecMode getFECMode(PortID portId) const;

  phy::InterfaceType getInterfaceType(PortID portID) const;

  TransmitterTechnology getMedium(PortID portID) const;

  uint8_t getNumPmdLanes(PortSaiId saiPortId) const;
  std::vector<uint32_t> getPmdLaneList(PortSaiId saiPortId) const;
  void loadPortQueuesForAddedPort(const std::shared_ptr<Port>& swPort);
  void loadPortQueuesForChangedPort(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  cfg::PortType getPortType(PortID portId) const;
  void programSwitchWideLinkUpDebounceTimeout(
      const std::shared_ptr<Port>& swPort);
  void setClm(PortID portId, bool clmEnabled);
  bool isClmEnabled(PortID portId) const;
  bool fecCorrectedBitsSupported(PortID portID) const;
  bool fecCorrectedSymbolsSupported(PortID portID) const;
  bool rxFrequencyRPMSupported() const;
  bool rxSerdesParametersSupported() const;
  bool rxSNRSupported() const;
  phy::LinkTrainingStatus getLinkTrainingStatus(
      const PortSaiId& saiPortId,
      PortID portID,
      bool linkTrainingEnabled) const;
  bool linkTrainingSupportedOnPort(const std::shared_ptr<Port>& swPort) const;
  bool fecCodewordsStatsSupported(PortID portID) const;
  void addPortShelEnable(const std::shared_ptr<Port>& swPort) const;
  void changePortShelEnable(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort) const;
  // Throws on SDKs that cannot program the attribute, rather than dropping a
  // mode the config asked for.
  static SaiPortTraits::Attributes::LinkScanMode linkScanModeAttribute(
      cfg::LinkScanMode mode);
  /**
   * Increment the PFC deadlock detection counter for a given port.
   *
   * @param portId - The ID of the port for which the counter is to be
   * incremented.
   */
  void incrementPfcDeadlockCounter(const PortID& portId);

  /**
   * Increment the PFC deadlock recovery counter for a given port.
   *
   * @param portId - The ID of the port for which the counter is to be
   * incremented.
   */
  void incrementPfcRecoveryCounter(const PortID& portId);
  void updateFabricMacTransmitQueueStuck(
      const PortID& portId,
      HwPortStats& currPortStats,
      const HwPortStats& prevPortStats);
  void setFabricLinkMonitoringSystemPortId(
      const PortID& portId,
      sai_object_id_t sysPortObj);
  void resetFabricLinkMonitoringSystemPortId(const PortID& portId);
  void processPortBufferPoolConfigs(const std::shared_ptr<Port>& swPort);

 private:
  PortSaiId addPortImpl(const std::shared_ptr<Port>& swPort);
  void changePortImpl(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void addRemovedHandle(const PortID& portID);
  void removeRemovedHandleIf(const PortID& portID);
  void releasePorts();
  void releasePortPfcBuffers();

  std::vector<std::pair<sai_qos_map_type_t, QosMapSaiId>>
  getNullSaiIdsForQosMaps();
  std::vector<std::pair<sai_qos_map_type_t, QosMapSaiId>> getSaiIdsForQosMaps(
      const SaiQosMapHandle* qosMapHandle);

  void setQosMapsOnPort(
      PortID portID,
      std::vector<std::pair<sai_qos_map_type_t, QosMapSaiId>>& qosMaps);
  template <typename SaiPortAttribute>
  void setPortQosBufferProfiles(
      const PortID& portID,
      const std::vector<std::shared_ptr<SaiBufferProfileHandle>>&
          bufferProfileHandles,
      const char* direction);
  void setPortQosEgressBufferProfiles(
      const PortID& portID,
      const std::vector<std::shared_ptr<SaiBufferProfileHandle>>&
          bufferProfileHandles);
  void setPortQosIngressBufferProfiles(
      const PortID& portID,
      const std::vector<std::shared_ptr<SaiBufferProfileHandle>>&
          bufferProfileHandles);
  const std::vector<sai_stat_id_t>& supportedStats(PortID port);
  void fillInSupportedStats(PortID port);
  void fillInSupportedVendorExtStats(std::vector<sai_stat_id_t>& counterIds);
  bool fecStatsSupported(PortID portID) const;
  bool fecCorrectedCounterSupported(PortID portID) const;
  SaiPortHandle* getPortHandleImpl(PortID swId) const;
  SaiQueueHandle* getQueueHandleImpl(
      PortID swId,
      const SaiQueueConfig& saiQueueConfig) const;
  void loadPortQueues(const Port& swPort);
  void programSerdes(
      std::shared_ptr<SaiPort> saiPort,
      std::shared_ptr<Port> swPort,
      SaiPortHandle* portHandle);
  void programLlr(std::shared_ptr<Port> swPort, SaiPortHandle* portHandle);
  bool llrProfileBindingChanged(
      std::optional<sai_object_id_t> boundProfile,
      const std::shared_ptr<Port>& swPort);
  void programSampling(
      PortID portId,
      SamplePacketDirection direction,
      SamplePacketAction action,
      uint64_t sampleRate,
      std::optional<cfg::SampleDestination> sampleDestination);
  void programMirror(
      PortID portId,
      MirrorDirection direction,
      MirrorAction action,
      std::optional<std::string> mirrorId);
  void programSamplingMirror(
      PortID portId,
      MirrorDirection direction,
      MirrorAction action,
      std::optional<std::string> mirrorId);
  void addNode(const std::shared_ptr<Port>& swPort);
  void removeMirror(const std::shared_ptr<Port>& swPort);
  void addSamplePacket(const std::shared_ptr<Port>& swPort);
  void removeSamplePacket(const std::shared_ptr<Port>& swPort);
  void changeMirror(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeSamplePacket(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  bool checkPortSerdesAttributes(
      const SaiPortSerdesTraits::CreateAttributes& fromStore,
      const SaiPortSerdesTraits::CreateAttributes& fromSwPort);
  void programMacsec(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void resetSamplePacket(SaiPortHandle* portHandle);
  SaiPortPfcInfo getPfcAttributes(sai_uint8_t txPfc, sai_uint8_t rxPfc) const;
  SaiPortPfcInfo getPortPfcAttributes(
      const std::shared_ptr<Port>& swPort) const;
  void programPfc(
      const std::shared_ptr<Port>& swPort,
      sai_uint8_t txPfc,
      sai_uint8_t rxPfc);
  void programPfcWatchdog(
      const std::shared_ptr<Port>& swPort,
      std::vector<PfcPriority>& enabledPfcPriorities,
      const bool portPfcWdEnabled);
  void programPfcWatchdogTimers(
      const std::shared_ptr<Port>& swPort,
      std::vector<PfcPriority>& enabledPfcPriorities);
  void programPfcWatchdogPerQueueEnable(
      const std::shared_ptr<Port>& swPort,
      std::vector<PfcPriority>& enabledPfcPriorities,
      const bool portPfcWdEnabled);
  std::pair<sai_uint8_t, sai_uint8_t> preparePfcConfigs(
      const std::shared_ptr<Port>& swPort) const;
  std::vector<sai_map_t> preparePfcDeadlockQueueTimers(
      std::vector<PfcPriority>& enabledPfcPriorities,
      uint32_t timerVal);
  void addPfc(const std::shared_ptr<Port>& swPort);
  void changePfc(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void removePfc(const std::shared_ptr<Port>& swPort);
  void addPfcWatchdog(const std::shared_ptr<Port>& swPort);
  void changePfcWatchdog(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void removePfcWatchdog(const std::shared_ptr<Port>& swPort);
  void setPortType(PortID portId, cfg::PortType portType);
  void changePfcBuffers(
      std::shared_ptr<Port> oldPort,
      std::shared_ptr<Port> newPort);
  void removePfcBuffers(const std::shared_ptr<Port>& swPort);
  sai_port_prbs_config_t getSaiPortPrbsConfig(bool enabled) const;
  void initAsicPrbsStats(const std::shared_ptr<Port>& swPort);
  void removeIngressPriorityGroupMappings(SaiPortHandle* portHandle);
  void applyPriorityGroupBufferProfile(
      const std::shared_ptr<Port>& swPort,
      std::shared_ptr<SaiBufferProfile> bufferProfile,
      IngressPriorityGroupSaiId ingressPgSaiId);
  std::vector<IngressPriorityGroupSaiId> getIngressPriorityGroupSaiIds(
      const std::shared_ptr<Port>& swPort);
  void changePortByRecreate(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  std::optional<FabricEndpoint> getFabricConnectivity(
      const PortID& portId,
      const SaiPortHandle* portHandle) const;
  void changeRxLaneSquelch(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeZeroPreemphasis(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeQosPolicy(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeTxEnable(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeResetQueueCreditBalance(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void reloadSixTapAttributes(
      SaiPortHandle* portHandle,
      SaiPortSerdesTraits::CreateAttributes& attr);
  std::shared_ptr<SaiPort> createPortWithBasicAttributes(
      const std::shared_ptr<Port>& swPort);
  double calculateRate(uint32_t speed);
  void updatePrbsStatsEntryRate(const std::shared_ptr<Port>& swPort);
  void resetCableLength(PortID portId);
  void createSerdesWithZeroPreemphasis(
      SaiPortHandle* portHandle,
      const std::vector<phy::PinConfig>& pinConfigs);
  void changePortFlowletConfig(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void changeClm(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void clearPortFlowletConfig(const PortID& portId);
  void programPfcDurationCounterEnable(
      const std::shared_ptr<Port>& swPort,
      const std::optional<cfg::PortPfc>& newPfc,
      const std::optional<cfg::PortPfc>& oldPfc);
  SaiPortSerdesTraits::Attributes::RxReach::ValueType getSaiRxReach(
      const std::vector<phy::RxReach>& rxReaches) const;
  void programPfcDurationCounter(
      const std::shared_ptr<Port>& swPort,
      const std::optional<cfg::PortPfc>& newPfc,
      const std::optional<cfg::PortPfc>& oldPfc);
  void setPfcDurationStatsEnabled(
      const PortID& portId,
      bool txEnabled,
      bool rxEnabled);
  const std::vector<sai_stat_id_t>& getSupportedPfcDurationStats(
      const PortID& portId);

  /**
   * Enum to specify which PFC counter to increment.
   */
  enum class PfcCounterType { DEADLOCK, RECOVERY };

  /**
   * Increment the PFC counter for a given port and counter type.
   *
   * @param portId - The ID of the port for which the counter is to be
   * incremented.
   * @param counterType - The type of PFC counter to increment (DEADLOCK or
   * RECOVERY).
   */
  void incrementPfcCounter(const PortID& portId, PfcCounterType counterType);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  // Pure virtual function cannot be called in destructor. Thus, cache it.
  bool removePortsAtExit_;
  ConcurrentIndices* concurrentIndices_;
  Handles handles_;
  // on some platforms port can not be removed freely. on such platforms
  // retain removed port handle so it does not invoke remove port api.
  Handles removedHandles_;
  Stats portStats_;
  std::map<PortID, PrbsStatsTable> portAsicPrbsStats_;

  std::optional<SaiPortTraits::Attributes::PtpMode> getPtpMode() const;
  std::unordered_map<PortID, cfg::PortType> port2PortType_;
  std::unordered_map<PortID, bool> port2ClmEnabled_;
  std::unordered_map<PortID, std::vector<sai_stat_id_t>> port2SupportedStats_;
  std::unordered_map<PortID, std::shared_ptr<Port>> pendingNewPorts_;
  bool hwLaneListIsPmdLaneList_;
  bool tcToQueueMapAllowedOnPort_;
  bool globalQosMapSupported_;
  std::unordered_map<PortID, time_t> lastFecCounterReadTime_;
  std::unordered_map<PortID, time_t> lastPrbsRxStateReadTime_;
  FRIEND_TEST(PortManagerTest, calculateRate);
  FRIEND_TEST(PortManagerTest, updatePrbsStatsEntryRate);
};

} // namespace facebook::fboss
