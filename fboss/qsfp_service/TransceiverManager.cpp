// Copyright 2021-present Facebook. All Rights Reserved.
#include "fboss/qsfp_service/TransceiverManager.h"

#include <fmt/core.h>

#include <algorithm>

#include <fb303/ThreadCachedServiceData.h>
#include <folly/FileUtil.h>
#include <folly/json/DynamicConverter.h>
#include <folly/json/json.h>
#include <re2/re2.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/lib/AlertLogger.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"

using namespace std::chrono;

// @nolint(facebook-hte-PreventUseOfDeclareGflag) - Flag defined in
// QsfpConfig.cpp
DECLARE_string(qsfp_service_volatile_dir);

DEFINE_bool(
    can_qsfp_service_warm_boot,
    true,
    "Enable/disable warm boot functionality for qsfp_service");

DEFINE_bool(
    firmware_upgrade_supported,
    false,
    "Set to true to enable firmware upgrade support");

DEFINE_int32(
    max_concurrent_evb_fw_upgrade,
    1,
    "How many transceivers sharing the same evb to schedule a firmware upgrade on at a time");

DEFINE_int32(
    firmware_upgrade_time_limit,
    1080,
    "Maximum time limit (in seconds) for a firmware upgrade test. Exceeding this limit will increment an fb303 counter that keeps track of slow firmware upgrade tests.");

DEFINE_bool(
    enable_tcvr_validation,
    false,
    "Enable transceiver validation feature in qsfp_service");

DEFINE_bool(
    firmware_upgrade_on_coldboot,
    false,
    "Set to true to automatically upgrade firmware on coldboot");

DEFINE_bool(
    firmware_upgrade_on_tcvr_insert,
    false,
    "Set to true to automatically upgrade firmware when a transceiver is inserted");

DEFINE_string(
    transceiver_properties_config,
    "",
    "Path to transceiver_properties.json config file. "
    "Empty string disables config-driven transceiver properties.");

DEFINE_bool(
    port_manager_mode,
    false,
    "Set to true to enable Port Manager mode. This means PortManager object will manage all port-level logic and TransceiverManager object will only manage transceiver-level logic.");

DEFINE_bool(
    override_program_iphy_ports_for_test,
    false,
    "Override wedge_agent programInternalPhyPorts(). For test only");

namespace {
constexpr auto kFbossPortNameRegex = "(eth|fab)(\\d+)/(\\d+)/(\\d+)";
constexpr auto kForceColdBootFileName = "cold_boot_once_qsfp_service";
constexpr auto kWarmBootFlag = "can_warm_boot";
constexpr auto kWarmbootStateFileName = "qsfp_service_state";
static constexpr auto kStateMachineThreadHeartbeatMissed =
    "state_machine_thread_heartbeat_missed";
// A CPO module presents up to (max CMIS banks) x (host lanes per bank) global
// host lanes. Expressed as a product so the cap tracks a future change to the
// bank capacity or per-bank lane count instead of a magic 32. (This is the
// module-agnostic theoretical max used for validation here; CmisModule caps
// per-module reads at the actual getMaxNumBanks() * kMaxOsfpNumLanes.)
constexpr uint8_t kMaxCmisBanks = 4;
constexpr uint8_t kHostLanesPerBank = 8;
constexpr uint8_t kMaxCpoHostLanes = kMaxCmisBanks * kHostLanesPerBank;
static constexpr auto kSuccessfulOpticsFirmwareUpgrade =
    "qsfp.optics_firmware_upgrade.success";
static constexpr auto kFailedOpticsFirmwareUpgrade =
    "qsfp.optics_firmware_upgrade.failed";
static constexpr auto kExceededTimeLimitFirmwareUpgrade =
    "qsfp.optics_firmware_upgrade.exceeded_time_limit";

std::map<int, facebook::fboss::NpuPortStatus> getNpuPortStatus(
    const std::map<int32_t, facebook::fboss::PortStatus>& portStatus) {
  std::map<int, facebook::fboss::NpuPortStatus> npuPortStatus;
  for (const auto& [portId, status] : portStatus) {
    facebook::fboss::NpuPortStatus npuStatus;
    npuStatus.portId = portId;
    npuStatus.operState = folly::copy(status.up().value());
    npuStatus.portEnabled = folly::copy(status.enabled().value());
    npuStatus.profileID = status.profileID().value();
    npuPortStatus.emplace(portId, npuStatus);
  }
  return npuPortStatus;
}

void bumpSuccessfulFwUpgrade() {
  facebook::tcData().addStatValue(
      kSuccessfulOpticsFirmwareUpgrade, 1, facebook::fb303::SUM);
}

void bumpFailedFwUpgrade() {
  facebook::tcData().addStatValue(
      kFailedOpticsFirmwareUpgrade, 1, facebook::fb303::SUM);
}

void bumpTimeExceededFwUpgrade() {
  facebook::tcData().addStatValue(
      kExceededTimeLimitFirmwareUpgrade, 1, facebook::fb303::SUM);
}

// Returns a string version of transceiverId. For (eth|fab)X/Y/Z, returns
// (eth|fab)X/Y
std::string getTcvrNameFromPortName(const std::string& portName) {
  std::string portType;
  int pimID = 0;
  int transceiverID = 0;
  int laneID = 0;
  re2::RE2 portNameRe(kFbossPortNameRegex);
  if (!re2::RE2::FullMatch(
          portName, portNameRe, &portType, &pimID, &transceiverID, &laneID)) {
    throw facebook::fboss::FbossError(
        "Can't figure out transceiver name for port:", portName);
  }

  return fmt::format("{}{}/{}", portType, pimID, transceiverID);
}

bool isTransceiverComponent(
    const facebook::fboss::phy::PortComponent& component) {
  return component == facebook::fboss::phy::PortComponent::TRANSCEIVER_SYSTEM ||
      component == facebook::fboss::phy::PortComponent::TRANSCEIVER_LINE;
}

// Same clock as the agent's ConfigAppliedInfo timestamps
int64_t nowInMs() {
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}
} // namespace

namespace facebook::fboss {

TransceiverManager::TransceiverManager(
    std::unique_ptr<TransceiverPlatformApi> api,
    const std::shared_ptr<const PlatformMapping> platformMapping,
    const std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads)
    : qsfpPlatApi_(std::move(api)),
      platformMapping_(platformMapping),
      qsfpServiceThreads_(qsfpServiceThreads),
      tcvrToPortInfo_(setupTransceiverToPortInfo()),
      stateMachineControllers_(setupTransceiverToStateMachineControllerMap()) {
  // Cache the static mapping based on platformMapping_
  const auto& platformPorts = platformMapping_->getPlatformPorts();
  const auto& chips = platformMapping_->getChips();
  for (const auto& [portIDInt, platformPort] : platformPorts) {
    PortID portID = PortID(portIDInt);
    const auto& portName = *platformPort.mapping()->name();
    portNameToPortID_.insert(PortNameIdMap::value_type(portName, portID));
    SwPortInfo portInfo;
    portInfo.name = portName;
    auto tcvrIds = utility::getTransceiverIds(platformPort, chips);
    portInfo.tcvrID =
        tcvrIds.empty() ? std::nullopt : std::make_optional(tcvrIds[0]);
    portToSwPortInfo_.emplace(portID, std::move(portInfo));
  }
  try {
    fwStorage_ =
        std::make_unique<FbossFwStorage>(FbossFwStorage::initStorage());
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Couldn't create FbossFwStorage instance: "
              << folly::exceptionStr(ex);
  }

  initPortToModuleMap();

  // Initialize the resetFunctionMap_ with proper function
  // per reset type/action. map is better than multiple switch
  // statements when we support more reset types.
  resetFunctionMap_[std::make_pair(
      ResetType::HARD_RESET, ResetAction::RESET_THEN_CLEAR)] =
      &TransceiverManager::triggerQsfpHardReset;
  resetFunctionMap_[std::make_pair(ResetType::HARD_RESET, ResetAction::RESET)] =
      &TransceiverManager::holdTransceiverReset;
  resetFunctionMap_[std::make_pair(
      ResetType::HARD_RESET, ResetAction::CLEAR_RESET)] =
      &TransceiverManager::releaseTransceiverReset;

  tcData().addStatExportType(
      kSuccessfulOpticsFirmwareUpgrade, facebook::fb303::SUM);
  tcData().addStatExportType(
      kFailedOpticsFirmwareUpgrade, facebook::fb303::SUM);
  tcData().addStatExportType(
      kExceededTimeLimitFirmwareUpgrade, facebook::fb303::SUM);
}

TransceiverManager::~TransceiverManager() {
  // Make sure if gracefulExit() is not called, we will still stop the threads
  if (!isExiting_) {
    setGracefulExitingFlag();
    stopThreads();
  }
  restart_time::stop();
}

void TransceiverManager::initPortToModuleMap() {
  const auto& platformPorts = platformMapping_->getPlatformPorts();
  for (const auto& it : platformPorts) {
    auto port = it.second;
    // Get the transceiver id based on the port info from platform mapping.
    auto portId = *port.mapping()->id();
    auto transceiverId = getTransceiverID(PortID(portId));
    if (!transceiverId) {
      XLOG(ERR) << "Did not find transceiver id for port id " << portId;
      continue;
    }
    // Add the port to the transceiver indexed port group.
    auto portGroupIt = portGroupMap_.find(transceiverId.value());
    if (portGroupIt == portGroupMap_.end()) {
      portGroupMap_[transceiverId.value()] =
          std::set<cfg::PlatformPortEntry>{port};
    } else {
      portGroupIt->second.insert(port);
    }
    std::string portName = *port.mapping()->name();
    portNameToModule_[portName] = transceiverId.value();
    auto tcvrName = getTcvrNameFromPortName(portName);
    tcvrIdToTcvrName_[transceiverId.value()] = tcvrName;
    XLOG(DBG2) << "Added port " << portName << " with portId " << portId
               << " to transceiver id: " << transceiverId.value()
               << " transceiver name: " << tcvrName;
  }
}

void TransceiverManager::initTcvrValidator() {
  auto tcvrValConfig = qsfpConfig_->thrift.transceiverValidationConfig();

  if (FLAGS_enable_tcvr_validation && tcvrValConfig.has_value()) {
    tcvrValidator_ =
        std::make_unique<TransceiverValidator>(tcvrValConfig.value());
    XLOG(INFO) << "Enabling transceiver config validation.";
  } else {
    XLOG(INFO) << "Not enabling transceiver config validation.";
  }
}

void TransceiverManager::readWarmBootStateFile() {
  CHECK(canWarmBoot_);

  std::string warmBootJson;
  const auto& warmBootStateFile = warmBootStateFileName();
  if (!folly::readFile(warmBootStateFile.c_str(), warmBootJson)) {
    XLOG(WARN) << "Warm Boot state file: " << warmBootStateFile
               << " doesn't exist, skip restoring warm boot state";
    return;
  }

  warmBootState_ = folly::parseJson(warmBootJson);
}

void TransceiverManager::init() {
  // Check whether we can warm boot
  canWarmBoot_ = checkWarmBootFlags();

  if (!canWarmBoot_) {
    // For cold boot, remove the xphy warm boot state directory if it exists
    std::string xphyDir = xphyWarmBootStateDirectory();
    if (checkFileExists(xphyDir)) {
      XLOG(INFO) << "Cold boot: removing xphy warm boot state directory: "
                 << xphyDir;
      removeDir(xphyDir);
    }
  }

  XLOG(INFO) << "Will attempt " << (canWarmBoot_ ? "WARM" : "COLD") << " boot";

  restart_time::init(FLAGS_qsfp_service_volatile_dir, canWarmBoot_);

  if (canWarmBoot_) {
    // Read the warm boot state file for a warm boot
    readWarmBootStateFile();
    restoreAgentConfigAppliedInfo();
  }

  // Load transceiver properties config (file override or built-in default)
  if (!FLAGS_transceiver_properties_config.empty()) {
    TransceiverPropertiesManager::init(FLAGS_transceiver_properties_config);
  } else {
    TransceiverPropertiesManager::initDefault();
  }

  // Now we might need to start threads
  startThreads();

  // Initialize the PhyManager all ExternalPhy for the system
  initExternalPhyMap(phyManager_.get());
  // Initialize the I2c bus
  initTransceiverMap();

  // Create data structures for validating transceiver configs
  initTcvrValidator();

  if (!isSystemInitialized_) {
    isSystemInitialized_ = true;
    restart_time::mark(RestartEvent::INITIALIZED);
  }
}

QsfpServiceRunState TransceiverManager::getRunState() const {
  if (isExiting()) {
    return QsfpServiceRunState::EXITING;
  }
  if (isUpgradingFirmware()) {
    return QsfpServiceRunState::UPGRADING_FIRMWARE;
  }
  if (isFullyInitialized()) {
    return QsfpServiceRunState::ACTIVE;
  }
  if (isSystemInitialized()) {
    return QsfpServiceRunState::INITIALIZED;
  }
  return QsfpServiceRunState::UNINITIALIZED;
}

void TransceiverManager::restoreAgentConfigAppliedInfo() {
  // Reached in Port Manager mode via init(); PortManager::init() restores its
  // own copy.
  if (FLAGS_port_manager_mode) {
    PORT_MGR_INTENTIONAL_SKIP_LOG("restoreAgentConfigAppliedInfo");
    return;
  }
  if (warmBootState_.isNull()) {
    return;
  }
  if (const auto& agentConfigAppliedIt = warmBootState_.find(
          TransceiverManager::kAgentConfigAppliedInfoStateKey);
      agentConfigAppliedIt != warmBootState_.items().end()) {
    auto agentConfigAppliedInfo = agentConfigAppliedIt->second;
    ConfigAppliedInfo wbConfigAppliedInfo;
    // Restore the last agent config applied timestamp from warm boot state if
    // it exists
    if (const auto& lastAppliedIt = agentConfigAppliedInfo.find(
            TransceiverManager::kAgentConfigLastAppliedInMsKey);
        lastAppliedIt != agentConfigAppliedInfo.items().end()) {
      wbConfigAppliedInfo.lastAppliedInMs() =
          folly::convertTo<long>(lastAppliedIt->second);
    }
    // Restore the last agent coldboot timestamp from warm boot state if
    // it exists
    if (const auto& lastColdBootIt = agentConfigAppliedInfo.find(
            TransceiverManager::kAgentConfigLastColdbootAppliedInMsKey);
        lastColdBootIt != agentConfigAppliedInfo.items().end()) {
      wbConfigAppliedInfo.lastColdbootAppliedInMs() =
          folly::convertTo<long>(lastColdBootIt->second);
    }

    configAppliedInfo_ = wbConfigAppliedInfo;
  }
}

void TransceiverManager::clearAllTransceiverReset() {
  {
    auto tscvrsInReset = tcvrsHeldInReset_.rlock();
    if (tscvrsInReset->empty()) {
      qsfpPlatApi_->clearAllTransceiverReset();
    } else {
      const auto numModules = getNumQsfpModules();
      for (auto idx = 0; idx < numModules; idx++) {
        if (tscvrsInReset->count(idx) == 0) {
          // This api accepts 1 based module id however the module id in
          // TransceiverManager is 0 based.
          qsfpPlatApi_->releaseTransceiverReset(idx + 1);
        }
      }
    }
  }
  // Required delay time between a transceiver getting out of reset and fully
  // functional.
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
  sleep(kSecAfterModuleOutOfReset);
}

void TransceiverManager::setForceRemoveTransceiver(TransceiverID id) {
  auto stateMachineControllerItr = stateMachineControllers_.find(id);
  if (stateMachineControllerItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }

  stateMachineControllerItr->second->getStateMachine().wlock()->get_attribute(
      forceRemoveTransceiver) = true;
}

void TransceiverManager::hardResetAction(
    void (TransceiverPlatformApi::*func)(unsigned int),
    int idx,
    bool holdInReset,
    bool removeTransceiver) {
  // Order of locking is important.
  auto trscvrsInreset = tcvrsHeldInReset_.wlock();
  TransceiverID id = TransceiverID(idx);
  if (holdInReset) {
    trscvrsInreset->insert(idx);
  } else {
    trscvrsInreset->erase(idx);
  }
  // This api accepts 1 based module id however the module id in
  // TransceiverManager is 0 based.
  (qsfpPlatApi_.get()->*func)(idx + 1);
  if (removeTransceiver) {
    setForceRemoveTransceiver(id);
    {
      // Read Lock to trigger all state machine changes
      auto lockedTransceivers = transceivers_.rlock();
      if (auto it = lockedTransceivers->find(id);
          it != lockedTransceivers->end()) {
        it->second->removeTransceiver();
        removeTransceiver = true;
      }
    }

    // Write lock to remove the transceiver
    auto lockedTransceivers = transceivers_.wlock();
    lockedTransceivers->erase(id);
    // Reset transceiver associated port states in FSDB.
    resetPortState(id);
  }
}

void TransceiverManager::triggerQsfpHardReset(int idx) {
  hardResetAction(
      &TransceiverPlatformApi::triggerQsfpHardReset,
      idx,
      false /*holdInReset*/,
      true /*RemoveTransceiver*/);
  MODULE_LOG(INFO, "", idx) << "triggerQsfpHardReset called";
}

void TransceiverManager::holdTransceiverReset(int idx) {
  hardResetAction(
      &TransceiverPlatformApi::holdTransceiverReset,
      idx,
      true /*holdInReset*/,
      true /*RemoveTransceiver*/);
  MODULE_LOG(INFO, "", idx) << "holdTransceiverReset called";
}

void TransceiverManager::releaseTransceiverReset(int idx) {
  hardResetAction(
      &TransceiverPlatformApi::releaseTransceiverReset,
      idx,
      false /*holdInReset*/,
      false /*RemoveTransceiver*/);
  MODULE_LOG(INFO, "", idx) << "releaseTransceiverReset called";
}

void TransceiverManager::gracefulExit() {
  steady_clock::time_point begin = steady_clock::now();
  XLOG(INFO) << "[Exit] Starting TransceiverManager graceful exit";
  // Stop all the threads before shutdown
  setGracefulExitingFlag();
  stopThreads();
  steady_clock::time_point stopThreadsDone = steady_clock::now();
  XLOG(INFO) << "[Exit] Stopped all state machine threads. Stop time: "
             << duration_cast<duration<float>>(stopThreadsDone - begin).count();

  // In Port Manager mode, PortManager will be responsible for cleaning up PHY
  // state.
  if (!FLAGS_port_manager_mode) {
    // Set all warm boot related files before gracefully shut down
    setWarmBootState();

    // Do a graceful shutdown of the phy.
    if (phyManager_) {
      phyManager_->gracefulExit();
    }
  }

  setCanWarmBoot();

  steady_clock::time_point setWBFilesDone = steady_clock::now();
  XLOG(INFO) << "[Exit] Done creating Warm Boot related files. Stop time: "
             << duration_cast<duration<float>>(setWBFilesDone - stopThreadsDone)
                    .count()
             << std::endl
             << "[Exit] Total TransceiverManager graceful Exit time: "
             << duration_cast<duration<float>>(setWBFilesDone - begin).count();
  XLOG(INFO) << "[Exit] QSFP Service Warm boot state: " << std::endl
             << qsfpServiceWarmbootState_;
}

const TransceiverManager::PortNameMap&
TransceiverManager::getPortNameToModuleMap() const {
  if (portNameToModule_.empty()) {
    const auto& platformPorts = platformMapping_->getPlatformPorts();
    for (const auto& it : platformPorts) {
      auto port = it.second;
      auto transceiverIds =
          utility::getTransceiverIds(port, platformMapping_->getChips());
      if (transceiverIds.empty()) {
        continue;
      }

      auto& portName = *(port.mapping()->name());
      portNameToModule_[portName] = transceiverIds[0];
    }
  }

  return portNameToModule_;
}

const std::set<std::string> TransceiverManager::getPortNames(
    TransceiverID tcvrId) const {
  std::set<std::string> ports;
  auto it = portGroupMap_.find(tcvrId);
  if (it != portGroupMap_.end() && !it->second.empty()) {
    for (const auto& port : it->second) {
      ports.insert(*port.mapping()->name());
    }
  }
  return ports;
}

const std::string TransceiverManager::getPortName(TransceiverID tcvrId) const {
  auto portNames = getPortNames(tcvrId);
  return portNames.empty() ? "" : *portNames.begin();
}

const std::string TransceiverManager::getTransceiverName(
    const TransceiverID& tcvrId) const {
  if (tcvrIdToTcvrName_.find(tcvrId) != tcvrIdToTcvrName_.end()) {
    return tcvrIdToTcvrName_.at(tcvrId);
  }
  return "";
}

std::map<std::string, FirmwareUpgradeData>
TransceiverManager::getPortsRequiringOpticsFwUpgrade() const {
  std::map<std::string, FirmwareUpgradeData> ports;
  if (!isFullyInitialized()) {
    throw FbossError("Service is still initializing...");
  }
  if (!FLAGS_firmware_upgrade_supported) {
    return ports;
  }
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& tcvrIt : *lockedTransceivers) {
    auto firmwareUpgradeData = getFirmwareUpgradeData(*tcvrIt.second);
    if (firmwareUpgradeData.has_value()) {
      ports[getPortName(tcvrIt.first)] = firmwareUpgradeData.value();
    }
  }
  return ports;
}

