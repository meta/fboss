/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <folly/String.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/DsfNodeUtils.h"
#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"
#include "fboss/agent/hw/switch_asics/Jericho4Asic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatformInitImpl.h"
#include "fboss/agent/state/Port.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/hw/sai/switch/SaiHandler.h"

DEFINE_string(
    dll_path,
    "/etc/packages/neteng-fboss-wedge_agent/current",
    "directory of HSDK warmboot DLL files");

DEFINE_string(
    firmware_path,
    "/etc/packages/neteng-fboss-wedge_agent/current",
    "Path to load the firmware");

DEFINE_bool(
    enable_delay_drop_congestion_threshold,
    false,
    "Enable new delay drop congestion threshold in CGM");

namespace {

std::unordered_map<std::string, std::string> kSaiProfileValues;

const char* saiProfileGetValue(
    sai_switch_profile_id_t /*profile_id*/,
    const char* variable) {
  auto saiProfileValItr = kSaiProfileValues.find(variable);
  return saiProfileValItr != kSaiProfileValues.end()
      ? saiProfileValItr->second.c_str()
      : nullptr;
}

int saiProfileGetNextValue(
    sai_switch_profile_id_t /* profile_id */,
    const char** variable,
    const char** value) {
  static auto saiProfileValItr = kSaiProfileValues.begin();
  if (!value) {
    saiProfileValItr = kSaiProfileValues.begin();
    return 0;
  }
  if (saiProfileValItr == kSaiProfileValues.end()) {
    return -1;
  }
  *variable = saiProfileValItr->first.c_str();
  *value = saiProfileValItr->second.c_str();
  ++saiProfileValItr;
  return 0;
}

sai_service_method_table_t kSaiServiceMethodTable = {
    .profile_get_value = saiProfileGetValue,
    .profile_get_next_value = saiProfileGetNextValue,
};

using namespace facebook::fboss;

SaiSwitchTraits::Attributes::HwInfo getHwInfo(SaiPlatform* platform) {
  std::vector<int8_t> connectionHandle;
  // Use connection handle from switchInfo if available
  const auto agentConfig = platform->getConfig();
  const auto switchSettings = agentConfig->thrift.sw()->switchSettings();
  for (const auto& switchIdAndInfo : *switchSettings->switchIdToSwitchInfo()) {
    if (switchIdAndInfo.first == platform->getAsic()->getSwitchId()) {
      if (switchIdAndInfo.second.connectionHandle()) {
        auto connStr = *switchIdAndInfo.second.connectionHandle();
        std::copy(
            connStr.c_str(),
            connStr.c_str() + connStr.size() + 1,
            std::back_inserter(connectionHandle));
        return connectionHandle;
      }
    }
  }
  auto connStr = platform->getPlatformAttribute(
      cfg::PlatformAttributes::CONNECTION_HANDLE);
  if (connStr.has_value()) {
    std::copy(
        connStr->c_str(),
        connStr->c_str() + connStr->size() + 1,
        std::back_inserter(connectionHandle));
  }
  return connectionHandle;
}

const auto& getFirmwareForSwitch(
    const auto& switchIdToSwitchInfo,
    int64_t switchId) {
  auto iter = switchIdToSwitchInfo.value().find(switchId);
  if (iter == switchIdToSwitchInfo.value().end()) {
    throw FbossError("SwitchId not found: ", switchId);
  }

  return *iter->second.firmwareNameToFirmwareInfo();
}

} // namespace

