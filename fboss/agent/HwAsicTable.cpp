// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/HwAsicTable.h"
#include <optional>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/Utils.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace facebook::fboss {
HwAsicTable::HwAsicTable(
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo,
    const std::optional<cfg::SdkVersion>& sdkVersion,
    const std::map<int64_t, cfg::DsfNode> switchIdToDsfNodes) {
  for (const auto& switchIdAndSwitchInfo : switchIdToSwitchInfo) {
    auto switchInfo = switchIdAndSwitchInfo.second;
    if (!switchInfo.switchMac()) {
      try {
        switchInfo.switchMac() = getLocalMacAddress().toString();
      } catch (const std::exception&) {
        // Expected when tests run without config
        switchInfo.switchMac() = folly::MacAddress().toString();
      }
    }

    std::optional<HwAsic::FabricNodeRole> fabricNodeRole{std::nullopt};
    auto switchId = switchIdAndSwitchInfo.first;
    auto switchType = *switchInfo.switchType();
    if (switchType == cfg::SwitchType::FABRIC) {
      fabricNodeRole = HwAsic::FabricNodeRole::SINGLE_STAGE_L1;
      const auto& dsfNodesConfig = switchIdToDsfNodes;
      const auto& dsfNodeConfig = dsfNodesConfig.find(switchId);
      if (dsfNodeConfig != dsfNodesConfig.end() &&
          dsfNodeConfig->second.fabricLevel().has_value()) {
        auto fabricLevel = *dsfNodeConfig->second.fabricLevel();
        if (fabricLevel == 2) {
          fabricNodeRole = HwAsic::FabricNodeRole::DUAL_STAGE_L2;
        } else if (numFabricLevels(dsfNodesConfig) == 2) {
          // fabric level can only be 1 or 2
          CHECK_EQ(fabricLevel, 1);
          // Dual stage, node fabric level == 1
          fabricNodeRole = HwAsic::FabricNodeRole::DUAL_STAGE_L1;
        }
      }
    }

    hwAsics_.emplace(
        SwitchID(switchIdAndSwitchInfo.first),
        HwAsic::makeAsic(
            switchIdAndSwitchInfo.first,
            switchInfo,
            sdkVersion,
            fabricNodeRole));
  }
}

HwAsic* HwAsicTable::getHwAsicIfImpl(SwitchID switchID) const {
  auto iter = hwAsics_.find(switchID);
  if (iter != hwAsics_.end()) {
    return iter->second.get();
  }
  return nullptr;
}

const HwAsic* HwAsicTable::getHwAsicIf(SwitchID switchID) const {
  return getHwAsicIfImpl(switchID);
}

HwAsic* HwAsicTable::getHwAsicIf(SwitchID switchID) {
  return getHwAsicIfImpl(switchID);
}

const HwAsic* HwAsicTable::getHwAsic(SwitchID switchID) const {
  auto asic = getHwAsicIfImpl(switchID);
  if (!asic) {
    throw FbossError("Unable to find asic for switch ID: ", switchID);
  }
  return asic;
}

const std::map<SwitchID, const HwAsic*> HwAsicTable::getHwAsics() const {
  std::map<SwitchID, const HwAsic*> hwAsicsMap;
  for (const auto& [id, asic] : hwAsics_) {
    hwAsicsMap.emplace(id, asic.get());
  }
  return hwAsicsMap;
}

std::unordered_set<SwitchID> HwAsicTable::getSwitchIDs() const {
  std::unordered_set<SwitchID> swIds;
  for (const auto& [swId, _] : hwAsics_) {
    swIds.insert(SwitchID(swId));
  }
  return swIds;
}

bool HwAsicTable::isFeatureSupported(SwitchID switchId, HwAsic::Feature feature)
    const {
  auto asic = getHwAsicIf(switchId);
  CHECK(asic);
  return asic->isSupported(feature);
}

bool HwAsicTable::isFeatureSupportedOnAnyAsic(HwAsic::Feature feature) const {
  for (const auto& entry : hwAsics_) {
    if (isFeatureSupported(entry.first, feature)) {
      return true;
    }
  }
  return false;
}