void TransceiverManager::ensureFwUpgradeAllowed() const {
  std::string reason;
  if (!isFullyInitialized()) {
    reason = "Service is still initializing...";
  } else if (isUpgradingFirmware()) {
    reason = "Service is already upgrading firmware...";
  } else if (!FLAGS_firmware_upgrade_supported) {
    reason =
        "Firmware upgrade is not supported, --firmware_upgrade_supported is false";
  }
  if (reason.empty()) {
    return;
  }
  XLOG(ERR) << "Rejecting optics firmware upgrade request: " << reason;
  throw FbossError(reason);
}

std::map<std::string, FirmwareUpgradeData>
TransceiverManager::triggerAllOpticsFwUpgrade() {
  ensureFwUpgradeAllowed();
  std::map<std::string, FirmwareUpgradeData> ports;
  auto portsForFwUpgrade = getPortsRequiringOpticsFwUpgrade();

  std::vector<std::string> interfaces;
  interfaces.reserve(portsForFwUpgrade.size());
  for (const auto& [portName, _] : portsForFwUpgrade) {
    interfaces.push_back(portName);
  }
  return triggerOpticsFwUpgrade(interfaces);
}

std::map<std::string, FirmwareUpgradeData>
TransceiverManager::triggerOpticsFwUpgrade(
    const std::vector<std::string>& interfaces) {
  ensureFwUpgradeAllowed();
  auto portsForFwUpgrade = getPortsRequiringOpticsFwUpgrade();
  std::map<std::string, FirmwareUpgradeData> portsSelectedForUpgrade;
  auto tcvrsToUpgradeWLock = tcvrsForFwUpgrade.wlock();

  for (const auto& portName : interfaces) {
    if (portsForFwUpgrade.find(portName) == portsForFwUpgrade.end()) {
      XLOG(ERR) << "Port " << portName << " does not require firmware upgrade";
      continue;
    }
    if (portNameToModule_.find(portName) != portNameToModule_.end()) {
      auto tcvrID = TransceiverID(portNameToModule_[portName]);
      XLOG(INFO)
          << FirmwareUpgradeAlert()
          << "Selected for firmware upgrade through triggerOpticsFwUpgrade function"
          << TransceiverParam(tcvrID) << PortParam(portName);
      tcvrsToUpgradeWLock->insert(TransceiverID(portNameToModule_[portName]));

      portsSelectedForUpgrade[portName] = portsForFwUpgrade[portName];
    }
  }
  return portsSelectedForUpgrade;
}

bool TransceiverManager::firmwareUpgradeRequired(TransceiverID id) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    FW_LOG(ERR, id) << "FirmwareUpgradeRequired is not in transceiver map";
    return false;
  }
  auto& tcvr = *tcvrIt->second;
  bool canUpgrade = false;
  bool iOevbBusy = false;
  bool present = tcvr.isPresent();
  std::string partNumber = tcvr.getPartNumber();
  bool requiresUpgrade = present && getFirmwareUpgradeData(tcvr).has_value();
  if (forceFirmwareUpgradeForTesting_ || requiresUpgrade) {
    // If we are here, it means that this transceiver is present and has the
    // firmware version mismatch and hence requires upgrade
    // We also need to check that at any time one i2c evb should run firmware
    // upgrade. If not, the other transceivers will be inoperational for a long
    // time until the first transceiver is done upgrading
    {
      auto fwEvbWLock = evbsRunningFirmwareUpgrade_.wlock();
      auto moduleEvb = tcvrIt->second->getEvb();
      if (fwEvbWLock->find(moduleEvb) == fwEvbWLock->end()) {
        fwEvbWLock->emplace(moduleEvb, std::vector<TransceiverID>());
      }
      if (fwEvbWLock->at(moduleEvb).size() <
          FLAGS_max_concurrent_evb_fw_upgrade) {
        fwEvbWLock->at(moduleEvb).push_back(id);
        canUpgrade = true;
      } else {
        iOevbBusy = true;
      }
    } // end of fwEvbWLock lock
  }

  FW_LOG(INFO, id) << "FirmwareUpgradeRequired: " << " present: " << present
                   << " partNumber: " << partNumber
                   << " iOevbBusy:" << iOevbBusy
                   << " canUpgrade: " << canUpgrade
                   << " requiresUpgrade: " << requiresUpgrade;
  return canUpgrade;
}

std::optional<cfg::Firmware> TransceiverManager::getFirmwareFromCfg(
    Transceiver& tcvr) const {
  int tcvrID = tcvr.getID();
  if (!qsfpConfig_) {
    FW_LOG(DBG4, tcvrID) << "QsfpConfig is NULL. No Firmware to return";
    return std::nullopt;
  }

  const auto& qsfpCfg = qsfpConfig_->thrift;
  auto qsfpCfgFw = qsfpCfg.transceiverFirmwareVersions();
  if (!qsfpCfgFw.has_value()) {
    FW_LOG(DBG4, tcvrID)
        << "TransceiverFirmwareVersions is NULL. No Firmware to return";
    return std::nullopt;
  }

  auto cachedTcvrInfo = tcvr.getTransceiverInfo();
  auto vendor = cachedTcvrInfo.tcvrState()->vendor();
  if (!vendor.has_value()) {
    FW_LOG(DBG4, tcvrID) << "Vendor not set. No Firmware to return";
    return std::nullopt;
  }

  auto fwVersionInCfgIt =
      qsfpCfgFw->versionsMap()->find(vendor->partNumber().value());
  if (fwVersionInCfgIt == qsfpCfgFw->versionsMap()->end()) {
    FW_LOG(DBG4, tcvrID)
        << "TransceiverFirmwareVersions doesn't have a firmware version for part number "
        << vendor->partNumber().value();
    return std::nullopt;
  }

  return fwVersionInCfgIt->second;
}

std::optional<FirmwareUpgradeData> TransceiverManager::getFirmwareUpgradeData(
    Transceiver& tcvr) const {
  // Returns a FirmwareUpgrade if the current firmware revision is different
  // than the one in qsfp config else std::nullopt
  auto cachedTcvrInfo = tcvr.getTransceiverInfo();
  auto moduleStatus = cachedTcvrInfo.tcvrState()->status();
  int tcvrID = tcvr.getID();
  const std::string partNumber = tcvr.getPartNumber();

  if (!moduleStatus.has_value()) {
    FW_LOG(DBG4, tcvrID)
        << "Part Number " << partNumber
        << " moduleStatus not set. Returning nullopt from getFirmwareUpgradeData";
    return std::nullopt;
  }

  auto cmisModuleState = moduleStatus->cmisModuleState();

  auto fwStatus = moduleStatus->fwStatus();
  if (!fwStatus.has_value()) {
    FW_LOG(DBG4, tcvrID)
        << "Part Number " << partNumber
        << " fwStatus not set. Returning nullopt from getFirmwareUpgradeData";
    return std::nullopt;
  }
  FirmwareUpgradeData fwUpgradeData;
  fwUpgradeData.partNumber() = partNumber;

  auto fwFromConfig = getFirmwareFromCfg(tcvr);
  if (!fwFromConfig.has_value()) {
    FW_LOG(DBG4, tcvrID)
        << "Part Number " << partNumber
        << " Fw not available in config. Returning nullopt from getFirmwareUpgradeData";
    return std::nullopt;
  }

  auto& versions = *fwFromConfig->versions();
  for (auto fwIt : versions) {
    const auto& fwType = folly::copy(fwIt.fwType().value());
    if (fwType == cfg::FirmwareType::APPLICATION && fwStatus->version()) {
      auto currentVersion = *fwStatus->version();
      auto configVersion = fwIt.version().value();

      // If config specifies a 3-tuple version (e.g., "1.0.7680"), append the
      // module's buildNumber to the reported version. If the module does not report a buildNumber, skip upgrade
      // since the config is treated as SOT and the module can't be validated.
      // 2-tuple configs (e.g., "1.0") compare as before.
      if (std::count(configVersion.begin(), configVersion.end(), '.') >= 2) {
        if (fwStatus->buildNumber().has_value()) {
          currentVersion = fmt::format(
              "{}.{}", currentVersion, fwStatus->buildNumber().value());
        } else {
          FW_LOG(INFO, tcvrID)
              << "Part Number " << partNumber
              << " Config specifies 3-tuple version=" << configVersion
              << " but module does not report buildNumber."
              << " Skipping firmware upgrade";
          continue;
        }
      }

      if (configVersion != currentVersion) {
        FW_LOG(INFO, tcvrID)
            << "Part Number " << partNumber
            << " Application Version in cfg=" << configVersion
            << " current operational version= " << currentVersion
            << ". Returning valid getFirmwareUpgradeData";
        fwUpgradeData.currentFirmwareVersion() = currentVersion;
        fwUpgradeData.desiredFirmwareVersion() = configVersion;
        return fwUpgradeData;
      }
    }
    if (fwType == cfg::FirmwareType::DSP && fwStatus->dspFwVer() &&
        fwIt.version().value() != *fwStatus->dspFwVer()) {
      if (cmisModuleState &&
          cmisModuleState.value() == CmisModuleState::LOW_POWER &&
          fwStatus->dspFwVer().value() == "0.0" &&
          partNumber == "QDD-400G-XDR4") {
        // QDD-400G-XDR4 has a bug in the firmware where it reports a 0.0 in DSP
        // FW version when in low power mode. This is a known issue and since
        // it's an old part, we don't plan to fix it forward. Thus adding a
        // special check for this part here
        FW_LOG(INFO, tcvrID)
            << "Not considering for DSP FW Upgrade as QDD-400G-XDR4 incorrectly reports 0.0 as DSP FW Version in low power mode";
      } else {
        FW_LOG(INFO, tcvrID)
            << "Part Number " << partNumber
            << " DSP Version in cfg=" << fwIt.version().value()
            << " current operational version= " << *fwStatus->dspFwVer()
            << ". Returning valid getFirmwareUpgradeData";
        fwUpgradeData.currentFirmwareVersion() = *fwStatus->dspFwVer();
        fwUpgradeData.desiredFirmwareVersion() = fwIt.version().value();
        return fwUpgradeData;
      }
    }
    FW_LOG(DBG, tcvrID) << "Part Number " << partNumber << " FW Type Cfg "
                        << apache::thrift::util::enumNameSafe(fwType)
                        << " FW Version CFG " << fwIt.version().value()
                        << " FW Version Status "
                        << fwStatus->version().value_or("NOT_SET");
  }

  FW_LOG(INFO, tcvrID)
      << "Part Number " << partNumber
      << " num versions found: " << versions.size()
      << " Version match in getFirmwareUpgradeData. Not Upgrading";

  // Versions match. No need to upgrade firmware
  return std::nullopt;
}

bool TransceiverManager::upgradeFirmware(Transceiver& tcvr) {
  std::optional<cfg::Firmware> fwFromConfig = getFirmwareFromCfg(tcvr);
  const auto tcvrID = tcvr.getID();
  std::string partNumber = tcvr.getPartNumber();
  if (fwFromConfig.has_value()) {
    FW_LOG(INFO, tcvrID)
        << "Upgrading firmware to the one in qsfp config. PartNumber="
        << partNumber;
  } else {
    FW_LOG(ERR, tcvrID) << "No firmware version found to upgrade. partNumber="
                        << partNumber;
    return false;
  }

  std::string fwStorageHandleName = tcvr.getFwStorageHandle();
  if (fwStorageHandleName.empty()) {
    FW_LOG(ERR, tcvrID) << "Can't find the fwStorage handle. Part Number="
                        << partNumber
                        << " fwStorageHandle=" << fwStorageHandleName
                        << ". Skipping fw upgrade";
    return false;
  }

  if (!fwStorage()) {
    FW_LOG(ERR, tcvrID)
        << "FbossFwStorage not initialized. Firmware upgrade not supported. "
        << "Part Number=" << partNumber
        << " fwStorageHandle=" << fwStorageHandleName
        << ". Skipping fw upgrade";
    return false;
  }

  std::vector<std::unique_ptr<FbossFirmware>> fwList;

  auto& fwVersions = *(fwFromConfig->versions());
  for (const auto& fw : fwVersions) {
    fwList.emplace_back(
        fwStorage()->getFirmware(fwStorageHandleName, *fw.version()));

    FW_LOG(INFO, tcvrID) << "Adding FW for upgrade. Firmware type="
                         << apache::thrift::util::enumNameSafe(*fw.fwType())
                         << " Part Number=" << partNumber
                         << " fwStorageHandle=" << fwStorageHandleName
                         << " Version=" << *fw.version();
  }

  auto start = std::chrono::steady_clock::now();
  bool upgradeResult = tcvr.upgradeFirmware(fwList);
  auto end = std::chrono::steady_clock::now();
  auto upgradeTime = static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(end - start).count());

  if (upgradeTime > FLAGS_firmware_upgrade_time_limit) {
    bumpTimeExceededFwUpgrade();
    int prevMaxTime = maxTimeTakenForFwUpgrade_.load();
    while (prevMaxTime < upgradeTime &&
           !maxTimeTakenForFwUpgrade_.compare_exchange_weak(
               prevMaxTime, upgradeTime)) {
      prevMaxTime = maxTimeTakenForFwUpgrade_.load();
    }
  }

  FW_LOG(INFO, tcvrID) << "Firmware upgrade time was " << upgradeTime
                       << " seconds. Expected time was "
                       << FLAGS_firmware_upgrade_time_limit << " seconds.";
  return upgradeResult;
}

void TransceiverManager::doTransceiverFirmwareUpgrade(TransceiverID tcvrID) {
  std::vector<folly::Future<bool>> futResponses;
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(tcvrID);
  if (tcvrIt == lockedTransceivers->end() || !tcvrIt->second->isPresent()) {
    FW_LOG(ERR, tcvrID) << "Not found. Can't do firmware upgrade";
    return;
  }
  auto& tcvr = *tcvrIt->second;
  auto portName = getPortName(tcvrID);
  XLOG(INFO) << FirmwareUpgradeAlert()
             << "Starting firmware upgrade attempt for"
             << TransceiverParam(tcvrID) << PortParam(portName)
             << " Part Number=" << tcvr.getPartNumber();

  auto updateStateInFsdb = [&](bool status) {
    if (FLAGS_publish_stats_to_fsdb) {
      auto tcvrInfo = tcvr.updateFwUpgradeStatusInTcvrInfoLocked(status);
      updateTcvrStateInFsdb(tcvrIt->first, std::move(*tcvrInfo.tcvrState()));
    }
  };

  updateStateInFsdb(true);
  if (upgradeFirmware(*tcvrIt->second)) {
    bumpSuccessfulFwUpgrade();
    XLOG(INFO) << FirmwareUpgradeAlert() << "Firmware upgrade SUCCESSFUL for"
               << TransceiverParam(tcvrID) << PortParam(portName);
  } else {
    bumpFailedFwUpgrade();
    XLOG(ERR) << FirmwareUpgradeAlert() << "Firmware upgrade FAILED for"
              << TransceiverParam(tcvrID) << PortParam(portName);
  }
  // We will leave the fwUpgradeStatus as true for now because we still have a
  // lot to do after upgrading the firmware. The optic goes through reset, the
  // state machine goes back to discovered state. IPHY + XPHY ports get
  // programmed again, Optic itself gets programmed again. Therefore, we'll
  // set the fwUpgradeInProgress status to false after we are truly done
  // getting the optic ready after fw upgrade
}

void TransceiverManager::getPortMediaInterface(
    std::map<std::string, MediaInterfaceCode>& portMediaInterface) {
  std::map<int32_t, TransceiverInfo> infoMap;
  getTransceiversInfo(
      infoMap, std::make_unique<std::vector<int32_t>>(std::vector<int32_t>()));

  for (const auto& tcvrInfo : infoMap) {
    auto& tcvrState = *tcvrInfo.second.tcvrState();
    auto tcvrSettings = tcvrState.settings();
    if (!tcvrSettings) {
      continue;
    }
    auto mediaInterface = tcvrSettings->mediaInterface();
    if (!mediaInterface) {
      continue;
    }
    for (const auto& [portName, mediaLanes] :
         *tcvrState.portNameToMediaLanes()) {
      if (!mediaLanes.empty()) {
        auto firstLane = *mediaLanes.begin();
        if (firstLane < mediaInterface->size()) {
          portMediaInterface[portName] =
              mediaInterface->at(firstLane).code().value();
        }
      }
    }
  }
}

