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

#include <boost/bimap.hpp>
#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/types.h"
#include "fboss/lib/ThreadHeartbeat.h"
#include "fboss/lib/firmware_storage/FbossFwStorage.h"
#include "fboss/lib/i2c/gen-cpp2/i2c_controller_stats_types.h"
#include "fboss/lib/link_snapshots/SnapshotManager.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"
#include "fboss/lib/usb/TransceiverPlatformApi.h"
#include "fboss/qsfp_service/QsfpConfig.h"
#include "fboss/qsfp_service/QsfpServiceThreads.h"
#include "fboss/qsfp_service/SlotThreadHelper.h"
#include "fboss/qsfp_service/StateMachineController.h"
#include "fboss/qsfp_service/TransceiverLogging.h"
#include "fboss/qsfp_service/TransceiverStateMachine.h"
#include "fboss/qsfp_service/TransceiverValidator.h"
#include "fboss/qsfp_service/TypedStateMachineUpdate.h"
#include "fboss/qsfp_service/fsdb/QsfpFsdbSyncManager.h"
#include "fboss/qsfp_service/if/gen-cpp2/port_state_types.h"
#include "fboss/qsfp_service/module/Transceiver.h"

#include <folly/IntrusiveList.h>
#include <folly/SpinLock.h>
#include <folly/Synchronized.h>
#include <functional>
#include <map>
#include <vector>

#ifndef IS_OSS
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#endif
#endif

#define MODULE_LOG(level, Module, tcvrID) \
  TCVR_LOG_BASE(level, Module, tcvrID)    \
      << getTransceiverName(TransceiverID(tcvrID)) << ": "

#define MODULE_LOG_IF(level, Module, cond, tcvrID) \
  TCVR_LOG_BASE_IF(level, cond, Module, tcvrID)    \
      << getTransceiverName(TransceiverID(tcvrID)) << ": "

#define FW_LOG(level, tcvrID) MODULE_LOG(level, "[FWUPG]", tcvrID)

#define SM_LOG(level, tcvrID) MODULE_LOG(level, "[SM]", tcvrID)

// Reached in Port Manager mode, where PortManager performs the equivalent
// work. Doing nothing here is the intended behavior.
#define PORT_MGR_INTENTIONAL_SKIP_LOG(func) \
  XLOG(DBG2) << func << " called in Port Manager mode. Skipping."

// Not reachable in Port Manager mode: PortManager owns this path and every
// caller is routed to it. Getting here means a caller was missed during the
// migration, which would otherwise silently do nothing.
#define PORT_MGR_UNEXPECTED_CALL(func)                                 \
  XLOG(FATAL) << func                                                  \
              << " called in Port Manager mode, but PortManager owns " \
                 "this path. This is a Port Manager routing bug."

DECLARE_string(qsfp_service_volatile_dir);
DECLARE_bool(can_qsfp_service_warm_boot);
DECLARE_bool(enable_tcvr_validation);
DECLARE_bool(port_manager_mode);

namespace facebook::fboss {
struct TransceiverConfig;

struct NpuPortStatus {
  int portId{0};
  bool operState{false}; // true for link up, false for link down
  bool portEnabled{false}; // true for enabled, false for disabled
  bool asicPrbsEnabled{false}; // true for enabled, false for disabled
  std::string profileID;
};

class TransceiverManager {
  using PortNameMap = std::map<std::string, int32_t>;
  using PortGroups = std::map<int32_t, std::set<cfg::PlatformPortEntry>>;
  using PortNameIdMap = boost::bimap<std::string, PortID>;
  using TcvrIdToTcvrNameMap = std::map<TransceiverID, std::string>;
  using TransceiverStateMachineController = StateMachineController<
      TransceiverID,
      TransceiverStateMachineEvent,
      TransceiverStateMachineState,
      TransceiverStateMachine>;
  using TransceiverStateMachineUpdate =
      TypedStateMachineUpdate<TransceiverStateMachineEvent>;
  using BlockingTransceiverStateMachineUpdate =
      BlockingStateMachineUpdate<TransceiverStateMachineEvent>;

 public:
  static constexpr const char* kPhyStateKey = "phy";
  static constexpr const char* kAgentConfigAppliedInfoStateKey =
      "agentConfigAppliedInfo";
  static constexpr const char* kAgentConfigLastAppliedInMsKey =
      "agentConfigLastAppliedInMs";
  static constexpr const char* kAgentConfigLastColdbootAppliedInMsKey =
      "agentConfigLastColdbootAppliedInMs";
  static constexpr auto kSnapshotIntervalSeconds = 10;

  static void writeWarmBootState(
      const folly::dynamic* phyWarmbootState,
      const ConfigAppliedInfo& configInfo,
      std::string& cachedState,
      const std::string& fileName);

  using TcvrInfoMap = std::map<int32_t, TransceiverInfo>;

  explicit TransceiverManager(
      std::unique_ptr<TransceiverPlatformApi> api,
      const std::shared_ptr<const PlatformMapping> platformMapping,
      const std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads);
  virtual ~TransceiverManager();
  void gracefulExit();
  void setGracefulExitingFlag() {
    isExiting_ = true;
  }

