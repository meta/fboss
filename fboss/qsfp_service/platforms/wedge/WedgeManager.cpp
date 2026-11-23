// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/qsfp_service/QsfpConfig.h"
#include "fboss/qsfp_service/QsfpServiceThreads.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/I2cLogBuffer.h"
#include "fboss/qsfp_service/module/QsfpModule.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/sff/Sff8472Module.h"
#include "fboss/qsfp_service/module/sff/SffModule.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeQsfp.h"
#include "folly/futures/Future.h"

#include <fb303/ThreadCachedServiceData.h>

#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <unistd.h>
#include <chrono>
#include <numeric>

DEFINE_bool(
    optics_data_post_to_rest,
    false,
    "Enable qsfp_service to post optics thermal data to BMC");

// @nolint(facebook-hte-PreventUseOfDeclareGflag) - Flag defined in
// QsfpConfig.cpp
DECLARE_string(qsfp_service_volatile_dir);

namespace facebook {
namespace fboss {

namespace {

static const std::unordered_set<TransceiverID> kEmptryTransceiverIDs = {};

static const std::string kQsfpToBmcSyncDataVersion{"1.0"};

static const int kOpticsThermalSyncInterval = 10;

static constexpr auto kPimsWithError = "qsfp.pims_with_error";

void setPimsWithError(int pimsWithError) {
  facebook::tcData().setCounter(kPimsWithError, pimsWithError);
}

} // namespace

using LockedTransceiversPtr = folly::Synchronized<
    std::map<TransceiverID, std::unique_ptr<Transceiver>>>::WLockedPtr;

WedgeManager::WedgeManager(
    std::unique_ptr<TransceiverPlatformApi> api,
    const std::shared_ptr<const PlatformMapping> platformMapping,
    PlatformType type,
    const std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads)
    : TransceiverManager(std::move(api), platformMapping, qsfpServiceThreads),
      platformType_(type) {
  /* Constructor for WedgeManager class:
   * Get the TransceiverPlatformApi object from the creator of this object,
   * this object will be used for controlling the QSFP devices on board.
   * Going foward the qsfpPlatApi_ will be used to controll the QSFP devices
   * on FPGA managed platforms and the wedgeI2cBus_ will be used to control
   * the QSFP devices on I2C/CPLD managed platforms
   */
  if (FLAGS_publish_state_to_fsdb || FLAGS_publish_stats_to_fsdb) {
    fsdbSyncManager_ = std::make_shared<QsfpFsdbSyncManager>();
  }

  dataCenter_ = getDeviceDatacenter();
  hostnameScheme_ = getDeviceHostnameScheme();
}

WedgeManager::~WedgeManager() {
  if (fsdbSyncManager_) {
    fsdbSyncManager_->stop();
  }
}

void WedgeManager::loadConfig() {
  // Process QSFP config here
  qsfpConfig_ = QsfpConfig::fromDefaultFile();
  const auto& qsfpCfg = qsfpConfig_->thrift;
  std::map<std::string, std::string> partNumberToFwHandle;
  if (const auto& tcvrFw = qsfpCfg.transceiverFirmwareVersions()) {
    partNumberToFwHandle = *tcvrFw->fwHandleMap();
  }
  tcvrConfig_ = std::make_shared<TransceiverConfig>(
      *qsfpCfg.transceiverConfigOverrides(),
      partNumberToFwHandle,
      qsfpCfg.transceiverVendorOverrides().to_optional());

  qsfpConfig_->writePhyConfigToFile();

  if (FLAGS_publish_state_to_fsdb) {
    fsdbSyncManager_->updateConfig(qsfpCfg);
    // We should only start the fsdbSyncManager_ once. isSystemInitialized is
    // the flag for us to know whether this is the first time or not. In tests,
    // we call loadConfig again and adding this check avoids the sync manager to
    // start again (and subsequently throw an exception).
    if (!isSystemInitialized()) {
      fsdbSyncManager_->start();
    }
  }
}

void WedgeManager::hardResetTransceivers(const std::vector<int>& tcvrs) {
  if (tcvrs.empty()) {
    return;
  }
  for (auto idx : tcvrs) {
    try {
      triggerQsfpHardReset(idx);
    } catch (const std::exception& ex) {
      MODULE_LOG(ERR, "", idx)
          << "Failed to triggerQsfpHardReset: " << ex.what();
    }
  }
  // Required delay time between a transceiver getting out of reset and fully
  // functional.
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
  sleep(kSecAfterModuleOutOfReset);
}

void WedgeManager::initTransceiverMap() {
  // If we can't get access to the USB devices, don't bother to
  // create the QSFP objects;  this is likely to be a permanent
  // error.
  try {
    wedgeI2cBus_ = getI2CBus();
  } catch (const I2cError& ex) {
    XLOG(ERR) << "failed to initialize I2C interface: " << ex.what();
    return;
  }

  // Check if a cold boot has been forced
  if (!canWarmBoot()) {
    XLOG(INFO) << "[COLD Boot] Will trigger all " << getNumQsfpModules()
               << " qsfps hard reset";
    // Force hard resets on the transceivers which forces a cold boot of the
    // modules.
    std::vector<int> allTcvrs(getNumQsfpModules());
    std::iota(allTcvrs.begin(), allTcvrs.end(), 0);
    hardResetTransceivers(allTcvrs);
  }

  // Also try to load the config file here so that we have transceiver to port
  // mapping and port name recognization.
  loadConfig();

  // Set overrideTcvrToPortAndProfileForTest_ if
  // FLAGS_override_program_iphy_ports_for_test true.
  setOverrideTcvrToPortAndProfileForTesting();

  initQsfpImplMap();

  // Create Qsfp to BMC sync interface through Rest client
  createQsfpToBmcSyncInterface();

  refreshTransceivers();
}

bool WedgeManager::platformSupportsI2cLogging() const {
  auto platform = getPlatformType();
  switch (platform) {
    case PlatformType::PLATFORM_WEDGE100:
      return false;
    default:
      return true;
  }
  return true;
}

void WedgeManager::initQsfpImplMap() {
  auto i2cLogConfig = qsfpConfig_->thrift.transceiverI2cLogging();
  bool i2c_logging_Enabled =
      i2cLogConfig.has_value() && platformSupportsI2cLogging();
  if (i2c_logging_Enabled) {
    auto logConfig = apache::thrift::can_throw(i2cLogConfig.value());
    XLOG(INFO) << fmt::format(
        "Create i2c log buffer per tcvr: size {} read {} write {} disOnFail {} ",
        logConfig.bufferSlots().value(),
        logConfig.readLog().value(),
        logConfig.writeLog().value(),
        logConfig.disableOnFail().value());
  } else {
    XLOG(INFO) << "Did not create tcvr i2c log buffers";
  }
  // Create WedgeQsfp for each QSFP module present in the system
  for (int idx = 0; idx < getNumQsfpModules(); idx++) {
    std::unique_ptr<I2cLogBuffer> logBuffer;
    if (i2c_logging_Enabled) {
      auto fileName = getI2cLogName(idx);
      auto logConfig = apache::thrift::can_throw(i2cLogConfig.value());
      logBuffer = std::make_unique<I2cLogBuffer>(logConfig, fileName);
    }
    qsfpImpls_.push_back(
        std::make_unique<WedgeQsfp>(
            idx, wedgeI2cBus_.get(), this, std::move(logBuffer)));
  }
}

void WedgeManager::createQsfpToBmcSyncInterface() {
  if (FLAGS_optics_data_post_to_rest) {
    try {
      qsfpRestClient_ = std::make_unique<QsfpRestClient>();
      XLOG(INFO) << "Created QSFP to BMC Sync Interface through Rest Client";
    } catch (const std::exception&) {
      XLOG(ERR)
          << "Failed to created QSFP to BMC Sync Interface through Rest Client";
    }
  }
}

// TODO(ccpowers): we should probably modify this function signature to just
// return the map instead of passing it as an argument. It's easy to forget to
// clear this between calls in tests
void WedgeManager::getTransceiversInfo(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversInfo, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is invalid, just skip and continue to the next.
      continue;
    }
    try {
      auto tcvrID = TransceiverID(i);
      info.insert({i, getTransceiverInfo(tcvrID)});
      // Use the lock-free snapshot so this status read never blocks on an
      // in-progress state update (e.g. a firmware flash holding the state
      // lock), which would otherwise starve the thrift worker pool.
      auto currentState = getCurrentStateSnapshot(tcvrID);
      info[i].tcvrState()->stateMachineState() = currentState;
    } catch (const std::exception& ex) {
      MODULE_LOG(ERR, "", i)
          << "Error calling getTransceiverInfo(): " << ex.what();
    }
  }
}