void TransceiverManager::triggerTransceiverEventsForAgentConfigChangeEvent(
    bool resetDataPath,
    ConfigAppliedInfo newConfigAppliedInfo) {
  int numResetToDiscovered{0}, numResetToNotPresent{0};
  const auto& presentTransceivers = getPresentTransceivers();
  BlockingStateUpdateResultList results;
  if (resetDataPath) {
    resetDataPathColdbootInMs_ =
        newConfigAppliedInfo.lastColdbootAppliedInMs().value_or(0);
  }
  for (auto& stateMachine : stateMachineControllers_) {
    // Only need to set true to `needResetDataPath` attribute here. And leave
    // the state machine to change it to false once it finishes
    // programTransceiver
    if (resetDataPath) {
      stateMachine.second->getStateMachine().wlock()->get_attribute(
          needResetDataPath) = true;
    }
    auto tcvrID = stateMachine.first;
    if (presentTransceivers.find(tcvrID) != presentTransceivers.end()) {
      if (auto result = updateStateBlockingWithoutWait(
              tcvrID,
              TransceiverStateMachineEvent::TCVR_EV_RESET_TO_DISCOVERED)) {
        ++numResetToDiscovered;
        results.push_back(result);
      }
    } else {
      if (auto result = updateStateBlockingWithoutWait(
              tcvrID,
              TransceiverStateMachineEvent::TCVR_EV_RESET_TO_NOT_PRESENT)) {
        ++numResetToNotPresent;
        results.push_back(result);
      }
    }
  }
  waitForAllBlockingStateUpdateDone(results);
  XLOG(INFO) << "triggerTransceiverEventsForAgentConfigChangeEvent has "
             << numResetToDiscovered
             << " transceivers state machines set back to discovered, "
             << numResetToNotPresent << " set back to not_present";
  configAppliedInfo_ = newConfigAppliedInfo;
}

TransceiverManager::TransceiverToStateMachineControllerMap
TransceiverManager::setupTransceiverToStateMachineControllerMap() {
  TransceiverToStateMachineControllerMap stateMachineMap;
  for (const auto& tcvrID :
       utility::getTransceiverIds(platformMapping_->getChips())) {
    auto stateMachineController =
        std::make_unique<TransceiverManager::TransceiverStateMachineController>(
            tcvrID);
    auto& stateMachine = stateMachineController->getStateMachine();
    stateMachine.withWLock([&](auto& lockedStateMachine) {
      lockedStateMachine.get_attribute(transceiverMgrPtr) = this;
    });
    stateMachineMap.emplace(tcvrID, std::move(stateMachineController));
  }

  return stateMachineMap;
}

TransceiverManager::TransceiverToPortInfo
TransceiverManager::setupTransceiverToPortInfo() {
  TransceiverManager::TransceiverToPortInfo tcvrToPortInfo;
  for (const auto& tcvrID :
       utility::getTransceiverIds(platformMapping_->getChips())) {
    auto portToPortInfo =
        std::make_shared<folly::Synchronized<PortToPortInfo>>();
    tcvrToPortInfo.emplace(tcvrID, std::move(portToPortInfo));
  }

  return tcvrToPortInfo;
}

void TransceiverManager::startThreads() {
  // Setup all TransceiverStateMachineHelper thread
  if (!qsfpServiceThreads_) {
    throw FbossError(
        "Attempting to initialize TransceiverManager without initializing thread object.");
  }

  for (auto& [threadId, threadHelper] : qsfpServiceThreads_->threadIdToThread) {
    threadHelper.startThread();
    heartbeats_.push_back(threadHelper.getThreadHeartbeat());
  }

  XLOG(DBG2) << "Started TransceiverStateMachineUpdateThread";
  updateEventBase_ = std::make_unique<folly::EventBase>();
  updateThread_.reset(new std::thread([=, this] {
    this->threadLoop(
        "TransceiverStateMachineUpdateThread", updateEventBase_.get());
  }));

  auto heartbeatStatsFunc = [this](int /* delay */, int /* backLog */) {};
  updateThreadHeartbeat_ = std::make_shared<ThreadHeartbeat>(
      updateEventBase_.get(),
      "updateThreadHeartbeat",
      FLAGS_state_machine_update_thread_heartbeat_ms,
      heartbeatStatsFunc);
  heartbeats_.push_back(updateThreadHeartbeat_);

  // Create a watchdog that will monitor the heartbeats of all the threads and
  // increment the missed counter when there is no heartbeat on at least one
  // thread in the last FLAGS_state_machine_update_thread_heartbeat_ms * 10
  // time
  heartbeatWatchdog_ = std::make_unique<ThreadHeartbeatWatchdog>(
      std::chrono::milliseconds(
          FLAGS_state_machine_update_thread_heartbeat_ms * 10),
      [this]() {
        stateMachineThreadHeartbeatMissedCount_ += 1;
        tcData().setCounter(
            kStateMachineThreadHeartbeatMissed,
            stateMachineThreadHeartbeatMissedCount_);
      });
  // Initialize the kStateMachineThreadHeartbeatMissed counter
  tcData().setCounter(kStateMachineThreadHeartbeatMissed, 0);
  // Start monitoring the heartbeats of all the threads
  for (auto heartbeat : heartbeats_) {
    heartbeatWatchdog_->startMonitoringHeartbeat(heartbeat);
  }
  // Kick off the heartbeat monitoring
  heartbeatWatchdog_->start();
}

void TransceiverManager::stopThreads() {
  if (heartbeatWatchdog_) {
    heartbeatWatchdog_->stop();
    heartbeatWatchdog_.reset();
  }
  for (auto heartbeat_ : heartbeats_) {
    heartbeat_.reset();
  }

  // We use runInEventBaseThread() to terminateLoopSoon() rather than calling
  // it directly here.  This ensures that any events already scheduled via
  // runInEventBaseThread() will have a chance to run.
  drainAllStateMachineUpdates();
  if (updateThread_) {
    updateEventBase_->runInEventBaseThread(
        [this] { updateEventBase_->terminateLoopSoon(); });
    updateThread_->join();
    XLOG(DBG2) << "Terminated TransceiverStateMachineUpdateThread";
  }

  // And finally stop all TransceiverStateMachineHelper thread
  for (auto& [threadId, threadHelper] : qsfpServiceThreads_->threadIdToThread) {
    threadHelper.stopThread();
  }
}

void TransceiverManager::threadLoop(
    folly::StringPiece name,
    folly::EventBase* eventBase) {
  initThread(name);
  eventBase->loopForever();
}

void TransceiverManager::updateStateBlocking(
    TransceiverID id,
    TransceiverStateMachineEvent event) {
  auto result = updateStateBlockingWithoutWait(id, event);
  if (result) {
    result->wait();
  }
}

std::shared_ptr<BlockingStateMachineUpdateResult>
TransceiverManager::enqueueStateUpdateForTcvrWithoutExecuting(
    TransceiverID id,
    TransceiverStateMachineEvent event) {
  auto result = std::make_shared<BlockingStateMachineUpdateResult>();
  auto update = std::make_unique<
      TransceiverManager::BlockingTransceiverStateMachineUpdate>(event, result);
  if (enqueueStateUpdate(id, std::move(update))) {
    // Only return blocking result if the update has been added in queue
    return result;
  }
  return nullptr;
}

std::shared_ptr<BlockingStateMachineUpdateResult>
TransceiverManager::updateStateBlockingWithoutWait(
    TransceiverID id,
    TransceiverStateMachineEvent event) {
  auto result = std::make_shared<BlockingStateMachineUpdateResult>();
  auto update = std::make_unique<
      TransceiverManager::BlockingTransceiverStateMachineUpdate>(event, result);
  if (updateState(id, std::move(update))) {
    // Only return blocking result if the update has been added in queue
    return result;
  }
  return nullptr;
}

bool TransceiverManager::enqueueStateUpdate(
    const TransceiverID& tcvrID,
    std::unique_ptr<TransceiverStateMachineUpdate> update) {
  const std::string eventName =
      apache::thrift::util::enumNameSafe(update->getEvent());
  if (isExiting_) {
    SM_LOG(WARN, tcvrID) << "Skipped queueing event: " << eventName
                         << ", since exit already started";
    return false;
  }
  if (!updateEventBase_) {
    SM_LOG(WARN, tcvrID) << "Skipped queueing event: " << eventName
                         << ", since updateEventBase_ is not created yet";
    return false;
  }
  auto stateMachineItr = stateMachineControllers_.find(tcvrID);
  if (stateMachineItr == stateMachineControllers_.end()) {
    SM_LOG(WARN, tcvrID) << "Skipped queueing event: " << eventName
                         << ", since TransceiverStateMachine doesn't exist";
    return false;
  }
  stateMachineItr->second->enqueueUpdate(std::move(update));

  return true;
}

void TransceiverManager::executeStateUpdates() {
  // Signal the update thread that updates are pending.
  updateEventBase_->runInEventBaseThread(
      [this] { this->handlePendingUpdates(); });
}

bool TransceiverManager::updateState(
    const TransceiverID& tcvrID,
    std::unique_ptr<TransceiverStateMachineUpdate> update) {
  if (!enqueueStateUpdate(tcvrID, std::move(update))) {
    return false;
  }
  // Signal the update thread that updates are pending.
  executeStateUpdates();
  return true;
}

void TransceiverManager::handlePendingUpdates() {
  // Try to run one state machine updates on each transceiver.
  // Invoked once per enqueued state update, so rate limit to keep this from
  // dominating the log on large transceiver-count platforms.
  XLOG_EVERY_MS(DBG2, 60000)
      << "Trying to update all TransceiverStateMachines";

  // To expedite all these different transceivers state update, use Future
  std::vector<folly::Future<folly::Unit>> stateUpdateTasks;
  for (auto& [tcvrID, stateMachineController] : stateMachineControllers_) {
    auto* eventBase = getEventBaseForTcvr(qsfpServiceThreads_, tcvrID);
    if (!eventBase) {
      MODULE_LOG(WARN, "", tcvrID)
          << "Unrecognized, can't find EventBase for it. Skip updating.";
      continue;
    }

    stateUpdateTasks.push_back(
        folly::via(eventBase).thenValue([&stateMachineController](auto&&) {
          stateMachineController->executeSingleUpdate();
        }));
  }
  folly::collectAll(stateUpdateTasks).wait();
}

TransceiverStateMachineState TransceiverManager::getCurrentState(
    TransceiverID id) const {
  auto stateMachineItr = stateMachineControllers_.find(id);
  if (stateMachineItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }
  return stateMachineItr->second->getCurrentState();
}

TransceiverStateMachineState TransceiverManager::getCurrentStateSnapshot(
    TransceiverID id) const {
  auto stateMachineItr = stateMachineControllers_.find(id);
  if (stateMachineItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }
  return stateMachineItr->second->getCurrentStateSnapshot();
}

const state_machine<TransceiverStateMachine>&
TransceiverManager::getStateMachineForTesting(TransceiverID id) const {
  auto stateMachineItr = stateMachineControllers_.find(id);
  if (stateMachineItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }
  const auto& lockedStateMachine =
      stateMachineItr->second->getStateMachine().rlock();
  return *lockedStateMachine;
}

bool TransceiverManager::getNeedResetDataPath(TransceiverID id) const {
  auto stateMachineItr = stateMachineControllers_.find(id);
  if (stateMachineItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }
  return stateMachineItr->second->getStateMachine().rlock()->get_attribute(
      needResetDataPath);
}

std::vector<TransceiverID> TransceiverManager::triggerProgrammingEvents() {
  std::vector<TransceiverID> programmedTcvrs;
  int32_t numProgramIphy{0}, numProgramXphy{0}, numProgramTcvr{0},
      numPrepareTcvr{0};
  BlockingStateUpdateResultList results;
  steady_clock::time_point begin = steady_clock::now();

  // If Port Manager mode is enabled, fetch the programReady status of all
  // transceivers. Create a copy of the map so we don't have to hold the lock
  // on tcvrsReadyForProgramming for the duration of the function.
  const auto tcvrsReadyForProgramming = FLAGS_port_manager_mode
      ? getTcvrsReadyForProgramming()
      : std::unordered_set<TransceiverID>{};

  // PHY programming is managed by PortManager when Port Manager mode is
  // enabled.
  bool shouldProgramPhy = !FLAGS_port_manager_mode;
  for (auto& stateMachine : stateMachineControllers_) {
    bool needProgramIphy{false}, needProgramXphy{false}, needProgramTcvr{false},
        moduleStateReady{false};
    {
      const auto& lockedStateMachine =
          stateMachine.second->getStateMachine().rlock();
      needProgramIphy = !lockedStateMachine->get_attribute(isIphyProgrammed) &&
          shouldProgramPhy;
      needProgramXphy = !lockedStateMachine->get_attribute(isXphyProgrammed) &&
          shouldProgramPhy;
      needProgramTcvr =
          !lockedStateMachine->get_attribute(isTransceiverProgrammed);
      moduleStateReady =
          (getStateByOrder(*lockedStateMachine->current_state()) ==
           TransceiverStateMachineState::TRANSCEIVER_READY);
    }
    auto tcvrID = stateMachine.first;
    if (needProgramIphy) {
      if (auto result = updateStateBlockingWithoutWait(
              tcvrID, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_IPHY)) {
        programmedTcvrs.push_back(tcvrID);
        ++numProgramIphy;
        results.push_back(result);
      }
    } else if (needProgramXphy && phyManager_ != nullptr) {
      if (auto result = updateStateBlockingWithoutWait(
              tcvrID, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_XPHY)) {
        programmedTcvrs.push_back(tcvrID);
        ++numProgramXphy;
        results.push_back(result);
      }
    } else if (needProgramTcvr) {
      std::shared_ptr<BlockingStateMachineUpdateResult> result{nullptr};

      if (moduleStateReady) {
        if (FLAGS_port_manager_mode) {
          // If Port Manager mode is enabled, check if the transceiver is
          // marked ready for programming by Port Manager. If not, skip
          // programming the transceiver.
          if (tcvrsReadyForProgramming.find(tcvrID) ==
              tcvrsReadyForProgramming.end()) {
            continue;
          }
        }

        result = updateStateBlockingWithoutWait(
            tcvrID, TransceiverStateMachineEvent::TCVR_EV_PROGRAM_TRANSCEIVER);
        if (result) {
          ++numProgramTcvr;
        }
      } else {
        result = updateStateBlockingWithoutWait(
            tcvrID, TransceiverStateMachineEvent::TCVR_EV_PREPARE_TRANSCEIVER);
        if (result) {
          ++numPrepareTcvr;
        }
      }
      if (result) {
        programmedTcvrs.push_back(tcvrID);
        results.push_back(result);
      }
    }
  }
  waitForAllBlockingStateUpdateDone(results);
  XLOG_IF(DBG2, !programmedTcvrs.empty())
      << "triggerProgrammingEvents has " << numProgramIphy
      << " IPHY programming, " << numProgramXphy << " XPHY programming, "
      << numProgramTcvr << " TCVR programming, " << numPrepareTcvr
      << " TCVR prepare. Total execute time(ms):"
      << duration_cast<milliseconds>(steady_clock::now() - begin).count();
  return programmedTcvrs;
}

void TransceiverManager::programInternalPhyPorts(TransceiverID id) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("programInternalPhyPorts");
  }

  std::map<int32_t, cfg::PortProfileID> programmedIphyPorts;
  if (auto overridePortAndProfileIt =
          overrideTcvrToPortAndProfileForTest_.find(id);
      overridePortAndProfileIt != overrideTcvrToPortAndProfileForTest_.end()) {
    // NOTE: This is only used for testing.
    for (const auto& [portID, profileID] : overridePortAndProfileIt->second) {
      programmedIphyPorts.emplace(portID, profileID);
    }
  } else {
    // Then call wedge_agent programInternalPhyPorts
    auto wedgeAgentClient = utils::createWedgeAgentClient();
    wedgeAgentClient->sync_programInternalPhyPorts(
        programmedIphyPorts, getTransceiverInfo(id), false);
  }

  std::string logStr = folly::to<std::string>(
      "programInternalPhyPorts() for Transceiver=", id, " return [");
  for (const auto& [portID, profileID] : programmedIphyPorts) {
    logStr = folly::to<std::string>(
        logStr,
        portID,
        " : ",
        apache::thrift::util::enumNameSafe(profileID),
        ", ");
  }
  XLOG(INFO) << logStr << "]";

  // Now update the programmed SW port to profile mapping
  if (auto portToPortInfoIt = tcvrToPortInfo_.find(id);
      portToPortInfoIt != tcvrToPortInfo_.end()) {
    auto portToPortInfoWithLock = portToPortInfoIt->second->wlock();
    portToPortInfoWithLock->clear();
    for (auto [portID, profileID] : programmedIphyPorts) {
      TransceiverPortInfo portInfo;
      portInfo.profile = profileID;
      portToPortInfoWithLock->emplace(PortID(portID), portInfo);
    }
  }
}

TransceiverManager::PortToPortInfo
TransceiverManager::getProgrammedIphyPortToPortInfo(TransceiverID id) const {
  if (auto tcvrToPortInfo_It = tcvrToPortInfo_.find(id);
      tcvrToPortInfo_It != tcvrToPortInfo_.end()) {
    return *(tcvrToPortInfo_It->second->rlock());
  }
  return {};
}

std::shared_ptr<folly::Synchronized<TransceiverManager::PortToPortInfo>>
TransceiverManager::getSynchronizedProgrammedIphyPortToPortInfo(
    TransceiverID id) {
  if (auto tcvrToPortInfoIt = tcvrToPortInfo_.find(id);
      tcvrToPortInfoIt != tcvrToPortInfo_.end()) {
    return tcvrToPortInfoIt->second;
  }
  return {};
}

void TransceiverManager::resetProgrammedIphyPortToPortInfo(TransceiverID id) {
  if (auto it = tcvrToPortInfo_.find(id); it != tcvrToPortInfo_.end()) {
    auto portToPortInfoWithLock = it->second->wlock();
    portToPortInfoWithLock->clear();
  }
}

void TransceiverManager::resetProgrammedIphyPortToPortInfoForPorts(
    const std::unordered_set<PortID>& portIds) {
  for (auto& [_, portToPortInfo] : tcvrToPortInfo_) {
    auto lockedPortToPortInfo = portToPortInfo->wlock();
    for (auto it = lockedPortToPortInfo->begin();
         it != lockedPortToPortInfo->end();) {
      if (portIds.find(it->first) != portIds.end()) {
        it = lockedPortToPortInfo->erase(it);
      } else {
        ++it;
      }
    }
  }
}

std::unordered_map<PortID, cfg::PortProfileID>
TransceiverManager::getOverrideProgrammedIphyPortAndProfileForTest(
    TransceiverID id) const {
  if (auto portAndProfileIt = overrideTcvrToPortAndProfileForTest_.find(id);
      portAndProfileIt != overrideTcvrToPortAndProfileForTest_.end()) {
    return portAndProfileIt->second;
  }
  return {};
}