bool HwAsicTable::isFeatureSupportedOnAllAsic(HwAsic::Feature feature) const {
  for (const auto& entry : hwAsics_) {
    if (!isFeatureSupported(entry.first, feature)) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> HwAsicTable::asicNames() const {
  std::vector<std::string> asicNames;
  for (const auto& entry : hwAsics_) {
    asicNames.emplace_back(entry.second->getAsicTypeStr());
  }
  return asicNames;
}

std::set<cfg::StreamType> HwAsicTable::getCpuPortQueueStreamTypes() const {
  std::set<cfg::StreamType> streamTypes;
  for (const auto& switchId : getL3SwitchIds()) {
    auto asic = getHwAsicIf(switchId);
    if (asic->isSupported(HwAsic::Feature::CPU_PORT)) {
      if (streamTypes.empty()) {
        streamTypes = asic->getQueueStreamTypes(cfg::PortType::CPU_PORT);
      } else if (
          streamTypes != asic->getQueueStreamTypes(cfg::PortType::CPU_PORT)) {
        throw FbossError("Cannot support switches with different stream types");
      }
    }
  }
  if (streamTypes.empty()) {
    throw FbossError("No Switches with CPU port support present");
  }
  return streamTypes;
}

std::unordered_set<SwitchID> HwAsicTable::getSwitchIdsOfType(
    cfg::SwitchType type) const {
  std::unordered_set<SwitchID> switchIds;
  for (const auto& entry : hwAsics_) {
    if (entry.second->getSwitchType() == type) {
      switchIds.insert(entry.first);
    }
  }
  return switchIds;
}

std::unordered_set<SwitchID> HwAsicTable::getL3SwitchIds() const {
  std::unordered_set<SwitchID> switchIds;
  for (const auto switchType : {cfg::SwitchType::NPU, cfg::SwitchType::VOQ}) {
    const auto typeSwitchIds = getSwitchIdsOfType(switchType);
    switchIds.insert(typeSwitchIds.begin(), typeSwitchIds.end());
  }
  return switchIds;
}

std::vector<const HwAsic*> HwAsicTable::getL3Asics() const {
  std::vector<const HwAsic*> l3Asics;
  for (const auto& switchId : getL3SwitchIds()) {
    l3Asics.push_back(getHwAsic(switchId));
  }
  return l3Asics;
}

std::vector<const HwAsic*> HwAsicTable::getFabricAsics() const {
  auto fabricSwitchIds = getSwitchIdsOfType(cfg::SwitchType::FABRIC);
  std::vector<const HwAsic*> fabricAsics;
  fabricAsics.reserve(fabricSwitchIds.size());
  for (const auto& switchId : fabricSwitchIds) {
    fabricAsics.push_back(getHwAsic(switchId));
  }
  return fabricAsics;
}

std::vector<const HwAsic*> HwAsicTable::getVoqAsics() const {
  auto voqSwitchIds = getSwitchIdsOfType(cfg::SwitchType::VOQ);
  std::vector<const HwAsic*> voqAsics;
  voqAsics.reserve(voqSwitchIds.size());
  for (const auto& switchId : voqSwitchIds) {
    voqAsics.push_back(getHwAsic(switchId));
  }
  return voqAsics;
}

std::set<cfg::StreamType> HwAsicTable::getQueueStreamTypes(
    SwitchID switchId,
    cfg::PortType portType) const {
  auto asic = getHwAsicIf(switchId);
  if (!asic) {
    throw FbossError("Invalid switch ID for getQueueStreamTypes ", switchId);
  }
  return asic->getQueueStreamTypes(portType);
}

std::unordered_set<SwitchID> HwAsicTable::getSwitchIDs(
    HwAsic::Feature feature) const {
  std::unordered_set<SwitchID> result{};
  for (const auto& entry : hwAsics_) {
    if (isFeatureSupported(entry.first, feature)) {
      result.insert(entry.first);
    }
  }
  return result;
}

} // namespace facebook::fboss