void WedgeManager::getAllTransceiversValidationInfo(
    std::map<int32_t, std::string>& info,
    std::unique_ptr<std::vector<int32_t>> ids,
    bool getConfigString) {
  XLOG(INFO)
      << "Received request for getAllTransceiversValidationInfo, with ids: "
      << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  if (!FLAGS_enable_tcvr_validation) {
    return;
  }

  const auto& presentTransceivers = getPresentTransceivers();
  for (const auto& i : *ids) {
    auto tcvrID = TransceiverID(i);
    if (!isValidTransceiver(i) || !presentTransceivers.contains(tcvrID)) {
      // If the transceiver idx is invalid or the transceiver is not present,
      // skip to the next one.
      continue;
    }
    try {
      std::string validationInfo;
      if (getConfigString) {
        validationInfo = getTransceiverValidationConfigString(tcvrID);
      } else {
        // validatePortProfile is always enabled by link test / CLI access
        validateTransceiverById(tcvrID, validationInfo, true);
      }

      info.insert({i, validationInfo});
    } catch (const std::exception& ex) {
      MODULE_LOG(ERR, "", i)
          << "Error calling getAllTransceiversValidationInfo(): " << ex.what();
    }
  }
}

void WedgeManager::getTransceiversRawDOMData(
    std::map<int32_t, RawDOMData>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversRawDOMData, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    RawDOMData data;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      try {
        data = it->second->getRawDOMData();
      } catch (const std::exception& ex) {
        MODULE_LOG(ERR, "", i)
            << "Error calling getRawDOMData(): " << ex.what();
      }
      info[i] = data;
    }
  }
}

void WedgeManager::getTransceiversDOMDataUnion(
    std::map<int32_t, DOMDataUnion>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversDOMDataUnion, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    DOMDataUnion data;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      try {
        data = it->second->getDOMDataUnion();
      } catch (const std::exception& ex) {
        MODULE_LOG(ERR, "", i)
            << "Error calling getDOMDataUnion(): " << ex.what();
      }
      info[i] = data;
    }
  }
}

