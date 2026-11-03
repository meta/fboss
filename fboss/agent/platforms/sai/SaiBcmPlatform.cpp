/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiBcmPlatform.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/common/utils/BcmYamlConfig.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

#include <folly/logging/xlog.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

DECLARE_bool(disable_looped_fabric_ports);

namespace facebook::fboss {

namespace {

// Returns the NPU-specific HSDK yamlConfig for switchIndex if the asicConfig
// contains a matching per-NPU yamlConfig entry, std::nullopt otherwise.
std::optional<std::string> getNpuSpecificYamlConfig(
    const cfg::AsicConfig& asicConfig,
    int16_t switchIndex) {
  if (!asicConfig.npuEntries()) {
    return std::nullopt;
  }
  const auto& npuEntries = asicConfig.npuEntries().value();
  const auto npuEntry = npuEntries.find(switchIndex);
  if (npuEntry == npuEntries.end()) {
    return std::nullopt;
  }
  if (npuEntry->second.getType() != cfg::AsicConfigEntry::Type::yamlConfig) {
    XLOG(WARN) << "NPU-specific asicConfig entry for switchIndex="
               << switchIndex
               << " is not a yamlConfig; falling back to common config";
    return std::nullopt;
  }
  return npuEntry->second.yamlConfig().value();
}

// Returns the common HSDK yamlConfig if present, std::nullopt otherwise.
std::optional<std::string> getCommonYamlConfig(
    const cfg::AsicConfig& asicConfig) {
  if (asicConfig.common()->getType() !=
      cfg::AsicConfigEntry::Type::yamlConfig) {
    return std::nullopt;
  }
  return asicConfig.common()->yamlConfig().value();
}

// SOC properties pointing at a diag/SOC command file the SDK runs at init.
constexpr auto kSaiPreinitCmdFile = "sai_preinit_cmd_file";
constexpr auto kSaiPostinitCmdFile = "sai_postinit_cmd_file";
} // namespace

std::vector<std::pair<std::string, std::string>>
SaiBcmPlatform::getBcmSdkLogFileSocProperties() const {
  if (FLAGS_bcm_sdk_log_file.empty()) {
    return {};
  }
  // Fail fast on a bad path: otherwise a wrong/unreadable file silently no-ops
  // or fails opaquely deep in SDK init, at a Tier-0 boundary.
  if (!std::filesystem::exists(FLAGS_bcm_sdk_log_file)) {
    throw FbossError(
        "--bcm_sdk_log_file does not exist: ", FLAGS_bcm_sdk_log_file);
  }
  // This flag is process-global. In multi-switch mode every co-located
  // fboss_hw_agent reads the same value, so all SDK instances run the same
  // command file. Log this switch's index once so an operator can correlate
  // output and knows per-ASIC separation needs single-ASIC hosts or a
  // switch-index-aware 'log file=' line inside the command file itself.
  [[maybe_unused]] static const bool logged = [this] {
    XLOG(INFO) << "--bcm_sdk_log_file set: injecting SDK init command file "
               << FLAGS_bcm_sdk_log_file << " (switchIndex "
               << getAsic()->getSwitchIndex() << ")";
    return true;
  }();
  return {
      {kSaiPreinitCmdFile, FLAGS_bcm_sdk_log_file},
      {kSaiPostinitCmdFile, FLAGS_bcm_sdk_log_file}};
}

std::string SaiBcmPlatform::getHwConfig() {
  // Compute once so validation/logging happen a single time and every config
  // path below sees a consistent snapshot of the flag.
  const auto socProperties = getBcmSdkLogFileSocProperties();
  if (getAsic()->isSupported(HwAsic::Feature::HSDK)) {
    std::string yamlConfig;
    try {
      const auto& asicConfig =
          config()->thrift.platform()->chip()->asicConfig().value();
      const int16_t switchIndex = getAsic()->getSwitchIndex();
      // Prefer a per-NPU yamlConfig (needed for differing polarities across
      // NPUs on multi-NPU platforms); an empty per-NPU entry defers to the
      // common config.
      auto npuYamlConfig = getNpuSpecificYamlConfig(asicConfig, switchIndex);
      if (npuYamlConfig && !npuYamlConfig->empty()) {
        XLOG(INFO) << "Loading NPU-specific yamlConfig for switchIndex="
                   << switchIndex;
        yamlConfig = std::move(*npuYamlConfig);
      } else if (auto commonYamlConfig = getCommonYamlConfig(asicConfig)) {
        XLOG(INFO) << "Loading common HSDK yamlConfig for switchIndex="
                   << switchIndex;
        yamlConfig = std::move(*commonYamlConfig);
      }
      if (yamlConfig.empty()) {
        throw FbossError("No HSDK yamlConfig found in asicConfig");
      }
    } catch (const std::exception& e) {
      /*
       * (TODO): Once asic config v2 is rolled out to the fleet, we
       * should remove this fallback and always use the config v2
       */
      XLOG(WARN) << "Exception in SaiBcmPlatform::getHwConfig: " << e.what()
                 << ", falling back to bcm.yamlConfig()";
      yamlConfig = *(config()->thrift.platform()->chip()->bcm()->yamlConfig());
    }
    if (!yamlConfig.empty()) {
      // For HSDK only yamlConfig reaches the SDK, so SOC properties must be
      // injected into the bcm_device.<n>.global section of the yaml.
      if (supportsDynamicBcmConfig() || !socProperties.empty()) {
        BcmYamlConfig bcmYamlConfig;
        bcmYamlConfig.setBaseConfig(yamlConfig);
        if (supportsDynamicBcmConfig()) {
          auto ports = config()->thrift.sw()->ports().value();
          bcmYamlConfig.modifyCoreMaps(
              getPlatformMapping()->getCorePinMapping(ports));
        }
        for (const auto& [name, value] : socProperties) {
          bcmYamlConfig.setGlobalProperty(name, value);
        }
        return bcmYamlConfig.getConfig();
      }
      return yamlConfig;
    }
    throw FbossError("Failed to get bcm yaml config from agent config");
  }
  try {
    std::unordered_map<std::string, std::string> overrides;
    if (!FLAGS_detect_wrong_fabric_connections) {
      overrides.insert({"fabric_wrong_connectivity_protection_en", "0"});
    }
    if (!FLAGS_enable_balanced_input_mode) {
      overrides.insert({"fabric_load_balancing_mode", "NORMAL_LOAD_BALANCE"});
    }
    auto hwConfig = getHwAsicConfig(overrides);
    // getHwAsicConfig only substitutes values for keys already present in the
    // base asic config and never inserts new ones. The SOC log-file properties
    // are debug knobs that are never part of the asic config, so append them
    // (and record them in hwConfig_ so getHwConfigValue stays consistent with
    // the other config paths).
    for (const auto& [name, value] : socProperties) {
      hwConfig += folly::to<std::string>('\n', name, '=', value);
      hwConfig_.insert_or_assign(name, value);
    }
    return hwConfig;
  } catch (const FbossError&) {
    /*
     * (TODO): Once asic config v2 is rolled out to the fleet, we
     * should remove this fallback and always use the config v2
     */
    auto& cfg = *config()->thrift.platform()->chip()->bcm()->config();
    std::vector<std::string> nameValStrs;
    for (const auto& entry : cfg) {
      // Skip keys we override below so the emitted string and hwConfig_ carry
      // no duplicates even if the base cfg already contained them.
      if (!socProperties.empty() &&
          (entry.first == kSaiPreinitCmdFile ||
           entry.first == kSaiPostinitCmdFile)) {
        continue;
      }
      nameValStrs.emplace_back(
          folly::to<std::string>(entry.first, '=', entry.second));
      hwConfig_.emplace(entry.first, entry.second);
    }
    for (const auto& [name, value] : socProperties) {
      nameValStrs.emplace_back(folly::to<std::string>(name, '=', value));
      hwConfig_.insert_or_assign(name, value);
    }
    auto hwConfig = folly::join('\n', nameValStrs);
    return hwConfig;
  }
}

std::vector<PortID> SaiBcmPlatform::getAllPortsInGroup(PortID portID) const {
  std::vector<PortID> allPortsinGroup;
  if (const auto& platformPorts = getPlatformPorts(); !platformPorts.empty()) {
    const auto& portList =
        utility::getPlatformPortsByControllingPort(platformPorts, portID);
    for (const auto& port : portList) {
      allPortsinGroup.emplace_back(*port.mapping()->id());
    }
  }
  return allPortsinGroup;
}

const char* SaiBcmPlatform::getHwConfigValue(const std::string& key) const {
  auto it = hwConfig_.find(key);
  return it == hwConfig_.end() ? nullptr : it->second.c_str();
}

std::optional<sai_port_interface_type_t> SaiBcmPlatform::getInterfaceType(
    TransmitterTechnology transmitterTech,
    cfg::PortSpeed speed) const {
  if (!getAsic()->isSupported(HwAsic::Feature::PORT_INTERFACE_TYPE)) {
    return std::nullopt;
  }
#if defined(IS_OSS)
  return std::nullopt;
#else
  static std::map<
      cfg::PortSpeed,
      std::map<TransmitterTechnology, sai_port_interface_type_t>>
      kSpeedAndMediaType2InterfaceType = {
          {cfg::PortSpeed::HUNDREDG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR4},
            {TransmitterTechnology::OPTICAL, SAI_PORT_INTERFACE_TYPE_CAUI},
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_CAUI}}},
          {cfg::PortSpeed::FIFTYG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR2},
            {TransmitterTechnology::OPTICAL, SAI_PORT_INTERFACE_TYPE_CAUI},
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_CR2}}},
          {cfg::PortSpeed::FORTYG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR4},
            {TransmitterTechnology::OPTICAL, SAI_PORT_INTERFACE_TYPE_XLAUI},
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_XLAUI}}},
          {cfg::PortSpeed::TWENTYFIVEG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR},
            {TransmitterTechnology::OPTICAL, SAI_PORT_INTERFACE_TYPE_CAUI},
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_CR}}},
          {cfg::PortSpeed::TWENTYG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR},
            // We don't expect 20G optics
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_CR}}},
          {cfg::PortSpeed::XG,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_CR},
            {TransmitterTechnology::OPTICAL, SAI_PORT_INTERFACE_TYPE_SFI},
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_CR}}},
          {cfg::PortSpeed::GIGE,
           {{TransmitterTechnology::COPPER, SAI_PORT_INTERFACE_TYPE_GMII},
            // We don't expect 1G optics
            // What to default to
            {TransmitterTechnology::UNKNOWN, SAI_PORT_INTERFACE_TYPE_GMII}}}};
  auto mediaType2InterfaceTypeIter =
      kSpeedAndMediaType2InterfaceType.find(speed);
  if (mediaType2InterfaceTypeIter == kSpeedAndMediaType2InterfaceType.end()) {
    throw FbossError(
        "unsupported speed for interface type retrieval : ", speed);
  }
  auto interfaceTypeIter =
      mediaType2InterfaceTypeIter->second.find(transmitterTech);
  if (interfaceTypeIter == mediaType2InterfaceTypeIter->second.end()) {
    throw FbossError(
        "unsupported media type for interface type retrieval : ",
        transmitterTech);
  }
  return interfaceTypeIter->second;
