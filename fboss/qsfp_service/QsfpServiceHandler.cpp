// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/QsfpServiceHandler.h"
#include "fboss/agent/FbossError.h"
#include "fboss/fsdb/common/Flags.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/qsfp_service/SdkDumpPath.h"

#include <fboss/lib/LogThriftCall.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

DEFINE_string(
    sak_list_warmboot_config,
    "/var/facebook/fboss/mka_service/sak_lists/",
    "path to store the physervice SAK config for service restart");
DEFINE_string(
    sak_list_warmboot_filename,
    "sak_config",
    "filename of warmbootconfig");

DEFINE_int64(
    sak_config_validity_in_secs,
    3600,
    "warmboot config validity in secs");

DEFINE_int32(
    phy_service_macsec_port,
    5910,
    "Port for the phy service thrift service");

namespace facebook {
namespace fboss {

template <typename Type>
static void valid(const Type& val) {
  if (!val) {
    throw FbossError("Invalid input");
  }
}

QsfpServiceHandler::QsfpServiceHandler(
    std::unique_ptr<TransceiverManager> tcvrManager,
    std::unique_ptr<PortManager> portManager,
    std::shared_ptr<mka::MacsecHandler> handler)
    : ::facebook::fb303::FacebookBase2DeprecationMigration("QsfpService"),
      tcvrManager_(std::move(tcvrManager)),
      portManager_(std::move(portManager)),
      macsecHandler_(handler) {
  XLOG(INFO) << "FbossPhyMacsecService inside QsfpServiceHandler Started";

  // Validate that when port manager mode is enabled, port manager is available.
  if (FLAGS_port_manager_mode && !portManager_) {
    throw FbossError(
        "--port-manager-mode is enabled but PortManager is undefined");
  }
}

QsfpServiceHandler::~QsfpServiceHandler() {
  if (fsdbSubscriber_) {
    fsdbSubscriber_->stop();
  }
}

void QsfpServiceHandler::init() {
  XLOG(INFO) << "Initializing QsfpServiceHandler";

  XLOG(INFO) << "Initializing TransceiverManager";
  tcvrManager_->init();
  if (FLAGS_port_manager_mode) {
    XLOG(INFO) << "Initializing PortManager";
    portManager_->init();
  }

  if (FLAGS_subscribe_to_state_from_fsdb) {
    fsdbSubscriber_ = std::make_unique<QsfpFsdbSubscriber>();
    fsdbSubscriber_->subscribeToSwitchStatePortMap(
        getTransceiverManager(), getPortManager());
  }

  XLOG(INFO) << "QsfpServiceHandler initialization complete";
}

facebook::fb303::cpp2::fb_status QsfpServiceHandler::getStatus() {
  return facebook::fb303::cpp2::fb_status::ALIVE;
}

TransceiverType QsfpServiceHandler::getType(int32_t /* unused */) {
  auto log = LOG_THRIFT_CALL(INFO);
  return TransceiverType::QSFP;
}

void QsfpServiceHandler::getTransceiverInfo(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getTransceiversInfo(info, std::move(ids));
}

void QsfpServiceHandler::getPortStateMachineState(
    std::map<int32_t, PortStateMachineState>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    // Only populate if port manager mode is enabled.
    portManager_->getPortStates(info, std::move(ids));
  }
}

void QsfpServiceHandler::getPortStateMachineStateFromPortNames(
    std::map<std::string, PortStateMachineState>& info,
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    // Only populate if port manager mode is enabled.
    portManager_->getPortStates(info, std::move(portNames));
  }
}

void QsfpServiceHandler::getPortMediaInterface(
    std::map<std::string, MediaInterfaceCode>& portMediaInterface) {
  auto log = LOG_THRIFT_CALL(INFO);
  // Controlled by TransceiverManager even in Port Manager mode
  tcvrManager_->getPortMediaInterface(portMediaInterface);
}