void TransceiverManager::programExternalPhyPorts(
    TransceiverID id,
    bool needResetDataPath) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("programExternalPhyPorts");
  }

  auto phyManager = getPhyManager();
  if (!phyManager) {
    return;
  }
  // Get programmed iphy port profile
  const auto& programmedPortToPortInfo = getProgrammedIphyPortToPortInfo(id);
  if (programmedPortToPortInfo.empty()) {
    // This is due to the iphy ports are disabled. So no need to program xphy
    MODULE_LOG(DBG2, "", id)
        << "Skip programming xphy ports. Can't find programmed iphy port and port info";
    return;
  }
  const auto& supportedXphyPorts = phyManager->getXphyPorts();
  const auto& transceiver = getTransceiverInfo(id);
  for (const auto& [portID, portInfo] : programmedPortToPortInfo) {
    if (std::find(
            supportedXphyPorts.begin(), supportedXphyPorts.end(), portID) ==
        supportedXphyPorts.end()) {
      MODULE_LOG(DBG2, "", id)
          << "Skip programming xphy ports, Port=" << portID
          << ". Can't find supported xphy";
      continue;
    }

    phyManager->programOnePort(
        portID, portInfo.profile, transceiver, needResetDataPath);
    MODULE_LOG(INFO, "", id) << "Programmed XPHY port, Port=" << portID
                        << ", Profile="
                        << apache::thrift::util::enumNameSafe(portInfo.profile)
                        << ", needResetDataPath=" << needResetDataPath;
  }
}

std::optional<TransceiverInfo> TransceiverManager::getTransceiverInfoOptional(
    TransceiverID id) const {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt != lockedTransceivers->end()) {
    try {
      return tcvrIt->second->getTransceiverInfo();
    } catch (const std::exception&) {
      // if the transceiver is present but we've never successfully collected
      // tcvr info, return a dummy value (returning nullopt would be interpreted
      // as the tcvr not being present)
      // TODO(T247435135): Right now the handling of tcvrInfo error cases is
      // very convoluted, we should simplify this
      TransceiverInfo tcvrInfo;
      tcvrInfo.tcvrState()->present() = true;
      tcvrInfo.tcvrState()->eepromCsumValid() = true;
      return tcvrInfo;
    }
  }
  return std::nullopt;
}

TransceiverInfo TransceiverManager::getTransceiverInfo(TransceiverID id) const {
  TransceiverInfo tcvrInfo;
  const auto& cachedTcvrInfo = getTransceiverInfoOptional(id);
  if (cachedTcvrInfo) {
    tcvrInfo = *cachedTcvrInfo;
  } else {
    tcvrInfo.tcvrState()->present() = false;
    tcvrInfo.tcvrState()->port() = id;

    const auto& interfaces = getPortNames(id);
    tcvrInfo.tcvrState()->interfaces() = interfaces;
    tcvrInfo.tcvrStats()->interfaces() = interfaces;

    const std::string& tcvrName = getTransceiverName(id);
    tcvrInfo.tcvrState()->tcvrName() = tcvrName;
    tcvrInfo.tcvrStats()->tcvrName() = tcvrName;

    tcvrInfo.tcvrState()->timeCollected() = std::time(nullptr);
    tcvrInfo.tcvrStats()->timeCollected() = std::time(nullptr);

    // To avoid reporting false checksum-invalid on absent transceivers,
    // we set the checksum to a valid.
    tcvrInfo.tcvrState()->eepromCsumValid() = true;
  }

  // set the communicationError flag if i2c errors made us fail to collect data
  // during the previous polling cycle
  // TODO(T243916924): This is very hacky, in the future we should probably
  // refactor how we handle !present tcvrs/tcvrs with i2c errors
  auto erroredTransceivers = erroredTransceivers_.rlock();
  if (erroredTransceivers->find(id) != erroredTransceivers->end()) {
    tcvrInfo.tcvrState()->communicationError() = true;
    // We need to overwrite timeCollected otherwise we'll keep reporting the
    // timeCollected of the last successful refresh
    tcvrInfo.tcvrState()->timeCollected() = std::time(nullptr);
    tcvrInfo.tcvrStats()->timeCollected() = std::time(nullptr);
  }

  return tcvrInfo;
}

/*
 * getAllPortSupportedProfiles
 *
 * This function returns the list of all supported port profiles on every port
 * configured by agent config at that moment. If the checkOptics is False then
 * it returns all possible port profiles for every configured port as
 * mentioned in the platform mapping. If the checkOptics is True then it will
 * exclude the port profiles which current optics does not support.
 */
void TransceiverManager::getAllPortSupportedProfiles(
    std::map<std::string, std::vector<cfg::PortProfileID>>&
        supportedPortProfiles,
    bool checkOptics) {
  // Find the list of all available ports from agent config
  std::vector<std::string> availablePorts;

  for (auto& [tcvrId, tcvrPortInfoMap] : tcvrToPortInfo_) {
    auto tcvrPortInfoMapLocked = tcvrPortInfoMap->rlock();
    for (auto& [portId, tcvrPortInfo] : *tcvrPortInfoMapLocked) {
      if (auto portName = getPortNameByPortId(portId)) {
        availablePorts.push_back(portName.value());
      }
    }
  }

  // Get all possible port profiles for all the ports from platform mapping.
  // Exclude the ports which are not configured by agent config
  auto allPossiblePortProfiles = platformMapping_->getAllPortProfiles();
  std::map<std::string, std::vector<cfg::PortProfileID>>
      allConfiguredPortProfiles;
  for (auto& portName : availablePorts) {
    if (allPossiblePortProfiles.find(portName) !=
        allPossiblePortProfiles.end()) {
      allConfiguredPortProfiles[portName] = allPossiblePortProfiles[portName];
    }
  }

  // If we don't need to check the optics support of the profile then return
  // all supported port profiles from the platform mapping which are
  // configured by agent config
  if (!checkOptics) {
    supportedPortProfiles = allConfiguredPortProfiles;
    return;
  }

  for (auto& [portName, portProfiles] : allConfiguredPortProfiles) {
    auto portID = getPortIDByPortName(portName);
    if (!portID.has_value()) {
      continue;
    }
    // Check if the transceiver supports the port profile
    for (auto& profileID : portProfiles) {
      auto tcvrHostLanes = platformMapping_->getTransceiverHostLanes(
          PlatformPortProfileConfigMatcher(
              profileID /* profileID */,
              *portID /* portID */,
              std::nullopt /* portConfigOverrideFactor */));
      if (tcvrHostLanes.empty()) {
        continue;
      }
      auto tcvrStartLane = *tcvrHostLanes.begin();
      auto profileCfgOpt = platformMapping_->getPortProfileConfig(
          PlatformPortProfileConfigMatcher(profileID));
      if (!profileCfgOpt) {
        continue;
      }
      const auto speed = *profileCfgOpt->speed();
      TransceiverPortState portState;
      portState.portName = portName;
      portState.startHostLane = tcvrStartLane;
      portState.speed = speed;
      portState.numHostLanes = tcvrHostLanes.size();
      portState.transmitterTech = profileCfgOpt->iphy()->medium().value_or({});

      auto tcvrIDOpt = getTransceiverID(*portID);
      if (!tcvrIDOpt.has_value()) {
        continue;
      }

      if (isTransceiverPortStateSupported(*tcvrIDOpt, portState)) {
        supportedPortProfiles[portName].push_back(profileID);
      }
    }
  }
}

bool TransceiverManager::isTransceiverPortStateSupported(
    TransceiverID tcvrID,
    TransceiverPortState& tcvrPortState) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(tcvrID);
  return tcvrIt != lockedTransceivers->end() &&
      tcvrIt->second->tcvrPortStateSupported(tcvrPortState);
}

/*
 * Return the DriverPeaking values per lane for a given port in a transceiver,
 * if the portConfigOverrides exist and matches the PortID, PortProfileID, and
 * Transceiver Vendor.
 */
std::optional<std::map<uint8_t, uint8_t>>
TransceiverManager::getDriverPeakingOverrides(
    TransceiverID tcvrId,
    cfg::PortProfileID profile,
    size_t numberOfLanes) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(tcvrId);
  if (tcvrIt == lockedTransceivers->end()) {
    return std::nullopt;
  }

  const auto info = tcvrIt->second->getTransceiverInfo();
  auto factor = buildPlatformPortConfigOverrideFactor(info);

  // Get all the driver peaking for all the ports in the transceiver
  // that match the profile. Add them to the TransceiverPortState.
  // This is because in CmisModule we may program the AppSel at once
  // for all the ports that match the profile, and we want the overrides
  // then.
  auto portIds = platformMapping_->getSwPortListFromTransceiverId(tcvrId);
  bool overridesFound = false;
  std::map<uint8_t, uint8_t> driverPeakings;
  for (const auto& port : portIds) {
    PlatformPortProfileConfigMatcher matcher(profile, port, factor);
    auto driverPeakingOpt =
        platformMapping_->getPortDriverPeakingOverrides(matcher);
    if (!driverPeakingOpt) {
      continue;
    }
    if (driverPeakingOpt->size() != numberOfLanes) {
      MODULE_LOG(ERR, "", tcvrId)
          << "Driver Peaking overrides do not match the number of lanes: port "
          << port << ". Not overriding";
      continue;
    }
    overridesFound = true;
    // Thrift does not support an array of uint8_t, so we need to convert to
    // vector<uint8_t>. The value applied to CMIS per spec is 4 bits per lane.
    for (const auto& [lane, driverPeakingValue] : *driverPeakingOpt) {
      driverPeakings.insert(
          std::make_pair(
              static_cast<uint8_t>(lane),
              static_cast<uint8_t>(driverPeakingValue)));
    }
  }

  if (!overridesFound) {
    return std::nullopt;
  }
  return driverPeakings;
}

/*
 * getOpticalChannelConfig - Retrieve optical channel configuration for a
 * transceiver
 *
 * This function retrieves the optical channel configuration from the QSFP
 * service configuration for a specific transceiver. The configuration contains
 * frequency, transmit power and AppSel code settings that are used for tunable
 * optics programming.
 *
 * Function workflow:
 * 1. Validates that the QSFP configuration exists
 * 2. Checks if tunable optics configuration is available in the QSFP config
 * 3. Iterates through all port names associated with the given transceiver ID
 * 4. Searches for optical channel configuration matching each port name
 * 5. If found, extracts and logs the frequency configuration details
 * 6. Returns the optical channel configuration object if found, otherwise a
 * nullopt
 *
 * @param id The TransceiverID to look up optical channel config for
 * @return std::optional<cfg::OpticalChannelConfig> - The optical channel
 * configuration if found, std::nullopt if no configuration exists or if QSFP
 * config is unavailable
 */

std::optional<cfg::OpticalChannelConfig>
TransceiverManager::getOpticalChannelConfig(TransceiverID id) const {
  if (!qsfpConfig_) {
    XLOG(DBG2) << "QsfpConfig is NULL.";
    return std::nullopt;
  }

  const auto& qsfpCfg = qsfpConfig_->thrift;
  auto qsfpCfgTunableOptics = qsfpCfg.tunableOpticsConfig();
  if (!qsfpCfgTunableOptics.has_value()) {
    return std::nullopt;
  }

  MODULE_LOG(DBG2, "", id) << "Tunable Optics config is present";
  auto portNames = getPortNames(id);
  for (const auto& portName : portNames) {
    MODULE_LOG(DBG2, "", id) << "Tunable Optics config is present, port_name "
                        << folly::join(",", portNames);
    auto tunableOpticsConfigInCfgIt = qsfpCfgTunableOptics->find(portName);
    if (tunableOpticsConfigInCfgIt != qsfpCfgTunableOptics->end()) {
      const auto& opticalChannelConfig = tunableOpticsConfigInCfgIt->second;
      const auto& freqConfig = opticalChannelConfig.frequencyConfig();
      const auto& centerFreq = freqConfig->centerFrequencyConfig();
      if (centerFreq->getType() ==
          facebook::fboss::cfg::CenterFrequencyConfig::Type::frequencyMhz) {
        int freqMhz = centerFreq.as_const()->frequencyMhz().value();
        MODULE_LOG(DBG2, "", id) << "The frequency is " << freqMhz
                            << " port_name " << portName;
      }
      // Use the frequency config from tunable optics config
      return opticalChannelConfig;
    }
  }
  return std::nullopt;
}

void TransceiverManager::programTransceiver(
    TransceiverID id,
    bool needResetDataPath) {
  // Get programmed iphy port profile
  const auto& programmedPortToPortInfo = getProgrammedIphyPortToPortInfo(id);
  if (programmedPortToPortInfo.empty()) {
    // This is due to the iphy ports are disabled. So no need to program tcvr
    MODULE_LOG(DBG2, "", id)
        << "Skip programming. Can't find programmed iphy port and port info";
    return;
  }

  // Port manager mode infers operState from the port state machine, so it is
  // not evidence of a live link there.
  if (needResetDataPath && !FLAGS_port_manager_mode) {
    const int64_t coldbootInMs = resetDataPathColdbootInMs_;
    for (const auto& [portID, portInfo] : programmedPortToPortInfo) {
      if (portInfo.status && portInfo.status->operState &&
          portInfo.statusReceivedInMs >= coldbootInMs) {
        MODULE_LOG(INFO, "", id) << "Skip data path reset: Port=" << portID
                                 << " is already up after agent config change";
        needResetDataPath = false;
        break;
      }
    }
  }

  // Try to get frequency config from tunable optics config if available
  const auto opticalChannelConfig = getOpticalChannelConfig(id);

  ProgramTransceiverState programTcvrState;
  for (const auto& portToPortInfo : programmedPortToPortInfo) {
    auto portProfile = portToPortInfo.second.profile;
    auto portName = getPortNameByPortId(portToPortInfo.first);
    if (!portName.has_value()) {
      throw FbossError(
          "Can't find a portName for portId ", portToPortInfo.first);
    }
    uint8_t tcvrStartLane = 0;
    // Use platform mapping to fetch the transceiver start lane given the port
    // id and profile id
    auto tcvrHostLanes = platformMapping_->getTransceiverHostLanes(
        PlatformPortProfileConfigMatcher(
            portProfile /* profileID */,
            portToPortInfo.first /* portID */,
            std::nullopt /* portConfigOverrideFactor */));
    if (tcvrHostLanes.empty()) {
      throw FbossError("tcvrHostLanes empty for portId ", portToPortInfo.first);
    }
    // tcvrHostLanes is an ordered set. So begin() gives us the first lane
    tcvrStartLane = *tcvrHostLanes.begin();

    // CPO modules present up to kMaxCpoHostLanes global host lanes (banks x
    // lanes-per-bank), so a port's start lane can be anywhere in
    // [0, kMaxCpoHostLanes). Non-CPO modules use the lower end of this range.
    if (tcvrStartLane >= kMaxCpoHostLanes) {
      throw FbossError(
          "Invalid start lane of ",
          tcvrStartLane,
          " for portId ",
          portToPortInfo.first);
    }
    auto profileCfgOpt = platformMapping_->getPortProfileConfig(
        PlatformPortProfileConfigMatcher(portProfile));
    if (!profileCfgOpt) {
      throw FbossError(
          "Can't find profile config for profileID=",
          apache::thrift::util::enumNameSafe(portProfile));
    }
    const auto speed = *profileCfgOpt->speed();
    auto bankId = platformMapping_->getTransceiverBankId(
        PlatformPortProfileConfigMatcher(
            portProfile, portToPortInfo.first, std::nullopt));
    TransceiverPortState portState;
    portState.portName = *portName;
    portState.startHostLane = tcvrStartLane;
    portState.speed = speed;
    portState.numHostLanes = tcvrHostLanes.size();
    portState.opticalChannelConfig = opticalChannelConfig;
    portState.driverPeaking =
        getDriverPeakingOverrides(id, portProfile, tcvrHostLanes.size());
    portState.bankId = bankId;
    programTcvrState.ports.emplace(*portName, portState);
  }

  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    MODULE_LOG(DBG2, "", id) << "Skip programming. Not present";
    return;
  }

  tcvrIt->second->programTransceiver(programTcvrState, needResetDataPath);

  // Once the transceiver has been programmed, report the port state
  // (start of programming time, end of programming time) to the
  // fsdb.
  publishPortStatesToFsdb(id, tcvrIt->second->getPortState());
  MODULE_LOG(INFO, "", id) << "Programmed"
                      << (needResetDataPath ? " with" : " without")
                      << " resetting data path";

  // We also want to remove this transceiver from tcvrsReadyForProgramming_ to
  // avoid programming with old settings received from PortManager.
  if (FLAGS_port_manager_mode) {
    markTransceiverReadyForProgramming(id, false);
  }
}

/*
 * readyTransceiver
 *
 * Calls the module type specific function to check their power control
 * configuration and if needed, corrects it. Returns if the module is in ready
 * state to proceed further with programming.
 *
 * For ZR module, this function checks if tunable optics config
 * is present in qsfp_service_config and passes this information to the
 * transceiver. If the config is not present for a ZR optic,
 * the module will not be moved to high power mode.
 */
bool TransceiverManager::readyTransceiver(TransceiverID id) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    MODULE_LOG(DBG2, "", id) << "Skip Ready Checking. Not present";
    return true;
  }

  // Check if tunable optics config is present for this transceiver
  const auto opticalChannelConfig = getOpticalChannelConfig(id);
  bool hasTunableOpticsConfig = opticalChannelConfig.has_value();

  return tcvrIt->second->readyTransceiver(hasTunableOpticsConfig);
}

void TransceiverManager::resetPortState(const TransceiverID& id) {
  auto portNames = getPortNames(id);
  for (auto& portName : portNames) {
    publishPortStateToFsdb(std::string(portName), portstate::PortState());
  }
}

void TransceiverManager::getPortTransceiverIDs(
    std::map<std::string, std::vector<int32_t>>& portTransceiverIds) const {
  for (const auto& [portName, tcvrId] : portNameToModule_) {
    // TODO: Handle multi-transceiver ports. portNameToModule_ only includes one
    // transceiver per port
    portTransceiverIds[portName].push_back(tcvrId);
  }
}

void TransceiverManager::publishPortStatesToFsdb(
    const TransceiverID& id,
    const portstate::PortState& state) {
  auto portNames = getPortNames(id);
  for (auto& portName : portNames) {
    publishPortStateToFsdb(std::string(portName), portstate::PortState(state));
  }
}

bool TransceiverManager::tryRemediateTransceiver(TransceiverID id) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    MODULE_LOG(DBG2, "", id) << "Skip remediating. Not present";
    return false;
  }
  bool allPortsDown;
  std::vector<std::string> portsToRemediate;
  std::tie(allPortsDown, portsToRemediate) = areAllPortsDown(id);
  bool didRemediate = tcvrIt->second->tryRemediate(
      allPortsDown, pauseRemediationUntil_, portsToRemediate);
  MODULE_LOG_IF(INFO, "", didRemediate, id)
      << "Remediated, ports=" << folly::join(",", portsToRemediate);
  return didRemediate;
}

bool TransceiverManager::supportRemediateTransceiver(TransceiverID id) {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    MODULE_LOG(DBG2, "", id) << "is not present and can't support remediate";
    return false;
  }
  return tcvrIt->second->supportRemediate();
}

void TransceiverManager::syncNpuPortStatusUpdate(
    std::map<int, facebook::fboss::NpuPortStatus>& portStatus) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("syncNpuPortStatusUpdate");
  }
  XLOG(INFO) << "Syncing NPU port status update";
  updateNpuPortStatusCache(portStatus);
  // Update state machine after receiving a new port status update
  updateTransceiverPortStatus();
}