namespace facebook::fboss {

SaiPlatform::SaiPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : Platform(std::move(productInfo), std::move(platformMapping), localMac) {
  const auto& portsByMasterPort =
      utility::getSubsidiaryPortIDs(getPlatformPorts());
  const auto& platPorts = getPlatformPorts();
  CHECK(portsByMasterPort.size() > 1);
  for (const auto& itPort : portsByMasterPort) {
    if ((FLAGS_hide_fabric_ports &&
         *platPorts.find(static_cast<int32_t>(itPort.first))
                 ->second.mapping()
                 ->portType() == cfg::PortType::FABRIC_PORT) ||
        (FLAGS_hide_interface_ports &&
         *platPorts.find(static_cast<int32_t>(itPort.first))
                 ->second.mapping()
                 ->portType() == cfg::PortType::INTERFACE_PORT) ||
        (FLAGS_hide_management_ports &&
         *platPorts.find(static_cast<int32_t>(itPort.first))
                 ->second.mapping()
                 ->portType() == cfg::PortType::MANAGEMENT_PORT)) {
      continue;
    }
    masterLogicalPortIds_.push_back(itPort.first);
  }
}

SaiPlatform::~SaiPlatform() = default;

void SaiPlatform::updatePorts(const StateDelta& delta) {
  for (const auto& entry : delta.getPortsDelta()) {
    const auto newPort = entry.getNew();
    if (newPort) {
      getPort(newPort->getID())->portChanged(newPort, entry.getOld());
    }
  }
}

HwSwitch* SaiPlatform::getHwSwitch() const {
  return saiSwitch_.get();
}

void SaiPlatform::onHwInitialized(HwSwitchCallback* sw) {
  initLEDs();
  /*
   * In multiswitch mode, the platform is part hw agent
   * binary and has no access to swswitch. Skip state
   * observer registration. The state updates will be
   * propagated to platform as part of oper delta sync
   * from sw agent to hw agent
   */
  if (sw) {
    sw->registerStateObserver(this, "SaiPlatform");
  }
}

void SaiPlatform::stateUpdated(const StateDelta& delta) {
  updatePorts(delta);
}

std::shared_ptr<apache::thrift::AsyncProcessorFactory>
SaiPlatform::createHandler() {
  return std::make_shared<SaiHandler>(saiSwitch_.get());
}

TransceiverIdxThrift SaiPlatform::getPortMapping(
    PortID portId,
    cfg::PortProfileID profileID) const {
  return getPort(portId)->getTransceiverMapping(profileID);
}

std::string SaiPlatform::getHwConfigDumpFile() {
  return getDirectoryUtil()->getVolatileStateDir() + "/" +
      FLAGS_hw_config_file + "_idx" + std::to_string(FLAGS_switchIndex);
}

std::string SaiPlatform::getHwConfigDumpFile_deprecated() {
  return getDirectoryUtil()->getVolatileStateDir() + "/" + FLAGS_hw_config_file;
}

void SaiPlatform::generateHwConfigFile() {
  auto hwConfig = getHwConfig();
  if (!folly::writeFile(hwConfig, getHwConfigDumpFile().c_str())) {
    throw FbossError(errno, "failed to generate hw config file. write failed");
  }
}

std::string SaiPlatform::getHwAsicConfig(
    const std::unordered_map<std::string, std::string>& overrides) {
  /*
   * This function is used to dump the HW config into a file based
   * on new asic config format. Newer platforms will begin using
   * this format.
   *
   * Asic Config V2 has 2 components:
   * 1. Common Config
   * 2. NPU Specific Config
   *
   * Common Config contains all the common config parameters that are
   * applicable across all NPUs.
   *
   * NPU Specific Config contains all the NPU specific config parameters.
   *
   * Based on the switch index, hw config is generated by reading the
   * corresponding NPU config and appending it to the common config.
   *
   * Example:
   * If we have 4 NPUs, then the hw config will be of the following format:
   * - Common Config
   * - NPU 0 Config
   * - NPU 1 Config
   * - NPU 2 Config
   * - NPU 3 Config
   *
   * There will be 4 hw_config files created one per NPU. The filename
   * will be <hw_config_file>_<switch_index>.
   *
   * If the chip config type is not ASIC_CONFIG_V2, then an exception will
   * be thrown to fallback to bcm or asic config.
   */
  auto chipConfigType = config()->thrift.platform()->chip()->getType();
  if (chipConfigType != facebook::fboss::cfg::ChipConfig::Type::asicConfig) {
    throw FbossError("No asic config v2 found in agent config");
  }
  auto asicConfig = config()->thrift.platform()->chip()->asicConfig().value();
  auto& commonConfigs = asicConfig.common()->config().as_const().value();
  std::vector<std::string> nameValStrs;
  auto addNameValue = [&nameValStrs, &overrides](const auto& keyAndVal) {
    auto oitr = overrides.find(keyAndVal.first);
    nameValStrs.emplace_back(
        folly::to<std::string>(
            keyAndVal.first,
            '=',
            oitr == overrides.end() ? keyAndVal.second : oitr->second));
  };
  for (const auto& entry : commonConfigs) {
    addNameValue(entry);
  }

  /*
   * Single NPU platfroms will not have any npu entries. In such cases,
   * we can directly use the common config.
   *
   * For multi-NPU platforms, we need to read the NPU specific config
   * by looking up the switch index from the list of npu entries.
   */
  if (asicConfig.npuEntries()) {
    auto& npuEntries = asicConfig.npuEntries().value();
    CHECK_LT(FLAGS_switchIndex, npuEntries.size());
    const auto& npuEntry = npuEntries.find(FLAGS_switchIndex);
    if (npuEntry != npuEntries.end()) {
      for (const auto& entry : npuEntry->second.config().as_const().value()) {
        addNameValue(entry);
      }
    }
  }
  auto hwConfig = folly::join('\n', nameValStrs);
  return hwConfig;
}

void SaiPlatform::initSaiProfileValues() {
  auto platformMode = getType();
  // LADAKH800BCLS and LEH800BCLS need SAI_CUSTOM_KERNEL_BDE_NAME to support
  // mnpu
  if (platformMode == PlatformType::PLATFORM_LADAKH800BCLS ||
      platformMode == PlatformType::PLATFORM_LEH800BCLS) {
    kSaiProfileValues.insert(
        std::make_pair("SAI_CUSTOM_KERNEL_BDE_NAME", "linux_ngbde"));
  }
  kSaiProfileValues.insert(
      std::make_pair(SAI_KEY_INIT_CONFIG_FILE, getHwConfigDumpFile()));
  kSaiProfileValues.insert(
      std::make_pair(
          SAI_KEY_WARM_BOOT_READ_FILE,
          getWarmBootHelper()->warmBootDataPath()));
  kSaiProfileValues.insert(
      std::make_pair(
          SAI_KEY_WARM_BOOT_WRITE_FILE,
          getWarmBootHelper()->warmBootDataPath()));
  kSaiProfileValues.insert(
      std::make_pair(
          SAI_KEY_BOOT_TYPE, getWarmBootHelper()->canWarmBoot() ? "1" : "0"));
  auto vendorProfileValues = getSaiProfileVendorExtensionValues();
  kSaiProfileValues.insert(
      vendorProfileValues.begin(), vendorProfileValues.end());
}

void SaiPlatform::initImpl(uint32_t hwFeaturesDesired) {
  initSaiProfileValues();
  SaiApiTable::getInstance()->queryApis(
      getServiceMethodTable(), getSupportedApiList());
  saiSwitch_ = std::make_unique<SaiSwitch>(this, hwFeaturesDesired);
  generateHwConfigFile();
}

void SaiPlatform::initPorts() {
  auto switchId =
      SwitchID(getAsic()->getSwitchId() ? *getAsic()->getSwitchId() : 0);
  XLOG(DBG4) << "Platform ports are initialized with switch ID: " << switchId;
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>({switchId}));
  for (auto& port : getPlatformPorts()) {
    std::unique_ptr<SaiPlatformPort> saiPort;
    PortID portId(port.first);
    if (scopeResolver()->scope(portId) != matcher) {
      XLOG(DBG5) << "Skipping platform port " << portId
                 << " due to scope mismatch";
      continue;
    }
    saiPort = createSaiPlatformPort(portId, this);
    if (!saiPort) {
      throw FbossError(
          "Unsupported SAI platform port type: ",
          apache::thrift::util::enumNameSafe(getType()));
    }
    portMapping_.insert(std::make_pair(portId, std::move(saiPort)));
  }
}