void QsfpServiceHandler::getPortsRequiringOpticsFwUpgrade(
    std::map<std::string, FirmwareUpgradeData>& ports) {
  auto log = LOG_THRIFT_CALL(INFO);
  // Controlled by TransceiverManager even in Port Manager mode
  ports = tcvrManager_->getPortsRequiringOpticsFwUpgrade();
}

void QsfpServiceHandler::triggerAllOpticsFwUpgrade(
    std::map<std::string, FirmwareUpgradeData>& ports) {
  auto log = LOG_THRIFT_CALL(INFO);
  ports = tcvrManager_->triggerAllOpticsFwUpgrade();
}

void QsfpServiceHandler::triggerOpticsFwUpgrade(
    std::map<std::string, FirmwareUpgradeData>& ports,
    std::unique_ptr<std::vector<std::string>> interfaces) {
  auto log = LOG_THRIFT_CALL(INFO, interfaces);
  ports = tcvrManager_->triggerOpticsFwUpgrade(*interfaces);
}

void QsfpServiceHandler::getPortTransceiverIDs(
    std::map<std::string, std::vector<int32_t>>& portTransceiverIds) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getPortTransceiverIDs(portTransceiverIds);
}

void QsfpServiceHandler::getTransceiverInfoByPortName(
    std::map<std::string, TransceiverInfo>& info,
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL(INFO);

  // Get port name -> transceiver ID mapping (single ID per port)
  const auto& portNameToModule = tcvrManager_->getPortNameToModuleMap();

  // Collect unique transceiver IDs and build reverse mapping
  std::set<int32_t> neededIds;
  std::unordered_map<int32_t, std::vector<std::string>> tcvrIdToPortNames;
  for (const auto& name : *portNames) {
    auto it = portNameToModule.find(name);
    if (it != portNameToModule.end()) {
      auto tcvrId = it->second;
      neededIds.insert(tcvrId);
      tcvrIdToPortNames[tcvrId].push_back(name);
    }
  }

  if (neededIds.empty()) {
    return;
  }

  // Fetch transceiver info for those IDs
  auto idVec = std::make_unique<std::vector<int32_t>>(
      neededIds.begin(), neededIds.end());
  std::map<int32_t, TransceiverInfo> tcvrInfo;
  tcvrManager_->getTransceiversInfo(tcvrInfo, std::move(idVec));

  // Map back to all port names sharing each transceiver
  for (const auto& [tcvrId, tcvr] : tcvrInfo) {
    auto it = tcvrIdToPortNames.find(tcvrId);
    if (it != tcvrIdToPortNames.end()) {
      for (const auto& name : it->second) {
        info[name] = tcvr;
      }
    }
  }
}

void QsfpServiceHandler::getTransceiverConfigValidationInfo(
    std::map<int32_t, std::string>& info,
    std::unique_ptr<std::vector<int32_t>> ids,
    bool getConfigString) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getAllTransceiversValidationInfo(
      info, std::move(ids), getConfigString);
}

void QsfpServiceHandler::getTransceiverRawDOMData(
    std::map<int32_t, RawDOMData>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getTransceiversRawDOMData(info, std::move(ids));
}

void QsfpServiceHandler::getTransceiverDOMDataUnion(
    std::map<int32_t, DOMDataUnion>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getTransceiversDOMDataUnion(info, std::move(ids));
}

void QsfpServiceHandler::syncPorts(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::map<int32_t, PortStatus>> ports) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->syncPorts(info, std::move(ports));
  } else {
    tcvrManager_->syncPorts(info, std::move(ports));
  }
}

void QsfpServiceHandler::resetTransceiver(
    std::unique_ptr<std::vector<std::string>> portNames,
    ResetType resetType,
    ResetAction resetAction) {
  auto log = LOG_THRIFT_CALL(INFO, portNames);
  tcvrManager_->resetTransceiver(std::move(portNames), resetType, resetAction);
}

void QsfpServiceHandler::pauseRemediation(
    int32_t timeout,
    std::unique_ptr<std::vector<std::string>> portList) {
  auto log = LOG_THRIFT_CALL(INFO, timeout, portList);
  tcvrManager_->setPauseRemediation(timeout, std::move(portList));
}