void TransceiverManager::updateNpuPortStatusCache(
    std::map<int, facebook::fboss::NpuPortStatus>& portStatus) {
  auto lockedCache = npuPortStatusCache_.wlock();
  lockedCache->swap(portStatus);
  npuPortStatusCacheUpdatedInMs_ = nowInMs();
}

void TransceiverManager::updateTransceiverPortStatus() noexcept {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("updateTransceiverPortStatus");
  }
  steady_clock::time_point begin = steady_clock::now();
  std::map<int32_t, NpuPortStatus> newPortToPortStatus;
  int64_t statusReceivedInMs = nowInMs();
  if (!overrideAgentPortStatusForTesting_.empty()) {
    XLOG(WARN) << "[TEST ONLY] Use overrideAgentPortStatusForTesting_ "
               << "for wedge_agent getPortStatus()";
    newPortToPortStatus = overrideAgentPortStatusForTesting_;
  } else if (FLAGS_subscribe_to_state_from_fsdb) {
    auto lockedCache = npuPortStatusCache_.rlock();
    newPortToPortStatus = *lockedCache;
    statusReceivedInMs = npuPortStatusCacheUpdatedInMs_;
  } else {
    try {
      // Then call wedge_agent getPortStatus() to get current port status
      auto wedgeAgentClient = utils::createWedgeAgentClient();
      std::map<int32_t, PortStatus> portStatus;
      wedgeAgentClient->sync_getPortStatus(portStatus, {});
      newPortToPortStatus = getNpuPortStatus(portStatus);
    } catch (const std::exception& ex) {
      // We have retry mechanism to handle failure. No crash here
      XLOG(WARN) << "Failed to call wedge_agent getPortStatus(). "
                 << folly::exceptionStr(ex);
    }
  }
  if (newPortToPortStatus.empty()) {
    XLOG(WARN) << "No port status to process in updateTransceiverPortStatus";
    return;
  }

  int numResetToDiscovered{0}, numResetToNotPresent{0}, numPortStatusChanged{0};
  auto genStateMachineResetEvent =
      [&numResetToDiscovered, &numResetToNotPresent](
          std::optional<TransceiverStateMachineEvent>& event,
          bool isTcvrPresent) {
        // Update present transceiver state machine back to DISCOVERED
        // and absent transeiver state machine back to NOT_PRESENT
        if (event.has_value()) {
          // If event is already set, no-op
          return;
        }
        if (isTcvrPresent) {
          ++numResetToDiscovered;
          event.emplace(
              TransceiverStateMachineEvent::TCVR_EV_RESET_TO_DISCOVERED);
        } else {
          ++numResetToNotPresent;
          event.emplace(
              TransceiverStateMachineEvent::TCVR_EV_RESET_TO_NOT_PRESENT);
        }
      };

  const auto& presentTransceivers = getPresentTransceivers();
  BlockingStateUpdateResultList results;
  std::vector<std::pair<TransceiverID, TransceiverStateMachineEvent>>
      updateArgsToExecute;
  for (auto& [tcvrID, portToPortInfo] : tcvrToPortInfo_) {
    std::unordered_set<PortID> statusChangedPorts;
    bool anyPortUp = false;
    bool isTcvrPresent =
        (presentTransceivers.find(tcvrID) != presentTransceivers.end());
    bool isTcvrJustProgrammed =
        (getCurrentState(tcvrID) ==
         TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
    std::optional<TransceiverStateMachineEvent> event;
    { // lock block for portToPortInfo
      auto portToPortInfoWithLock = portToPortInfo->wlock();
      // All possible platform ports for such transceiver
      const auto& portIDs = getAllPlatformPorts(tcvrID);
      for (auto portID : portIDs) {
        auto portStatusIt = newPortToPortStatus.find(portID);
        auto cachedPortInfoIt = portToPortInfoWithLock->find(portID);
        // If portStatus from agent doesn't have such port
        if (portStatusIt == newPortToPortStatus.end()) {
          if (cachedPortInfoIt == portToPortInfoWithLock->end()) {
            continue;
          } else {
            // Agent remove such port, we need to trigger a state machine
            // reset to trigger programming to get the new sw ports
            portToPortInfoWithLock->erase(cachedPortInfoIt);
            genStateMachineResetEvent(event, isTcvrPresent);
          }
        } else { // If portStatus exists
          // But if the port is disabled, we don't need disabled ports in the
          // cache, since we only store enabled ports as we do in the
          // programInternalPhyPorts()
          if (!(portStatusIt->second.portEnabled)) {
            if (cachedPortInfoIt != portToPortInfoWithLock->end()) {
              portToPortInfoWithLock->erase(cachedPortInfoIt);
              genStateMachineResetEvent(event, isTcvrPresent);
            }
          } else {
            // Only care about enabled port status
            anyPortUp = anyPortUp || portStatusIt->second.operState;
            // Agent create such port, we need to trigger a state machine
            // reset to trigger programming to get the new sw ports
            if (cachedPortInfoIt == portToPortInfoWithLock->end()) {
              TransceiverPortInfo portInfo;
              portInfo.status.emplace(portStatusIt->second);
              portInfo.statusReceivedInMs = statusReceivedInMs;
              portToPortInfoWithLock->insert({portID, std::move(portInfo)});
              genStateMachineResetEvent(event, isTcvrPresent);
            } else {
              // Both agent and cache here have such port, update the cached
              // status
              if (!cachedPortInfoIt->second.status ||
                  cachedPortInfoIt->second.status->operState !=
                      portStatusIt->second.operState) {
                statusChangedPorts.insert(portID);
              }
              cachedPortInfoIt->second.status.emplace(portStatusIt->second);
              cachedPortInfoIt->second.statusReceivedInMs = statusReceivedInMs;
            }
          }
        }
      }
      // If event is not set, it means not reset event is needed, now check
      // whether we need port status event.
      // Make sure we update active state for a transceiver which just
      // finished programming
      if (!event && ((!statusChangedPorts.empty()) || isTcvrJustProgrammed)) {
        event.emplace(
            anyPortUp ? TransceiverStateMachineEvent::TCVR_EV_PORT_UP
                      : TransceiverStateMachineEvent::TCVR_EV_ALL_PORTS_DOWN);
        ++numPortStatusChanged;
      }

      // Make sure the port event will be added to the update queue under the
      // lock of portToPortInfo, so that it will make sure the cached status
      // and the state machine will be in sync
      if (event.has_value()) {
        updateArgsToExecute.emplace_back(tcvrID, *event);
      }
    } // lock block for portToPortInfo
    // After releasing portToPortInfo lock, publishLinkSnapshots() will use
    // transceivers_ lock later
    for (auto portID : statusChangedPorts) {
      try {
        publishLinkSnapshots(portID);
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Port " << portID
                  << " failed publishLinkSnapshpts(): " << ex.what();
      }
    }
  }
  if (!updateArgsToExecute.empty()) {
    for (auto& [tcvrID, event] : updateArgsToExecute) {
      if (auto result =
              enqueueStateUpdateForTcvrWithoutExecuting(tcvrID, event)) {
        results.push_back(result);
      }
    }
    executeStateUpdates();
    waitForAllBlockingStateUpdateDone(results);
  }
  XLOG_IF(
      DBG2,
      numResetToDiscovered + numResetToNotPresent + numPortStatusChanged > 0)
      << "updateTransceiverPortStatus has " << numResetToDiscovered
      << " transceivers state machines set back to discovered, "
      << numResetToNotPresent << " set back to not_present, "
      << numPortStatusChanged
      << " transceivers need to update port status. Total execute time(ms):"
      << duration_cast<milliseconds>(steady_clock::now() - begin).count();
}

void TransceiverManager::triggerResetEvents(
    const std::unordered_set<TransceiverID>& tcvrs) {
  if (tcvrs.empty()) {
    return;
  }
  const auto& presentTransceivers = getPresentTransceivers();

  BlockingStateUpdateResultList results;
  for (auto tcvrID : tcvrs) {
    bool isTcvrPresent =
        (presentTransceivers.find(tcvrID) != presentTransceivers.end());
    auto event = isTcvrPresent
        ? TransceiverStateMachineEvent::TCVR_EV_RESET_TO_DISCOVERED
        : TransceiverStateMachineEvent::TCVR_EV_RESET_TO_NOT_PRESENT;
    if (auto result =
            enqueueStateUpdateForTcvrWithoutExecuting(tcvrID, event)) {
      results.push_back(result);
    }
  }

  if (!results.empty()) {
    executeStateUpdates();
    waitForAllBlockingStateUpdateDone(results);
  }
}

void TransceiverManager::triggerFirmwareUpgradeEvents(
    const std::unordered_set<TransceiverID>& tcvrs) {
  if (!FLAGS_firmware_upgrade_supported || tcvrs.empty()) {
    return;
  }
  BlockingStateUpdateResultList results;
  for (auto tcvrID : tcvrs) {
    TransceiverStateMachineEvent event =
        TransceiverStateMachineEvent::TCVR_EV_UPGRADE_FIRMWARE;
    auto tcvrThreadIdIt = qsfpServiceThreads_->tcvrToThreadId.find(tcvrID);
    if (tcvrThreadIdIt != qsfpServiceThreads_->tcvrToThreadId.end()) {
      auto& threadHelper =
          qsfpServiceThreads_->threadIdToThread.at(tcvrThreadIdIt->second);
      heartbeatWatchdog_->pauseMonitoringHeartbeat(
          threadHelper.getThreadHeartbeat());
    }
    // Only enqueue updates for now, we'll execute them at once after this
    // loop
    if (auto result =
            enqueueStateUpdateForTcvrWithoutExecuting(tcvrID, event)) {
      results.push_back(result);
    }
  }
  if (!results.empty()) {
    isUpgradingFirmware_ = true;
    executeStateUpdates();
    heartbeatWatchdog_->pauseMonitoringHeartbeat(updateThreadHeartbeat_);
    waitForAllBlockingStateUpdateDone(results);

    resetUpgradedTransceiversToDiscovered();
  }
}

void TransceiverManager::updateTransceiverActiveState(
    const std::set<TransceiverID>& tcvrs,
    const std::map<int32_t, PortStatus>& portStatus) noexcept {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("updateTransceiverActiveState");
  }
  std::map<int32_t, NpuPortStatus> npuPortStatus = getNpuPortStatus(portStatus);
  int numPortStatusChanged{0};
  BlockingStateUpdateResultList results;
  for (auto tcvrID : tcvrs) {
    auto tcvrToPortInfoIt = tcvrToPortInfo_.find(tcvrID);
    if (tcvrToPortInfoIt == tcvrToPortInfo_.end()) {
      MODULE_LOG(WARN, "", tcvrID)
          << "Unrecognized, skip updateTransceiverActiveState()";
      continue;
    }
    MODULE_LOG(INFO, "", tcvrID) << "Syncing ports";
    std::unordered_set<PortID> statusChangedPorts;
    bool anyPortUp = false;
    bool isTcvrJustProgrammed =
        (getCurrentState(tcvrID) ==
         TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED);
    { // lock block for portToPortInfo
      auto portToPortInfoWithLock = tcvrToPortInfoIt->second->wlock();
      for (auto& [portID, tcvrPortInfo] : *portToPortInfoWithLock) {
        // Check whether there's a new port status for such port
        auto portStatusIt = npuPortStatus.find(portID);
        // If port doesn't need to be updated, use the current cached status
        // to indicate whether we need a state update
        if (portStatusIt == npuPortStatus.end()) {
          if (tcvrPortInfo.status) {
            anyPortUp = anyPortUp || tcvrPortInfo.status->operState;
          }
        } else {
          // Only care about enabled port status
          if (portStatusIt->second.portEnabled) {
            anyPortUp = anyPortUp || portStatusIt->second.operState;
            if (!tcvrPortInfo.status ||
                tcvrPortInfo.status->operState !=
                    portStatusIt->second.operState) {
              statusChangedPorts.insert(portID);
              // No need to do the transceiverRefresh() in this code path
              // because that will again enqueue state machine update on i2c
              // event base. That will result in deadlock with
              // stateMachineRefresh() generated update which also runs in
              // same i2c event base
            }
            // And also update the cached port status
            tcvrPortInfo.status = portStatusIt->second;
            tcvrPortInfo.statusReceivedInMs = nowInMs();
          }
        }
      }

      // Make sure the port event will be added to the update queue under the
      // lock of portToPortInfo, so that it will make sure the cached status
      // and the state machine will be in sync
      // Make sure we update active state for a transceiver which just
      // finished programming
      if ((!statusChangedPorts.empty()) || isTcvrJustProgrammed) {
        auto event = anyPortUp
            ? TransceiverStateMachineEvent::TCVR_EV_PORT_UP
            : TransceiverStateMachineEvent::TCVR_EV_ALL_PORTS_DOWN;
        ++numPortStatusChanged;
        if (auto result = updateStateBlockingWithoutWait(tcvrID, event)) {
          results.push_back(result);
        }
      }
    } // lock block for portToPortInfo
    // After releasing portToPortInfo lock, publishLinkSnapshots() will use
    // transceivers_ lock later
    for (auto portID : statusChangedPorts) {
      try {
        publishLinkSnapshots(portID);
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Port " << portID
                  << " failed publishLinkSnapshpts(): " << ex.what();
      }
    }
  }
  waitForAllBlockingStateUpdateDone(results);
  XLOG_IF(DBG2, numPortStatusChanged > 0)
      << "updateTransceiverActiveState has " << numPortStatusChanged
      << " transceivers need to update port status.";
}

void TransceiverManager::resetUpgradedTransceiversToDiscovered() {
  BlockingStateUpdateResultList results;
  std::vector<TransceiverID> tcvrsToReset;
  for (auto& stateMachine : stateMachineControllers_) {
    const auto& lockedStateMachine =
        stateMachine.second->getStateMachine().rlock();
    if (lockedStateMachine->get_attribute(needToResetToDiscovered)) {
      tcvrsToReset.push_back(stateMachine.first);
    }
  }

  for (auto tcvrID : tcvrsToReset) {
    FW_LOG(INFO, tcvrID)
        << "Resetting transceiver state to DISCOVERED since it was recently upgraded";
    TransceiverStateMachineEvent event =
        TransceiverStateMachineEvent::TCVR_EV_RESET_TO_DISCOVERED;
    if (auto result = updateStateBlockingWithoutWait(tcvrID, event)) {
      results.push_back(result);
    }
  }
  waitForAllBlockingStateUpdateDone(results);
}

TransceiverValidationInfo TransceiverManager::getTransceiverValidationInfo(
    TransceiverID id,
    bool validatePortProfile) const {
  TransceiverValidationInfo tcvrInfo;
  tcvrInfo.id = id;

  const auto& cachedTcvrInfo = getTransceiverInfo(id);
  const auto& cachedTcvrState = cachedTcvrInfo.tcvrState();
  tcvrInfo.validEepromChecksums = cachedTcvrState->eepromCsumValid().value();

  auto vendor = cachedTcvrState->vendor();
  if (!vendor.has_value() || vendor->name().value().empty()) {
    tcvrInfo.requiredFields = std::make_pair(false, "missingVendor");
    return tcvrInfo;
  }
  tcvrInfo.vendorName = vendor->name().value();

  tcvrInfo.vendorSerialNumber = vendor->serialNumber().value();
  if (vendor->partNumber().value().empty()) {
    tcvrInfo.requiredFields = std::make_pair(false, "missingVendorPartNumber");
    return tcvrInfo;
  }
  tcvrInfo.vendorPartNumber = vendor->partNumber().value();

  auto mediaInterface = cachedTcvrState->moduleMediaInterface();
  tcvrInfo.mediaInterfaceCode = mediaInterface.has_value()
      ? apache::thrift::util::enumNameSafe(mediaInterface.value())
      : "NOVALUE";

  // TODO: Once firmware sync is enabled, consider firmware versions to
  // be required.
  auto moduleStatus = cachedTcvrState->status();
  if (moduleStatus.has_value() && moduleStatus->fwStatus().has_value()) {
    tcvrInfo.firmwareVersion = moduleStatus->fwStatus()->version().value_or("");
    tcvrInfo.dspFirmwareVersion =
        moduleStatus->fwStatus()->dspFwVer().value_or("");
  }

  if (validatePortProfile) {
    const auto& programmedPortToPortInfo = getProgrammedIphyPortToPortInfo(id);
    for (const auto& [portID, portInfo] : programmedPortToPortInfo) {
      tcvrInfo.portProfileIds.push_back(portInfo.profile);
    }
    if (!tcvrInfo.portProfileIds.size()) {
      tcvrInfo.requiredFields = std::make_pair(false, "missingPortProfileIds");
    }
  }

  return tcvrInfo;
}

bool TransceiverManager::validateTransceiverById(
    TransceiverID id,
    std::string& notValidatedReason,
    bool validatePortProfile) {
  if (tcvrValidator_ == nullptr) {
    XLOG(DBG5) << "Transceiver Validation not enabled. Skipping.";
    return false;
  }

  TransceiverValidationInfo tcvrInfo =
      getTransceiverValidationInfo(id, validatePortProfile);

  bool isValidated =
      validateTransceiverConfiguration(tcvrInfo, notValidatedReason);
  updateValidationCache(id, isValidated);
  return isValidated;
}

void TransceiverManager::checkPresentThenValidateTransceiver(TransceiverID id) {
  {
    auto lockedTransceivers = transceivers_.rlock();
    if (lockedTransceivers->find(id) == lockedTransceivers->end()) {
      return;
    }
  }
  std::string notValidatedReason;
  validateTransceiverById(id, notValidatedReason, true);
}

std::string TransceiverManager::getTransceiverValidationConfigString(
    TransceiverID id) const {
  if (tcvrValidator_ == nullptr) {
    XLOG(DBG5) << "Transceiver Validation not enabled. Skipping.";
    return "";
  }

  TransceiverValidationInfo tcvrInfo = getTransceiverValidationInfo(id, true);
  std::string notValidatedReason;
  if (validateTransceiverConfiguration(tcvrInfo, notValidatedReason)) {
    return "";
  }

  folly::dynamic r = folly::dynamic::object;
  std::vector<std::string> portProfileIdStrings;
  for (auto portProfileId : tcvrInfo.portProfileIds) {
    portProfileIdStrings.push_back(
        apache::thrift::util::enumNameSafe(portProfileId));
  }

  r["Transceiver ID"] = static_cast<int>(tcvrInfo.id);
  r["Transceiver Vendor"] = tcvrInfo.vendorName;
  r["Transceiver Serial Number"] = tcvrInfo.vendorSerialNumber;
  r["Transceiver Part Number"] = tcvrInfo.vendorPartNumber;
  r["Transceiver Media Interface Code"] = tcvrInfo.mediaInterfaceCode;
  r["Transceiver Application Firmware Version"] = tcvrInfo.firmwareVersion;
  r["Transceiver DSP Firmware Version"] = tcvrInfo.dspFirmwareVersion;
  r["Transceiver Port Profile Ids"] = folly::join(",", portProfileIdStrings);
  r["Non-Validated Attribute"] = notValidatedReason;

  return folly::toPrettyJson(r);
}