void WedgeManager::readTransceiverRegister(
    std::map<int32_t, ReadResponse>& responses,
    std::unique_ptr<ReadRequest> request) {
  auto ids = *(request->ids());
  XLOG(INFO) << "Received request for reading transceiver registers for ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  auto lockedTransceivers = transceivers_.rlock();

  std::vector<folly::Future<std::pair<int32_t, std::unique_ptr<IOBuf>>>>
      futResponses;
  for (const auto& i : ids) {
    // Initialize responses with valid = false. This will be overwritten with
    // the correct valid flag later
    responses[i].valid() = false;
    if (isValidTransceiver(i)) {
      if (auto it = lockedTransceivers->find(TransceiverID(i));
          it != lockedTransceivers->end()) {
        auto param = *(request->parameter());
        futResponses.push_back(it->second->futureReadTransceiver(param));
      }
    }
  }

  folly::collectAllUnsafe(futResponses)
      .thenValue([&responses](
                     const std::vector<folly::Try<
                         std::pair<int32_t, std::unique_ptr<IOBuf>>>>& tries) {
        for (const auto& tryResponse : tries) {
          if (tryResponse.hasValue()) {
            ReadResponse resp;
            auto tcvrId = tryResponse.value().first;
            resp.data() = *(tryResponse.value().second);
            resp.valid() = resp.data()->length() > 0;
            responses[tcvrId] = resp;
          } // We have already set valid to false above for all responses so
            // don't need to handle the exception here
        }
      })
      .wait();
}

void WedgeManager::writeTransceiverRegister(
    std::map<int32_t, WriteResponse>& responses,
    std::unique_ptr<WriteRequest> request) {
  auto ids = *(request->ids());
  XLOG(INFO) << "Received request for writing transceiver register for ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  auto lockedTransceivers = transceivers_.rlock();

  std::vector<folly::Future<std::pair<int32_t, bool>>> futResponses;

  for (const auto& i : ids) {
    // Initialize responses with success = false. This will be overwritten with
    // the correct success flag later
    responses[i].success() = false;
    if (isValidTransceiver(i)) {
      if (auto it = lockedTransceivers->find(TransceiverID(i));
          it != lockedTransceivers->end()) {
        auto param = *(request->parameter());
        if (request->bytes().has_value()) {
          // Converting signed int vector (Thrift type requirement) to unsigned
          // int vector.
          std::vector<uint8_t> unsignedVector(
              request->bytes()->begin(), request->bytes()->end());
          futResponses.push_back(
              it->second->futureWriteTransceiver(param, unsignedVector));
        } else {
          param.length() = 1;
          futResponses.push_back(it->second->futureWriteTransceiver(
              param, {static_cast<uint8_t>(*(request->data()))}));
        }
      }
    }
  }

  folly::collectAllUnsafe(futResponses)
      .thenValue(
          [&responses](
              const std::vector<folly::Try<std::pair<int32_t, bool>>>& tries) {
            for (const auto& tryResponse : tries) {
              if (tryResponse.hasValue()) {
                WriteResponse resp;
                auto tcvrId = tryResponse.value().first;
                resp.success() = tryResponse.value().second;
                responses[tcvrId] = resp;
              } // We have already set success to false above for all responses
                // so don't need to handle the exception here
            }
          })
      .wait();
}

size_t WedgeManager::getI2cLogBufferCapacity(int32_t portId) {
  return qsfpImpls_[portId]->getI2cLogBufferCapacity();
}

std::pair<size_t, size_t> WedgeManager::dumpTransceiverI2cLog(
    const std::string& portName) {
  auto itr = portNameToModule_.find(portName);
  if (itr == portNameToModule_.end()) {
    throw FbossError("Can't find transceiver module for port name: ", portName);
  }
  return dumpTransceiverI2cLog(itr->second);
}

void WedgeManager::syncPorts(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::map<int32_t, PortStatus>> ports) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("syncPorts");
  }

  std::set<TransceiverID> tcvrIDs;
  for (const auto& [portID, portStatus] : *ports) {
    if (auto tcvrIdx = portStatus.transceiverIdx()) {
      tcvrIDs.insert(TransceiverID(*tcvrIdx->transceiverId()));
    }
  }
  // Update Transceiver active state
  updateTransceiverActiveState(tcvrIDs, *ports);
  // Only fetch the transceivers for the input ports
  for (auto tcvrID : tcvrIDs) {
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(tcvrID);
        it != lockedTransceivers->end()) {
      try {
        info[tcvrID] = it->second->getTransceiverInfo();
      } catch (const std::exception& ex) {
        MODULE_LOG(ERR, "", tcvrID)
            << "Error calling getTransceiverInfo(): " << ex.what();
      }
    }
  }
}

void WedgeManager::updateTransceiverLogInfo(
    const std::vector<TransceiverID>& transceivers) {
  for (auto tcvrID : transceivers) {
    TransceiverInfo tcvrInfo;
    try {
      tcvrInfo = getTransceiverInfo(tcvrID);
    } catch (const QsfpModuleError&) {
      MODULE_LOG(INFO, "", tcvrID) << "Failed to update tcvr log info";
      continue;
    }
    const auto& state = tcvrInfo.tcvrState();
    std::optional<Vendor> vendor = std::nullopt;
    std::optional<FirmwareStatus> fwStatus = std::nullopt;
    auto mgmtIf = TransceiverManagementInterface::NONE;
    if (state->transceiverManagementInterface().has_value()) {
      mgmtIf = state->transceiverManagementInterface().value();
    }
    if (state->vendor().has_value()) {
      vendor = state->vendor().value();
    }
    const auto& moduleStatus = state->status();
    if (moduleStatus.has_value() && moduleStatus->fwStatus().has_value()) {
      fwStatus = moduleStatus->fwStatus().value();
    }
    if (tcvrID < qsfpImpls_.size()) {
      auto portNames = getPortNames(tcvrID);
      qsfpImpls_[tcvrID]->setTcvrInfoInLog(mgmtIf, portNames, fwStatus, vendor);
    } else {
      MODULE_LOG(ERR, "", tcvrID) << "is not in the range of qsfpImpls_";
    }
  }
}