void QsfpServiceHandler::unpauseRemediation(
    std::unique_ptr<std::vector<std::string>> portList) {
  auto log = LOG_THRIFT_CALL(INFO, portList);
  tcvrManager_->setPauseRemediation(0, std::move(portList));
}

void QsfpServiceHandler::getRemediationUntilTime(
    std::map<std::string, int32_t>& info,
    std::unique_ptr<std::vector<std::string>> portList) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getPauseRemediationUntil(info, std::move(portList));
}

void QsfpServiceHandler::getSymbolErrorHistogram(
    CdbDatapathSymErrHistogram& symErr,
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO);
  tcvrManager_->getSymbolErrorHistogram(symErr, *portNameStr);
}

void QsfpServiceHandler::getAllPortSupportedProfiles(
    std::map<std::string, std::vector<cfg::PortProfileID>>&
        supportedPortProfiles,
    bool checkOptics) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getAllPortSupportedProfiles(
        supportedPortProfiles, checkOptics);
  } else {
    tcvrManager_->getAllPortSupportedProfiles(
        supportedPortProfiles, checkOptics);
  }
}

void QsfpServiceHandler::readTransceiverRegister(
    std::map<int32_t, ReadResponse>& response,
    std::unique_ptr<ReadRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto param = *(request->parameter());
  auto offset = *(param.offset());
  if (offset < 0 || offset > 255) {
    throw FbossError("Offset cannot be < 0 or > 255");
  }
  auto page_ref = param.page();
  if (page_ref.has_value() && *page_ref < 0) {
    throw FbossError("Page cannot be < 0");
  }
  auto length_ref = param.length();
  if (length_ref.has_value()) {
    if (*length_ref < 0 || *length_ref > 255) {
      throw FbossError("Length cannot be < 0 or > 255");
    } else if (*length_ref + offset > 256) {
      throw FbossError("Offset + Length cannot be > 256");
    }
  }
  tcvrManager_->readTransceiverRegister(response, std::move(request));
}

void QsfpServiceHandler::writeTransceiverRegister(
    std::map<int32_t, WriteResponse>& response,
    std::unique_ptr<WriteRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto param = *(request->parameter());
  auto offset = *(param.offset());
  if (offset < 0 || offset > 255) {
    throw FbossError("Offset cannot be < 0 or > 255");
  }
  auto page_ref = param.page();
  if (page_ref.has_value() && *page_ref < 0) {
    throw FbossError("Page cannot be < 0");
  }
  auto length_ref = param.length();
  if (length_ref.has_value()) {
    if (*length_ref < 0 || *length_ref > 255) {
      throw FbossError("Length cannot be < 0 or > 255");
    } else if (*length_ref + offset > 256) {
      throw FbossError("Offset + Length cannot be > 256");
    }
  }
  tcvrManager_->writeTransceiverRegister(response, std::move(request));
}

QsfpServiceRunState QsfpServiceHandler::getQsfpServiceRunState() {
  auto log = LOG_THRIFT_CALL(INFO);
  // Controlled by TransceiverManager even in Port Manager mode
  return tcvrManager_->getRunState();
}

void QsfpServiceHandler::programXphyPort(
    int32_t portId,
    cfg::PortProfileID portProfileId) {
  auto log = LOG_THRIFT_CALL(INFO, portId);
  if (FLAGS_port_manager_mode) {
    portManager_->programXphyPort(PortID(portId), portProfileId);
  } else {
    tcvrManager_->programXphyPort(PortID(portId), portProfileId);
  }
}

void QsfpServiceHandler::getXphyInfo(phy::PhyInfo& response, int32_t portId) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    response = portManager_->getXphyInfo(PortID(portId));
  } else {
    response = tcvrManager_->getXphyInfo(PortID(portId));
  }
}

void QsfpServiceHandler::getSupportedPrbsPolynomials(
    std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getSupportedPrbsPolynomials(
        prbsCapabilities, *portNameStr, component);
  } else {
    tcvrManager_->getSupportedPrbsPolynomials(
        prbsCapabilities, *portNameStr, component);
  }
}