int TransceiverManager::getNumNonValidatedTransceiverConfigs(
    const std::map<int32_t, TransceiverInfo>& infoMap) const {
  auto nonValidatedSet = nonValidTransceiversCache_.rlock();
  int numConfigs = 0;

  for (auto& [tcvrId, tcvrInfo] : infoMap) {
    auto isPresent = tcvrInfo.tcvrState()->present().value();
    auto isNonValid =
        nonValidatedSet->find(static_cast<TransceiverID>(tcvrId)) !=
        nonValidatedSet->end();

    if (isPresent && isNonValid) {
      numConfigs++;
    }
  }

  return numConfigs;
}

void TransceiverManager::updateValidationCache(TransceiverID id, bool isValid) {
  auto nonValidatedSet = nonValidTransceiversCache_.wlock();
  if (!isValid) {
    nonValidatedSet->insert(id);
  } else {
    nonValidatedSet->erase(id);
  }
}

std::unordered_set<TransceiverID>
TransceiverManager::findPotentialTcvrsForFirmwareUpgrade(
    const std::vector<TransceiverID>& presentXcvrIds) {
  bool firstRefreshAfterColdboot = !canWarmBoot_ && !isFullyInitialized();
  std::unordered_set<TransceiverID> potentialTcvrsForFwUpgrade;
  for (auto tcvrId : presentXcvrIds) {
    auto curState = getCurrentState(tcvrId);
    if (curState == TransceiverStateMachineState::DISCOVERED) {
      if (FLAGS_firmware_upgrade_on_coldboot && firstRefreshAfterColdboot) {
        // First refresh after cold boot and module is still in
        // discovered state
        auto portName = getPortName(tcvrId);
        XLOG(INFO) << FirmwareUpgradeAlert()
                   << "Selected for potential FW upgrade due to coldboot"
                   << TransceiverParam(tcvrId) << PortParam(portName);
        potentialTcvrsForFwUpgrade.insert(tcvrId);
      } else if (FLAGS_firmware_upgrade_on_tcvr_insert) {
        auto stateMachine = stateMachineControllers_.find(tcvrId);
        if (stateMachine != stateMachineControllers_.end() &&
            stateMachine->second->getStateMachine().rlock()->get_attribute(
                newTransceiverInsertedAfterInit)) {
          // Not the first refresh but the module is in discovered state and
          // was just inserted
          auto portName = getPortName(tcvrId);
          XLOG(INFO)
              << FirmwareUpgradeAlert()
              << "Selected for potential firmware upgrade due to optics insertion"
              << TransceiverParam(tcvrId) << PortParam(portName);
          potentialTcvrsForFwUpgrade.insert(tcvrId);
        }
      }
    }
  }

  return potentialTcvrsForFwUpgrade;
}

void TransceiverManager::findAndTriggerPotentialFirmwareUpgradeEvents(
    const std::vector<TransceiverID>& presentXcvrIds) {
  const auto& potentialTcvrsForFwUpgrade =
      findPotentialTcvrsForFirmwareUpgrade(presentXcvrIds);
  {
    auto tcvrsToUpgradeWLock = tcvrsForFwUpgrade.wlock();
    triggerFirmwareUpgradeEvents(*tcvrsToUpgradeWLock);
    tcvrsToUpgradeWLock->clear();
  }

  if (!potentialTcvrsForFwUpgrade.empty()) {
    triggerFirmwareUpgradeEvents(potentialTcvrsForFwUpgrade);
  }
}

void TransceiverManager::refreshStateMachines() {
  XLOG(INFO) << "refreshStateMachines started";

  // Step1: Fetch current port status from wedge_agent.
  // Since the following steps, like refreshTransceivers() might need to use
  // port status to decide whether it's safe to reset a transceiver.
  // Therefore, always do port status update first.
  if (!FLAGS_port_manager_mode) {
    updateTransceiverPortStatus();
  }

  // Step2: Refresh all transceivers so that we can get an update
  // TransceiverInfo
  const auto& presentXcvrIds = refreshTransceivers();

  // Step3: Check whether there's a wedge_agent config change
  triggerAgentConfigChangeEvent();

  // Step4: Once the transceivers are detected, trigger programming events
  const auto& programmedTcvrs = triggerProgrammingEvents();

  // Step5: Remediate inactive transceivers
  // Only need to remediate ports which are not recently finished
  // programming. Because if they only finished early stage programming like
  // iphy without programming xphy or tcvr, the ports of such transceiver
  // will still be not stable to be remediated.
  std::vector<TransceiverID> stableTcvrs;
  for (auto tcvrID : presentXcvrIds) {
    if (std::find(programmedTcvrs.begin(), programmedTcvrs.end(), tcvrID) ==
        programmedTcvrs.end()) {
      stableTcvrs.push_back(tcvrID);
    }
  }
  triggerRemediateEvents(stableTcvrs);

  publishPimStatesToFsdb();

  completeRefresh();

  // Update the warmboot state if there is a change.
  setWarmBootState();

  XLOG(INFO) << "refreshStateMachines ended";
}

void TransceiverManager::completeRefresh() {
  // Resume heartbeats at the end of refresh loop in case they were paused by
  // any of the operations during the refresh cycle
  for (auto& [threadId, threadHelper] : qsfpServiceThreads_->threadIdToThread) {
    heartbeatWatchdog_->resumeMonitoringHeartbeat(
        threadHelper.getThreadHeartbeat());
  }
  heartbeatWatchdog_->resumeMonitoringHeartbeat(updateThreadHeartbeat_);
  isUpgradingFirmware_ = false;

  if (!isFullyInitialized_) {
    isFullyInitialized_ = true;
    // On successful initialization, set warm boot flag in case of a
    // qsfp_service crash (no gracefulExit).

    /* We don't want to set warm boot flag here for platforms with external
     * PHYs The reason is SAI based external PHYs platforms needs to
     * gracefully shutdown to store the warmboot state */
    if (!hasExternalPhy()) {
      setCanWarmBoot();
    }

    restart_time::mark(RestartEvent::CONFIGURED);
  }
}

void TransceiverManager::triggerAgentConfigChangeEvent() {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("triggerAgentConfigChangeEvent");
  }
  auto wedgeAgentClient = utils::createWedgeAgentClient();
  ConfigAppliedInfo newConfigAppliedInfo;
  try {
    wedgeAgentClient->sync_getConfigAppliedInfo(newConfigAppliedInfo);
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    XLOG(WARN) << "Failed to call wedge_agent getConfigAppliedInfo(). "
               << folly::exceptionStr(ex);

    // For testing only, if overrideAgentConfigAppliedInfoForTesting_ is set,
    // use it directly; otherwise return without trigger any config changed
    // events
    if (overrideAgentConfigAppliedInfoForTesting_) {
      XLOG(INFO)
          << "triggerAgentConfigChangeEvent is using override ConfigAppliedInfo"
          << ", lastAppliedInMs="
          << *overrideAgentConfigAppliedInfoForTesting_->lastAppliedInMs()
          << ", lastColdbootAppliedInMs="
          << (overrideAgentConfigAppliedInfoForTesting_
                      ->lastColdbootAppliedInMs()
                  ? *overrideAgentConfigAppliedInfoForTesting_
                         ->lastColdbootAppliedInMs()
                  : 0);
      newConfigAppliedInfo = *overrideAgentConfigAppliedInfoForTesting_;
    } else {
      return;
    }
  }

  // Now check if the new timestamp is later than the cached one.
  if (*newConfigAppliedInfo.lastAppliedInMs() <=
      *configAppliedInfo_.lastAppliedInMs()) {
    return;
  }

  // Only need to reset data path if there's a new coldboot
  bool resetDataPath = false;
  std::string resetDataPathLog;
  if (auto lastColdbootAppliedInMs =
          newConfigAppliedInfo.lastColdbootAppliedInMs()) {
    if (auto oldLastColdbootAppliedInMs =
            configAppliedInfo_.lastColdbootAppliedInMs()) {
      resetDataPath = (*lastColdbootAppliedInMs > *oldLastColdbootAppliedInMs);
      if (resetDataPath) {
        resetDataPathLog = folly::to<std::string>(
            "Need reset data path. [Old Coldboot time:",
            *oldLastColdbootAppliedInMs,
            ", New Coldboot time:",
            *lastColdbootAppliedInMs,
            "]");
      }
    } else {
      // Always reset data path the cached info doesn't have coldboot config
      // applied time
      resetDataPath = true;
      resetDataPathLog = folly::to<std::string>(
          "Need reset data path. [Old Coldboot time:0, New Coldboot time:",
          *lastColdbootAppliedInMs,
          "]");
    }
  }

  XLOG(INFO) << "New Agent config applied time:"
             << *newConfigAppliedInfo.lastAppliedInMs()
             << " and last cached time:"
             << *configAppliedInfo_.lastAppliedInMs()
             << ". Issue all ports reprogramming events. " << resetDataPathLog;

  triggerTransceiverEventsForAgentConfigChangeEvent(
      resetDataPath, newConfigAppliedInfo);
}

void TransceiverManager::waitForAllBlockingStateUpdateDone(
    const TransceiverManager::BlockingStateUpdateResultList& results) {
  for (const auto& result : results) {
    if (isExiting_) {
      XLOG(INFO)
          << "Terminating waitForAllBlockingStateUpdateDone for graceful exit";
      return;
    }
    result->wait();
  }
}

/*
 * getPortIDByPortName
 *
 * This function takes the port name string (eth2/1/1) and returns the
 * software port id (or the agent port id) for that
 */
std::optional<PortID> TransceiverManager::getPortIDByPortName(
    const std::string& portName) const {
  auto portMapIt = portNameToPortID_.left.find(portName);
  if (portMapIt != portNameToPortID_.left.end()) {
    return portMapIt->second;
  }
  return std::nullopt;
}

/*
 * getPortNameByPortId
 *
 * This function takes the software port id and returns corresponding port
 * name string (ie: eth2/1/1)
 */
std::optional<std::string> TransceiverManager::getPortNameByPortId(
    PortID portId) const {
  auto portMapIt = portNameToPortID_.right.find(portId);
  if (portMapIt != portNameToPortID_.right.end()) {
    return portMapIt->second;
  }
  return std::nullopt;
}

std::vector<PortID> TransceiverManager::getAllPlatformPorts(
    TransceiverID tcvrID) const {
  std::vector<PortID> ports;
  for (const auto& [portID, portInfo] : portToSwPortInfo_) {
    if (portInfo.tcvrID && *portInfo.tcvrID == tcvrID) {
      ports.push_back(portID);
    }
  }
  return ports;
}

std::set<TransceiverID> TransceiverManager::getPresentTransceivers() const {
  std::set<TransceiverID> presentTcvrs;
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& tcvrIt : *lockedTransceivers) {
    if (tcvrIt.second->isPresent()) {
      presentTcvrs.insert(tcvrIt.first);
    }
  }
  return presentTcvrs;
}

void TransceiverManager::setOverrideAgentPortStatusForTesting(
    bool up,
    bool enabled,
    bool clearOnly) {
  // Use overrideTcvrToPortAndProfileForTest_ to prepare
  // overrideAgentPortStatusForTesting_
  overrideAgentPortStatusForTesting_.clear();
  if (clearOnly) {
    return;
  }
  for (const auto& it : overrideTcvrToPortAndProfileForTest_) {
    for (const auto& [portID, profileID] : it.second) {
      NpuPortStatus status;
      status.portEnabled = enabled;
      status.operState = up;
      status.profileID = apache::thrift::util::enumNameSafe(profileID);
      overrideAgentPortStatusForTesting_.emplace(portID, std::move(status));
    }
  }
}

void TransceiverManager::setOverrideAgentConfigAppliedInfoForTesting(
    std::optional<ConfigAppliedInfo> configAppliedInfo) {
  overrideAgentConfigAppliedInfoForTesting_ = configAppliedInfo;
}

std::pair<bool, std::vector<std::string>> TransceiverManager::areAllPortsDown(
    TransceiverID id) const noexcept {
  auto portToPortInfoIt = tcvrToPortInfo_.find(id);
  if (portToPortInfoIt == tcvrToPortInfo_.end()) {
    MODULE_LOG(WARN, "", id) << "Can't find in cached tcvrToPortInfo_";
    return {false, {}};
  }
  auto portToPortInfoWithLock = portToPortInfoIt->second->rlock();
  if (portToPortInfoWithLock->empty()) {
    MODULE_LOG(WARN, "", id) << "Can't find any programmed port in cached tcvrToPortInfo_";
    //  If no port information for the transceiver is found, we must assume that
    //  ports are up to ensure we don't accidentally remediate on warmboot. This
    //  is consistent for Port Manager mode and non-Port Manager mode.
    return {false, {}};
  }
  bool anyPortUp = false;
  std::vector<std::string> downPorts;
  for (const auto& [portID, portInfo] : *portToPortInfoWithLock) {
    if (!portInfo.status.has_value()) {
      // If no status set, assume ports are up so we won't trigger any
      // disruptive event
      return {false, {}};
    }
    if (portInfo.status->operState) {
      anyPortUp = true;
    } else {
      auto portName = getPortNameByPortId(portID);
      if (portName.has_value()) {
        downPorts.push_back(*portName);
      }
    }
  }
  return {!anyPortUp, downPorts};
}

bool TransceiverManager::isRunningAsicPrbs(TransceiverID tcvr) const {
  if (FLAGS_port_manager_mode) {
    // npuPortStatusCache_ is only populated in legacy mode
    for (const auto& [_, portInfo] : getProgrammedIphyPortToPortInfo(tcvr)) {
      if (portInfo.status.has_value() && portInfo.status->asicPrbsEnabled) {
        return true;
      }
    }
    return false;
  }

  auto ports = getAllPlatformPorts(tcvr);
  // Hold a single locked view for the whole scan so the cache can't be
  // wholesale-swapped out (see updateNpuPortStatusCache()) between the
  // find() and the end()-check/dereference below.
  auto lockedNpuPortStatusCache = npuPortStatusCache_.rlock();
  for (const auto& port : ports) {
    auto npuPortStatusCacheItr = lockedNpuPortStatusCache->find(port);
    if (npuPortStatusCacheItr == lockedNpuPortStatusCache->end()) {
      continue;
    }
    if (npuPortStatusCacheItr->second.asicPrbsEnabled) {
      return true;
    }
  }
  return false;
}

void TransceiverManager::triggerRemediateEvents(
    const std::vector<TransceiverID>& stableTcvrs) {
  if (stableTcvrs.empty()) {
    return;
  }
  if (isExiting_) {
    XLOG(INFO) << "Skip triggerRemediateEvents during graceful exit";
    return;
  }
  BlockingStateUpdateResultList results;
  for (auto tcvrID : stableTcvrs) {
    // Check if any of the ports are running ASIC PRBS. If yes, skip
    // triggering remediation on transceiver.
    if (isRunningAsicPrbs(tcvrID)) {
      MODULE_LOG(DBG2, "", tcvrID)
          << "Skip remediating. Transceiver is running ASIC PRBS";
      continue;
    }

    const auto& programmedPortToPortInfo =
        getProgrammedIphyPortToPortInfo(tcvrID);
    if (programmedPortToPortInfo.empty()) {
      // This is due to the iphy ports are disabled. So no need to remediate
      continue;
    }

    auto curState = getCurrentState(tcvrID);
    // If we are not in the active or inactive state, don't try to remediate
    // yet

    bool isValidState = FLAGS_port_manager_mode
        ? curState == TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED
        : (curState == TransceiverStateMachineState::ACTIVE ||
           curState == TransceiverStateMachineState::INACTIVE);
    if (!isValidState) {
      continue;
    }

    // If we are here because we are in active state, check if any of the
    // ports are down. If yes, try to remediate (partial). If we are here
    // because we are in inactive state, areAllPortsDown will return a
    // non-empty list of down ports anyways, so we will try to remediate
    if (areAllPortsDown(tcvrID).second.empty()) {
      continue;
    }

    // Then check whether we should remediate so that we don't have to create
    // too many unnecessary state machine update
    auto lockedTransceivers = transceivers_.rlock();
    auto tcvrIt = lockedTransceivers->find(tcvrID);
    if (tcvrIt == lockedTransceivers->end()) {
      MODULE_LOG(DBG2, "", tcvrID)
          << "Skip remediating. Not present";
      continue;
    }
    if (!tcvrIt->second->shouldRemediate(pauseRemediationUntil_)) {
      continue;
    }
    if (auto result = updateStateBlockingWithoutWait(
            tcvrID,
            TransceiverStateMachineEvent::TCVR_EV_REMEDIATE_TRANSCEIVER)) {
      results.push_back(result);
    }
  }
  waitForAllBlockingStateUpdateDone(results);
  XLOG_IF(INFO, !results.empty())
      << "triggerRemediateEvents has " << results.size()
      << " transceivers kicked off remediation";
}

void TransceiverManager::markLastDownTime(TransceiverID id) noexcept {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    MODULE_LOG(DBG2, "", id)
        << "Skip markLastDownTime. Not present";
    return;
  }
  tcvrIt->second->markLastDownTime();
}

void TransceiverManager::updateLastDownTimeFromPortStatus(
    const std::map<TransceiverID, bool>& tcvrPortStatusChanges) noexcept {
  for (const auto& [tcvrId, portsNowActive] : tcvrPortStatusChanges) {
    auto stateMachineItr = stateMachineControllers_.find(tcvrId);
    if (stateMachineItr == stateMachineControllers_.end()) {
      MODULE_LOG(DBG2, "", tcvrId)
          << "Skip updateLastDownTimeFromPortStatus, state machine not found";
      continue;
    }

    auto lockedStateMachine =
        stateMachineItr->second->getStateMachine().wlock();

    if (portsNowActive) {
      // Equivalent to activeStateEntry: enable marking for next DOWN transition
      lockedStateMachine->get_attribute(needMarkLastDownTime) = true;
      MODULE_LOG(DBG2, "", tcvrId)
          << "Port Manager mode: Port(s) now active, set needMarkLastDownTime=true";
    } else {
      // Equivalent to markLastDownTime (INACTIVE state entry):
      // Only mark if needMarkLastDownTime is true, then clear it
      if (lockedStateMachine->get_attribute(needMarkLastDownTime)) {
        markLastDownTime(tcvrId);
        lockedStateMachine->get_attribute(needMarkLastDownTime) = false;
        MODULE_LOG(DBG2, "", tcvrId)
            << "Port Manager mode: All ports down, marked lastDownTime";
      } else {
        MODULE_LOG(DBG3, "", tcvrId)
            << "Port Manager mode: All ports down, but needMarkLastDownTime=false, skipping";
      }
    }
  }
}

time_t TransceiverManager::getLastDownTime(TransceiverID id) const {
  auto lockedTransceivers = transceivers_.rlock();
  auto tcvrIt = lockedTransceivers->find(id);
  if (tcvrIt == lockedTransceivers->end()) {
    throw FbossError(
        "Can't find Transceiver=", id, ". Transceiver is not present");
  }
  return tcvrIt->second->getLastDownTime();
}