// NOTE: this may refresh transceivers multiple times if they're newly plugged
//  in, as refresh() is called both via updateTransceiverMap and futureRefresh
std::vector<TransceiverID> WedgeManager::refreshTransceivers() {
  clearEvbsRunningFirmwareUpgrade();

  try {
    wedgeI2cBus_->verifyBus(false);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Error calling verifyBus(): " << ex.what();
    return {};
  }

  // Clear Transceiver reset for all transceivers not held
  // in reset.
  clearAllTransceiverReset();

  // Since transceivers may appear or disappear, we need to update our
  // transceiver mapping and type here.
  // Make this return a list of transceivers inserted. We can update
  // The logs of those transceivers with TransceiverInfo
  const auto discoveredTransceivers = updateTransceiverMap();

  // Finally refresh all transceivers without specifying any ids
  auto refreshedTransceivers =
      TransceiverManager::refreshTransceivers(kEmptryTransceiverIDs);

  // After refresh, add the info for discoveredTransceivers to the log
  updateTransceiverLogInfo(discoveredTransceivers);

  // Send the optical thermal data to BMC if needed
  auto currTime = std::time(nullptr);
  if (FLAGS_optics_data_post_to_rest && qsfpRestClient_.get() &&
      (nextOpticsToBmcSyncTime_ <= currTime)) {
    // Post the optics thermal data to BMC
    auto qsfpToBmcSyncData = getQsfpToBmcSyncDataSerialized();
    // Post data to BMC using eth0.4088 Rest endpoint
    try {
      auto ret = qsfpRestClient_->postQsfpThermalData(qsfpToBmcSyncData);
      if (!ret) {
        XLOG(ERR)
            << "Failed to send QsfpThermal data to Rest endpoint, will try in "
            << kOpticsThermalSyncInterval << " seconds again";
      }
    } catch (const FbossError& e) {
      XLOG(ERR) << "Failed to send QsfpThermal data to Rest endpoint, will try "
                << "in " << kOpticsThermalSyncInterval << " seconds again"
                << ", error=" << e.what();
    }
    nextOpticsToBmcSyncTime_ = currTime + kOpticsThermalSyncInterval;
  }

  findAndTriggerPotentialFirmwareUpgradeEvents(refreshedTransceivers);

  return refreshedTransceivers;
}

void WedgeManager::updateTcvrStateInFsdb(
    TransceiverID tcvrID,
    facebook::fboss::TcvrState&& newState) {
  QsfpFsdbSyncManager::TcvrStateMap states;
  states[tcvrID] = std::move(newState);
  fsdbSyncManager_->updateTcvrStates(std::move(states));
}

void WedgeManager::updatePimStateInFsdb(
    int pimID,
    facebook::fboss::PimState&& newState) {
  fsdbSyncManager_->updatePimState(pimID, std::move(newState));
}

void WedgeManager::publishPimStatesToFsdb() {
  if (!FLAGS_publish_state_to_fsdb) {
    return;
  }

  int pimsWithError = 0;
  pimStates_ = getPimStates();
  QsfpFsdbSyncManager::PimStatesMap pimStates = folly::copy(pimStates_);
  for (auto& [id, pimState] : pimStates) {
    if (!pimState.errors()->empty()) {
      ++pimsWithError;
      for (auto error : *pimState.errors()) {
        XLOG(ERR) << "PIM " << id << " has error: "
                  << apache::thrift::util::enumNameSafe(error);
      }
    }
    updatePimStateInFsdb(id, std::move(pimState));
  }

  setPimsWithError(pimsWithError);
}

void WedgeManager::publishTransceiversToFsdb() {
  if (!FLAGS_publish_stats_to_fsdb) {
    return;
  }

  // Get the transceiver info for all transceivers
  TcvrInfoMap tcvrInfos;
  getTransceiversInfo(tcvrInfos, std::make_unique<std::vector<int32_t>>());
  QsfpFsdbSyncManager::TcvrStatsMap stats;
  QsfpFsdbSyncManager::TcvrStateMap states;
  for (auto& [id, info] : tcvrInfos) {
    stats[id] = *info.tcvrStats();
    states[id] = *info.tcvrState();
  }
  fsdbSyncManager_->updateTcvrStates(std::move(states));
  fsdbSyncManager_->updateTcvrStats(std::move(stats));
}

int WedgeManager::scanTransceiverPresence(
    std::unique_ptr<std::vector<int32_t>> ids) {
  // If the id list is empty, we default to scan the presence of all the
  // transcievers.
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  std::map<int32_t, ModulePresence> presenceUpdate;
  for (auto id : *ids) {
    presenceUpdate[id] = ModulePresence::UNKNOWN;
  }

  wedgeI2cBus_->scanPresence(presenceUpdate);

  int numTransceiversUp = 0;
  for (const auto& presence : presenceUpdate) {
    if (presence.second == ModulePresence::PRESENT) {
      numTransceiversUp++;
    }
  }
  return numTransceiversUp;
}

std::unique_ptr<TransceiverI2CApi> WedgeManager::getI2CBus() {
  return std::make_unique<WedgeI2CBusLock>(std::make_unique<WedgeI2CBus>());
}

