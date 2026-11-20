/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/phy/ExternalPhy.h"

#include "fboss/agent/FbossError.h"
#include "fboss/mdio/MdioError.h"
#include "folly/json/json.h"

#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <sstream>

namespace {
template <typename T>
folly::dynamic thriftToDynamic(const T& val) {
  return folly::parseJson(
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(val));
}

template <typename T>
folly::dynamic thriftOptToDynamic(const std::optional<T>& opt) {
  return opt.has_value() ? thriftToDynamic(opt.value()) : "null";
}
} // namespace

namespace facebook::fboss::phy {

ExternalPhyPortStats ExternalPhyPortStats::fromPhyInfo(const PhyInfo& phyInfo) {
  ExternalPhyPortStats phyStats;

  auto fillSideStatsFromInfo = [](ExternalPhyPortSideStats& sideStats,
                                  const PhySideStats& phySideStats,
                                  const PhySideState& phySideState) {
    if (auto pcsStats = phySideStats.pcs()) {
      if (auto fecStats = pcsStats->rsFec()) {
        sideStats.fecCorrectableErrors =
            folly::copy(fecStats->correctedCodewords().value());
        sideStats.fecUncorrectableErrors =
            folly::copy(fecStats->uncorrectedCodewords().value());
      }
    }

    for (auto const& [lane, phyLaneState] : *phySideState.pmd()->lanes()) {
      ExternalPhyLaneStats laneStats;

      const auto statsIt = phySideStats.pmd()->lanes()->find(lane);
      if (statsIt == phySideStats.pmd()->lanes()->end()) {
        continue;
      }
      const auto& phyLaneStats = statsIt->second;
      if (auto snrInfo = phyLaneStats.snr()) {
        laneStats.signalToNoiseRatio = *snrInfo;
      }
      laneStats.signalDetect =
          apache::thrift::get_pointer(phyLaneState.signalDetectLive());
      laneStats.cdrLock =
          apache::thrift::get_pointer(phyLaneState.cdrLockLive());

      sideStats.lanes[lane] = laneStats;
    }
  };

  if (auto sysSideStats = phyInfo.stats()->system()) {
    if (auto sysSideState = phyInfo.state()->system()) {
      fillSideStatsFromInfo(phyStats.system, *sysSideStats, *sysSideState);
    }
  }

  auto lineSideStats = phyInfo.stats()->line();
  auto lineSideState = phyInfo.state()->line();
  fillSideStatsFromInfo(phyStats.line, *lineSideStats, *lineSideState);

  return phyStats;
}

bool LaneConfig::operator==(const LaneConfig& rhs) const {
  return (polaritySwap == rhs.polaritySwap) && (tx == rhs.tx);
}

bool PhySideConfig::operator==(const PhySideConfig& rhs) const {
  return std::equal(
      lanes.begin(), lanes.end(), rhs.lanes.begin(), rhs.lanes.end());
}

bool ExternalPhyConfig::operator==(const ExternalPhyConfig& rhs) const {
  return (system == rhs.system) && (line == rhs.line);
}

bool ExternalPhyProfileConfig::operator==(
    const ExternalPhyProfileConfig& rhs) const {
  return (speed == rhs.speed) && (system == rhs.system) && (line == rhs.line);
}

ExternalPhyConfig ExternalPhyConfig::fromConfigeratorTypes(
    PortPinConfig portPinConfig,
    const std::map<int32_t, PolaritySwap>& linePolaritySwapMap) {
  ExternalPhyConfig xphyCfg;

  if (!portPinConfig.xphySys()) {
    throw MdioError("Port pin config is missing xphySys");
  }
  if (!portPinConfig.xphyLine()) {
    throw MdioError("Port pin config is missing xphyLine");
  }

  auto fillLaneConfigs =
      [](const std::vector<PinConfig>& pinConfigs,
         const std::map<int32_t, PolaritySwap>& polaritySwapMap,
         std::map<LaneID, LaneConfig>& laneConfigs) {
        for (auto pinCfg : pinConfigs) {
          LaneConfig laneCfg;
          if (pinCfg.tx()) {
            laneCfg.tx = *pinCfg.tx();
          }
          if (auto it = polaritySwapMap.find(*pinCfg.id()->lane());
              it != polaritySwapMap.end()) {
            laneCfg.polaritySwap = it->second;
          }
          laneConfigs.emplace(*pinCfg.id()->lane(), laneCfg);
        }
      };

  fillLaneConfigs(*portPinConfig.xphySys(), {}, xphyCfg.system.lanes);
  fillLaneConfigs(
      *portPinConfig.xphyLine(), linePolaritySwapMap, xphyCfg.line.lanes);

  return xphyCfg;
}

bool PhyPortConfig::operator==(const PhyPortConfig& rhs) const {
  return config == rhs.config && profile == rhs.profile;
}

bool PhyPortConfig::operator!=(const PhyPortConfig& rhs) const {
  return !(*this == rhs);
}

ExternalPhyProfileConfig ExternalPhyProfileConfig::fromPortProfileConfig(
    const PortProfileConfig& portCfg) {
  if (!portCfg.xphySystem()) {
    throw MdioError(
        "Attempted to create xphy config without xphy system settings");
  }
  if (!portCfg.xphyLine()) {
    throw MdioError(
        "Attempted to create xphy config without xphy line settings");
  }
  ExternalPhyProfileConfig xphyCfg;
  xphyCfg.speed = *portCfg.speed();
  xphyCfg.system = *portCfg.xphySystem();
  xphyCfg.line = *portCfg.xphyLine();
  return xphyCfg;
}

folly::dynamic LaneConfig::toDynamic() const {
  folly::dynamic obj = folly::dynamic::object;
  obj["polaritySwap"] = thriftOptToDynamic(polaritySwap);
  obj["tx"] = thriftOptToDynamic(tx);

  return obj;
}

folly::dynamic PhySideConfig::toDynamic() const {
  folly::dynamic elements = folly::dynamic::array;
  for (auto pair : lanes) {
    elements.push_back(
        folly::dynamic::object(
            std::to_string(pair.first), pair.second.toDynamic()));
  }

  return elements;
}

std::vector<PinConfig> PhySideConfig::getPinConfigs() const {
  std::vector<phy::PinConfig> pinCfgs;
  for (const auto& lane : lanes) {
    PinConfig pinConfig;
    pinConfig.id()->lane() = lane.first;
    if (auto tx = lane.second.tx) {
      pinConfig.tx() = *tx;
    }
    if (auto polaritySwap = lane.second.polaritySwap) {
      pinConfig.polaritySwap() = *polaritySwap;
    }
    pinCfgs.push_back(pinConfig);
  }
  return pinCfgs;
}

folly::dynamic ExternalPhyConfig::toDynamic() const {
  folly::dynamic obj = folly::dynamic::object;
  obj["system"] = system.toDynamic();
  obj["line"] = line.toDynamic();

  return obj;
}

folly::dynamic ExternalPhyProfileConfig::toDynamic() const {
  folly::dynamic obj = folly::dynamic::object;
  obj["speed"] = apache::thrift::util::enumNameSafe(speed);
  obj["system"] = thriftToDynamic(system);
  obj["line"] = thriftToDynamic(line);

  return obj;
}

folly::dynamic PhyPortConfig::toDynamic() const {
  folly::dynamic obj = folly::dynamic::object;
  obj["config"] = config.toDynamic();
  obj["profile"] = profile.toDynamic();
  return obj;
}

int PhyPortConfig::getLaneSpeedInMb(Side side) const {
  switch (side) {
    case Side::SYSTEM:
      return static_cast<int>(profile.speed) / *profile.system.numLanes();
    case Side::LINE:
      return static_cast<int>(profile.speed) / *profile.line.numLanes();
  }
  throw FbossError(
      "Unrecognized side:", apache::thrift::util::enumNameSafe(side));
}

std::string PhyIDInfo::str() const {
  return folly::to<std::string>(
      "[PIM:", pimID, ", MDIO:", controllerID, ", PHY:", phyAddr, "]");
}

std::string getLaneListStr(const std::vector<facebook::fboss::LaneID>& lanes) {
  std::stringstream ss;
  for (auto i = 0; i < lanes.size(); i++) {
    ss << static_cast<int>(lanes[i]) << (i == lanes.size() - 1 ? "" : ",");
  }
  return ss.str();
}

std::string ExternalPhy::featureName(Feature feature) {
  switch (feature) {
    case Feature::LOOPBACK:
      return "LOOPBACK";
    case Feature::MACSEC:
      return "MACSEC";
    case Feature::PRBS:
      return "PRBS";
    case Feature::PRBS_STATS:
      return "PRBS_STATS";
    case Feature::PORT_STATS:
      return "PORT_STATS";
    case Feature::PORT_INFO:
      return "PORT_INFO";
  }
  throw FbossError("Unrecognized features");
}

} // namespace facebook::fboss::phy