void TransceiverManager::getAllInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos) {
  for (auto [portName, _] : getPortNameToModuleMap()) {
    getInterfacePhyInfo(phyInfos, portName);
  }
}

void TransceiverManager::getInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos,
    const std::string& portName) {
  auto portIDOpt = getPortIDByPortName(portName);
  if (!portIDOpt) {
    throw FbossError(
        "Unrecoginized portName:", portName, ", can't find port id");
  }
  try {
    phyInfos[portName] = getXphyInfo(*portIDOpt);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Fetching PhyInfo for " << portName << " failed with "
              << ex.what();
  }
}

void TransceiverManager::publishLinkSnapshots(std::string portName) {
  auto portIDOpt = getPortIDByPortName(portName);
  if (!portIDOpt) {
    throw FbossError(
        "Unrecoginized portName:", portName, ", can't find port id");
  }
  publishLinkSnapshots(*portIDOpt);
}

void TransceiverManager::publishLinkSnapshots(PortID portID) {
  // Publish xphy snapshots if there's a phyManager and xphy ports
  if (phyManager_) {
    phyManager_->publishXphyInfoSnapshots(portID);
  }
  // Publish transceiver snapshots if there's a transceiver
  publishLinkSnapshotsTransceiver(portID);
}

void TransceiverManager::publishLinkSnapshotsTransceiver(PortID portID) {
  if (auto tcvrIDOpt = getTransceiverID(portID)) {
    const auto& snapshotManagersLocked = snapshotManagers_.wlock();
    if (auto snapshotManagerIt = snapshotManagersLocked->find(*tcvrIDOpt);
        snapshotManagerIt != snapshotManagersLocked->end()) {
      snapshotManagerIt->second.publishAllSnapshots();
      snapshotManagerIt->second.publishFutureSnapshots();
    }
  }
}

std::optional<TransceiverID> TransceiverManager::getTransceiverID(
    PortID portID) const {
  auto swPortInfo = portToSwPortInfo_.find(portID);
  if (swPortInfo == portToSwPortInfo_.end()) {
    throw FbossError("Failed to find SwPortInfo for port ID ", portID);
  }
  return swPortInfo->second.tcvrID;
}

bool TransceiverManager::verifyEepromChecksumsLocked(TransceiverID id) {
  ensureTransceiversMapLocked(
      "verifyEepromChecksumsLocked: transceivers_ is not locked.");
  auto tcvrIt = transceivers_.unsafeGetUnlocked().find(id);
  if (tcvrIt == transceivers_.unsafeGetUnlocked().end()) {
    MODULE_LOG(DBG2, "", id)
        << "Skip verifying eeprom checksum. Not present";
    return true;
  }
  return tcvrIt->second->verifyEepromChecksums();
}

bool TransceiverManager::checkWarmBootFlags() {
  // Return true if coldBootOnceFile does not exist and canWarmBoot file
  // exists
  const auto& forceColdBootFile = forceColdBootFileName();
  const auto& warmBootFile = warmBootFlagFileName();

  bool forceColdBoot = removeFile(forceColdBootFile);
  if (forceColdBoot || !FLAGS_can_qsfp_service_warm_boot) {
    XLOG(INFO) << "Force Cold Boot file: " << forceColdBootFile
               << " is set. Removing Warm Boot file " << warmBootFile;
    removeWarmBootFlag();
    return false;
  }

  // Instead of removing the can_warm_boot file, we keep it unless it's a
  // coldboot, so that qsfp_service crash can still use warm boot.
  bool canWarmBoot = checkFileExists(warmBootFile);
  XLOG(INFO) << "Warm Boot flag: " << warmBootFile << " is "
             << (canWarmBoot ? "set" : "missing");
  return canWarmBoot;
}

void TransceiverManager::removeWarmBootFlag() {
  removeFile(warmBootFlagFileName());
}

std::string TransceiverManager::forceColdBootFileName() {
  return folly::to<std::string>(
      FLAGS_qsfp_service_volatile_dir, "/", kForceColdBootFileName);
}

std::string TransceiverManager::warmBootFlagFileName() {
  return folly::to<std::string>(
      FLAGS_qsfp_service_volatile_dir, "/", kWarmBootFlag);
}

std::string TransceiverManager::warmBootStateFileName() const {
  return folly::to<std::string>(
      FLAGS_qsfp_service_volatile_dir, "/", kWarmbootStateFileName);
}

std::string TransceiverManager::xphyWarmBootStateDirectory() const {
  return folly::to<std::string>(
      FLAGS_qsfp_service_volatile_dir, "/", kPhyStateKey);
}

/* static */ void TransceiverManager::writeWarmBootState(
    const folly::dynamic* phyWarmbootState,
    const ConfigAppliedInfo& configInfo,
    std::string& cachedState,
    const std::string& fileName) {
  steady_clock::time_point begin = steady_clock::now();
  folly::dynamic qsfpServiceState = folly::dynamic::object;

  if (phyWarmbootState) {
    qsfpServiceState[kPhyStateKey] = *phyWarmbootState;
  }

  folly::dynamic configState = folly::dynamic::object;
  configState[kAgentConfigLastAppliedInMsKey] =
      *configInfo.lastAppliedInMs();
  if (auto lastColdboot = configInfo.lastColdbootAppliedInMs()) {
    configState[kAgentConfigLastColdbootAppliedInMsKey] = *lastColdboot;
  }
  qsfpServiceState[kAgentConfigAppliedInfoStateKey] = configState;

  std::string currentState = folly::toPrettyJson(qsfpServiceState);
  if (cachedState != currentState) {
    cachedState = std::move(currentState);

    steady_clock::time_point assembled = steady_clock::now();
    XLOG(INFO)
        << "Finish assembling warm boot state. Time: "
        << duration_cast<duration<float>>(assembled - begin).count();
    folly::writeFile(cachedState, fileName.c_str());
    steady_clock::time_point written = steady_clock::now();
    XLOG(INFO) << "Finish writing warm boot state to file. Time: "
               << duration_cast<duration<float>>(written - assembled).count();
  }
}

void TransceiverManager::setWarmBootState() {
  if (phyManager_) {
    folly::dynamic phyState = phyManager_->getWarmbootState();
    writeWarmBootState(
        &phyState,
        configAppliedInfo_,
        qsfpServiceWarmbootState_,
        warmBootStateFileName());
  } else {
    writeWarmBootState(
        nullptr,
        configAppliedInfo_,
        qsfpServiceWarmbootState_,
        warmBootStateFileName());
  }
}

void TransceiverManager::setCanWarmBoot() {
  const auto& warmBootFile = warmBootFlagFileName();
  auto createFd = createFile(warmBootFile);
  close(createFd);
  XLOG(INFO) << "Wrote can warm boot flag: " << warmBootFile;
}

void TransceiverManager::restoreWarmBootPhyState() {
  // Reached in Port Manager mode via WedgeManager::initExternalPhyMap();
  // PortManager::initExternalPhyMap() restores its own copy afterwards.
  if (FLAGS_port_manager_mode) {
    PORT_MGR_INTENTIONAL_SKIP_LOG("restoreWarmbootPhyState");
    return;
  }

  // Only need to restore warm boot state if this is a warm boot
  if (!canWarmBoot_) {
    XLOG(INFO) << "[Cold Boot] No need to restore warm boot state";
    return;
  }

  if (const auto& phyStateIt =
          warmBootState_.find(TransceiverManager::kPhyStateKey);
      phyManager_ && phyStateIt != warmBootState_.items().end()) {
    phyManager_->restoreFromWarmbootState(phyStateIt->second);
  }
}

namespace {
phy::Side prbsComponentToPhySide(phy::PortComponent component) {
  switch (component) {
    case phy::PortComponent::ASIC:
      throw FbossError("qsfp_service doesn't support program ASIC prbs");
    case phy::PortComponent::GB_SYSTEM:
    case phy::PortComponent::TRANSCEIVER_SYSTEM:
      return phy::Side::SYSTEM;
    case phy::PortComponent::GB_LINE:
    case phy::PortComponent::TRANSCEIVER_LINE:
      return phy::Side::LINE;
  };
  throw FbossError(
      "Unsupported prbs component: ",
      apache::thrift::util::enumNameSafe(component));
}
} // namespace

void TransceiverManager::setInterfacePrbs(
    std::string portName,
    phy::PortComponent component,
    const prbs::InterfacePrbsState& state) {
  // Get the port ID first
  auto portId = getPortIDByPortName(portName);
  if (!portId.has_value()) {
    throw FbossError("Can't find a portID for portName ", portName);
  }

  // Sanity check
  if (!state.generatorEnabled().has_value() &&
      !state.checkerEnabled().has_value()) {
    throw FbossError("Neither generator or checker specified for PRBS setting");
  }

  if (isTransceiverComponent(component)) {
    setInterfacePrbsTransceiver(portId.value(), portName, component, state);
  } else {
    if (!phyManager_) {
      throw FbossError("Current platform doesn't support xphy");
    }
    // PhyManager is using old portPrbsState
    phy::PortPrbsState phyPrbs;
    phyPrbs.polynominal() = static_cast<int>(state.polynomial().value());
    phyPrbs.enabled() = (state.generatorEnabled().has_value() &&
                         state.generatorEnabled().value()) ||
        (state.checkerEnabled().has_value() && state.checkerEnabled().value());
    phyManager_->setPortPrbs(
        portId.value(), prbsComponentToPhySide(component), phyPrbs);
  }
}

void TransceiverManager::setInterfacePrbsTransceiver(
    PortID portId,
    const std::string& portName,
    phy::PortComponent component,
    const prbs::InterfacePrbsState& state) {
  if (auto tcvrID = getTransceiverID(portId)) {
    phy::Side side = prbsComponentToPhySide(component);
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      if (!it->second->setPortPrbs(portName, side, state)) {
        throw FbossError("Failed to set PRBS on transceiver ", *tcvrID);
      }
    } else {
      throw FbossError("Can't find transceiver ", *tcvrID);
    }
  } else {
    throw FbossError("Can't find transceiverID for portID ", portId);
  }
}

phy::PrbsStats TransceiverManager::getPortPrbsStats(
    PortID portId,
    phy::PortComponent component) const {
  phy::Side side = prbsComponentToPhySide(component);
  if (isTransceiverComponent(component)) {
    return getPortPrbsStatsTransceiver(portId, side);
  } else {
    if (!phyManager_) {
      throw FbossError("Current platform doesn't support xphy");
    }
    phy::PrbsStats stats;
    auto lanePrbsStats = phyManager_->getPortPrbsStats(portId, side);
    for (const auto& lane : lanePrbsStats) {
      stats.laneStats()->push_back(lane);
      auto timeCollected = lane.timeCollected().value();
      // Store most recent timeCollected across all lane stats
      if (timeCollected > stats.timeCollected()) {
        stats.timeCollected() = timeCollected;
      }
    }
    stats.portId() = portId;
    stats.component() = component;
    return stats;
  }
}

phy::PrbsStats TransceiverManager::getPortPrbsStatsTransceiver(
    PortID portId,
    phy::Side side) const {
  auto portName = getPortNameByPortId(portId);
  auto lockedTransceivers = transceivers_.rlock();
  if (auto tcvrID = getTransceiverID(portId)) {
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      if (portName.has_value()) {
        return it->second->getPortPrbsStats(portName.value(), side);
      } else {
        throw FbossError("Can't find a portName for portId ", portId);
      }
    } else {
      throw FbossError("Can't find transceiver ", *tcvrID);
    }
  } else {
    throw FbossError("Can't find transceiverID for portID ", portId);
  }
}

void TransceiverManager::clearPortPrbsStats(
    PortID portId,
    phy::PortComponent component) {
  auto portName = getPortNameByPortId(portId);
  if (!portName.has_value()) {
    throw FbossError("Can't find a portName for portId ", portId);
  }
  phy::Side side = prbsComponentToPhySide(component);
  if (isTransceiverComponent(component)) {
    clearPortPrbsStatsTransceiver(portId, *portName, side);
  } else if (!phyManager_) {
    throw FbossError("Current platform doesn't support xphy");
  } else {
    phyManager_->clearPortPrbsStats(portId, prbsComponentToPhySide(component));
  }
}

void TransceiverManager::clearPortPrbsStatsTransceiver(
    PortID portId,
    const std::string& portName,
    phy::Side side) {
  auto lockedTransceivers = transceivers_.rlock();
  if (auto tcvrID = getTransceiverID(portId)) {
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      it->second->clearTransceiverPrbsStats(portName, side);
    } else {
      throw FbossError("Can't find transceiver ", *tcvrID);
    }
  } else {
    throw FbossError("Can't find transceiverID for portID ", portId);
  }
}

std::vector<prbs::PrbsPolynomial>
TransceiverManager::getTransceiverPrbsCapabilities(
    TransceiverID tcvrID,
    phy::Side side) {
  auto lockedTransceivers = transceivers_.rlock();
  if (auto it = lockedTransceivers->find(tcvrID);
      it != lockedTransceivers->end()) {
    return it->second->getPrbsCapabilities(side);
  }
  return std::vector<prbs::PrbsPolynomial>();
}

void TransceiverManager::getSupportedPrbsPolynomials(
    std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
    std::string portName,
    phy::PortComponent component) {
  phy::Side side = prbsComponentToPhySide(component);
  if (isTransceiverComponent(component)) {
    if (portNameToModule_.find(portName) == portNameToModule_.end()) {
      throw FbossError("Can't find transceiver module for port ", portName);
    }
    prbsCapabilities = getTransceiverPrbsCapabilities(
        TransceiverID(portNameToModule_[portName]), side);
  } else {
    throw FbossError(
        "PRBS on ",
        apache::thrift::util::enumNameSafe(component),
        " not supported by qsfp_service");
  }
}

void TransceiverManager::setPortPrbs(
    PortID portId,
    phy::PortComponent component,
    const phy::PortPrbsState& state) {
  auto portName = getPortNameByPortId(portId);
  if (!portName.has_value()) {
    throw FbossError("Can't find a portName for portId ", portId);
  }

  prbs::InterfacePrbsState newState;
  newState.polynomial() = prbs::PrbsPolynomial(state.polynominal().value());
  newState.generatorEnabled() = state.enabled().value();
  newState.checkerEnabled() = state.enabled().value();
  setInterfacePrbs(portName.value(), component, newState);
}

void TransceiverManager::getInterfacePrbsState(
    prbs::InterfacePrbsState& prbsState,
    const std::string& portName,
    phy::PortComponent component) const {
  if (auto portID = getPortIDByPortName(portName)) {
    if (isTransceiverComponent(component)) {
      getInterfacePrbsStateTransceiver(prbsState, *portID, portName, component);
    } else {
      throw FbossError(
          "getInterfacePrbsState not supported on component ",
          apache::thrift::util::enumNameSafe(component));
    }
  } else {
    throw FbossError("Can't find a portID for portName ", portName);
  }
}

void TransceiverManager::getInterfacePrbsStateTransceiver(
    prbs::InterfacePrbsState& prbsState,
    PortID portId,
    const std::string& portName,
    phy::PortComponent component) const {
  if (auto tcvrID = getTransceiverID(portId)) {
    phy::Side side = prbsComponentToPhySide(component);
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      prbsState = it->second->getPortPrbsState(portName, side);
      return;
    } else {
      throw FbossError("Can't find transceiver ", *tcvrID);
    }
  } else {
    throw FbossError("Can't find transceiverID for portID ", portId);
  }
}

void TransceiverManager::getAllInterfacePrbsStates(
    std::map<std::string, prbs::InterfacePrbsState>& prbsStates,
    phy::PortComponent component) const {
  const auto& platformPorts = platformMapping_->getPlatformPorts();
  for (const auto& platformPort : platformPorts) {
    auto portName = platformPort.second.mapping()->name();
    try {
      prbs::InterfacePrbsState prbsState;
      getInterfacePrbsState(prbsState, *portName, component);
      prbsStates[*portName] = prbsState;
    } catch (const std::exception& ex) {
      // If PRBS is not enabled on this port, return
      // a default stats / State.
      XLOG(DBG2) << "Failed to get prbs state for port " << *portName
                 << " with error: " << ex.what();
      prbsStates[*portName] = prbs::InterfacePrbsState();
    }
  }
}

phy::PrbsStats TransceiverManager::getInterfacePrbsStats(
    const std::string& portName,
    phy::PortComponent component) const {
  if (auto portID = getPortIDByPortName(portName)) {
    return getPortPrbsStats(*portID, component);
  }
  throw FbossError("Can't find a portID for portName ", portName);
}

void TransceiverManager::getAllInterfacePrbsStats(
    std::map<std::string, phy::PrbsStats>& prbsStats,
    phy::PortComponent component) const {
  const auto& platformPorts = platformMapping_->getPlatformPorts();
  for (const auto& platformPort : platformPorts) {
    auto portName = platformPort.second.mapping()->name();
    try {
      auto prbsStatsEntry = getInterfacePrbsStats(*portName, component);
      prbsStats[*portName] = prbsStatsEntry;
    } catch (const std::exception& ex) {
      // If PRBS is not enabled on this port, return
      // a default stats / State.
      XLOG(DBG2) << "Failed to get prbs stats for port " << *portName
                 << " with error: " << ex.what();
      prbsStats[*portName] = phy::PrbsStats();
    }
  }
}

void TransceiverManager::clearInterfacePrbsStats(
    std::string portName,
    phy::PortComponent component) {
  if (auto portID = getPortIDByPortName(portName)) {
    clearPortPrbsStats(*portID, component);
  } else {
    throw FbossError("Can't find a portID for portName ", portName);
  }
}

void TransceiverManager::bulkClearInterfacePrbsStats(
    std::unique_ptr<std::vector<std::string>> interfaces,
    phy::PortComponent component) {
  for (const auto& interface : *interfaces) {
    clearInterfacePrbsStats(interface, component);
  }
}

std::optional<DiagsCapability> TransceiverManager::getDiagsCapability(
    TransceiverID id) const {
  auto lockedTransceivers = transceivers_.rlock();
  if (auto it = lockedTransceivers->find(id); it != lockedTransceivers->end()) {
    return it->second->getDiagsCapability();
  }
  MODULE_LOG(WARN, "", id) << "Return nullopt DiagsCapability. Not present";
  return std::nullopt;
}

void TransceiverManager::setDiagsCapabilityLocked(TransceiverID id) {
  ensureTransceiversMapLocked(
      "setDiagsCapabilityLocked: transceivers_ is not locked.");
  auto tcvrIt = transceivers_.unsafeGetUnlocked().find(id);
  if (tcvrIt != transceivers_.unsafeGetUnlocked().end()) {
    return tcvrIt->second->setDiagsCapability();
  }
  MODULE_LOG(DBG2, "", id) << "Skip setting DiagsCapability. Not present";
}