SaiPlatformPort* SaiPlatform::getPort(PortID id) const {
  auto saiPortIter = portMapping_.find(id);
  if (saiPortIter == portMapping_.end()) {
    throw FbossError("failed to find port: ", id);
  }
  return saiPortIter->second.get();
}

PlatformPort* SaiPlatform::getPlatformPort(PortID port) const {
  return getPort(port);
}

sai_service_method_table_t* SaiPlatform::getServiceMethodTable() const {
  return &kSaiServiceMethodTable;
}

HwSwitchWarmBootHelper* SaiPlatform::getWarmBootHelper() {
  if (!wbHelper_) {
    wbHelper_ = std::make_unique<HwSwitchWarmBootHelper>(
        getAsic()->getSwitchIndex(),
        getDirectoryUtil()->getWarmBootDir(),
        "sai_adaptor_state_");
  }
  return wbHelper_.get();
}

std::pair<PortID, std::vector<cfg::PortProfileID>>
SaiPlatform::findPortIDAndProfiles(
    cfg::PortSpeed speed,
    std::vector<uint32_t> lanes,
    PortSaiId portSaiId) const {
  std::vector<cfg::PortProfileID> matchingProfiles;

  for (const auto& portMapping : portMapping_) {
    const auto& platformPort = portMapping.second;
    auto profiles = platformPort->getAllProfileIDsForSpeed(speed);
    for (auto profileID : profiles) {
      XLOG(DBG5) << "portID: " << portMapping.first
                 << " profileID : " << static_cast<int>(profileID)
                 << " expected: " << static_cast<int>(speed)
                 << " lanes: " << folly::join(",", lanes) << " got lanes: "
                 << folly::join(",", platformPort->getHwPortLanes(profileID));
      if (platformPort->getHwPortLanes(profileID) == lanes) {
        matchingProfiles.push_back(profileID);
      }
    }
    if (matchingProfiles.size() > 0) {
      return std::make_pair(platformPort->getPortID(), matchingProfiles);
    }
  }
  throw FbossError(
      "platform port not found ",
      (PortID)portSaiId,
      " speed: ",
      static_cast<int>(speed),
      ", lanes ",
      folly::join(",", lanes));
}

std::vector<SaiPlatformPort*> SaiPlatform::getPortsWithTransceiverID(
    TransceiverID id) const {
  std::vector<SaiPlatformPort*> ports;
  for (const auto& port : portMapping_) {
    if (auto tcvrID = port.second->getTransceiverID()) {
      if (tcvrID == id &&
          port.second->getCurrentProfile() !=
              cfg::PortProfileID::PROFILE_DEFAULT) {
        ports.push_back(port.second.get());
      }
    }
  }
  return ports;
}

SaiSwitchTraits::CreateAttributes SaiPlatform::getSwitchAttributes(
    bool mandatoryOnly,
    cfg::SwitchType swType,
    std::optional<int64_t> swId,
    BootType bootType) {
  SaiSwitchTraits::Attributes::InitSwitch initSwitch(true);

  std::optional<SaiSwitchTraits::Attributes::HwInfo> hwInfo = getHwInfo(this);
  std::optional<SaiSwitchTraits::Attributes::SrcMac> srcMac;
  std::optional<SaiSwitchTraits::Attributes::MacAgingTime> macAgingTime;
  if (!mandatoryOnly && swType != cfg::SwitchType::FABRIC) {
    srcMac = getLocalMac();
    macAgingTime = getDefaultMacAgingTime();
  }

  std::optional<SaiSwitchTraits::Attributes::AclFieldList> aclFieldList =
      getAclFieldList();

  std::optional<SaiSwitchTraits::Attributes::UseEcnThresholds> useEcnThresholds{
      std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::SAI_ECN_WRED)) {
    useEcnThresholds = true;
  }
  std::optional<SaiSwitchTraits::Attributes::SwitchType> switchType;
  std::optional<SaiSwitchTraits::Attributes::SwitchId> switchId;
  std::optional<SaiSwitchTraits::Attributes::MaxSystemCores> maxSystemCores;
  std::optional<SaiSwitchTraits::Attributes::MaxCores> maxCores;
  std::optional<SaiSwitchTraits::Attributes::SysPortConfigList> sysPortConfigs;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::optional<SaiSwitchTraits::Attributes::CreditWd> creditWd;
  std::optional<SaiSwitchTraits::Attributes::CreditWdTimer> creditWdMs;
  if (getAsic()->isSupported(HwAsic::Feature::CREDIT_WATCHDOG)) {
    // Use SAI defaults
    creditWd = true;
    creditWdMs = 500;
  }
