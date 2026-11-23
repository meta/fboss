/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/SwitchSettingsConfigUtils.h"

#include <array>
#include <string_view>
#include <utility>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"

namespace facebook::fboss::utility {
namespace {

constexpr std::array<std::string_view, 2> kLadakhOrLehIntelConnectionHandles{
    "0000:15:00=0",
    "0000:18:00=0"};
constexpr std::array<std::string_view, 2> kLadakhOrLehAmdConnectionHandles{
    "0000:03:00=0",
    "0000:04:00=0"};

bool isLadakhOrLehPlatform(std::optional<PlatformType> platformType) {
  return platformType == PlatformType::PLATFORM_LADAKH800BCLS ||
      platformType == PlatformType::PLATFORM_LEH800BCLS;
}

const std::array<std::string_view, 2>& getLadakhOrLehConnectionHandles(
    PlatformCPUType platformCPUType) {
  switch (platformCPUType) {
    case PlatformCPUType::INTEL:
      return kLadakhOrLehIntelConnectionHandles;
    case PlatformCPUType::AMD:
      return kLadakhOrLehAmdConnectionHandles;
  }
  throw FbossError(
      "Unsupported PlatformCPUType ", static_cast<int>(platformCPUType));
}

cfg::Range64 getPortIdRange(int16_t switchIndex) {
  const auto rangeMin =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  const auto rangeMax =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX();
  const auto rangeSize = rangeMax - rangeMin + 1;

  cfg::Range64 range;
  range.minimum() = rangeMin + switchIndex * rangeSize;
  range.maximum() = rangeMax + switchIndex * rangeSize;
  return range;
}

cfg::SwitchInfo makeNpuSwitchInfo(
    cfg::AsicType asicType,
    int16_t switchIndex,
    cfg::Range64 portIdRange,
    std::optional<std::string_view> connectionHandle = std::nullopt) {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::NPU;
  switchInfo.asicType() = asicType;
  switchInfo.switchIndex() = switchIndex;
  switchInfo.portIdRange() = std::move(portIdRange);
  if (connectionHandle) {
    switchInfo.connectionHandle() = *connectionHandle;
  }
  return switchInfo;
}

} // namespace

std::optional<std::string_view> getDefaultConnectionHandle(
    cfg::AsicType asicType) {
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_RAMON:
      return "0c:00";
    case cfg::AsicType::ASIC_TYPE_RAMON3:
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_JERICHO4:
      return "15:00";
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
      return "68:00";
    case cfg::AsicType::ASIC_TYPE_EBRO:
    case cfg::AsicType::ASIC_TYPE_P200:
    case cfg::AsicType::ASIC_TYPE_YUBA:
    case cfg::AsicType::ASIC_TYPE_G202X:
      return "/dev/uio0";
    default:
      return std::nullopt;
  }
}

cfg::SwitchSettings genNpuSwitchSettingsConfig(
    cfg::AsicType asicType,
    std::optional<PlatformType> platformType,
    std::optional<PlatformCPUType> platformCPUType) {
  cfg::SwitchSettings switchSettings;
  switchSettings.switchType() = cfg::SwitchType::NPU;
  switchSettings.needL2EntryForNeighbor() = true;

  if (isLadakhOrLehPlatform(platformType)) {
    const auto cpuType = platformCPUType.value_or(PlatformCPUType::INTEL);
    const auto& connectionHandles = getLadakhOrLehConnectionHandles(cpuType);
    for (int16_t switchIndex = 0;
         switchIndex < static_cast<int16_t>(connectionHandles.size());
         ++switchIndex) {
      switchSettings.switchIdToSwitchInfo()->emplace(
          switchIndex,
          makeNpuSwitchInfo(
              asicType,
              switchIndex,
              getPortIdRange(switchIndex),
              connectionHandles[switchIndex]));
    }
    return switchSettings;
  }

  switchSettings.switchIdToSwitchInfo()->emplace(
      0,
      makeNpuSwitchInfo(
          asicType,
          0,
          getPortIdRange(0),
          getDefaultConnectionHandle(asicType)));
  return switchSettings;
}

} // namespace facebook::fboss::utility