std::vector<TransceiverID> WedgeManager::updateTransceiverMap() {
  std::vector<folly::Future<TransceiverManagementInterface>> futInterfaces;
  const auto numTransceivers = getNumQsfpModules();
  CHECK_EQ(qsfpImpls_.size(), numTransceivers);
  for (int idx = 0; idx < numTransceivers; idx++) {
    futInterfaces.push_back(
        qsfpImpls_[idx]->futureGetTransceiverManagementInterface());
  }
  folly::collectAllUnsafe(futInterfaces.begin(), futInterfaces.end()).wait();

  std::vector<TransceiverID> retVal;
  std::unordered_set<int> tcvrsToCreate;
  std::unordered_set<int> tcvrsToDelete;
  std::unordered_set<int> tcvrsToHardReset;

  {
    // Order of locking is important here.
    auto transceiversInReset = tcvrsHeldInReset_.rlock();
    auto lockedTransceiversRPtr = transceivers_.rlock();
    for (int idx = 0; idx < numTransceivers; idx++) {
      if (!futInterfaces[idx].isReady()) {
        MODULE_LOG(ERR, "", idx)
            << "Failed getting TransceiverManagementInterface";
        continue;
      }
      auto it = lockedTransceiversRPtr->find(TransceiverID(idx));
      if (it != lockedTransceiversRPtr->end()) {
        // In the case where we already have a transceiver recorded, try to
        // check whether they match the transceiver type.
        if (it->second->managementInterface() == futInterfaces[idx].value()) {
          // The management interface matches. Nothing needs to be done.
          continue;
        } else {
          // The management changes. Need to Delete the old module to make place
          // for the new one.
          it->second->removeTransceiver();
          tcvrsToDelete.insert(idx);
        }
      }

      // Either we don't have a transceiver here before or we had a new one
      // since the management interface changed, we want to create a new module
      // here.
      // If the transceiver is held in reset, we dont create a new one in its
      // place until it is released from reset.
      // Also only create transceivers that are defined in platform mapping
      // (have non empty port list)
      if (transceiversInReset->count(idx) != 0) {
        MODULE_LOG(INFO, "", idx)
            << "is held in reset. Not creating transceiver";
      } else if (getPortNames(static_cast<TransceiverID>(idx)).empty()) {
        MODULE_LOG(INFO, "", idx)
            << "is not in platform mapping. Not creating transceiver";
      } else {
        tcvrsToCreate.insert(idx);
      }
    }
  } // end of scope for transceivers_.rlock

  {
    auto lockedTransceiversWPtr = transceivers_.wlock();
    // Delete the transceivers first before potentially creating them later
    for (auto idx : tcvrsToDelete) {
      lockedTransceiversWPtr->erase(TransceiverID(idx));
      resetPortState(TransceiverID(idx));
    }

    auto tcvrConfig = getTransceiverConfig();

    bool cmisSupportRemediate = true;
    for (auto idx : tcvrsToCreate) {
      TransceiverID tcvrID(idx);
      auto mgmtIf = futInterfaces[idx].value();
      auto tcvrName = getTransceiverName(tcvrID);
      if (mgmtIf == TransceiverManagementInterface::CMIS) {
        MODULE_LOG(INFO, "", idx) << "Making CMIS QSFP";
        lockedTransceiversWPtr->emplace(
            tcvrID,
            std::make_unique<CmisModule>(
                getPortNames(tcvrID),
                qsfpImpls_[idx].get(),
                tcvrConfig,
                cmisSupportRemediate,
                tcvrName));
        retVal.emplace_back(idx);
      } else if (mgmtIf == TransceiverManagementInterface::SFF) {
        MODULE_LOG(INFO, "", idx) << "Making Sff QSFP";
        lockedTransceiversWPtr->emplace(
            tcvrID,
            std::make_unique<SffModule>(
                getPortNames(tcvrID),
                qsfpImpls_[idx].get(),
                tcvrConfig,
                tcvrName));
        retVal.emplace_back(idx);
      } else if (mgmtIf == TransceiverManagementInterface::SFF8472) {
        MODULE_LOG(INFO, "", idx) << "Making Sff8472 module";
        lockedTransceiversWPtr->emplace(
            tcvrID,
            std::make_unique<Sff8472Module>(
                getPortNames(tcvrID), qsfpImpls_[idx].get(), tcvrName));
        retVal.emplace_back(idx);
      } else {
        try {
          if (!qsfpImpls_[idx]->detectTransceiver()) {
            MODULE_LOG(INFO, "", idx) << "is not present";
            continue;
          } else {
            // If we fail to read the management interface, but the module is
            // detected, mark it as errored
            auto erroredTransceivers = erroredTransceivers_.wlock();
            erroredTransceivers->insert(TransceiverID(idx));
            MODULE_LOG(ERR, "", idx)
                << "is detected but has an unknown Transceiver interface: "
                << static_cast<int>(futInterfaces[idx].value());
          }
        } catch (const std::exception& ex) {
          MODULE_LOG(ERR, "", idx)
              << "Failed to detect transceiver: " << ex.what();
          continue;
        }

        // There are times when a module cannot be read however it's present.
        // Try to reset here since that may be able to bring it back.
        // Check if we have expected ports info synced over and if all of
        // the ports are down. If any of them is not down then we will not
        // perform the reset.
        bool safeToReset = areAllPortsDown(tcvrID).first;
        if (std::time(nullptr) <= pauseRemediationUntil_) {
          MODULE_LOG(WARN, "", idx)
              << "Remediation is paused, won't hard reset a present "
              << "transceiver with unknown interface";
        } else if (safeToReset) {
          tcvrsToHardReset.insert(idx);
        } else {
          MODULE_LOG(ERR, "", idx)
              << "Unknown interface of transceiver with ports up";
        }
      }
    }
  } // end of scope for transceivers_.wlock

  for (auto idx : tcvrsToHardReset) {
    MODULE_LOG(INFO, "", idx)
        << "Try hard reset a present transceiver with unknown interface";
  }
  hardResetTransceivers({tcvrsToHardReset.begin(), tcvrsToHardReset.end()});
  return retVal;
}