#endif
  if (swType == cfg::SwitchType::VOQ || swType == cfg::SwitchType::FABRIC) {
    switchType = swType == cfg::SwitchType::VOQ ? SAI_SWITCH_TYPE_VOQ
                                                : SAI_SWITCH_TYPE_FABRIC;
    switchId = swId;
    if (swType == cfg::SwitchType::VOQ) {
      auto agentCfg = config();
      CHECK(agentCfg) << " agent config must be set ";
      uint32_t maxCoreCount = 0;
      uint32_t maxSystemCoreCount = 0;
      auto localMac = getLocalMac();
      cfg::SwitchInfo swInfo;
      swInfo.switchIndex() = 0;
      swInfo.switchType() = cfg::SwitchType::VOQ;
      swInfo.switchMac() = localMac.toString();
      const Jericho3Asic j3(0, swInfo);
      const Qumran4DAsic qumran4d(0, swInfo);
      const Jericho4Asic j4(0, swInfo);
      for (const auto& [id, dsfNode] : *agentCfg->thrift.sw()->dsfNodes()) {
        if (dsfNode.type() != cfg::DsfNodeType::INTERFACE_NODE) {
          continue;
        }
        switch (*dsfNode.asicType()) {
          case cfg::AsicType::ASIC_TYPE_JERICHO3:
            // for directly connected interface nodes we don't expect
            // asic type to change across dsf nodes
            maxCoreCount = std::max(j3.getNumCores(), maxCoreCount);
            maxSystemCoreCount =
                std::max(maxSystemCoreCount, uint32_t(id + j3.getNumCores()));
            break;
          case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
            maxCoreCount = std::max(qumran4d.getNumCores(), maxCoreCount);
            maxSystemCoreCount = std::max(
                maxSystemCoreCount, uint32_t(id + qumran4d.getNumCores()));
            break;
          case cfg::AsicType::ASIC_TYPE_JERICHO4:
            maxCoreCount = std::max(j4.getNumCores(), maxCoreCount);
            maxSystemCoreCount =
                std::max(maxSystemCoreCount, uint32_t(id + j4.getNumCores()));
            break;
          default:
            throw FbossError("Unexpected asic type: ", *dsfNode.asicType());
        }
      }

      maxCores = maxCoreCount;
      maxSystemCores = maxSystemCoreCount;
      sysPortConfigs = SaiSwitchTraits::Attributes::SysPortConfigList{
          getInternalSystemPortConfig()};
    }
  }
  std::optional<SaiSwitchTraits::Attributes::SflowAggrNofSamples>
      sflowNofSamples{std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLES_PACKING)) {
    auto agentCfg = config();
    if (agentCfg->thrift.sw()
            ->switchSettings()
            ->numberOfSflowSamplesPerPacket()) {
      sflowNofSamples = *agentCfg->thrift.sw()
                             ->switchSettings()
                             ->numberOfSflowSamplesPerPacket();
    }
  }
  std::optional<SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement>
      measureCableLengths{std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::CABLE_PROPOGATION_DELAY)) {
    auto agentCfg = config();
    if (agentCfg->thrift.sw()->switchSettings()->measureCableLengths()) {
      measureCableLengths =
          *agentCfg->thrift.sw()->switchSettings()->measureCableLengths();
    }
  }
  std::optional<SaiSwitchTraits::Attributes::PortCl72RetryEnable>
      portCl72RetryEnable{std::nullopt};
#if defined(BRCM_SAI_SDK_XGS_GTE_14_2)
  if (FLAGS_enable_port_cl72_retry) {
    portCl72RetryEnable = true;
  }
#endif
  std::optional<SaiSwitchTraits::Attributes::AclDropSnoopDisabled>
      aclDropSnoopDisabled{std::nullopt};
#if defined(TAJO_SDK_VERSION_24_8_3001) || defined(TAJO_SDK_VERSION_25_11_4210)
  // Only the Gibraltar driver implements it (GR2/G2LL return NOT_IMPLEMENTED).
  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO) {
    aclDropSnoopDisabled = true;
  }
#endif
  std::optional<SaiSwitchTraits::Attributes::DllPath> dllPath;
#if defined(BRCM_SAI_SDK_XGS)
  auto platformMode = getType();
  if (platformMode == PlatformType::PLATFORM_FUJI ||
      platformMode == PlatformType::PLATFORM_ELBERT ||
      platformMode == PlatformType::PLATFORM_MONTBLANC ||
      platformMode == PlatformType::PLATFORM_TAHAN800BC) {
    std::vector<int8_t> dllPathCharArray;
    std::copy(
        FLAGS_dll_path.c_str(),
        FLAGS_dll_path.c_str() + FLAGS_dll_path.size() + 1,
        std::back_inserter(dllPathCharArray));
    dllPath = dllPathCharArray;
  }