  /*
   * Initialize the qsfp_service components, which might include:
   * - Check the warm boot flag
   * - Set up state machine update threads
   * - Initialize xphy if available
   * - Initialize transceiver
   */
  void init();
  virtual void getTransceiversInfo(
      std::map<int32_t, TransceiverInfo>& info,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;
  virtual void getAllTransceiversValidationInfo(
      std::map<int32_t, std::string>& info,
      std::unique_ptr<std::vector<int32_t>> ids,
      bool getConfigString) = 0;
  virtual void getTransceiversRawDOMData(
      std::map<int32_t, RawDOMData>& info,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;
  virtual void getTransceiversDOMDataUnion(
      std::map<int32_t, DOMDataUnion>& info,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;
  virtual void readTransceiverRegister(
      std::map<int32_t, ReadResponse>& response,
      std::unique_ptr<ReadRequest> request) = 0;
  virtual void writeTransceiverRegister(
      std::map<int32_t, WriteResponse>& response,
      std::unique_ptr<WriteRequest> request) = 0;
  virtual void syncPorts(
      std::map<int32_t, TransceiverInfo>& info,
      std::unique_ptr<std::map<int32_t, PortStatus>> ports) = 0;

  virtual PlatformType getPlatformType() const = 0;

  int getMaxTimeTakenForFwUpgrade() const {
    return maxTimeTakenForFwUpgrade_;
  }

  bool isValidTransceiver(int32_t id) {
    return id < getNumQsfpModules() && id >= 0;
  }
  virtual int getNumQsfpModules() const = 0;

  virtual std::vector<TransceiverID> refreshTransceivers() = 0;
  // Refresh specified Transceivers
  // If `transceivers` is empty, we'll refresh all existing transceivers
  // Return: The refreshed transceiver ids
  std::vector<TransceiverID> refreshTransceivers(
      const std::unordered_set<TransceiverID>& transceivers);

  /// Called to publish transceivers after a refresh
  virtual void publishTransceiversToFsdb() = 0;

  virtual void publishPimStatesToFsdb() = 0;

  virtual int scanTransceiverPresence(
      std::unique_ptr<std::vector<int32_t>> ids) = 0;

  void resetTransceiver(
      std::unique_ptr<std::vector<std::string>> portNames,
      ResetType resetType,
      ResetAction resetAction);

  void setForceFirmwareUpgradeForTesting(bool enable) {
    forceFirmwareUpgradeForTesting_ = enable;
  }

  void getPortTransceiverIDs(
      std::map<std::string, std::vector<int32_t>>& portTransceiverIds) const;

  /*
   * A function take a parameter representing number of seconds,
   * adding it to the time point of now and assign it to
   * pauseRemediationUntil_, which is a time point until when
   * the remediation of module will be paused.
   */
  void setPauseRemediation(
      int32_t timeout,
      std::unique_ptr<std::vector<std::string>> portList);
  void getPauseRemediationUntil(
      std::map<std::string, int32_t>& info,
      std::unique_ptr<std::vector<std::string>> portList);
  /* Virtual function to return the i2c transactions stats in a platform.
   * This will be overridden by derived classes which are platform specific
   * and has the platform specific implementation for this counter
   */
  virtual std::vector<I2cControllerStats> getI2cControllerStats() const = 0;

  /* Virtual function to update the I2c transaction stats to the ServiceData
   * object from where it will get picked up by FbAgent.
   * Implementation - The TransceieverManager base class is inherited
   * by platform speficic Transaceiver Manager class like WedgeManager.
   * That class has the function to get the I2c transaction status
   */
  virtual void publishI2cTransactionStats() = 0;

  /*
   * Virtual functions to get the cached transceiver signal flags, media lane
   * signals, and module status flags, and clear the cached data. This is
   * introduced mainly due to the mismatch of ODS reporting frequency and the
   * interval of us reading transceiver data. Some of the clear on read
   * information may be lost in this process and not being captured in the ODS
   * time series. This would bring difficulty in root cause link issues. Thus
   * here we provide a way of read and clear the data for the purpose of ODS
   * data reporting.
   */
  virtual void getAndClearTransceiversSignalFlags(
      std::map<int32_t, SignalFlags>& signalFlagsMap,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;
  virtual void getAndClearTransceiversMediaSignals(
      std::map<int32_t, std::map<int, MediaLaneSignals>>& mediaSignalsMap,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;
  virtual void getAndClearTransceiversModuleStatus(
      std::map<int32_t, ModuleStatus>& moduleStatusMap,
      std::unique_ptr<std::vector<int32_t>> ids) = 0;

  TransceiverPlatformApi* getQsfpPlatformApi() {
    return qsfpPlatApi_.get();
  }

  const PlatformMapping* getPlatformMapping() const {
    return platformMapping_.get();
  }
  /*
   * Virtual function to initialize all the Phy in the system
   * TODO(joseph5wu) Will move to protected so that outside of
   * TransceiverManager family won't call this function directly. Instead they
   * should use init() directly
   */
  virtual bool initExternalPhyMap(
      PhyManager* phyManager,
      bool forceWarmboot = false) = 0;

  PhyManager* getPhyManager() {
    return phyManager_.get();
  }

  // For when the PhyManager is owned elsewhere (PortManager in Port Manager
  // mode), so TransceiverManager still knows the platform has external PHYs.
  void setHasExternalPhy() {
    hasExternalPhy_ = true;
  }

  bool hasExternalPhy() const {
    return phyManager_ != nullptr || hasExternalPhy_;
  }

  void releasePhyManager() {
    if (phyManager_) {
      // Suppressing ASAN warnings as this is expected behavior
      __attribute__((unused)) auto* leakedPhyManager = phyManager_.release();
#ifndef IS_OSS
#if __has_feature(address_sanitizer)
      folly::lsan_ignore_object(leakedPhyManager);
#endif
#endif
    }
  }

  /*
   * Virtual function to program a PHY port on external PHY. This function
   * needs to be implemented by the platforms which support external PHY
   * and the PHY code is running in this qsfp_service process
   */
  virtual void programXphyPort(
      PortID portId,
      cfg::PortProfileID portProfileId) = 0;

  virtual void programXphyPortPrbs(
      PortID portID,
      phy::Side side,
      const phy::PortPrbsState& prbs) = 0;

  virtual phy::PhyInfo getXphyInfo(PortID portId) = 0;

  virtual phy::PortPrbsState getXphyPortPrbs(PortID portID, phy::Side side) = 0;

  virtual void updateAllXphyPortsStats() = 0;

  const PortGroups& getModuleToPortMap() const {
    return portGroupMap_;
  }

  const PortNameMap& getPortNameToModuleMap() const;

  const QsfpConfig* getQsfpConfig() const {
    return qsfpConfig_.get();
  };

  // Return a shared pointer to the transceiver config.
  // This is initialized during config loading in WedgeManager.
  // All transceivers will share the same transceiver config.
  std::shared_ptr<const TransceiverConfig> getTransceiverConfig() const {
    return tcvrConfig_;
  }

  virtual std::vector<PortID> getMacsecCapablePorts() const = 0;

  virtual std::string listHwObjects(
      std::vector<HwObjectType>& hwObjects,
      bool cached) const = 0;

  virtual bool getSdkState(std::string filename) const = 0;

  std::string getPortInfo(std::string portName);

  void setPortLoopbackState(
      std::string /* portName */,
      phy::PortComponent /* component */,
      bool /* setLoopback */,
      phy::LoopbackMode mode = phy::LoopbackMode::INPUT);

  void setPortLoopbackStateTransceiver(
      PortID portId,
      std::string portName,
      phy::PortComponent component,
      bool setLoopback,
      phy::LoopbackMode mode = phy::LoopbackMode::INPUT);

  void setPortAdminState(
      std::string /* portName */,
      phy::PortComponent /* component */,
      bool /* setAdminUp */);

  std::vector<phy::TxRxEnableResponse> setInterfaceTxRx(
      const std::vector<phy::TxRxEnableRequest>& txRxEnableRequests);

  void getSymbolErrorHistogram(
      CdbDatapathSymErrHistogram& symErr,
      const std::string& portName);

  virtual std::string saiPhyRegisterAccess(
      std::string /* portName */,
      bool /* opRead */,
      int /* phyAddr */,
      int /* devId */,
      int /* regOffset */,
      int /* data */) {
    return "";
  }

  virtual std::string saiPhySerdesRegisterAccess(
      std::string /* portName */,
      bool /* opRead */,
      int /* mdioAddr */,
      phy::Side /* side */,
      int /* serdesLane */,
      uint32_t /* regOffset */,
      uint32_t /* data */) {
    return "Function not implemented for this platform";
  }

  virtual std::string phyConfigCheckHw(std::string /* portName */) {
    return "The function phyConfigCheckHw not implemented for this platform";
  }

  // Returns the interface names for a given transceiverId
  // Returns empty list when there is no corresponding name for a given
  // transceiver ID
  const std::set<std::string> getPortNames(TransceiverID tcvrId) const;

  // Returns the first interface name found for the given transceiverId
  // returns empty string when there is no name found
  const std::string getPortName(TransceiverID tcvrId) const;

  const std::string getTransceiverName(const TransceiverID& tcvrId) const;

  // Since all the transceiver events need to have a proper order for the
  // correct port programming, we should always wait for the update results
  // before moving on to the next operations.
  void updateStateBlocking(
      TransceiverID id,
      TransceiverStateMachineEvent event);
  std::shared_ptr<BlockingStateMachineUpdateResult>
  updateStateBlockingWithoutWait(
      TransceiverID id,
      TransceiverStateMachineEvent event);
  std::shared_ptr<BlockingStateMachineUpdateResult>
  enqueueStateUpdateForTcvrWithoutExecuting(
      TransceiverID id,
      TransceiverStateMachineEvent event);

  TransceiverStateMachineState getCurrentState(TransceiverID id) const;

  TransceiverStateMachineState getCurrentStateSnapshot(TransceiverID id) const;

  const state_machine<TransceiverStateMachine>& getStateMachineForTesting(
      TransceiverID id) const;

  bool getNeedResetDataPath(TransceiverID id) const;

  TransceiverValidationInfo getTransceiverValidationInfo(
      TransceiverID id,
      bool validatePortProfile) const;

  bool validateTransceiverById(
      TransceiverID id,
      std::string& notValidatedReason,
      bool validatePortProfile);

  void checkPresentThenValidateTransceiver(TransceiverID id);

  std::string getTransceiverValidationConfigString(TransceiverID id) const;

  bool validateTransceiverConfiguration(
      TransceiverValidationInfo& tcvrInfo,
      std::string& notValidatedReason) const;

  int getNumNonValidatedTransceiverConfigs(
      const std::map<int32_t, TransceiverInfo>& infoMap) const;

  void updateValidationCache(TransceiverID id, bool isValid);

  // ========== Public functions for TransceiverStateMachine Start ==========
  // This refresh TransceiverStateMachine functions will handle all state
  // machine updates.
  void refreshStateMachines();

  void programInternalPhyPorts(TransceiverID id);

  virtual void programExternalPhyPorts(
      TransceiverID id,
      bool needResetDataPath);

  void programTransceiver(TransceiverID id, bool needResetDataPath);

  bool readyTransceiver(TransceiverID id);

  // Returns a pair of <boolean_for_all_ports_down, list_of_ports_down>
  std::pair<bool, std::vector<std::string>> areAllPortsDown(
      TransceiverID id) const noexcept;

  bool tryRemediateTransceiver(TransceiverID id);

  bool supportRemediateTransceiver(TransceiverID id);

  void markLastDownTime(TransceiverID id) noexcept;

  /*
   * updateLastDownTimeFromPortStatus is a Port Manager mode-only function that
   * provides information to TransceiverManager about aggregate port status for
   * a specific transceiver, so that we properly update markLastDownTime to
   * avoid early remediation.
   *
   * The input map (tcvrPortStatusChanges) should only contain transceivers that
   * experienced a relevant port status change:
   *
   *   - TransceiverID not present in map:
   *       No relevant port status change occurred for this transceiver.
   *       No action is taken.
   *
   *   - TransceiverID -> true:
   *       At least one port became active (equivalent to ACTIVE state entry)
   * since last status check. Sets needMarkLastDownTime = true in the state
   * machine, enabling lastDownTime to be marked on the next DOWN transition.
   *
   *   - TransceiverID -> false:
   *       All ports went down (equivalent to INACTIVE state entry) since last
   * status check. If needMarkLastDownTime is true, marks lastDownTime and sets
   *       the flag to false. If flag is already false, does nothing
   *       (prevents re-marking during failed remediation cycles).
   *
   * Replicates activeStateEntry and inactiveStateEntry logic from
   * TransceiverStateMachine.h.
   */
  void updateLastDownTimeFromPortStatus(
      const std::map<TransceiverID, bool>& tcvrPortStatusChanges) noexcept;

  // Calling thread MUST hold lock on transceivers_ while this function
  // executes.
  virtual bool verifyEepromChecksumsLocked(TransceiverID id);

  // Calling thread MUST hold lock on transceivers_ while this function
  // executes.
  void setDiagsCapabilityLocked(TransceiverID id);

  void resetProgrammedIphyPortToPortInfo(TransceiverID id);

  void resetProgrammedIphyPortToPortInfoForPorts(
      const std::unordered_set<PortID>& portIds);

  std::map<uint32_t, phy::PhyIDInfo> getAllPortPhyInfo();

  phy::PhyInfo getPhyInfo(const std::string& portName);

  // Reset the port state for all ports associated with given transceiver.
  void resetPortState(const TransceiverID& id);
  // ========== Public functions for TransceiverStateMachine End ==========

  // A struct to keep track of the software port profile and status
  struct TransceiverPortInfo {
    cfg::PortProfileID profile;
    std::optional<NpuPortStatus> status;
    int64_t statusReceivedInMs{0};
  };

  using PortToPortInfo = std::unordered_map<PortID, TransceiverPortInfo>;
  PortToPortInfo getProgrammedIphyPortToPortInfo(TransceiverID id) const;

  std::unordered_map<PortID, cfg::PortProfileID>
  getOverrideProgrammedIphyPortAndProfileForTest(TransceiverID id) const;

  // Used for sharing data with PortManager.
  std::shared_ptr<folly::Synchronized<PortToPortInfo>>
  getSynchronizedProgrammedIphyPortToPortInfo(TransceiverID id);

  // TEST ONLY
  void setOverrideAgentPortStatusForTesting(
      bool up,
      bool enabled,
      bool clearOnly = false);
  void setOverrideAgentConfigAppliedInfoForTesting(
      std::optional<ConfigAppliedInfo> configAppliedInfo);

  // An override of programmed iphy ports.
  // Due to hw_test won't be able to get wedge_agent running, this override
  // map will mimic the return of programmed iphy ports based on transceiver.
  // NOTE: Only use in test
  using OverrideTcvrToPortAndProfile = std::unordered_map<
      TransceiverID,
      std::unordered_map<PortID, cfg::PortProfileID>>;
  virtual void setOverrideTcvrToPortAndProfileForTesting(
      std::optional<OverrideTcvrToPortAndProfile> overrideTcvrToPortAndProfile =
          std::nullopt) = 0;
  OverrideTcvrToPortAndProfile getOverrideTcvrToPortAndProfileForTesting() {
    return overrideTcvrToPortAndProfileForTest_;
  }

  Transceiver* FOLLY_NULLABLE overrideTransceiverForTesting(
      TransceiverID id,
      std::unique_ptr<Transceiver> overrideTcvr);

  folly::Synchronized<std::unordered_map<TransceiverID, SnapshotManager>>&
  getSnapshotManagersForTesting() {
    return snapshotManagers_;
  }

  // If the transceiver doesn't exist, this will return std::nullopt.
  std::optional<TransceiverInfo> getTransceiverInfoOptional(
      TransceiverID id) const;

  // If the transceiver doesn't exit, it will still return a TransceiverInfo
  // with present filed is false.
  TransceiverInfo getTransceiverInfo(TransceiverID id) const;

  void getAllPortSupportedProfiles(
      std::map<std::string, std::vector<cfg::PortProfileID>>&
          supportedPortProfiles,
      bool checkOptics);

  bool isTransceiverPortStateSupported(
      TransceiverID tcvrID,
      TransceiverPortState& tcvrPortState);

  // Function to convert port name string to software port id
  std::optional<PortID> getPortIDByPortName(const std::string& portName) const;

  // Function to convert port id to port name
  std::optional<std::string> getPortNameByPortId(PortID portId) const;

  std::vector<PortID> getAllPlatformPorts(TransceiverID tcvrID) const;

  virtual void triggerVdmStatsCapture(std::vector<int32_t>& ids) = 0;

  // This function will bring all the transceivers out of reset, making use
  // of the specific implementation from each platform. Platforms that bring
  // transceiver out of reset by default will stay no op.
  virtual void clearAllTransceiverReset();

  // This function will trigger a hard reset on the specific transceiver,
  // making use of the specific implementation from each platform. It will
  // also remove the transceiver from the transceivers_ map.
  void triggerQsfpHardReset(int idx);

  // Hold the reset on a specific transceiver. It will also remove the
  // transceiver from the transceivers_ map.
  void holdTransceiverReset(int idx);

  // Release the reset on a specific transceiver. It will also remove the
  // transceiver from the transceivers_ map.
  void releaseTransceiverReset(int idx);

  // Trigger a specific reset action (RESET_THEN_CLEAR, RESET, CLEAR_RESET)
  void hardResetAction(
      void (TransceiverPlatformApi::*func)(unsigned int),
      int idx,
      bool holdInReset,
      bool removeTransceiver);

  void publishLinkSnapshots(std::string portName);
  void publishLinkSnapshotsTransceiver(PortID portID);

  void getInterfacePhyInfo(
      std::map<std::string, phy::PhyInfo>& phyInfos,
      const std::string& portName);

  void getAllInterfacePhyInfo(std::map<std::string, phy::PhyInfo>& phyInfos);

  time_t getLastDownTime(TransceiverID id) const;

  static std::string forceColdBootFileName();

  static std::string warmBootFlagFileName();

  void setPortPrbs(
      PortID portId,
      phy::PortComponent component,
      const phy::PortPrbsState& state);

  phy::PrbsStats getPortPrbsStats(PortID portId, phy::PortComponent component)
      const;

  phy::PrbsStats getPortPrbsStatsTransceiver(PortID portId, phy::Side side)
      const;

  void clearPortPrbsStats(PortID portId, phy::PortComponent component);

  void clearPortPrbsStatsTransceiver(
      PortID portId,
      const std::string& portName,
      phy::Side side);

  std::vector<prbs::PrbsPolynomial> getTransceiverPrbsCapabilities(
      TransceiverID tcvrID,
      phy::Side side);

  void getSupportedPrbsPolynomials(
      std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
      std::string portName,
      phy::PortComponent component);

  void setInterfacePrbs(
      std::string portName,
      phy::PortComponent component,
      const prbs::InterfacePrbsState& state);

  void setInterfacePrbsTransceiver(
      PortID portId,
      const std::string& portName,
      phy::PortComponent component,
      const prbs::InterfacePrbsState& state);

  void getInterfacePrbsState(
      prbs::InterfacePrbsState& prbsState,
      const std::string& portName,
      phy::PortComponent component) const;

  void getInterfacePrbsStateTransceiver(
      prbs::InterfacePrbsState& prbsState,
      PortID portId,
      const std::string& portName,
      phy::PortComponent component) const;

  void getAllInterfacePrbsStates(
      std::map<std::string, prbs::InterfacePrbsState>& prbsStates,
      phy::PortComponent component) const;

  phy::PrbsStats getInterfacePrbsStats(
      const std::string& portName,
      phy::PortComponent component) const;

  void getAllInterfacePrbsStats(
      std::map<std::string, phy::PrbsStats>& prbsStats,
      phy::PortComponent component) const;

  void clearInterfacePrbsStats(
      std::string portName,
      phy::PortComponent component);

  void bulkClearInterfacePrbsStats(
      std::unique_ptr<std::vector<std::string>> interfaces,
      phy::PortComponent component);

  std::optional<DiagsCapability> getDiagsCapability(TransceiverID id) const;

  long getStateMachineThreadHeartbeatMissedCount() const {
    return stateMachineThreadHeartbeatMissedCount_;
  }

  // Dump the transceiver I2C Log for a specific port.
  // Returns the number of lines in log header and number of log entries.
  // To be implemented by derived class.
  virtual std::pair<size_t, size_t> dumpTransceiverI2cLog(const std::string&) {
    return {0, 0};
  }

  virtual std::pair<size_t, size_t> dumpTransceiverI2cLog(int32_t) {
    return {0, 0};
  }

  virtual void publishPhyStateToFsdb(
      std::string&& /* portName */,
      std::optional<phy::PhyState>&& /* newState */) const {}

  virtual void publishPhyStatToFsdb(
      std::string&& /* portName */,
      phy::PhyStats&& /* stat */) const {}

  virtual void publishPortStatToFsdb(
      std::string&& /* portName */,
      HwPortStats&& /* stat */) const {}

  virtual void publishPortStateToFsdb(
      std::string&& /* portName */,
      portstate::PortState&& /* portState */) const {}

  // Publishes to FSDB the port state for all ports
  // belonging to a given transceiver.
  void publishPortStatesToFsdb(
      const TransceiverID& id,
      const portstate::PortState& state);

  std::optional<TransceiverID> getTransceiverID(PortID id) const;

  QsfpServiceRunState getRunState() const;

  bool isExiting() const {
    return isExiting_;
  }

  bool isUpgradingFirmware() const {
    return isUpgradingFirmware_;
  }

  bool isFullyInitialized() const {
    return isFullyInitialized_;
  }

  bool isSystemInitialized() const {
    return isSystemInitialized_;
  }

  /*
   * Sync the NpuPortStatus' received from FSDB
   */
  void syncNpuPortStatusUpdate(
      std::map<int, facebook::fboss::NpuPortStatus>& portStatus);

  bool firmwareUpgradeRequired(TransceiverID id);

  void doTransceiverFirmwareUpgrade(TransceiverID tcvrID);

  void resetUpgradedTransceiversToDiscovered();

  FbossFwStorage* fwStorage() const {
    return fwStorage_.get();
  }

  virtual std::unique_ptr<TransceiverI2CApi> getI2CBus() = 0;

  // Determine if transceiver FW requires upgrade.
  // Transceiver has to be present, and the version in the QsfpConfig
  // has to be different from whats already running in HW.
  std::optional<FirmwareUpgradeData> getFirmwareUpgradeData(
      Transceiver& tcvr) const;

  std::map<std::string, FirmwareUpgradeData> getPortsRequiringOpticsFwUpgrade()
      const;

  std::map<std::string, FirmwareUpgradeData> triggerAllOpticsFwUpgrade();

  std::map<std::string, FirmwareUpgradeData> triggerOpticsFwUpgrade(
      const std::vector<std::string>& interfaces);

  // portName to MediaInterfaceCode map
  void getPortMediaInterface(
      std::map<std::string, MediaInterfaceCode>& portMediaInterface);

  TcvrIdToTcvrNameMap getTcvrIdToTcvrNameMap() const {
    return tcvrIdToTcvrName_;
  }
  void triggerTransceiverEventsForAgentConfigChangeEvent(
      bool resetDataPath,
      ConfigAppliedInfo newConfigAppliedInfo);

  static bool opticalOrActiveCmisCable(const TcvrState& tcvrState);
  static bool opticalOrActiveCable(const TcvrState& tcvrState);
  static bool activeCable(const TcvrState& tcvrState);

  void triggerResetEvents(const std::unordered_set<TransceiverID>& tcvrs);

  void triggerFirmwareUpgradeEvents(
      const std::unordered_set<TransceiverID>& tcvrs);

  // Check whether iphy/xphy/transceiver programmed is done. If not, then
  // trigger the corresponding program event to program the component.
  // Return the list of transceivers that have programming events
  std::vector<TransceiverID> triggerProgrammingEvents();

  std::unordered_set<TransceiverID> findPotentialTcvrsForFirmwareUpgrade(
      const std::vector<TransceiverID>& presentXcvrIds);

  void findAndTriggerPotentialFirmwareUpgradeEvents(
      const std::vector<TransceiverID>& presentXcvrIds);

  void clearEvbsRunningFirmwareUpgrade() {
    // Clear the map that tracks the firmware upgrades in progress per evb
    evbsRunningFirmwareUpgrade_.wlock()->clear();
  }

  void markTransceiverReadyForProgramming(TransceiverID tcvrId, bool ready);

  // Set the can_warm_boot flag for qsfp service. Done after successful
  // initialization to avoid cold booting non-XPhy systems in case of a
  // non-graceful exit and also set during graceful exit.
  void setCanWarmBoot();

  void completeRefresh();

  // Store the warmboot state for qsfp_service. This will be updated
  // periodically after Transceiver State machine updates to maintain
  // the state if graceful shutdown did not happen.
  // Will also be called during graceful exit for qsfp_service once the state
  // machine stops.

  void setWarmBootState();

  std::string warmBootStateFileName() const;

  bool canWarmBoot() const {
    return canWarmBoot_;
  }

  folly::dynamic getWarmBootState() const {
    return warmBootState_;
  }

  void setPhyManager(std::unique_ptr<PhyManager> phyManager) {
    phyManager_ = std::move(phyManager);
    phyManager_->setPublishPhyCb(
        [this](auto&& portName, auto&& newInfo, auto&& portStats) {
          if (newInfo.has_value()) {
            publishPhyStateToFsdb(
                std::string(portName), std::move(*newInfo->state()));
            publishPhyStatToFsdb(
                std::string(portName), std::move(*newInfo->stats()));
          } else {
            publishPhyStateToFsdb(std::string(portName), std::nullopt);
          }
          if (portStats.has_value()) {
            publishPortStatToFsdb(std::move(portName), std::move(*portStats));
          }
        });
  }

  // Check whether the specified stableTcvrs need remediation and then trigger
  // the remediation events to remediate such transceivers.
  void triggerRemediateEvents(const std::vector<TransceiverID>& stableTcvrs);

  std::set<TransceiverID> getPresentTransceivers() const;

  bool transceiverJustRemediated(const TransceiverID& id) const;

  std::shared_ptr<QsfpFsdbSyncManager> getFsdbSyncManager() const {
    return fsdbSyncManager_;
  }

 protected:
  /*
   * Check to see if we can attempt a warm boot.
   * Returns true if 2 conditions are met
   *
   * 1) User did not create cold_boot_once_qsfp_service file
   * 2) can_warm_boot file exists, indicating that qsfp_service saved warm
   * boot state and shut down successfully.
   *
   * This function also remove the forceColdBoot file but keep the canWarmBoot
   * file so that for future qsfp_service crash, we can still use warm boot
   * without resetting the xphy or transceiver.
   */
  virtual bool checkWarmBootFlags();

  virtual void initTransceiverMap() = 0;

  virtual void loadConfig() = 0;

  // Update the cached PortStatus of TransceiverToPortInfo based on the input
  // This will only change the active state of the state machine.
  void updateTransceiverActiveState(
      const std::set<TransceiverID>& tcvrs,
      const std::map<int32_t, PortStatus>& portStatus) noexcept;

  void publishLinkSnapshots(PortID portID);

  // Restore phy state from the last cached warm boot qsfp_service state
  // Called this after initializing all the xphys during warm boot
  void restoreWarmBootPhyState();

  OverrideTcvrToPortAndProfile overrideTcvrToPortAndProfileForTest_;

  // NOTE: The locking order of tcvrsHeldInReset_ and transceivers_ should be
  // tcvrsHeldInReset_ and then transceivers_.

  // Set of ports held in reset.
  folly::Synchronized<std::unordered_set<int>> tcvrsHeldInReset_;

  // Map of TransceiverID to Transceiver Object
  folly::Synchronized<std::map<TransceiverID, std::unique_ptr<Transceiver>>>
      transceivers_;

  /* This variable stores the TransceiverPlatformApi object for controlling
   * the QSFP devies on board. This handle is populated from this class
   * constructor
   */
  std::unique_ptr<TransceiverPlatformApi> qsfpPlatApi_;
  /* This variable stores the PlatformMapping object which has a map for all
   * the components connected on different ports. This handle is populated
   * from this class constructor
   */
  const std::shared_ptr<const PlatformMapping> platformMapping_;
  // A time point until when the remediation of module will be paused.
  // Before reaching that time point, the module is paused
  // and it will resume once the time is reached.
  time_t pauseRemediationUntil_{0};

  mutable PortNameMap portNameToModule_;
  PortGroups portGroupMap_;
  std::unique_ptr<QsfpConfig> qsfpConfig_;
  std::shared_ptr<const TransceiverConfig> tcvrConfig_;

  /* This variable stores the TransceiverValidator object which maintains
   * data structures for all transceiver configurations currently deployed
   * in the fleet. This is left as a nullptr if either the feature flag
   * is not enabled or the relevant structs are not included in the config
   * file.
   */
  std::unique_ptr<TransceiverValidator> tcvrValidator_;

  // For platforms that needs to program xphy
  std::unique_ptr<PhyManager> phyManager_;
  bool hasExternalPhy_{false};

  // Shared pointer to QsfpFsdbSyncManager for publishing to FSDB.
  // Shared between WedgeManager and PortManager.
  std::shared_ptr<QsfpFsdbSyncManager> fsdbSyncManager_;

  // Use the following bidirectional map to cache the static mapping so that
  // we don't have to search from PlatformMapping again and again
  PortNameIdMap portNameToPortID_;

  TcvrIdToTcvrNameMap tcvrIdToTcvrName_;

  folly::Synchronized<std::unordered_set<TransceiverID>> erroredTransceivers_;

  struct SwPortInfo {
    std::optional<TransceiverID> tcvrID;
    std::string name;
  };
  std::unordered_map<PortID, SwPortInfo> portToSwPortInfo_;

  virtual void updateTcvrStateInFsdb(
      TransceiverID /* tcvrID */,
      facebook::fboss::TcvrState&& /* newState */) {}

 private:
  // Forbidden copy constructor and assignment operator
  TransceiverManager(TransceiverManager const&) = delete;
  TransceiverManager& operator=(TransceiverManager const&) = delete;

  using BlockingStateUpdateResultList =
      std::vector<std::shared_ptr<BlockingStateMachineUpdateResult>>;
  void waitForAllBlockingStateUpdateDone(
      const BlockingStateUpdateResultList& results);

  void setForceRemoveTransceiver(TransceiverID tcvrID);

  /*
   * If there are portConfigOverrides in the platform mapping, return
   * the Driver Peaking values for a given port ID.
   */
  std::optional<std::map<uint8_t, uint8_t>> getDriverPeakingOverrides(
      TransceiverID tcvrId,
      cfg::PortProfileID profile,
      size_t numberOfLanes);

  /*
   * This is the private class to capture all information a
   * TransceiverStateMachine needs
   * A Synchronized state_machine to keep track of the state
   * thread and EventBase so that we can operate multiple different
   * transceivers StateMachine update at the same time and also better
   * starting and terminating these threads.
   */
  class TransceiverThreadHelper {
   public:
    explicit TransceiverThreadHelper(const TransceiverID& tcvrID)
        : tcvrID_(tcvrID) {}

    void startThread();
    void stopThread();
    folly::EventBase* getEventBase() const {
      return updateEventBase_.get();
    }

    std::shared_ptr<ThreadHeartbeat> getThreadHeartbeat() {
      return heartbeat_;
    }

   private:
    TransceiverID tcvrID_;
    // Can't use ScopedEventBaseThread as it won't work well with
    // handcrafted RocketClientChannel client instead of servicerouter client
    std::unique_ptr<std::thread> updateThread_;
    std::unique_ptr<folly::EventBase> updateEventBase_;
    std::shared_ptr<ThreadHeartbeat> heartbeat_;
  };

  using TransceiverToPortInfo = std::unordered_map<
      TransceiverID,
      std::shared_ptr<folly::Synchronized<PortToPortInfo>>>;
  TransceiverToPortInfo setupTransceiverToPortInfo();

  void startThreads();
  void stopThreads();
  void threadLoop(folly::StringPiece name, folly::EventBase* eventBase);

  /**
   * Schedule an update to the switch state.
   *
   *  @param  update
              The update to be enqueued
   *  @return bool whether the update was queued or not
   * This schedules the specified TransceiverStateMachineUpdate to be invoked
   * in the update thread in order to update the TransceiverStateMachine.
   *
   */
  bool updateState(
      const TransceiverID& tcvrID,
      std::unique_ptr<TransceiverStateMachineUpdate> update);
  bool enqueueStateUpdate(
      const TransceiverID& tcvrID,
      std::unique_ptr<TransceiverStateMachineUpdate> update);
  void executeStateUpdates();

  void handlePendingUpdates();

  void triggerAgentConfigChangeEvent();

  // Update the cached PortStatus of TransceiverToPortInfo using wedge_agent
  // getPortStatus() results
  void updateTransceiverPortStatus() noexcept;

  std::string xphyWarmBootStateDirectory() const;

  /*
   * ONLY REMOVE can_warm_boot flag file if there's a cold_boot
   */
  void removeWarmBootFlag();

  void readWarmBootStateFile();
  void restoreAgentConfigAppliedInfo();

  bool upgradeFirmware(Transceiver& tcvr);

  bool isRunningAsicPrbs(TransceiverID tcvr) const;

  // Returns the Firmware object from qsfp config for the given module.
  // If there is no firmware in config, returns empty optional
  std::optional<cfg::Firmware> getFirmwareFromCfg(Transceiver& tcvr) const;

  bool isTransceiversMapLocked() const;

  void ensureTransceiversMapLocked(std::string message) const;

  void drainAllStateMachineUpdates();

  std::unordered_set<TransceiverID> getTcvrsReadyForProgramming() const;

  // Helper function to get optical channel config from qsfp config
  std::optional<cfg::OpticalChannelConfig> getOpticalChannelConfig(
      TransceiverID id) const;

  // Store the QSFP service state for warm boots.
  // Updated on every refresh of the state machine as well as during graceful
  // exit.
  std::string qsfpServiceWarmbootState_ = {};

  // TEST ONLY
  // This private map is an override of agent getPortStatus()
  std::map<int32_t, NpuPortStatus> overrideAgentPortStatusForTesting_;
  // This ConfigAppliedInfo is an override of agent getConfigAppliedInfo()
  std::optional<ConfigAppliedInfo> overrideAgentConfigAppliedInfoForTesting_;

  /*
   * A thread for processing TransceiverStateMachine updates.
   */
  std::unique_ptr<std::thread> updateThread_;
  std::unique_ptr<folly::EventBase> updateEventBase_;
  std::shared_ptr<ThreadHeartbeat> updateThreadHeartbeat_;

  // TODO(joseph5wu) Will add heartbeat watchdog later

  // A global flag to indicate whether the service is exiting.
  // If it is, we should not accept any state update
  std::atomic<bool> isExiting_{false};

  // A global flag to indicate whether the any optics firmware upgrade is in
  // progress
  std::atomic<bool> isUpgradingFirmware_{false};

  /*
   * Flag that indicates whether the service has been fully initialized.
   * Fully initialized = system, pim and phys initialized and atleast one
   * successful iteration of refreshStateMachines is complete
   */
  bool isFullyInitialized_{false};

  /*
   * Flag that indicates whether the systema has been initialized.
   * This is set at the end of transceiverManager_->init().
   * By this time, systemContainer, pimContainer and the phy objects
   * have been initialized.
   */
  bool isSystemInitialized_{false};

  /*
   * QsfpServiceThreads contains the threading infrastructure for the service,
   * including a map to maintain all threads for all transceivers.
   */
  const std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads_;

  /*
   * A map to maintain all transceivers(present and absent) programmed SW port
   * to TransceiverPortInfo mapping.
   */
  const TransceiverToPortInfo tcvrToPortInfo_;

  /*
   * A ConfigAppliedInfo to keep track of the last wedge_agent config applied
   * info. refreshStateMachines() will routinely call wedge_agent thrift api
   * to getConfigAppliedInfo() thrift api, and then we can use that to tell
   * whether there's a config change. This will probably:
   * 1) introduce an updated iphy port profile change, like reloading config
   * with new speed;
   * 2) agent coldboot to reset iphy
   * And for the above cases, we need to issue a port re-programming from
   * iphy to xphy to tcvr.
   */
  ConfigAppliedInfo configAppliedInfo_;

  /*
   * qsfp_service warm boot related attributes
   */
  bool canWarmBoot_{false};

  folly::dynamic warmBootState_;

  std::vector<std::shared_ptr<ThreadHeartbeat>> heartbeats_;

  /*
   * A thread dedicated to monitor above state machine thread heartbeats
   */
  std::unique_ptr<ThreadHeartbeatWatchdog> heartbeatWatchdog_;

  /*
   * Tracks how many times a heart beat (from any of the state machine
   * threads) was missed. This counter is periodically published to ODS by
   * StatsPublisher
   */
  std::atomic<long> stateMachineThreadHeartbeatMissedCount_{0};

  void updateNpuPortStatusCache(
      std::map<int, facebook::fboss::NpuPortStatus>& portStatus);

  /*
   * This cache is updated from the FSDB subscription thread and read by the
   * main thread
   */
  folly::Synchronized<
      std::map<int /* agent logical port id */, facebook::fboss::NpuPortStatus>>
      npuPortStatusCache_;
  // Guarded by npuPortStatusCache_'s lock.
  int64_t npuPortStatusCacheUpdatedInMs_{0};
  std::atomic<int64_t> resetDataPathColdbootInMs_{0};

  std::unique_ptr<FbossFwStorage> fwStorage_;

  folly::Synchronized<
      std::unordered_map<const folly::EventBase*, std::vector<TransceiverID>>>
      evbsRunningFirmwareUpgrade_;

  bool forceFirmwareUpgradeForTesting_{false};

  std::map<
      std::pair<ResetType, ResetAction>,
      std::function<void(TransceiverManager* const, int)>>
      resetFunctionMap_;

  /*
   * This cache stores the most recent result of validation for each
   * transceiver.
   */
  folly::Synchronized<std::unordered_set<TransceiverID>>
      nonValidTransceiversCache_;

  void initPortToModuleMap();

  void initTcvrValidator();

  std::atomic<int> maxTimeTakenForFwUpgrade_{0};

  folly::Synchronized<std::unordered_set<TransceiverID>> tcvrsForFwUpgrade;

  using TransceiverToStateMachineControllerMap = std::unordered_map<
      TransceiverID,
      std::unique_ptr<TransceiverStateMachineController>>;
  TransceiverToStateMachineControllerMap
  setupTransceiverToStateMachineControllerMap();

  /*
   * Map of TransceiverID to StateMachineController object, which contains
   * state machine and queue of updates to execute.
   */
  const TransceiverToStateMachineControllerMap stateMachineControllers_;

  folly::Synchronized<std::unordered_set<TransceiverID>>
      tcvrsReadyForProgramming_;

  folly::Synchronized<std::unordered_map<TransceiverID, SnapshotManager>>
      snapshotManagers_;

  void updateSnapshots();

  void ensureFwUpgradeAllowed() const;

  friend class TransceiverStateMachineTest;
};
} // namespace facebook::fboss