/* Get the i2c transaction counters from TranscieverManager base class
 * and update to fbagent. The TransceieverManager base class is inherited
 * by platform speficic Transaceiver Manager class like WedgeManager.
 * That class has the function to get the I2c transaction status.
 */
void WedgeManager::publishI2cTransactionStats() {
  // Get the i2c transaction stats from TransactionManager class (its
  // sub-class having platform specific implementation)
  auto counters = getI2cControllerStats();

  if (counters.size() == 0) {
    return;
  }

  // Populate the i2c stats per pim and per controller

  for (const I2cControllerStats& counter : counters) {
    // Publish all the counters to FbAgent

    auto statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readTotal");
    tcData().setCounter(statName, *counter.readTotal_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readFailed");
    tcData().setCounter(statName, *counter.readFailed_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readBytes");
    tcData().setCounter(statName, *counter.readBytes_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeTotal");
    tcData().setCounter(statName, *counter.writeTotal_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeFailed");
    tcData().setCounter(statName, *counter.writeFailed_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeBytes");
    tcData().setCounter(statName, *counter.writeBytes_());
  }
}

/*
 * This is introduced mainly due to the mismatch of ODS reporting frequency
 * and the interval of us reading transceiver data. Some of the clear on read
 * information may be lost in this process and not being captured in the ODS
 * time series. This would bring difficulty in root cause link issues. Thus
 * here we provide a way of read and clear the data for the purpose of ODS
 * data reporting.
 */
void WedgeManager::getAndClearTransceiversSignalFlags(
    std::map<int32_t, SignalFlags>& signalFlagsMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversSignalFlags, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    SignalFlags signalFlags;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      signalFlags = it->second->readAndClearCachedSignalFlags();
      signalFlagsMap[i] = signalFlags;
    }
  }
}

void WedgeManager::getAndClearTransceiversMediaSignals(
    std::map<int32_t, std::map<int, MediaLaneSignals>>& mediaSignalsMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversMediaSignals, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    std::map<int, MediaLaneSignals> mediaSignals;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      mediaSignals = it->second->readAndClearCachedMediaLaneSignals();
      mediaSignalsMap[i] = mediaSignals;
    }
  }
}

/*
 * triggerVdmStatsCapture
 *
 * This function triggers the next VDM data capture for the list of
 * transceiver Id's to be displayed in ODS
 */
void WedgeManager::triggerVdmStatsCapture(std::vector<int32_t>& ids) {
  XLOG(DBG2) << "triggerVdmStatsCapture, with ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  if (ids.empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      // Calling the trigger VDM stats capure function for transceiver
      try {
        it->second->triggerVdmStatsCapture();
      } catch (std::exception& e) {
        MODULE_LOG(ERR, "", TransceiverID(i))
            << "VDM could not be reset: " << e.what();
        continue;
      }
    }
  }
}

void WedgeManager::getAndClearTransceiversModuleStatus(
    std::map<int32_t, ModuleStatus>& moduleStatusMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversModuleStatus, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      moduleStatusMap[i] = it->second->readAndClearCachedModuleStatus();
    }
  }
}

phy::PhyInfo WedgeManager::getXphyInfo(PortID portID) {
  if (phyManager_ == nullptr) {
    throw FbossError("Unable to get xphy info when PhyManager is not set");
  }

  if (auto phyInfoOpt = phyManager_->getXphyInfo(portID)) {
    return *phyInfoOpt;
  } else {
    throw FbossError("Unable to get xphy info for port: ", portID);
  }
}

// This function is used to handle thrift requests from clients to test
// programming a xphy port on demand and used in the hw tests
void WedgeManager::programXphyPort(
    PortID portId,
    cfg::PortProfileID portProfileId) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("programXphyPort");
  }

  if (phyManager_ == nullptr) {
    throw FbossError("Unable to program xphy port when PhyManager is not set");
  }

  std::optional<TransceiverInfo> itTcvr;
  // Get the transceiver id for the given port id
  if (auto tcvrID = getTransceiverID(portId)) {
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      itTcvr = it->second->getTransceiverInfo();
    } else {
      MODULE_LOG(WARNING, "", *tcvrID)
          << "Port:" << portId << " doesn't have transceiver info";
    }
  }

  phyManager_->programOnePort(
      portId, portProfileId, itTcvr, false /* needResetDataPath */);
}

