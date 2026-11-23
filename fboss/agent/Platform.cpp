/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/Platform.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwitchInfoUtils.h"
#include "fboss/agent/Utils.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

#include <folly/logging/xlog.h>
#include <string>

namespace facebook::fboss {

Platform::Platform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : productInfo_(std::move(productInfo)),
      platformMapping_(std::move(platformMapping)),
      localMac_(localMac),
      scopeResolver_({}),
      agentDirUtil_(new AgentDirectoryUtil(
          FLAGS_volatile_state_dir,
          FLAGS_persistent_state_dir)) {}
Platform::~Platform() {}

const AgentConfig* Platform::config() {
  if (!config_) {
    return reloadConfig();
  }
  return config_.get();
}

const AgentConfig* Platform::reloadConfig() {
  auto agentConfig = AgentConfig::fromDefaultFile();
  setConfig(std::move(agentConfig));
  return config_.get();
}

void Platform::setConfig(std::unique_ptr<AgentConfig> config) {
  config_ = std::move(config);
  auto swConfig = config_->thrift.sw();
  scopeResolver_ =
      SwitchIdScopeResolver(getSwitchInfoFromConfig(&(swConfig.value())));
}

const std::map<int32_t, cfg::PlatformPortEntry>& Platform::getPlatformPorts()
    const {
  return platformMapping_->getPlatformPorts();
}

const std::optional<phy::PortProfileConfig> Platform::getPortProfileConfig(
    PlatformPortProfileConfigMatcher profileMatcher) const {
  return getPlatformMapping()->getPortProfileConfig(profileMatcher);
}

const std::optional<phy::DataPlanePhyChip> Platform::getDataPlanePhyChip(
    std::string chipName) const {
  const auto& chips = getDataPlanePhyChips();
  if (auto chip = chips.find(chipName); chip != chips.end()) {
    return chip->second;
  } else {
    return std::nullopt;
  }
}

const std::map<std::string, phy::DataPlanePhyChip>&
Platform::getDataPlanePhyChips() const {
  return platformMapping_->getChips();
}

cfg::PortSpeed Platform::getPortMaxSpeed(PortID portID) const {
  return platformMapping_->getPortMaxSpeed(portID);
}

void Platform::init(
    std::unique_ptr<AgentConfig> config,
    uint32_t hwFeaturesDesired,
    int16_t switchIndex) {
  // take ownership of the config if passed in
  setConfig(std::move(config));
  auto macStr = getPlatformAttribute(cfg::PlatformAttributes::MAC);
  const auto switchSettings = *config_->thrift.sw()->switchSettings();

  auto getSwitchInfo = [&switchSettings](int64_t switchIndex) {
    for (const auto& [switchId, switchInfo] :
         *switchSettings.switchIdToSwitchInfo()) {
      if (switchInfo.switchIndex() == switchIndex) {
        return std::make_pair(switchId, switchInfo);
      }
    }
    throw FbossError("No SwitchInfo found for switchIndex", switchIndex);
  };

  std::optional<HwAsic::FabricNodeRole> fabricNodeRole;
  int64_t switchId = 0;
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::NPU;
  if (switchSettings.switchIdToSwitchInfo()->size()) {
    auto switchIdAndInfo = getSwitchInfo(switchIndex);
    switchId = switchIdAndInfo.first;
    switchInfo = switchIdAndInfo.second;
    auto switchType = *switchInfo.switchType();
    if (switchType == cfg::SwitchType::FABRIC) {
      fabricNodeRole = HwAsic::FabricNodeRole::SINGLE_STAGE_L1;
      const auto& dsfNodesConfig = *config_->thrift.sw()->dsfNodes();
      const auto& dsfNodeConfig = dsfNodesConfig.find(switchId);
      if (dsfNodeConfig != dsfNodesConfig.end() &&
          dsfNodeConfig->second.fabricLevel().has_value()) {
        auto fabricLevel = *dsfNodeConfig->second.fabricLevel();
        if (fabricLevel == 2) {
          fabricNodeRole = HwAsic::FabricNodeRole::DUAL_STAGE_L2;
        } else if (numFabricLevels(*config_->thrift.sw()->dsfNodes()) == 2) {
          // fabric level can only be 1 or 2
          CHECK_EQ(fabricLevel, 1);
          // Dual stage, node fabric level == 1
          fabricNodeRole = HwAsic::FabricNodeRole::DUAL_STAGE_L1;
        }
      }
    }
    if (switchInfo.switchMac()) {
      macStr = *switchInfo.switchMac();
    }
  }

  // Override local mac from config if set
  if (macStr) {
    XLOG(DBG2) << " Setting platform mac to: " << macStr.value();
    localMac_ = folly::MacAddress(*macStr);
  }
  switchInfo.switchMac() = localMac_.toString();

  XLOG(DBG2) << "Initializing Platform with switch ID: " << switchId
             << " switch Index: " << switchIndex;

  setupAsic(switchId, switchInfo, fabricNodeRole);
  initImpl(hwFeaturesDesired);
  // We should always initPorts() here instead of leaving the hw/ to call
  initPorts();
}

void Platform::getProductInfo(ProductInfo& info) const {
  CHECK(productInfo_);
  productInfo_->getInfo(info);
}

PlatformType Platform::getType() const {
  CHECK(productInfo_);
  return productInfo_->getType();
}

void Platform::setOverrideTransceiverInfo(
    const TransceiverInfo& overrideTransceiverInfo) {
  // Use the template to create TransceiverInfo map based on PlatformMapping
  std::unordered_map<TransceiverID, TransceiverInfo> overrideTcvrs;
  for (const auto& port : getPlatformPorts()) {
    auto portID = PortID(port.first);
    auto platformPort = getPlatformPort(portID);
    if (auto transceiverID = platformPort->getTransceiverID(); transceiverID &&
        overrideTcvrs.find(*transceiverID) == overrideTcvrs.end()) {
      // Use the overrideTransceiverInfo_ as template to copy a new
      // TransceiverInfo with the corresponding TransceiverID
      auto tcvrInfo = TransceiverInfo(overrideTransceiverInfo);
      tcvrInfo.tcvrState()->port() = *transceiverID;
      overrideTcvrs.emplace(*transceiverID, tcvrInfo);
    }
  }
  XLOG(DBG2) << "Build override TransceiverInfo map, size="
             << overrideTcvrs.size();
  overrideTransceiverInfos_.emplace(overrideTcvrs);
}

std::optional<TransceiverInfo> Platform::getOverrideTransceiverInfo(
    PortID port) const {
  if (!overrideTransceiverInfos_) {
    return std::nullopt;
  }
  // only for test environments this will be set, to avoid querying QSFP in
  // HwTest
  if (auto tcvrID = getPlatformPort(port)->getTransceiverID()) {
    if (auto overrideTcvrInfo = overrideTransceiverInfos_->find(*tcvrID);
        overrideTcvrInfo != overrideTransceiverInfos_->end()) {
      return overrideTcvrInfo->second;
    }
  }
  return std::nullopt;
}

std::optional<std::unordered_map<TransceiverID, TransceiverInfo>>
Platform::getOverrideTransceiverInfos() const {
  return overrideTransceiverInfos_;
}

int Platform::getLaneCount(cfg::PortProfileID profile) const {
  switch (profile) {
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER:
      return 1;

    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_COPPER:
      return 2;

    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_COPPER:
      return 4;

    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_COPPER:
      return 8;

    case cfg::PortProfileID::PROFILE_DEFAULT:
      break;
  }
  return 1;
}

uint32_t Platform::getMMUCellBytes() const {
  throw FbossError("MMU Cell bytes not defined for this platform");
}

std::optional<std::string> Platform::getPlatformAttribute(
    cfg::PlatformAttributes platformAttribute) const {
  const auto& platform = *config_->thrift.platform();

  if (auto platformSettings = platform.platformSettings()) {
    auto platformIter = platformSettings->find(platformAttribute);
    if (platformIter == platformSettings->end()) {
      return std::nullopt;
    }
    return platformIter->second;
  } else {
    return std::nullopt;
  }
}

} // namespace facebook::fboss