#endif

  std::optional<SaiSwitchTraits::Attributes::FirmwarePathName> firmwarePathName{
      std::nullopt};

  const auto switchSettings = config()->thrift.sw()->switchSettings();
  if (switchSettings->firmwarePath().has_value()) {
    std::vector<int8_t> firmwarePathNameArray;
    std::copy(
        switchSettings->firmwarePath().value().c_str(),
        switchSettings->firmwarePath().value().c_str() +
            switchSettings->firmwarePath().value().size() + 1,
        std::back_inserter(firmwarePathNameArray));

    firmwarePathName = firmwarePathNameArray;
  } else {
    // TODO
    // We plan to migrate all use cases that use firmware path to more
    // geentalized config driven approach i.e. if-block.
    // After that migration, we will remove this else-block, HwAsic feature
    // SAI_FIRWWARE_PATH and FLAGS_firmware_path.
    // Fallback in the meanwhile.
    if (getAsic()->isSupported(HwAsic::Feature::SAI_FIRMWARE_PATH)) {
      std::vector<int8_t> firmwarePathNameArray;
      std::copy(
          FLAGS_firmware_path.c_str(),
          FLAGS_firmware_path.c_str() + FLAGS_firmware_path.size() + 1,
          std::back_inserter(firmwarePathNameArray));
      firmwarePathName = firmwarePathNameArray;
    }
  }

  std::optional<SaiSwitchTraits::Attributes::FirmwareCoreToUse>
      firmwareCoreToUse{std::nullopt};
  std::optional<SaiSwitchTraits::Attributes::FirmwareLogFile> firmwareLogFile{
      std::nullopt};
  std::optional<SaiSwitchTraits::Attributes::FirmwareLoadType> firmwareLoadType{
      std::nullopt};

#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  if (swId.has_value()) {
    const auto& firmwareNameToFirmwareInfo = getFirmwareForSwitch(
        switchSettings->switchIdToSwitchInfo(), swId.value());

    if (firmwareNameToFirmwareInfo.size() != 0) {
      if (firmwareNameToFirmwareInfo.size() > 1) {
        throw FbossError("Setting only one firmware is supported today");
      }
      auto [firmwareName, firmwareInfo] = *firmwareNameToFirmwareInfo.begin();
      XLOG(DBG2) << "FirmwareName: " << firmwareName
                 << " coreToUse: " << *firmwareInfo.coreToUse()
                 << " path: " << *firmwareInfo.path()
                 << " logPath: " << *firmwareInfo.logPath()
                 << " firmwareLoadType: "
                 << apache::thrift::util::enumNameSafe(
                        *firmwareInfo.firmwareLoadType());

      // Set Core
      firmwareCoreToUse = *firmwareInfo.coreToUse();

      // Set firmware path
      std::string firmwarePath = *firmwareInfo.path();
      std::vector<int8_t> firmwarePathNameArray;
      std::copy(
          firmwarePath.c_str(),
          firmwarePath.c_str() + firmwarePath.size() + 1,
          std::back_inserter(firmwarePathNameArray));
      firmwarePathName = firmwarePathNameArray;

      // Set firmware log path
      std::string firmwareLogPath = *firmwareInfo.logPath();
      std::vector<int8_t> firmwareLogPathArray;
      std::copy(
          firmwareLogPath.c_str(),
          firmwareLogPath.c_str() + firmwareLogPath.size() + 1,
          std::back_inserter(firmwareLogPathArray));
      firmwareLogFile = firmwareLogPathArray;

      // Set firmware load type
      switch (*firmwareInfo.firmwareLoadType()) {
        case cfg::FirmwareLoadType::FIRMWARE_LOAD_TYPE_START:
          firmwareLoadType = SAI_SWITCH_FIRMWARE_LOAD_TYPE_AUTO;
          break;
        case cfg::FirmwareLoadType::FIRMWARE_LOAD_TYPE_STOP:
          firmwareLoadType = SAI_SWITCH_FIRMWARE_LOAD_TYPE_STOP;
          break;
      }
    }
  }
#endif

  std::optional<SaiSwitchTraits::Attributes::SwitchIsolate> switchIsolate{
      std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::LINK_INACTIVE_BASED_ISOLATE)) {
    switchIsolate = true;
  }

  std::optional<int32_t> voqLatencyMinLocalNs;
  std::optional<int32_t> voqLatencyMaxLocalNs;
  std::optional<int32_t> voqLatencyMinLevel1Ns;
  std::optional<int32_t> voqLatencyMaxLevel1Ns;
  std::optional<int32_t> voqLatencyMinLevel2Ns;
  std::optional<int32_t> voqLatencyMaxLevel2Ns;
  // TODO: Look at making this part of config instead of hardcoding
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  voqLatencyMinLocalNs = 5000;
  voqLatencyMaxLocalNs = 4294967295;
  voqLatencyMinLevel1Ns = 500000;
  voqLatencyMaxLevel1Ns = 4294967295;
  voqLatencyMinLevel2Ns = 800000;
  voqLatencyMaxLevel2Ns = 4294967295;
#endif

  std::optional<SaiSwitchTraits::Attributes::RouteNoImplicitMetaData>
      routeNoImplicitMetaData{std::nullopt};
#if defined(BRCM_SAI_SDK_GTE_11_0)
  if (getAsic()->isSupported(HwAsic::Feature::ROUTE_METADATA) &&
      FLAGS_set_classid_for_my_subnet_and_ip_routes) {
    XLOG(DBG2) << "Disable configuring implicit route metadata by sai adapter";
    routeNoImplicitMetaData = true;
  }
#endif
  std::optional<SaiSwitchTraits::Attributes::DelayDropCongThreshold>
      delayDropCongThreshold{std::nullopt};
#if defined(TAJO_SDK_EBRO)
  if (getAsic()->isSupported(
          HwAsic::Feature::ENABLE_DELAY_DROP_CONGESTION_THRESHOLD) &&
      FLAGS_enable_delay_drop_congestion_threshold) {
    XLOG(DBG2) << "Enable new CGM delay drop congestion thresholds";
    delayDropCongThreshold = 1;
  }
#endif
  std::optional<
      SaiSwitchTraits::Attributes::FabricLinkLayerFlowControlThreshold>
      fabricLLFC;
  std::optional<int32_t> maxSystemPortId;
  std::optional<int32_t> maxLocalSystemPortId;
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
  std::optional<std::vector<sai_u16_range_t>> localSystemPortIdRangeList;