#endif
}

bool SaiBcmPlatform::needPortVcoChange(
    cfg::PortSpeed oldSpeed,
    phy::FecMode oldFec,
    cfg::PortSpeed newSpeed,
    phy::FecMode newFec) const {
  return getPortVcoFrequency(oldSpeed, oldFec) !=
      getPortVcoFrequency(newSpeed, newFec);
}

phy::VCOFrequency SaiBcmPlatform::getPortVcoFrequency(
    cfg::PortSpeed speed,
    phy::FecMode fec) const {
  switch (speed) {
    case cfg::PortSpeed::FOURHUNDREDG:
      [[fallthrough]];
    case cfg::PortSpeed::TWOHUNDREDANDTWELVEPOINTFIVEG:
      [[fallthrough]];
    case cfg::PortSpeed::TWOHUNDREDG:
      return phy::VCOFrequency::VCO_26_5625GHZ;
    case cfg::PortSpeed::HUNDREDG:
      switch (fec) {
        case phy::FecMode::RS544:
          [[fallthrough]];
        case phy::FecMode::RS544_2N:
          return phy::VCOFrequency::VCO_26_5625GHZ;
        case phy::FecMode::NONE:
          [[fallthrough]];
        case phy::FecMode::CL74:
          [[fallthrough]];
        case phy::FecMode::CL91:
          [[fallthrough]];
        case phy::FecMode::RS545:
          [[fallthrough]];
        case phy::FecMode::RS528:
          return phy::VCOFrequency::VCO_25_78125GHZ;
      }
    case cfg::PortSpeed::FIFTYG:
      [[fallthrough]];
    case cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG:
      [[fallthrough]];
    case cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG:
      [[fallthrough]];
    case cfg::PortSpeed::TWENTYFIVEG:
      switch (fec) {
        case phy::FecMode::RS544:
          [[fallthrough]];
        case phy::FecMode::RS544_2N:
          return phy::VCOFrequency::VCO_26_5625GHZ;
        case phy::FecMode::NONE:
          [[fallthrough]];
        case phy::FecMode::CL74:
          [[fallthrough]];
        case phy::FecMode::CL91:
          [[fallthrough]];
        case phy::FecMode::RS545:
          [[fallthrough]];
        case phy::FecMode::RS528:
          return phy::VCOFrequency::VCO_25_78125GHZ;
      }
    case cfg::PortSpeed::FORTYG:
      [[fallthrough]];
    case cfg::PortSpeed::TWENTYG:
      [[fallthrough]];
    case cfg::PortSpeed::XG:
      return phy::VCOFrequency::VCO_20_625GHZ;
    case cfg::PortSpeed::GIGE:
      [[fallthrough]];
    case cfg::PortSpeed::EIGHTHUNDREDG:
    case cfg::PortSpeed::ONEPOINTSIXT:
    case cfg::PortSpeed::THREEPOINTTWOT:
    case cfg::PortSpeed::DEFAULT:
      return phy::VCOFrequency::UNKNOWN;
  }
  return phy::VCOFrequency::UNKNOWN;
}

} // namespace facebook::fboss