bool WedgeManager::initExternalPhyMap(
    PhyManager* phyManager,
    bool forceWarmboot) {
  if (!phyManager) {
    // If there's no PhyManager for such platform, skip init xphy map
    return true;
  }

  // For platforms that require PHY config, fail hard if the phy config file
  // doesn't exist.
  if (requiresPhyConfig()) {
    auto phyConfigPath = folly::to<std::string>(
        FLAGS_qsfp_service_volatile_dir, "/", kPhyHwConfigFileName);
    if (!checkFileExists(phyConfigPath)) {
      throw FbossError(
          "Platform requires PHY config but phy config file does not exist: ",
          phyConfigPath,
          ". Ensure 'phyConfig' is present in qsfp_service_config.");
    }
  }

  // forceWarmboot is only used to skip checking the warmboot file
  // when we're running outside of qsfp_service (e.g. in the fboss-xphy CLI)
  bool warmboot = forceWarmboot || canWarmBoot();

  // First call PhyManager::initExternalPhyMap() to create xphy map
  auto rb = phyManager->initExternalPhyMap(warmboot);
  // TODO(ccpowers): We could probably clean this up a bit to separate out the
  // warmboot file logic and the phy init logic, to make it easier to
  // init phys from external CLIs

  // And then initialize the xphy for each pim which has xphy
  std::vector<folly::Future<folly::Unit>> initPimTasks;
  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();

  // Check if using XPHY_LEVEL threading model
  bool useXphyLevelThreading =
      (phyManager->getXphyThreadingModel() ==
       PhyManager::XphyThreadingModel::XPHY_LEVEL);

  for (int pimIndex = 0; pimIndex < phyManager->getNumOfSlot(); ++pimIndex) {
    auto pimID = PimID(pimIndex + phyManager->getPimStartNum());
    if (!phyManager->shouldInitializePimXphy(pimID)) {
      XLOG(WARN) << "Skip intializing pim xphy for pim="
                 << static_cast<int>(pimID);
      continue;
    }

    if (useXphyLevelThreading) {
      // Per-XPHY initialization - spawn a task for each xphy
      XLOG(DBG1) << "[" << (warmboot ? "WARM" : "COLD")
                 << " Boot] Initializing PIM " << static_cast<int>(pimID)
                 << " with per-xphy threading";

      // Get all xphys for this PIM and initialize each on its own thread
      auto xphyIDs = phyManager->getXphyIDsForPim(pimID);
      for (auto xphyID : xphyIDs) {
        auto* xphyEventBase = phyManager->getXphyEventBase(xphyID);
        initPimTasks.push_back(
            folly::via(xphyEventBase)
                .thenValue([phyManager, xphyID, warmboot](auto&&) {
                  phyManager->initializeXphy(xphyID, warmboot);
                })
                .thenError(
                    folly::tag_t<std::exception>{},
                    [xphyID](const std::exception& e) {
                      XLOG(WARNING) << "Exception in initializeXphy() for xphy="
                                    << static_cast<int>(xphyID) << ", "
                                    << folly::exceptionStr(e);
                    }));
      }
    } else {
      // PIM-level initialization (legacy behavior)
      XLOG(DBG1) << "[" << (warmboot ? "WARM" : "COLD")
                 << " Boot] Initializing PIM " << static_cast<int>(pimID);
      auto* pimEventBase = phyManager->getPimEventBase(pimID);
      initPimTasks.push_back(
          folly::via(pimEventBase)
              .thenValue([phyManager, pimID, warmboot](auto&&) {
                phyManager->initializeSlotPhys(pimID, warmboot);
              })
              .thenError(
                  folly::tag_t<std::exception>{},
                  [pimID](const std::exception& e) {
                    XLOG(WARNING)
                        << "Exception in initializeSlotPhys() for pim="
                        << static_cast<int>(pimID) << ", "
                        << folly::exceptionStr(e);
                  }));
    }
  }

  if (!initPimTasks.empty()) {
    folly::collectAll(initPimTasks).wait();
    XLOG(DBG2) << "Initialized all pims xphy took "
               << std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - begin)
                      .count()
               << " seconds";

    // Restore warm boot Phy state after initializing all the phys.
    restoreWarmBootPhyState();
  }
  return rb;
}

void WedgeManager::programXphyPortPrbs(
    PortID portID,
    phy::Side side,
    const phy::PortPrbsState& prbs) {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("programXphyPortPrbs");
  }
  phyManager_->setPortPrbs(portID, side, prbs);
}

phy::PortPrbsState WedgeManager::getXphyPortPrbs(
    PortID portID,
    phy::Side side) {
  return phyManager_->getPortPrbs(portID, side);
}

void WedgeManager::updateAllXphyPortsStats() {
  if (FLAGS_port_manager_mode) {
    PORT_MGR_UNEXPECTED_CALL("updateAllXphyPortsStats");
  }

  if (!phyManager_) {
    // If there's no PhyManager for such platform, skip updating xphy stats
    return;
  }
  // Then we need to update all the programmed port xphy stats
  phyManager_->updateAllXphyPortsStats();
}

std::vector<PortID> WedgeManager::getMacsecCapablePorts() const {
  if (!phyManager_) {
    return {};
  }
  return phyManager_->getPortsSupportingFeature(
      phy::ExternalPhy::Feature::MACSEC);
}

void WedgeManager::setOverrideTcvrToPortAndProfileForTesting(
    std::optional<OverrideTcvrToPortAndProfile> overrideTcvrToPortAndProfile) {
  if (overrideTcvrToPortAndProfile) {
    overrideTcvrToPortAndProfileForTest_ = *overrideTcvrToPortAndProfile;
  } else if (FLAGS_override_program_iphy_ports_for_test) {
    const auto& chips = platformMapping_->getChips();
    for (auto chip : chips) {
      if (*chip.second.type() != phy::DataPlanePhyChipType::TRANSCEIVER) {
        continue;
      }
      auto tcvrID = TransceiverID(*chip.second.physicalID());
      overrideTcvrToPortAndProfileForTest_[tcvrID] = {};
    }
    // Use QSFP config to get the iphy port and profile
    auto qsfpTestConfig = qsfpConfig_->thrift.qsfpTestConfig();
    CHECK(qsfpTestConfig.has_value());
    for (const auto& portPairs : *qsfpTestConfig->cabledPortPairs()) {
      auto aPortID = getPortIDByPortName(*portPairs.aPortName());
      auto zPortID = getPortIDByPortName(*portPairs.zPortName());
      CHECK(aPortID.has_value())
          << "Couldn't find port ID for " << *portPairs.aPortName();
      CHECK(zPortID.has_value())
          << "Couldn't find port ID for " << *portPairs.zPortName();
      // If the SW port has transceiver id, add it to
      // overrideTcvrToPortAndProfile
      if (auto tcvrID = getTransceiverID(PortID(*aPortID))) {
        overrideTcvrToPortAndProfileForTest_[*tcvrID].emplace(
            *aPortID, *portPairs.profileID());
      }
      if (auto tcvrID = getTransceiverID(PortID(*zPortID))) {
        overrideTcvrToPortAndProfileForTest_[*tcvrID].emplace(
            *zPortID, *portPairs.profileID());
      }
    }
  }
}