#endif
  std::optional<int32_t> maxSystemPorts;
  std::optional<int32_t> maxVoqs;
  std::optional<uint32_t> maxSwitchId;
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_RAMON3 ||
      getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
    // R3/J3 asics have a HW bug, whereby we need to constrain the max
    // switch id that's used in their deployment to be below the
    // default (HW advertised) value.
    auto agentConfig = config();
    if (swType == cfg::SwitchType::FABRIC) {
      HwAsic::FabricNodeRole fabricNodeRole = getAsic()->getFabricNodeRole();
      maxSwitchId =
          utility::getDsfFabricSwitchMaxSwitchId(*agentConfig, fabricNodeRole);
    } else {
      maxSwitchId = utility::getDsfVoqSwitchMaxSwitchId();
    }
    auto maxDsfConfigSwitchId = utility::maxDsfSwitchId(*agentConfig) +
        std::max(getAsic()->getNumCores(),
                 (maxCores.has_value() ? maxCores->value() : 0));

    XLOG(DBG2) << "Set max switch-id to: " << *maxSwitchId
               << " got maxDsfConfigSwitchId: " << maxDsfConfigSwitchId;
    CHECK_GE(*maxSwitchId, maxDsfConfigSwitchId);
    CHECK_LE(*maxSwitchId, getAsic()->getMaxSwitchId());
  }
#endif
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  if (getAsic()->getSwitchType() == cfg::SwitchType::FABRIC &&
      getAsic()->getFabricNodeRole() == HwAsic::FabricNodeRole::DUAL_STAGE_L1) {
    CHECK(getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_RAMON3)
        << " LLFC threshold values for non R3 chips in DUAL_STAGE_L1 role needs to figured out";
    // Vendor suggested value
    constexpr uint32_t kRamon3LlfcThreshold{800};
    fabricLLFC = std::vector<uint32_t>({kRamon3LlfcThreshold});
  }

  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3 ||
      getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_RAMON3) {
    if (isDualStage3Q2QMode()) {
      maxSystemPortId = 32694;
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
      localSystemPortIdRangeList = std::vector<sai_u16_range_t>{{0, 184}};
#else
      maxLocalSystemPortId = 184;
#endif
      maxSystemPorts = 22136;
      maxVoqs = 65284;
    } else if (FLAGS_dsf_single_stage_r192_f40_e32) {
      // Total System Ports in the cluster
      // =================================
      //
      // 1 Global recycle port
      // 1 Management port
      // RDSW: 36 x 400G NIF ports. Thus, 1 + 1 + 36 = 38 ports.
      // EDSW: 18 x 800G NIF ports. Thus, 1 + 1 + 18 = 20 ports.
      //
      // 4 CPU (1 per core) + 4 Recycle (1 per core) + 1 eventor +
      // 160 Fabric link monitoring + 16 hyerports = 185
      // Thus, Max local system PortID (starting 0) = 184.
      //
      // Max System Ports = 185 + (38 x 192) + (20 x 32) = 8121.
      //
      // System Port ID assignment
      // =========================
      //   Local Ports
      //   -----------
      //      [0-3]: CPU ports
      //      [4-7]: Recycle ports
      //          8: Eventor port
      //    [9-168]: 160 Fabric link monitoring ports (in the future)
      //  [169-184]: 16 Hyper ports (in the future)
      //
      //   The above assignment is same for ALL the RDSWs, EDSWs.
      //
      //   Global Ports for RDSW 1, base offset 184
      //   ----------------------------------------
      //        185: Recycle port for inband
      //        186: Management port
      //  [187-222]: One for each of the 36 x 400G NIF ports
      //
      //   Global Ports for EDSW 1, base offset 7480
      //   -----------------------------------------
      //        7481: Recycle port for inband
      //        7482: Management port
      //  [7483-7500: One for each of the 16 x 800G NIF ports
      maxSystemPortId = 8120;
      maxSystemPorts = 8121;
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
      if (FLAGS_hyper_port) {
        localSystemPortIdRangeList =
            std::vector<sai_u16_range_t>{{0, 10}, {6144, 6320}};
      } else {
        localSystemPortIdRangeList = std::vector<sai_u16_range_t>{{0, 184}};
      }
#else
      maxLocalSystemPortId = 184;
#endif
      maxVoqs = maxSystemPorts.value() * 8;
    } else {
      if (FLAGS_dsf_single_stage_r128_f40_e16_8k_sys_ports) {
        maxSystemPortId = 8120;
        maxSystemPorts = 8121;
      } else {
        maxSystemPortId = 6143;
        maxSystemPorts = 6144;
      }
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
      if (FLAGS_hyper_port) {
        localSystemPortIdRangeList =
            std::vector<sai_u16_range_t>{{0, 10}, {6144, 6320}};
      } else {
        localSystemPortIdRangeList = std::vector<sai_u16_range_t>{};
      }
#else
      maxLocalSystemPortId = -1;
#endif
      maxVoqs = maxSystemPorts.value() * 8;
    }
  }

  //
  // J4
  //   Local Ports
  //   -----------
  //   - [0-8]: CPU ports (one per datapath)
  //   - [9-16]: Recycle ports
  //   - 17: Eventor port
  //   - [18-161]: 144 Fabric link monitoring ports
  //
  //   TODO: Above numbers are for a single NPU.
  //   - What is the scheme for multi NPU (2xJ4)?
  //   - Worst case scale is 162 * 2 local sysports
  //   - First Global SystemPort = 325
  //   - 32xJ4 in chassis, 32x800G + 1x100G (MGMT)
  //   - 2K 400G ports per chassis, 32x100G QSFP MGMT
  //   - Total sysport scale: 2405 (2048+325+32)
  //   - Local CPU systemPort is always 0 per NPU
  //   TODO: confirm adjustLocalPortOffset.
  //
  // Q4D
  //   Local Ports (single FAP system)
  //   -----------
  //   - 32 800G per die, 64x800G per Q4D
  //   - 128x400G per Q4D (breakout)
  //   - 2x100G QSFP Talon ports (MGMT)
  //   - Total ports per Q4D = 130
  //
  //   Port Allocation:
  //   - [0-16]: CPU ports (one per datapath)
  //   - [17-32]: Recycle ports (local, 2 per core)
  //   - 33(core0), 34(core4): Eventor ports
  //   - 35: Global recycle port (scope=GLOBAL)
  //   - No Fabric link monitoring (no fabric)
  //   - MaxSystemPorts: 1024
  //

  // Q4D standalone box configuration
  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_QUMRAN4D) {
    maxSystemPorts = 1024;
    maxVoqs = 8 * 1024;
    maxSystemPortId = 1024 - 1;
    localSystemPortIdRangeList = std::vector<sai_u16_range_t>{};
  }
  // J4 configuration
  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO4) {
    maxSystemPorts = 1280;
    maxVoqs = 1280 * 8;
    maxSystemPortId = 1279;
    localSystemPortIdRangeList = std::vector<sai_u16_range_t>{{0, 161}};
  }