void QsfpServiceHandler::setInterfacePrbs(
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component,
    std::unique_ptr<prbs::InterfacePrbsState> state) {
  auto log = LOG_THRIFT_CALL(INFO, portNameStr);
  if (FLAGS_port_manager_mode) {
    portManager_->setInterfacePrbs(*portNameStr, component, *state);
  } else {
    tcvrManager_->setInterfacePrbs(*portNameStr, component, *state);
  }
}

void QsfpServiceHandler::getInterfacePrbsState(
    prbs::InterfacePrbsState& prbsState,
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getInterfacePrbsState(prbsState, *portNameStr, component);
  } else {
    tcvrManager_->getInterfacePrbsState(prbsState, *portNameStr, component);
  }
}

void QsfpServiceHandler::getAllInterfacePrbsStates(
    std::map<std::string, prbs::InterfacePrbsState>& prbsStates,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getAllInterfacePrbsStates(prbsStates, component);
  } else {
    tcvrManager_->getAllInterfacePrbsStates(prbsStates, component);
  }
}

void QsfpServiceHandler::getInterfacePrbsStats(
    phy::PrbsStats& response,
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    response = portManager_->getInterfacePrbsStats(*portNameStr, component);
  } else {
    response = tcvrManager_->getInterfacePrbsStats(*portNameStr, component);
  }
}

void QsfpServiceHandler::getAllInterfacePrbsStats(
    std::map<std::string, phy::PrbsStats>& prbsStats,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getAllInterfacePrbsStats(prbsStats, component);
  } else {
    tcvrManager_->getAllInterfacePrbsStats(prbsStats, component);
  }
}

void QsfpServiceHandler::clearInterfacePrbsStats(
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO, portNameStr);
  if (FLAGS_port_manager_mode) {
    portManager_->clearInterfacePrbsStats(*portNameStr, component);
  } else {
    tcvrManager_->clearInterfacePrbsStats(*portNameStr, component);
  }
}

void QsfpServiceHandler::bulkClearInterfacePrbsStats(
    std::unique_ptr<std::vector<std::string>> interfaces,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO, interfaces);
  if (FLAGS_port_manager_mode) {
    portManager_->bulkClearInterfacePrbsStats(std::move(interfaces), component);
  } else {
    tcvrManager_->bulkClearInterfacePrbsStats(std::move(interfaces), component);
  }
}

void QsfpServiceHandler::dumpTransceiverI2cLog(
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO, portNameStr);
  auto ret = tcvrManager_->dumpTransceiverI2cLog(*portNameStr);
  // if the header of the log has size 0, logging is not enabled.
  if (ret.first == 0) {
    throw FbossError(
        fmt::format("Failed to dump transceiver {} I2c log", *portNameStr));
  }
}

void QsfpServiceHandler::setPortPrbs(
    int32_t portId,
    phy::PortComponent component,
    std::unique_ptr<phy::PortPrbsState> state) {
  auto log = LOG_THRIFT_CALL(INFO, portId);
  if (FLAGS_port_manager_mode) {
    portManager_->setPortPrbs(PortID(portId), component, *state);
  } else {
    tcvrManager_->setPortPrbs(PortID(portId), component, *state);
  }
}

void QsfpServiceHandler::getPortPrbsStats(
    phy::PrbsStats& response,
    int32_t portId,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    response = portManager_->getPortPrbsStats(PortID(portId), component);
  } else {
    response = tcvrManager_->getPortPrbsStats(PortID(portId), component);
  }
}

void QsfpServiceHandler::clearPortPrbsStats(
    int32_t portId,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(INFO, portId);
  if (FLAGS_port_manager_mode) {
    portManager_->clearPortPrbsStats(PortID(portId), component);
  } else {
    tcvrManager_->clearPortPrbsStats(PortID(portId), component);
  }
}