std::string WedgeManager::listHwObjects(
    std::vector<HwObjectType>& hwObjects,
    bool cached) const {
  if (!phyManager_) {
    return "";
  }
  return phyManager_->listHwObjects(hwObjects, cached);
}

bool WedgeManager::getSdkState(std::string filename) const {
  if (!phyManager_) {
    throw FbossError("getSdkState: no external PHY on this platform");
  }
  return phyManager_->getSdkState(filename);
}

void WedgeManager::publishPhyStateToFsdb(
    std::string&& portName,
    std::optional<phy::PhyState>&& newState) const {
  if (FLAGS_publish_state_to_fsdb) {
    fsdbSyncManager_->updatePhyState(std::move(portName), std::move(newState));
  }
}

void WedgeManager::publishPhyStatToFsdb(
    std::string&& portName,
    phy::PhyStats&& stat) const {
  if (FLAGS_publish_stats_to_fsdb) {
    fsdbSyncManager_->updatePhyStat(std::move(portName), std::move(stat));
  }
}

void WedgeManager::publishPortStatToFsdb(
    std::string&& portName,
    HwPortStats&& stat) const {
  if (FLAGS_publish_stats_to_fsdb) {
    fsdbSyncManager_->updatePortStat(std::move(portName), std::move(stat));
  }
}

void WedgeManager::publishPortStateToFsdb(
    std::string&& portName,
    portstate::PortState&& state) const {
  if (FLAGS_publish_state_to_fsdb) {
    fsdbSyncManager_->updatePortState(std::move(portName), std::move(state));
  }
}

/*
 * getQsfpToBmcSyncData
 *
 * Returns the transceiver thermal data for all the present transceivers in the
 * system at any given time. This data can be synced to BMC for fan control
 */
QsfpToBmcSyncData WedgeManager::getQsfpToBmcSyncData() const {
  QsfpToBmcSyncData qsfpToBmcData;

  // Gather system data
  qsfpToBmcData.syncDataStructVersion() = kQsfpToBmcSyncDataVersion;
  qsfpToBmcData.timestamp() = std::time(nullptr);
  qsfpToBmcData.switchDeploymentInfo().value().dataCenter() =
      getSwitchDataCenter();
  qsfpToBmcData.switchDeploymentInfo().value().hostnameScheme() =
      getSwitchRole();

  // Gather Transceiver Data. Use a max heap and then push to the
  // returned map the top transceivers in terms of temperature (up to
  // kMaxTcvrTemperaturesToReport transceivers).
  std::vector<int32_t> ids;
  folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(ids);

  std::vector<std::pair<std::string, TransceiverThermalData>> temperatures;
  for (auto id : ids) {
    auto tcvrID = TransceiverID(id);
    auto portName = getPortName(tcvrID);
    TransceiverInfo tcvrInfo;

    try {
      tcvrInfo = getTransceiverInfo(tcvrID);
    } catch (const QsfpModuleError&) {
      MODULE_LOG(ERR, "", tcvrID) << "Module thermal data not available";
      continue;
    }

    // Skip non-optical modules and non AEC modules.
    if (!opticalOrActiveCable(*tcvrInfo.tcvrState())) {
      continue;
    }

    if (tcvrInfo.tcvrStats().value().sensor().has_value() &&
        tcvrInfo.tcvrState().value().moduleMediaInterface().has_value()) {
      double temp = tcvrInfo.tcvrStats()
                        .value()
                        .sensor()
                        .value()
                        .temp()
                        .value()
                        .value()
                        .value();
      MediaInterfaceCode moduleType =
          tcvrInfo.tcvrState().value().moduleMediaInterface().value();
      TransceiverThermalData tcvrThermalData;
      tcvrThermalData.temperature() = temp;
      tcvrThermalData.moduleMediaInterface() =
          apache::thrift::util::enumNameSafe(moduleType);

      temperatures.emplace_back(portName, tcvrThermalData);
    }
  }
  auto compareTemp =
      [](const std::pair<std::string, TransceiverThermalData>& a,
         const std::pair<std::string, TransceiverThermalData>& b) {
        return a.second.temperature().value() > b.second.temperature().value();
      };

  std::sort(temperatures.begin(), temperatures.end(), compareTemp);

  size_t elementsToReport = temperatures.size();
  if (elementsToReport > kMaxTcvrTemperaturesToReport) {
    XLOG(WARNING) << "Truncating transceiver thermal data to "
                  << kMaxTcvrTemperaturesToReport << " from "
                  << elementsToReport << " entries";
    elementsToReport = kMaxTcvrTemperaturesToReport;
  }
  std::map<std::string, TransceiverThermalData> tcvrData;
  for (int i = 0; i < elementsToReport; i++) {
    tcvrData[temperatures[i].first] = temperatures[i].second;
  }

  qsfpToBmcData.transceiverThermalData() = tcvrData;
  return qsfpToBmcData;
}

std::string WedgeManager::getQsfpToBmcSyncDataSerialized() const {
  auto qsfpToBmcData = getQsfpToBmcSyncData();
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(
      qsfpToBmcData);
}

} // namespace fboss
} // namespace facebook