#endif

  if (getAsic()->isSupported(HwAsic::Feature::SWITCH_ISOLATE) &&
      bootType == BootType::COLD_BOOT) {
    // FABRIC switches should always start in isolated state until we configure
    // the switch
    switchIsolate = true;
  }

  std::optional<SaiSwitchTraits::Attributes::NoAclsForTraps> noAclsForTraps{
      std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::NO_RX_REASON_TRAP)) {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
    noAclsForTraps = true;
#endif
  }

  std::optional<SaiSwitchTraits::Attributes::PfcTcDldTimerGranularityInterval>
      pfcWatchdogTimerGranularityMap;
#if defined(BRCM_SAI_SDK_XGS) && defined(BRCM_SAI_SDK_GTE_11_0)
  if (getAsic()->isSupported(HwAsic::Feature::PFC_WATCHDOG_TIMER_GRANULARITY)) {
    // We need to set the watchdog granularity to an appropriate value,
    // otherwise the default granularity in SAI/SDK may be incompatible with the
    // requested watchdog intervals. Auto-derivation is being requested in
    // CS00012393810.
    std::vector<sai_map_t> mapToValueList(
        cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX() + 1);
    for (int pri = 0;
         pri <= cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX();
         pri++) {
      sai_map_t mapping{};
      mapping.key = pri;
      mapping.value =
          switchSettings->pfcWatchdogTimerGranularityMsec().value_or(10);
      mapToValueList.at(pri) = mapping;
    }
    pfcWatchdogTimerGranularityMap =
        SaiSwitchTraits::Attributes::PfcTcDldTimerGranularityInterval{
            mapToValueList};
  }
#endif

  std::optional<SaiSwitchTraits::Attributes::SdkDumpRateLimitWindow>
      sdkDumpRateLimitWindow{std::nullopt};
#if defined(SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED)
  sdkDumpRateLimitWindow = FLAGS_sdk_dump_rate_limit_window_ms;