void QsfpServiceHandler::getMacsecCapablePorts(std::vector<int32_t>& ports) {
  auto log = LOG_THRIFT_CALL(INFO);
  std::vector<PortID> macsecPorts;
  if (FLAGS_port_manager_mode) {
    macsecPorts = portManager_->getMacsecCapablePorts();
  } else {
    macsecPorts = tcvrManager_->getMacsecCapablePorts();
  }

  std::for_each(macsecPorts.begin(), macsecPorts.end(), [&ports](auto portId) {
    ports.push_back(static_cast<int32_t>(portId));
  });
}

void QsfpServiceHandler::validateHandler() const {
  if (!macsecHandler_) {
    throw FbossError("Macsec handler not initialized");
  }
}

void QsfpServiceHandler::listHwObjects(
    std::string& out,
    std::unique_ptr<std::vector<HwObjectType>> hwObjects,
    bool cached) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    out = portManager_->listHwObjects(*hwObjects, cached);
  } else {
    out = tcvrManager_->listHwObjects(*hwObjects, cached);
  }
}

bool QsfpServiceHandler::getSdkState(std::unique_ptr<std::string> fileName) {
  auto log = LOG_THRIFT_CALL(INFO, fileName);
  // Confine the SDK debug dump to a service-owned directory. This rejects
  // empty/absolute/parent-traversing inputs and reduces the request to a
  // basename so a caller cannot overwrite arbitrary root-owned files.
  auto safePath = sanitizeSdkDumpPath(*fileName);
  // Ensure the service-owned dump directory exists before the SDK tries to
  // write into it; sanitizeSdkDumpPath confines safePath to kSdkDumpDir.
  createDir(kSdkDumpDir);
  if (FLAGS_port_manager_mode) {
    return portManager_->getSdkState(safePath);
  } else {
    // TransceiverManager::getSdkState takes its argument by value; move into it
    // to avoid an unnecessary copy of the sanitized path.
    return tcvrManager_->getSdkState(std::move(safePath));
  }
}

void QsfpServiceHandler::getPortInfo(
    std::string& out,
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    out = portManager_->getPortInfo(*portNameStr);
  } else {
    out = tcvrManager_->getPortInfo(*portNameStr);
  }
}

void QsfpServiceHandler::setPortLoopbackState(
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component,
    bool setLoopback,
    phy::LoopbackMode mode) {
  auto componentName = apache::thrift::util::enumNameSafe(component);
  auto modeName = apache::thrift::util::enumNameSafe(mode);
  auto log =
      LOG_THRIFT_CALL(INFO, portNameStr, componentName, setLoopback, modeName);
  if (FLAGS_port_manager_mode) {
    portManager_->setPortLoopbackState(
        *portNameStr, component, setLoopback, mode);
  } else {
    tcvrManager_->setPortLoopbackState(
        *portNameStr, component, setLoopback, mode);
  }
}

void QsfpServiceHandler::setPortAdminState(
    std::unique_ptr<std::string> portNameStr,
    phy::PortComponent component,
    bool setAdminUp) {
  auto log = LOG_THRIFT_CALL(INFO, portNameStr, setAdminUp);
  if (FLAGS_port_manager_mode) {
    portManager_->setPortAdminState(*portNameStr, component, setAdminUp);
  } else {
    tcvrManager_->setPortAdminState(*portNameStr, component, setAdminUp);
  }
}

void QsfpServiceHandler::setInterfaceTxRx(
    std::vector<phy::TxRxEnableResponse>& txRxEnableResponse,
    std::unique_ptr<std::vector<phy::TxRxEnableRequest>> txRxEnableRequests) {
  auto log = LOG_THRIFT_CALL(INFO);
  // Controlled by TransceiverManager even in Port Manager mode
  txRxEnableResponse = tcvrManager_->setInterfaceTxRx(*txRxEnableRequests);
}

void QsfpServiceHandler::saiPhyRegisterAccess(
    std::string& out,
    std::unique_ptr<std::string> portNameStr,
    bool opRead,
    int phyAddr,
    int devId,
    int regOffset,
    int data) {
  auto log = LOG_THRIFT_CALL(
      INFO, portNameStr, opRead, phyAddr, devId, regOffset, data);
  if (FLAGS_port_manager_mode) {
    out = portManager_->saiPhyRegisterAccess(
        *portNameStr, opRead, phyAddr, devId, regOffset, data);
  } else {
    out = tcvrManager_->saiPhyRegisterAccess(
        *portNameStr, opRead, phyAddr, devId, regOffset, data);
  }
}

