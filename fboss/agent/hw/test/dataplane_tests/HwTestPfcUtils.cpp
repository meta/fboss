/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/dataplane_tests/HwTestPfcUtils.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/lib/platforms/PlatformMode.h"

namespace facebook::fboss::utility {

std::vector<int> kLosslessPgs(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_FUJI:
      return {2};
    default:
      //  this is for fake bcm
      return {6, 7};
  }
}

std::vector<int> kLossyPgs(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_FUJI:
      return {0, 6, 7};
    default:
      //  this is for fake bcm
      return {};
  }
}

int kPgMinLimitCells() {
  return 19;
}

int kGlobalSharedBufferCells(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_WEDGE400:
    case PlatformType::PLATFORM_WEDGE400_GRANDTETON:
    case PlatformType::PLATFORM_MINIPACK:
    case PlatformType::PLATFORM_YAMP:
      return 117436;
    // Using default value for FUJI till Buffer tuning value is finalized.
    case PlatformType::PLATFORM_FUJI:
      return 203834;
    default:
      //  this is for fake bcm
      return 115196;
  }
}
int kGlobalHeadroomBufferCells(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_FUJI:
      return 25383;
    default:
      //  this is for fake bcm
      return 12432;
  }
}
int kDownlinkPgHeadroomLimitCells(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_FUJI:
      return 2160;
    default:
      //  this is for fake bcm
      return 1156;
  }
}
int kUplinkPgHeadroomLimitCells(const HwSwitch* hwSwitch) {
  auto type = hwSwitch->getPlatform()->getType();
  switch (type) {
    case PlatformType::PLATFORM_FUJI:
      return 3184;
    default:
      //  this is for fake bcm
      return 1668;
  }
}
int kPgResumeLimitCells() {
  return 19;
}

void enablePfcConfig(
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::string& pgConfigName) {
  cfg::PortPfc pfc;
  pfc.tx() = true;
  pfc.rx() = true;
  pfc.portPgConfigName() = pgConfigName;

  for (const auto& portID : ports) {
    auto portCfg = utility::findCfgPort(cfg, portID);
    portCfg->pfc() = pfc;
  }
}

void enablePfcMapsConfig(cfg::SwitchConfig& cfg) {
  // update pfc maps
  std::map<int16_t, int16_t> tc2PgId;
  std::map<int16_t, int16_t> pfcPri2PgId;
  std::map<int16_t, int16_t> pfcPri2QueueId;
  // program defaults
  // note PORT_PG_VALUE_MAX, PFC_PRIORITY_VALUE_MAX have same value
  for (auto i = 0; i <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
       i++) {
    tc2PgId.emplace(i, i);
    pfcPri2PgId.emplace(i, i);
    pfcPri2QueueId.emplace(i, i);
  }

  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          cfg.qosPolicies()) &&
      cfg.qosPolicies()[0].qosMap().has_value()) {
    cfg.qosPolicies()[0].qosMap()->trafficClassToPgId() = std::move(tc2PgId);
    cfg.qosPolicies()[0].qosMap()->pfcPriorityToPgId() = std::move(pfcPri2PgId);
    cfg.qosPolicies()[0].qosMap()->pfcPriorityToQueueId() =
        std::move(pfcPri2QueueId);
  }
}

void createPortPgConfig(
    const int& pgId,
    const int mmuCellBytes,
    cfg::PortPgConfig& pgConfig) {
  CHECK_LE(pgId, cfg::switch_config_constants::PORT_PG_VALUE_MAX());
  pgConfig.id() = pgId;
  pgConfig.bufferPoolName() = "bufferNew";
  // provide atleast 1 cell worth of minLimit
  pgConfig.minLimitBytes() = kPgMinLimitCells() * mmuCellBytes;
  pgConfig.scalingFactor() = cfg::MMUScalingFactor::ONE;
  pgConfig.resumeOffsetBytes() = kPgResumeLimitCells() * mmuCellBytes;
}

void enablePgConfigConfig(
    cfg::SwitchConfig& cfg,
    const int mmuCellBytes,
    const HwSwitch* hwSwitch,
    const std::map<std::string, bool>& pgProfileMap) {
  std::vector<cfg::PortPgConfig> portPgConfigs;
  std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
  for (const auto& [pgProfileName, useUplinkProfile] : pgProfileMap) {
    for (const auto& pgId : kLosslessPgs(hwSwitch)) {
      cfg::PortPgConfig pgConfig;
      createPortPgConfig(pgId, mmuCellBytes, pgConfig);
      pgConfig.headroomLimitBytes() =
          (useUplinkProfile ? kUplinkPgHeadroomLimitCells(hwSwitch)
                            : kDownlinkPgHeadroomLimitCells(hwSwitch)) *
          mmuCellBytes;
      portPgConfigs.emplace_back(pgConfig);
    }
    for (const auto& pgId : kLossyPgs(hwSwitch)) {
      cfg::PortPgConfig pgConfig;
      createPortPgConfig(pgId, mmuCellBytes, pgConfig);
      portPgConfigs.emplace_back(pgConfig);
    }
    portPgConfigMap.insert({pgProfileName, portPgConfigs});
  }
  cfg.portPgConfigs() = portPgConfigMap;
}

void enableBufferPoolConfig(
    cfg::SwitchConfig& cfg,
    const int mmuCellBytes,
    const HwSwitch* hwSwitch) {
  // create buffer pool
  std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap;
  cfg::BufferPoolConfig poolConfig;
  poolConfig.sharedBytes() = kGlobalSharedBufferCells(hwSwitch) * mmuCellBytes;
  poolConfig.headroomBytes() =
      kGlobalHeadroomBufferCells(hwSwitch) * mmuCellBytes;
  bufferPoolCfgMap.insert({"bufferNew", poolConfig});
  cfg.bufferPoolConfigs() = bufferPoolCfgMap;
}

void addPfcConfig(
    cfg::SwitchConfig& cfg,
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& ports) {
  const std::string kPgConfigName = "foo";
  std::map<std::string, bool> pgProfileMap;
  const int mmuCellBytes = hwSwitch->getPlatform()->getMMUCellBytes();
  enablePfcConfig(cfg, ports, kPgConfigName);
  enablePfcMapsConfig(cfg);

  // create pg profile with specific name, also specify if we want
  // to use uplink/downlink profile for it or not
  pgProfileMap.insert({kPgConfigName, false});
  enablePgConfigConfig(cfg, mmuCellBytes, hwSwitch, pgProfileMap);
  enableBufferPoolConfig(cfg, mmuCellBytes, hwSwitch);
}

void addUplinkDownlinkPfcConfig(
    cfg::SwitchConfig& cfg,
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& uplinkPorts,
    const std::vector<PortID>& downllinkPorts) {
  const int mmuCellBytes = hwSwitch->getPlatform()->getMMUCellBytes();
  enablePfcConfig(cfg, uplinkPorts, "uplinks");
  enablePfcConfig(cfg, downllinkPorts, "downlinks");
  enablePfcMapsConfig(cfg);

  std::map<std::string, bool> pgProfileMap;
  // create pg profile with specific names, also specify if we want
  // to use uplink or downlink profiles for it
  pgProfileMap.insert({"uplinks", true});
  pgProfileMap.insert({"downlinks", false});
  enablePgConfigConfig(cfg, mmuCellBytes, hwSwitch, pgProfileMap);
  enableBufferPoolConfig(cfg, mmuCellBytes, hwSwitch);
}

}; // namespace facebook::fboss::utility