Transceiver* FOLLY_NULLABLE TransceiverManager::overrideTransceiverForTesting(
    TransceiverID id,
    std::unique_ptr<Transceiver> overrideTcvr) {
  auto lockedTransceivers = transceivers_.wlock();
  // Keep the same logic as updateTransceiverMap()
  if (auto it = lockedTransceivers->find(id); it != lockedTransceivers->end()) {
    it->second->removeTransceiver();
    lockedTransceivers->erase(it);
  }
  // Only set the override transceiver if it's not null so that we can support
  // removing transceiver in tests
  if (overrideTcvr) {
    lockedTransceivers->emplace(id, std::move(overrideTcvr));
    return lockedTransceivers->at(id).get();
  } else {
    return nullptr;
  }
}

std::vector<TransceiverID> TransceiverManager::refreshTransceivers(
    const std::unordered_set<TransceiverID>& transceivers) {
  std::vector<TransceiverID> transceiverIds;
  std::vector<folly::Future<bool>> futs;

  {
    auto lockedTransceivers = transceivers_.rlock();
    auto nTransceivers =
        transceivers.empty() ? lockedTransceivers->size() : transceivers.size();
    XLOG(INFO) << "Start refreshing " << nTransceivers << " transceivers...";

    for (const auto& transceiver : *lockedTransceivers) {
      TransceiverID id = TransceiverID(transceiver.second->getID());
      // If we're trying to refresh a subset and this transceiver is not in
      // that subset, skip it.
      if (!transceivers.empty() &&
          transceivers.find(id) == transceivers.end()) {
        continue;
      }
      MODULE_LOG(DBG3, "", id) << "Fired to refresh";
      transceiverIds.push_back(id);
      futs.push_back(transceiver.second->futureRefresh());
    }

    folly::collectAll(futs.begin(), futs.end()).wait();
    // If refresh() was successful, remove the tcvr from erroredTransceivers_
    // otherwise, add it to erroredTransceivers_
    CHECK_EQ(transceiverIds.size(), futs.size());
    auto erroredTransceivers = erroredTransceivers_.wlock();
    for (auto i = 0; i < transceiverIds.size(); i++) {
      const auto& id = transceiverIds[i];
      const auto& fut = futs[i];
      if (fut.hasValue() && fut.value()) {
        erroredTransceivers->erase(id);
      } else {
        erroredTransceivers->insert(id);
      }
    }

    XLOG(INFO) << "Finished refreshing " << nTransceivers << " transceivers";
  }

  publishTransceiversToFsdb();

  updateSnapshots();

  return transceiverIds;
}

void TransceiverManager::resetTransceiver(
    std::unique_ptr<std::vector<std::string>> portNames,
    ResetType resetType,
    ResetAction resetAction) {
  if (!portNames || portNames->empty()) {
    throw FbossError("Invalid portNames argument");
  }

  // Check that the ResetType and ResetAction pair have a valid function
  // call associated with TransceiverPlatformApi.
  auto itr = resetFunctionMap_.find(std::make_pair(resetType, resetAction));
  if (itr == resetFunctionMap_.end()) {
    throw FbossError(
        "Unsupported reset Type and reset action ", resetType, resetAction);
  }

  // Validate all transceivers before any reset action.
  std::vector<int> transceivers;
  for (auto portName : *portNames) {
    auto itr2 = portNameToModule_.find(portName);
    if (itr2 == portNameToModule_.end()) {
      throw FbossError(
          "Can't find transceiver module for port name: ", portName);
    }
    transceivers.push_back(itr2->second);
  }

  // Perform the proper reset action/type on each port.
  for (auto transceiver : transceivers) {
    itr->second(this, transceiver);
  }
}

void TransceiverManager::setPauseRemediation(
    int32_t timeout,
    std::unique_ptr<std::vector<std::string>> portList) {
  if (!portList.get() || portList->empty()) {
    pauseRemediationUntil_ = std::time(nullptr) + timeout;
  } else {
    auto lockedTransceivers = transceivers_.rlock();
    for (auto port : *portList) {
      if (portNameToModule_.find(port) == portNameToModule_.end()) {
        throw FbossError("Can't find transceiver module for port ", port);
      }

      auto it =
          lockedTransceivers->find(TransceiverID(portNameToModule_.at(port)));
      if (it != lockedTransceivers->end()) {
        it->second->setModulePauseRemediation(timeout);
      }
    }
  }
}

void TransceiverManager::getPauseRemediationUntil(
    std::map<std::string, int32_t>& info,
    std::unique_ptr<std::vector<std::string>> portList) {
  if (!portList.get() || portList->empty()) {
    info["all"] = pauseRemediationUntil_;
  } else {
    auto lockedTransceivers = transceivers_.rlock();
    for (auto port : *portList) {
      if (portNameToModule_.find(port) == portNameToModule_.end()) {
        throw FbossError("Can't find transceiver module for port ", port);
      }
      auto it =
          lockedTransceivers->find(TransceiverID(portNameToModule_.at(port)));
      if (it != lockedTransceivers->end()) {
        info[port] = it->second->getModulePauseRemediationUntil();
      }
    }
  }
}

void TransceiverManager::setPortLoopbackState(
    std::string portName,
    phy::PortComponent component,
    bool setLoopback,
    phy::LoopbackMode mode) {
  auto swPort = getPortIDByPortName(portName);
  if (!swPort.has_value()) {
    throw FbossError(
        fmt::format("setPortLoopbackState: Invalid port {}", portName));
  }
  if (component != phy::PortComponent::GB_SYSTEM &&
      component != phy::PortComponent::GB_LINE &&
      component != phy::PortComponent::TRANSCEIVER_SYSTEM &&
      component != phy::PortComponent::TRANSCEIVER_LINE) {
    XLOG(INFO)
        << " TransceiverManager::setPortLoopbackState - component not supported "
        << apache::thrift::util::enumNameSafe(component);
    return;
  }

  XLOG(INFO) << " TransceiverManager::setPortLoopbackState Port "
             << static_cast<int>(swPort.value());

  if (component == phy::PortComponent::GB_SYSTEM ||
      component == phy::PortComponent::GB_LINE) {
    if (mode != phy::LoopbackMode::INPUT) {
      throw FbossError(
          fmt::format(
              "Loopback mode {} is not supported on {}",
              apache::thrift::util::enumNameSafe(mode),
              apache::thrift::util::enumNameSafe(component)));
    }
    if (!getPhyManager()) {
      throw FbossError(
          "Unable to set xphy loopback state when PhyManager is not set");
    }
    getPhyManager()->setPortLoopbackState(
        PortID(swPort.value()), component, setLoopback);
  } else {
    setPortLoopbackStateTransceiver(
        swPort.value(), portName, component, setLoopback, mode);
  }
}

void TransceiverManager::setPortLoopbackStateTransceiver(
    PortID portId,
    std::string portName,
    phy::PortComponent component,
    bool setLoopback,
    phy::LoopbackMode mode) {
  // Get the Transceiver ID
  auto tcvrId = getTransceiverID(portId);
  if (!tcvrId.has_value()) {
    throw FbossError(
        fmt::format(
            "setPortLoopbackStateTransceiver: Transceiver not found for port {}",
            portName));
  }

  auto lockedTransceivers = transceivers_.rlock();
  if (auto it = lockedTransceivers->find(tcvrId.value());
      it != lockedTransceivers->end()) {
    if (component == phy::PortComponent::TRANSCEIVER_LINE) {
      it->second->setTransceiverLoopback(
          portName, phy::Side::LINE, setLoopback, mode);
    } else {
      it->second->setTransceiverLoopback(
          portName, phy::Side::SYSTEM, setLoopback, mode);
    }
  }
}

void TransceiverManager::setPortAdminState(
    std::string portName,
    phy::PortComponent component,
    bool setAdminUp) {
  auto swPort = getPortIDByPortName(portName);
  if (!swPort.has_value()) {
    throw FbossError(
        fmt::format("setPortAdminState: Invalid port {}", portName));
  }
  if (component != phy::PortComponent::GB_SYSTEM &&
      component != phy::PortComponent::GB_LINE) {
    XLOG(INFO)
        << " TransceiverManager::setPortAdminState - component not supported "
        << apache::thrift::util::enumNameSafe(component);
    return;
  }

  XLOG(INFO) << " TransceiverManager::setPortAdminState Port "
             << static_cast<int>(swPort.value());
  if (!getPhyManager()) {
    throw FbossError(
        "Unable to set xphy admin state when PhyManager is not set");
  }
  getPhyManager()->setPortAdminState(
      PortID(swPort.value()), component, setAdminUp);
}

/*
 * setInterfaceTxRx
 *
 * Set the interface Tx/Rx output state as per the request. Currently this API
 * supports the Transceiver system and line side control only. The lanes
 * corresponding to the SW ports are disabled/enabled in transceiver. If the
 * channel mask is explicitly specified then only those channels are
 * enabled/disabled
 */
std::vector<phy::TxRxEnableResponse> TransceiverManager::setInterfaceTxRx(
    const std::vector<phy::TxRxEnableRequest>& txRxEnableRequests) {
  std::vector<phy::TxRxEnableResponse> txRxEnableResponses;
  for (const auto& txRxEnableRequest : txRxEnableRequests) {
    auto portName = txRxEnableRequest.portName().value();
    auto component = txRxEnableRequest.component().value();
    auto enable = txRxEnableRequest.enable().value();
    auto channelMask = txRxEnableRequest.laneMask().to_optional();
    auto direction = txRxEnableRequest.direction().value();

    auto swPort = getPortIDByPortName(portName);
    if (!swPort.has_value()) {
      throw FbossError(
          fmt::format("setInterfaceTxRx: Invalid port {}", portName));
    }
    if (component != phy::PortComponent::TRANSCEIVER_LINE &&
        component != phy::PortComponent::TRANSCEIVER_SYSTEM) {
      throw FbossError(
          fmt::format(
              "TransceiverManager::setInterfaceTxRx - component not supported {}",
              apache::thrift::util::enumNameSafe(component)));
    }
    if (direction == phy::Direction::RECEIVE) {
      throw FbossError(
          fmt::format(
              "setInterfaceTxRx: Transceiver Rx lane control not implemented for {}",
              portName));
    }

    XLOG(INFO) << fmt::format(
        "TransceiverManager::setInterfaceTxRx Port {:s}", portName);

    // Get Transceiver ID for this SW Port
    auto tcvrId = getTransceiverID(swPort.value());
    if (!tcvrId.has_value()) {
      throw FbossError(
          fmt::format(
              "setInterfaceTxRx: Transceiver not found for port {}", portName));
    }

    // Finally call the transceiver object with SW Port channel list and
    // optionally user requested channel mask
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(tcvrId.value());
        it != lockedTransceivers->end()) {
      std::optional<uint8_t> tcvrChannelMask{std::nullopt};
      if (channelMask.has_value()) {
        tcvrChannelMask = channelMask.value();
      }

      phy::TxRxEnableResponse response;
      response.portName() = portName;
      auto side = (component == phy::PortComponent::TRANSCEIVER_LINE)
          ? phy::Side::LINE
          : phy::Side::SYSTEM;
      auto result =
          it->second->setTransceiverTx(portName, side, tcvrChannelMask, enable);
      response.success() = result;

      txRxEnableResponses.push_back(response);
    }
  }
  return txRxEnableResponses;
}

/*
 * getSymbolErrorHistogram
 *
 * This function returns the Symbol error histogram for a givem port. The
 * return value is a map of datapath id to CDB symbol error histogram values
 */
void TransceiverManager::getSymbolErrorHistogram(
    CdbDatapathSymErrHistogram& symErr,
    const std::string& portName) {
  std::map<std::string, CdbDatapathSymErrHistogram> symbolErrors;

  auto swPort = getPortIDByPortName(portName);
  if (!swPort.has_value()) {
    throw FbossError(
        fmt::format("getSymbolErrorHistogram: Invalid port {}", portName));
  }

  // Get Transceiver ID for this SW Port
  auto tcvrId = getTransceiverID(swPort.value());
  if (!tcvrId.has_value()) {
    throw FbossError(
        fmt::format(
            "getSymbolErrorHistogram: Transceiver not found for port {}",
            portName));
  }

  // Finally call the transceiver object with for symbol error get function
  auto lockedTransceivers = transceivers_.rlock();
  if (auto it = lockedTransceivers->find(tcvrId.value());
      it != lockedTransceivers->end()) {
    symbolErrors = it->second->getSymbolErrorHistogram();
  }

  for (auto& [pName, datapathSymErr] : symbolErrors) {
    if (pName != portName) {
      continue;
    }
    for (auto& [bin, datapathBinSymErr] : datapathSymErr.media().value()) {
      symErr.media()[bin] = datapathBinSymErr;
    }
    for (auto& [bin, datapathBinSymErr] : datapathSymErr.host().value()) {
      symErr.host()[bin] = datapathBinSymErr;
    }
  }
}

/*
 * getAllPortPhyInfo
 *
 * Get the map of software port id to PortPhyInfo in the system. This function
 * mainly for debugging
 */
std::map<uint32_t, phy::PhyIDInfo> TransceiverManager::getAllPortPhyInfo() {
  std::map<uint32_t, phy::PhyIDInfo> resultMap;

  const auto& allPlatformPortsIt = platformMapping_->getPlatformPorts();
  for (const auto& platformPortIt : allPlatformPortsIt) {
    auto portId = platformPortIt.first;
    GlobalXphyID xphyId;
    try {
      xphyId = phyManager_->getGlobalXphyIDbyPortID(PortID(portId));
    } catch (FbossError&) {
      continue;
    }
    phy::PhyIDInfo phyIdInfo = phyManager_->getPhyIDInfo(xphyId);
    resultMap[portId] = phyIdInfo;
  }

  return resultMap;
}

/*
 * getPhyInfo
 *
 * Returns the phy line params for a port
 */
phy::PhyInfo TransceiverManager::getPhyInfo(const std::string& portName) {
  auto swPort = getPortIDByPortName(portName);
  if (!swPort.has_value()) {
    throw FbossError(fmt::format("getPhyInfo: Invalid port {}", portName));
  }
  return getPhyManager()->getPhyInfo(PortID(swPort.value()));
}

std::string TransceiverManager::getPortInfo(std::string portName) {
  auto swPort = getPortIDByPortName(portName);
  if (!swPort.has_value()) {
    throw FbossError(fmt::format("getPortInfo: Invalid port {}", portName));
  }
  return getPhyManager()->getPortInfoStr(PortID(swPort.value()));
}

bool TransceiverManager::validateTransceiverConfiguration(
    TransceiverValidationInfo& tcvrInfo,
    std::string& notValidatedReason) const {
  if (tcvrValidator_ == nullptr) {
    return false;
  }
  return tcvrValidator_->validateTcvr(tcvrInfo, notValidatedReason);
}

bool TransceiverManager::isTransceiversMapLocked() const {
  return !static_cast<bool>(transceivers_.tryWLock());
}

void TransceiverManager::ensureTransceiversMapLocked(
    std::string message) const {
  if (!isTransceiversMapLocked()) {
    throw FbossError(message);
  }
}

void TransceiverManager::drainAllStateMachineUpdates() {
  if (stateMachineControllers_.empty()) {
    XLOG(INFO) << "No state machines created - returning early.";
    return;
  }

  if (!updateEventBase_ || !qsfpServiceThreads_) {
    XLOG(INFO)
        << "updateEventBase_ or threads not initialized - returning early.";
    return;
  }

  // Enforce no updates can be added while draining.
  for (auto& [_, stateMachineController] : stateMachineControllers_) {
    stateMachineController->blockNewUpdates();
  }

  // Make sure threads are actually active before we start draining.
  bool allStateMachineThreadsActive{true};
  for (auto& [threadId, threadHelper] : qsfpServiceThreads_->threadIdToThread) {
    if (!threadHelper.isThreadActive()) {
      allStateMachineThreadsActive = false;
      break;
    }
  }

  if (!allStateMachineThreadsActive) {
    XLOG(INFO) << "All state machine threads are not active. Skip draining.";
    return;
  }

  // Drain any pending updates by calling handlePendingUpdates directly.
  bool updatesDrained = false;
  do {
    updatesDrained = true;
    executeStateUpdates();
    for (auto& [_, stateMachineController] : stateMachineControllers_) {
      if (stateMachineController->arePendingUpdates()) {
        updatesDrained = false;
        break;
      }
    }
  } while (!updatesDrained);
}

bool TransceiverManager::opticalOrActiveCmisCable(const TcvrState& tcvrState) {
  if (tcvrState.transceiverManagementInterface().has_value() &&
      tcvrState.transceiverManagementInterface().value() ==
          TransceiverManagementInterface::CMIS &&
      opticalOrActiveCable(tcvrState)) {
    return true;
  }
  return false;
}

bool TransceiverManager::opticalOrActiveCable(const TcvrState& tcvrState) {
  if (tcvrState.cable().has_value() &&
      ((tcvrState.cable()->transmitterTech() ==
        TransmitterTechnology::OPTICAL) ||
       activeCable(tcvrState))) {
    return true;
  }
  return false;
}

bool TransceiverManager::activeCable(const TcvrState& tcvrState) {
  if (tcvrState.cable().has_value() &&
      (tcvrState.cable()->mediaTypeEncoding().value_or(
           MediaTypeEncodings::UNKNOWN) == MediaTypeEncodings::ACTIVE_CABLES)) {
    return true;
  }
  return false;
}

void TransceiverManager::markTransceiverReadyForProgramming(
    TransceiverID tcvrId,
    bool ready) {
  if (!FLAGS_port_manager_mode) {
    return;
  }
  auto lockedTcvrsReadyForProgramming = tcvrsReadyForProgramming_.wlock();
  if (ready) {
    lockedTcvrsReadyForProgramming->insert(tcvrId);
  } else {
    lockedTcvrsReadyForProgramming->erase(tcvrId);
  }
}

bool TransceiverManager::transceiverJustRemediated(
    const TransceiverID& id) const {
  auto stateMachineItr = stateMachineControllers_.find(id);
  if (stateMachineItr == stateMachineControllers_.end()) {
    throw FbossError("Transceiver:", id, " doesn't exist");
  }
  return stateMachineItr->second->getStateMachine().rlock()->get_attribute(
      isTransceiverJustRemediated);
}

std::unordered_set<TransceiverID>
TransceiverManager::getTcvrsReadyForProgramming() const {
  return *tcvrsReadyForProgramming_.rlock();
}

void TransceiverManager::updateSnapshots() {
  auto lockedSnapshotManagers = snapshotManagers_.wlock();
  for (const auto& tcvrID :
       utility::getTransceiverIds(platformMapping_->getChips())) {
    auto tcvrInfo = getTransceiverInfo(tcvrID);

    auto it = lockedSnapshotManagers->find(tcvrID);
    if (it == lockedSnapshotManagers->end()) {
      auto portNames = getPortNames(tcvrID);
      it = lockedSnapshotManagers
               ->emplace(
                   std::piecewise_construct,
                   std::forward_as_tuple(tcvrID),
                   std::forward_as_tuple(
                       portNames,
                       SnapshotLogSource::QSFP_SERVICE,
                       kSnapshotIntervalSeconds))
               .first;
    }

    phy::LinkSnapshot snapshot;
    snapshot.transceiverInfo() = tcvrInfo;
    it->second.addSnapshot(snapshot);
  }
}
} // namespace facebook::fboss