void QsfpServiceHandler::saiPhySerdesRegisterAccess(
    std::string& out,
    std::unique_ptr<std::string> portNameStr,
    bool opRead,
    int16_t mdioAddr,
    phy::Side side,
    int serdesLane,
    int64_t regOffset,
    int64_t data) {
  auto log = LOG_THRIFT_CALL(
      INFO, portNameStr, opRead, mdioAddr, serdesLane, regOffset, data);
  if (FLAGS_port_manager_mode) {
    out = portManager_->saiPhySerdesRegisterAccess(
        *portNameStr, opRead, mdioAddr, side, serdesLane, regOffset, data);
  } else {
    out = tcvrManager_->saiPhySerdesRegisterAccess(
        *portNameStr, opRead, mdioAddr, side, serdesLane, regOffset, data);
  }
}

void QsfpServiceHandler::ensurePaiDiagShell() {
  std::lock_guard<std::mutex> lock(paiDiagInitLock_);
  if (paiDiagShell_) {
    return;
  }
  PhyManager* phyMgr = FLAGS_port_manager_mode ? portManager_->getPhyManager()
                                               : tcvrManager_->getPhyManager();
  if (!phyMgr) {
    throw FbossError("PaiDiagShell: no PhyManager available");
  }
  uint64_t switchId = getFirstSaiSwitchIdForPaiDiagShell(phyMgr);
  paiDiagShell_ = std::make_unique<StreamingPaiDiagShell>(switchId);
  paiDiagCmdServer_ = std::make_unique<PaiDiagCmdServer>(paiDiagShell_.get());
  XLOG(INFO) << "PAI diag shell initialized for switchId=0x" << std::hex
             << switchId;
}

apache::thrift::ResponseAndServerStream<std::string, std::string>
QsfpServiceHandler::startPaiDiagShell() {
  auto log = LOG_THRIFT_CALL(INFO);
  ensurePaiDiagShell();
  paiDiagShell_->tryConnect();
  auto streamAndPublisher =
      apache::thrift::ServerStream<std::string>::createPublisher(
          [this]() { paiDiagShell_->markResetPublisher(); });
  std::string firstPrompt =
      paiDiagShell_->start(std::move(streamAndPublisher.second));
  return {firstPrompt, std::move(streamAndPublisher.first)};
}

void QsfpServiceHandler::producePaiDiagShellInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  auto log = LOG_THRIFT_CALL(INFO);
  ensurePaiDiagShell();
  paiDiagShell_->consumeInput(std::move(input), std::move(client));
}

void QsfpServiceHandler::paiDiagCmd(
    std::string& result,
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  auto log = LOG_THRIFT_CALL(INFO);
  ensurePaiDiagShell();
  std::lock_guard<std::mutex> lock(paiDiagCmdLock_);
  result = paiDiagCmdServer_->paiDiagCmd(std::move(input), std::move(client));
}

void QsfpServiceHandler::phyConfigCheckHw(
    std::string& out,
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    out = portManager_->phyConfigCheckHw(*portNameStr);
  } else {
    out = tcvrManager_->phyConfigCheckHw(*portNameStr);
  }
}

void QsfpServiceHandler::publishLinkSnapshots(
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL(INFO, portNames);
  for (const auto& portNameStr : *portNames) {
    if (FLAGS_port_manager_mode) {
      portManager_->publishLinkSnapshots(portNameStr);
    } else {
      tcvrManager_->publishLinkSnapshots(portNameStr);
    }
  }
}

void QsfpServiceHandler::getAllInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (FLAGS_port_manager_mode) {
    portManager_->getAllInterfacePhyInfo(phyInfos);
  } else {
    tcvrManager_->getAllInterfacePhyInfo(phyInfos);
  }
}

