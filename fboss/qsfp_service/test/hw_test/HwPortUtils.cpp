/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"

#include "fboss/agent/AgentConfig.h"

#ifndef IS_OSS
#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/lib/phy/SaiPhyManager.h"
#endif
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

namespace facebook::fboss::utility {

void fillDefaultTxSettings(PhyManager* phyManager, phy::PhyPortConfig& config) {
  auto fillSide = [&phyManager](auto&& phySideConfig) {
    for (auto& [_, laneConfig] : phySideConfig.lanes) {
      if (!laneConfig.tx) {
        laneConfig.tx = phyManager->getDefaultTxSettings();
      }
    }
  };
  fillSide(config.config.system);
  fillSide(config.config.line);
}

void verifyPhyPortConfig(
    PortID portID,
    PhyManager* phyManager,
    const phy::PhyPortConfig& expectedConfig,
    PlatformType platformMode) {
  phy::PhyPortConfig filledConfig = expectedConfig;
  // fill default TX settings in the expectedConfig
  fillDefaultTxSettings(phyManager, filledConfig);

  // Now fetch the config actually programmed to the xphy
  const auto& actualPortConfig =
      phyManager->getHwPhyPortConfig(portID, true /* readFromHw */);

  // Check speed
  EXPECT_EQ(filledConfig.profile.speed, actualPortConfig.profile.speed);
  // Check ProfileSideConfig. Due to we couldn't fetch all the config yet.
  // Just check the attribures we can get
  auto checkProfileSideConfig = [&](const phy::ProfileSideConfig& expected,
                                    const phy::ProfileSideConfig& actual) {
    EXPECT_EQ(expected.numLanes().value(), actual.numLanes().value());
    EXPECT_EQ(expected.fec().value(), actual.fec().value());
    EXPECT_EQ(expected.modulation().value(), actual.modulation().value());
    if (auto interfaceType = actual.interfaceType()) {
      EXPECT_EQ(expected.interfaceType(), interfaceType);
    }
    // TODO(joseph5wu) Need to deprecate interfaceMode
    if (auto interfaceMode = actual.interfaceMode()) {
      EXPECT_EQ(expected.interfaceMode(), interfaceMode);
    }
  };
  checkProfileSideConfig(
      filledConfig.profile.system, actualPortConfig.profile.system);
  checkProfileSideConfig(
      filledConfig.profile.line, actualPortConfig.profile.line);

  // Currently we don't return tx_settings for Elbert system yet.
  // Only check phy::ExternalPhyConfig when the actual config exists
  const auto& actualSysLanesConf = actualPortConfig.config.system.lanes;
  const auto& acutualLineLanesConf = actualPortConfig.config.line.lanes;
  // FIXME: remove Sandia check when we start reading tx settings on Sandia XPHY
  if (!actualSysLanesConf.empty() && !acutualLineLanesConf.empty() &&
      platformMode != PlatformType::PLATFORM_SANDIA) {
    EXPECT_EQ(filledConfig.config, actualPortConfig.config)
        << " Mismatch between expected and actual port config.\nExpected "
        << filledConfig.config.toDynamic()
        << "\n Actual: " << actualPortConfig.config.toDynamic();
  } else {
    XLOG(DBG2) << "Actuall hardware config doesn't have ExternalPhyConfig";
  }
}

void verifyPhyPortConnector(PortID portID, HwQsfpEnsemble* qsfpEnsemble) {
  // FIXME: remove Sandia check when Sandia XPHY supports getattr for
  // PortConnector object in SAI
  if (!qsfpEnsemble->isSaiPlatform()) {
    return;
  }
  // FIXME: [oss-fix] Remove this when linking to a SAI library is supported in
  // OSS build
#ifndef IS_OSS
  auto saiPhyManager =
      static_cast<SaiPhyManager*>(qsfpEnsemble->getPhyManager());
  // The goal here is to check whether:
  // 1) the connector is using the correct system port and line port.
  // 2) both system and line port admin state is enabled
  // Expected config from PlatformMapping
  auto* saiPortHandle = saiPhyManager->getSaiSwitch(portID)
                            ->managerTable()
                            ->portManager()
                            .getPortHandle(portID);
  auto connector = saiPortHandle->connector;
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto linePortID = portApi.getAttribute(
      connector->adapterKey(),
      SaiPortConnectorTraits::Attributes::LineSidePortId{});
  EXPECT_EQ(linePortID, saiPortHandle->port->adapterKey());

  auto systemPortID = portApi.getAttribute(
      connector->adapterKey(),
      SaiPortConnectorTraits::Attributes::SystemSidePortId{});
  EXPECT_EQ(systemPortID, saiPortHandle->sysPort->adapterKey());

  auto adminState = portApi.getAttribute(
      saiPortHandle->port->adapterKey(),
      SaiPortTraits::Attributes::AdminState{});
  EXPECT_TRUE(adminState);

  adminState = portApi.getAttribute(
      saiPortHandle->sysPort->adapterKey(),
      SaiPortTraits::Attributes::AdminState{});
  EXPECT_TRUE(adminState);
#endif
}

std::optional<TransceiverID> getTranscieverIdx(
    PortID portId,
    const HwQsfpEnsemble* ensemble) {
  const auto& platformPorts =
      ensemble->getPlatformMapping()->getPlatformPorts();
  const auto& chips = ensemble->getPlatformMapping()->getChips();
  auto tcvrIds = utility::getTransceiverIds(
      platformPorts.find(static_cast<int32_t>(portId))->second, chips);

  return tcvrIds.empty() ? std::nullopt
                         : std::make_optional<TransceiverID>(tcvrIds[0]);
}

std::vector<TransceiverID> getTransceiverIds(
    const std::vector<PortID>& ports,
    const HwQsfpEnsemble* ensemble,
    bool ignoreNotFound) {
  std::vector<TransceiverID> transceivers;
  for (auto port : ports) {
    auto tid = getTranscieverIdx(port, ensemble);
    CHECK(tid.has_value() || ignoreNotFound);
    if (tid) {
      transceivers.push_back(*tid);
    }
  }
  return transceivers;
}

// This only returns cabled ports
IphyAndXphyPorts findAvailableCabledPorts(
    HwQsfpEnsemble* hwQsfpEnsemble,
    std::optional<cfg::PortProfileID> profile) {
  std::set<PortID> xPhyPorts;
  const auto& platformPorts =
      hwQsfpEnsemble->getPlatformMapping()->getPlatformPorts();
  const auto& chips = hwQsfpEnsemble->getPlatformMapping()->getChips();
  IphyAndXphyPorts ports;
  auto cabledPorts = getCabledPortsAndProfiles(hwQsfpEnsemble);
  for (auto& platformPort : platformPorts) {
    auto cabled = cabledPorts.find(PortID(platformPort.first));
    // If not a cabled port, don't include this port
    if (cabled == cabledPorts.end()) {
      continue;
    }
    // If profile is passed in, check if it matches the cabled port
    if (profile.has_value() && cabled->second != profile.value()) {
      continue;
    }

    cfg::PortProfileID profileToUse = cabled->second;
    auto portIDAndProfile =
        std::make_pair(PortID(platformPort.first), profileToUse);
    const auto& xphy = utility::getDataPlanePhyChips(
        platformPorts.find(platformPort.first)->second,
        chips,
        phy::DataPlanePhyChipType::XPHY);
    if (!xphy.empty()) {
      ports.xphyPorts.emplace_back(portIDAndProfile);
    }
    // All ports have iphys
    ports.iphyPorts.emplace_back(portIDAndProfile);
  }
  return ports;
}

std::map<PortID, cfg::PortProfileID> getCabledPortsAndProfiles(
    const HwQsfpEnsemble* ensemble) {
  std::map<PortID, cfg::PortProfileID> cabledPorts;
  const auto* wedgeManager = ensemble->getWedgeManager();
  auto qsfpTestConfig = wedgeManager->getQsfpConfig()->thrift.qsfpTestConfig();
  CHECK(qsfpTestConfig.has_value());
  for (const auto& cabledPairs : *qsfpTestConfig->cabledPortPairs()) {
    const auto& aPortName = *cabledPairs.aPortName();
    const auto& zPortName = *cabledPairs.zPortName();
    auto aPortId =
        ensemble->getQsfpServiceHandler()->getPortIdByPortName(aPortName);
    auto zPortId =
        ensemble->getQsfpServiceHandler()->getPortIdByPortName(zPortName);
    CHECK(aPortId.has_value());
    CHECK(zPortId.has_value());
    cabledPorts[*aPortId] = *cabledPairs.profileID();
    cabledPorts[*zPortId] = *cabledPairs.profileID();
  }
  return cabledPorts;
}

std::set<PortID> getCabledPorts(const HwQsfpEnsemble* ensemble) {
  std::set<PortID> cabledPorts;
  auto portsAndProfiles = getCabledPortsAndProfiles(ensemble);
  std::transform(
      portsAndProfiles.begin(),
      portsAndProfiles.end(),
      std::inserter(cabledPorts, cabledPorts.end()),
      [](auto pair) { return pair.first; });
  return cabledPorts;
}

std::vector<std::pair<std::string, std::string>> getCabledPairs(
    const HwQsfpEnsemble* ensemble) {
  std::vector<std::pair<std::string, std::string>> cabledPairs;
  auto wedgeManager = ensemble->getWedgeManager();
  auto qsfpTestConfig = wedgeManager->getQsfpConfig()->thrift.qsfpTestConfig();
  CHECK(qsfpTestConfig.has_value());
  for (const auto& cabledTestPairs : *qsfpTestConfig->cabledPortPairs()) {
    auto& aPortName = *cabledTestPairs.aPortName();
    auto& zPortName = *cabledTestPairs.zPortName();
    cabledPairs.emplace_back(aPortName, zPortName);
  }
  return cabledPairs;
}

// Cabled pairs where both ports have a transceiver.
std::vector<std::pair<std::string, std::string>> getCabledPairsWithTransceivers(
    const HwQsfpEnsemble* ensemble) {
  std::vector<std::pair<std::string, std::string>> pairs;
  const auto& handler = ensemble->getQsfpServiceHandler();
  for (const auto& [aPortName, zPortName] : getCabledPairs(ensemble)) {
    auto aPortId = handler->getPortIdByPortName(aPortName);
    auto zPortId = handler->getPortIdByPortName(zPortName);
    if (!aPortId.has_value() || !zPortId.has_value()) {
      continue;
    }
    if (!getTranscieverIdx(*aPortId, ensemble).has_value() ||
        !getTranscieverIdx(*zPortId, ensemble).has_value()) {
      continue;
    }
    pairs.emplace_back(aPortName, zPortName);
  }
  return pairs;
}

std::vector<TransceiverID> getCabledPortTranceivers(
    const HwQsfpEnsemble* ensemble) {
  std::unordered_set<TransceiverID> transceivers;
  // There could be multiple ports in a single transceiver. Therefore use a set
  // to get unique cabled transceivers.
  // Note: Some ports (e.g., backplane ports with XPHY) don't have transceivers,
  // so we skip them.
  for (auto port : getCabledPorts(ensemble)) {
    auto tcvrId = getTranscieverIdx(port, ensemble);
    if (tcvrId.has_value()) {
      transceivers.insert(*tcvrId);
    }
  }
  return std::vector<TransceiverID>(transceivers.begin(), transceivers.end());
}

bool match(std::vector<TransceiverID> l, std::vector<TransceiverID> r) {
  std::sort(r.begin(), r.end());
  std::sort(l.begin(), l.end());
  return l == r;
}

bool containsSubset(
    std::vector<TransceiverID> superset,
    std::vector<TransceiverID> subset) {
  std::sort(superset.begin(), superset.end());
  std::sort(subset.begin(), subset.end());
  return std::includes(
      superset.begin(), superset.end(), subset.begin(), subset.end());
}

std::vector<int32_t> legacyTransceiverIds(
    const std::vector<TransceiverID>& ids) {
  std::vector<int32_t> legacyIds;
  std::for_each(ids.begin(), ids.end(), [&legacyIds](auto id) {
    legacyIds.push_back(id);
  });
  return legacyIds;
}

void verifyXphyPort(
    PortID portID,
    cfg::PortProfileID profileID,
    std::optional<TransceiverInfo> tcvrOpt,
    HwQsfpEnsemble* ensemble) {
  auto* phyManager = ensemble->getPhyManager();
  const auto& expectedPhyPortConfig =
      phyManager->getDesiredPhyPortConfig(portID, profileID, tcvrOpt);

  // Make sure we cached the correct port profile and speed
  EXPECT_EQ(*phyManager->getProgrammedProfile(portID), profileID);
  EXPECT_EQ(
      *phyManager->getProgrammedSpeed(portID),
      expectedPhyPortConfig.profile.speed);

  utility::verifyPhyPortConfig(
      portID,
      ensemble->getPhyManager(),
      expectedPhyPortConfig,
      ensemble->getWedgeManager()->getPlatformType());

  utility::verifyPhyPortConnector(portID, ensemble);
}
} // namespace facebook::fboss::utility