#endif

  return {
      initSwitch,
      hwInfo, // hardware info
      srcMac, // source mac
      std::nullopt, // shell
      std::nullopt, // ecmp hash v4
      std::nullopt, // ecmp hash v6
      std::nullopt, // lag hash v4
      std::nullopt, // lag hash v6
      std::nullopt, // ecmp hash seed
      std::nullopt, // lag hash seed
      std::nullopt, // ecmp hash algo
      std::nullopt, // lag hash algo
      std::nullopt, // restart warm
      std::nullopt, // qos dscp to tc map
      std::nullopt, // qos tc to queue map
      std::nullopt, // qos exp to tc map
      std::nullopt, // qos tc to exp map
      macAgingTime,
      std::nullopt, // ingress acl
      std::nullopt, // egress acl
      aclFieldList,
      std::nullopt, // tam object list
      useEcnThresholds,
      std::nullopt, // counter refresh interval
      firmwareCoreToUse,
      firmwarePathName, // Firmware path name
      firmwareLogFile, // Firmware log file
      std::nullopt, // Firmware load method
      firmwareLoadType, // Firmware load type
      std::nullopt, // Hardware access bus
      std::nullopt, // Platform context
      std::nullopt, // Switch profile id
      switchId, // Switch id
      maxSystemCores,
      sysPortConfigs, // System port config list
      switchType,
      std::nullopt, // Read function
      std::nullopt, // Write function
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::nullopt, // Max ECMP member count
      std::nullopt, // ECMP member count
#endif
      dllPath,
      std::nullopt, // Restart Issu
      switchIsolate,
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      creditWd, // Credit Watchdog
      creditWdMs, // Credit Watchdog Timer
#endif
      maxCores, // Max cores
      std::nullopt, // PFC DLR Packet Action
      routeNoImplicitMetaData, // route no implicit meta data
      std::nullopt, // route allow implicit meta data
      std::nullopt, // multi-stage local switch ids
      voqLatencyMinLocalNs, // Local VoQ latency bin min
      voqLatencyMaxLocalNs, // Local VoQ latency bin max
      voqLatencyMinLevel1Ns, // Level1 VoQ latency bin min
      voqLatencyMaxLevel1Ns, // Level1 VoQ latency bin max
      voqLatencyMinLevel2Ns, // Level2 VoQ latency bin min
      voqLatencyMaxLevel2Ns, // Level2 VoQ latency bin max
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      std::nullopt, // ARS profile
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      std::nullopt, // PTP mode
#endif
      std::nullopt, // ReachabilityGroupList
      delayDropCongThreshold, // Delay Drop Cong Threshold
      fabricLLFC,
      std::nullopt, // SRAM free percent XOFF threshold
      std::nullopt, // SRAM free percent XON threshold
      noAclsForTraps, // No acls for traps
      maxSystemPortId,
      maxLocalSystemPortId,
      maxSystemPorts,
      maxVoqs,
      std::nullopt, // Fabric CLLFC TX credit threshold
      std::nullopt, // VOQ DRAM bound threshold
      std::nullopt, // Conditional Entropy Rehash Period
      std::nullopt, // Shel Source IP
      std::nullopt, // Shel Destination IP
      std::nullopt, // Shel Source MAC
      std::nullopt, // Shel Periodic Interval
      maxSwitchId, // Max switch Id
      sflowNofSamples, // Sflow aggr number of samples
      std::nullopt, // SDK Register dump log path
      sdkDumpRateLimitWindow, // SDK dump rate limit window
      std::nullopt, // Firmware Object list
      std::nullopt, // tc rate limit list
      pfcWatchdogTimerGranularityMap, // PFC watchdog timer granularity
      std::nullopt, // disable sll and hll timeout
      std::nullopt, // credit request profile scheduler mode
      std::nullopt, // module id to credit request profile param list
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
      localSystemPortIdRangeList, // range list of local scope system port ids
#endif
      std::nullopt, // enable PFC monitoring for the switch
      measureCableLengths, // enable cable propagation delay measurement
      portCl72RetryEnable, // enable CL72 link training retry
      aclDropSnoopDisabled, // disable snoop on ingress ACL drop
      std::nullopt, // switching mode (store-and-forward / cut-through)
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      // Link up debounce timeout. Derived from the per-port
      // portUpHoldoffTimeMs config, so it is programmed by SaiPortManager
      // once the port config is known rather than at switch create.
      std::nullopt,
#endif
#if defined(BRCM_PAI_SDK)
      std::nullopt, // SyncLock
      std::nullopt, // SyncUnlock
      std::nullopt, // WarmRecover
#endif
  };
}

std::vector<sai_system_port_config_t> SaiPlatform::getInternalSystemPortConfig()
    const {
  CHECK(getAsic()) << " Asic must be set before getting sys port info";
  auto internalSystemPortConfigs = getAsic()->getInternalSystemPortConfig(
      getPlatformMapping()->getCpuPortsCoreAndPortIdx());

  std::vector<sai_system_port_config_t> saiSystemPortConfigs;
  saiSystemPortConfigs.reserve(internalSystemPortConfigs.size());
  for (const auto& config : internalSystemPortConfigs) {
    saiSystemPortConfigs.push_back(
        {config.portId,
         config.switchId,
         config.coreIndex,
         config.corePortIndex,
         config.speedMbps,
         config.numVoqs});
  }
  return saiSystemPortConfigs;
}

uint32_t SaiPlatform::getDefaultMacAgingTime() const {
  static auto constexpr kL2AgeTimerSeconds = 300;
  return kL2AgeTimerSeconds;
}

const std::set<sai_api_t>& SaiPlatform::getDefaultSwitchAsicSupportedApis()
    const {
  // For now, most of the apis are supported for switch asic.
  // Therefore we can just reuse SaiApiTable::getFullApiList()
  // But in case in the future we have some special phy api which won't be
  // supported in the switch asic, we can also exclude those apis ad-hoc.
  static auto apis = SaiApiTable::getInstance()->getFullApiList();
  auto* asic = getAsic();
  CHECK(asic);
  // Macsec is not currently supported in the broadcom sai sdk
  apis.erase(facebook::fboss::MacsecApi::ApiType);
  /*
   * Only query the isolation group api where the asic is declared to support
   * isolation groups. Querying an api the adapter does not implement fails
   * SaiApiTable::queryApis, and that takes agent init down entirely -- not just
   * isolation group functionality -- so the default has to be to leave it out.
   */
  if (!asic->isSupported(HwAsic::Feature::ISOLATION_GROUP)) {
    apis.erase(facebook::fboss::IsolationGroupApi::ApiType);
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  // Querying an api the adapter does not implement fails init, so only query
  // it where the asic declares support.
  if (!asic->isSupported(HwAsic::Feature::MY_MAC)) {
    apis.erase(facebook::fboss::MyMacApi::ApiType);
  }
#endif
  return apis;
}
const std::set<sai_api_t>& SaiPlatform::getDefaultPhyAsicSupportedApis() const {
  static const std::set<sai_api_t> kSupportedPhyApiList = {
      facebook::fboss::AclApi::ApiType,
      facebook::fboss::PortApi::ApiType,
      facebook::fboss::SwitchApi::ApiType,
  };
  return kSupportedPhyApiList;
}

const std::set<sai_api_t>& SaiPlatform::getSupportedApiList() const {
  return getDefaultSwitchAsicSupportedApis();
}

void SaiPlatform::stateChanged(const StateDelta& delta) {
  updatePorts(delta);
}

} // namespace facebook::fboss