void QsfpServiceHandler::getInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos,
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL(INFO);
  for (const auto& portNameStr : *portNames) {
    if (FLAGS_port_manager_mode) {
      portManager_->getInterfacePhyInfo(phyInfos, portNameStr);
    } else {
      tcvrManager_->getInterfacePhyInfo(phyInfos, portNameStr);
    }
  }
}

void QsfpServiceHandler::refreshStateMachines() {
  if (FLAGS_port_manager_mode) {
    portManager_->refreshStateMachines();
  } else {
    tcvrManager_->refreshStateMachines();
  }
}

void QsfpServiceHandler::gracefulExit() {
  if (FLAGS_port_manager_mode) {
    portManager_->gracefulExit();
  }
  tcvrManager_->gracefulExit();
}

/*
 * Return a pointer to the port manager.
 */
PhyManager* QsfpServiceHandler::getPhyManager() const {
  if (FLAGS_port_manager_mode) {
    return portManager_->getPhyManager();
  } else {
    return tcvrManager_->getPhyManager();
  }
}

std::optional<std::string> QsfpServiceHandler::getPortNameByPortId(
    const PortID& portId) const {
  if (FLAGS_port_manager_mode) {
    return portManager_->getPortNameByPortId(portId);
  } else {
    return tcvrManager_->getPortNameByPortId(portId);
  }
}

void QsfpServiceHandler::setOverrideAgentPortStatusForTesting(
    bool up,
    bool enabled,
    bool clearOnly) {
  if (FLAGS_port_manager_mode) {
    portManager_->setOverrideAllAgentPortStatusForTesting(
        up, enabled, clearOnly);
  } else {
    tcvrManager_->setOverrideAgentPortStatusForTesting(up, enabled, clearOnly);
  }
}

void QsfpServiceHandler::setOverrideAgentConfigAppliedInfoForTesting(
    ConfigAppliedInfo info) {
  if (FLAGS_port_manager_mode) {
    portManager_->setOverrideAgentConfigAppliedInfoForTesting(info);
  } else {
    tcvrManager_->setOverrideAgentConfigAppliedInfoForTesting(info);
  }
}

std::optional<PortID> QsfpServiceHandler::getPortIdByPortName(
    const std::string& portNameStr) const {
  if (FLAGS_port_manager_mode) {
    return portManager_->getPortIDByPortName(portNameStr);
  } else {
    return tcvrManager_->getPortIDByPortName(portNameStr);
  }
}

void QsfpServiceHandler::programXphyPort(
    PortID portId,
    cfg::PortProfileID portProfileId) {
  if (FLAGS_port_manager_mode) {
    return portManager_->programXphyPort(portId, portProfileId);
  } else {
    return tcvrManager_->programXphyPort(portId, portProfileId);
  }
}

void QsfpServiceHandler::updateAllXphyPortsStats() {
  if (FLAGS_port_manager_mode) {
    return portManager_->updateAllXphyPortsStats();
  } else {
    return tcvrManager_->updateAllXphyPortsStats();
  }
}

void QsfpServiceHandler::programXphyPortPrbs(
    PortID portId,
    phy::Side side,
    const phy::PortPrbsState& prbs) {
  if (FLAGS_port_manager_mode) {
    portManager_->programXphyPortPrbs(portId, side, prbs);
  } else {
    tcvrManager_->programXphyPortPrbs(portId, side, prbs);
  }
}

phy::PortPrbsState QsfpServiceHandler::getXphyPortPrbs(
    const PortID& portId,
    phy::Side side) {
  if (FLAGS_port_manager_mode) {
    return portManager_->getXphyPortPrbs(portId, side);
  } else {
    return tcvrManager_->getXphyPortPrbs(portId, side);
  }
}

#if FOLLY_HAS_COROUTINES

folly::coro::Task<bool> QsfpServiceHandler::co_sakInstallRx(
    std::unique_ptr<mka::MKASak> sak,
    std::unique_ptr<mka::MKASci> sciToAdd) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  valid<decltype(sciToAdd)>(sciToAdd);
  bool ret = macsecHandler_->sakInstallRx(*sak, *sciToAdd);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    sess.sciList_ref()->emplace_back(std::move(*sciToAdd));
    sakSet_.items.emplace(std::move(sess));
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakInstallTx(
    std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  bool ret = macsecHandler_->sakInstallTx(*sak);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakDeleteRx(
    std::unique_ptr<mka::MKASak> sak,
    std::unique_ptr<mka::MKASci> sciToRemove) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  valid<decltype(sciToRemove)>(sciToRemove);
  bool ret = macsecHandler_->sakDeleteRx(*sak, *sciToRemove);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    std::unordered_set<mka::MKAActiveSakSession>::iterator iter =
        sakSet_.items.find(sess);
    if (iter == sakSet_.items.end()) {
      co_return ret;
    }
    // Expected copy. The unordered set values are const and can't be
    // modified, so we copy and replace it.
    std::vector<mka::MKASci> list = *iter->sciList_ref();
    for (auto sciIter = list.begin(); sciIter != list.end(); sciIter++) {
      auto& sci = *sciIter;
      if (*sci.macAddress_ref() == *sciToRemove->macAddress_ref() &&
          *sci.port_ref() == *sciToRemove->port_ref()) {
        list.erase(sciIter);
        break;
      }
    }
    sess.sciList_ref() = std::move(list);
    sakSet_.items.erase(iter);
    sakSet_.items.emplace(sess);
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakDelete(
    std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  bool ret = macsecHandler_->sakDelete(*sak);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    sakSet_.items.erase(sess);
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<std::unique_ptr<mka::MKASakHealthResponse>>
QsfpServiceHandler::co_sakHealthCheck(std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  // TODO:(shankaran) - when healthcheck fails remove the sak from set.
  co_return std::make_unique<mka::MKASakHealthResponse>(
      macsecHandler_->sakHealthCheck(*sak));
}

folly::coro::Task<std::unique_ptr<mka::MacsecPortPhyMap>>
QsfpServiceHandler::co_macsecGetPhyPortInfo(
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecPortPhyMap>(
      macsecHandler_->macsecGetPhyPortInfo(*portNames));
}

folly::coro::Task<PortOperState> QsfpServiceHandler::co_macsecGetPhyLinkInfo(
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return macsecHandler_->macsecGetPhyLinkInfo(*portNameStr);
}

folly::coro::Task<std::unique_ptr<phy::PhyInfo>>
QsfpServiceHandler::co_getPhyInfo(std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<phy::PhyInfo>(
      macsecHandler_->getPhyInfo(*portNameStr));
}

folly::coro::Task<bool> QsfpServiceHandler::co_deleteAllSc(
    std::unique_ptr<std::string> portNameStr) {
  auto log = LOG_THRIFT_CALL(INFO, portNameStr);
  validateHandler();
  co_return macsecHandler_->deleteAllSc(*portNameStr);
}

folly::coro::Task<bool> QsfpServiceHandler::co_setupMacsecState(
    std::unique_ptr<std::vector<std::string>> portList,
    bool macsecDesired,
    bool dropUnencrypted) {
  auto log = LOG_THRIFT_CALL(INFO, portList, macsecDesired, dropUnencrypted);
  validateHandler();
  co_return macsecHandler_->setupMacsecState(
      *portList, macsecDesired, dropUnencrypted);
}

folly::coro::Task<std::unique_ptr<std::map<std::string, MacsecStats>>>
QsfpServiceHandler::co_getAllMacsecPortStats(bool readFromHw) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<std::map<std::string, MacsecStats>>(
      macsecHandler_->getAllMacsecPortStats(readFromHw));
}

folly::coro::Task<std::unique_ptr<std::map<std::string, MacsecStats>>>
QsfpServiceHandler::co_getMacsecPortStats(
    std::unique_ptr<std::vector<std::string>> portNames,
    bool readFromHw) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<std::map<std::string, MacsecStats>>(
      macsecHandler_->getMacsecPortStats(*portNames, readFromHw));
}
#endif

} // namespace fboss
} // namespace facebook
