/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FabricLinkMonitoringManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss::utility {

namespace {

constexpr auto kRdswPerCluster = 128;
constexpr auto kJangaRdswPerCluster = 256;

int getRdswPerCluster(std::optional<PlatformType> platformType = std::nullopt) {
  int rdswCount;
  if (platformType.has_value() &&
      platformType.value() == PlatformType::PLATFORM_JANGA800BIC) {
    rdswCount = kJangaRdswPerCluster;
  } else {
    rdswCount = kRdswPerCluster;
  }
  return rdswCount;
}

int getDsfInterfaceNodeCount(std::optional<PlatformType> platformType) {
  // Janga MTIA topology doesn't use EDSWs in production
  // It uses only 256 RDSWs in single-stage mode
  if (platformType.has_value() &&
      platformType.value() == PlatformType::PLATFORM_JANGA800BIC) {
    return getMaxRdsw(platformType); // 256 RDSWs only, no EDSWs
  }
  // Other DSF platforms use traditional RDSW + EDSW topology
  return getMaxRdsw(platformType) + getMaxEdsw();
}

} // namespace
std::optional<std::map<int64_t, cfg::DsfNode>> addRemoteIntfNodeCfg(
    const std::map<int64_t, cfg::DsfNode>& curDsfNodes,
    std::optional<int> numRemoteNodes) {
  CHECK(!curDsfNodes.empty());
  auto dsfNodes = curDsfNodes;
  const auto& firstDsfNode = dsfNodes.begin()->second;
  CHECK(!firstDsfNode.systemPortRanges()->systemPortRanges()->empty());
  CHECK(firstDsfNode.nodeMac().has_value());
  CHECK(firstDsfNode.localSystemPortOffset().has_value());
  CHECK(firstDsfNode.globalSystemPortOffset().has_value());
  CHECK(firstDsfNode.inbandPortId().has_value());
  CHECK_EQ(*firstDsfNode.switchId(), 0);
  CHECK(*firstDsfNode.type() == cfg::DsfNodeType::INTERFACE_NODE);
  cfg::SwitchInfo switchInfo;
  switchInfo.asicType() = *firstDsfNode.asicType();
  switchInfo.switchType() = cfg::SwitchType::VOQ;
  switchInfo.switchIndex() = 0;
  switchInfo.switchMac() = *firstDsfNode.nodeMac();
  switchInfo.systemPortRanges() = *firstDsfNode.systemPortRanges();
  switchInfo.localSystemPortOffset() = *firstDsfNode.localSystemPortOffset();
  switchInfo.globalSystemPortOffset() = *firstDsfNode.globalSystemPortOffset();
  switchInfo.inbandPortId() = *firstDsfNode.inbandPortId();

  auto myAsic = HwAsic::makeAsic(
      *firstDsfNode.switchId(),
      switchInfo,
      std::nullopt,
      std::nullopt /* fabricNodeRole is N/A for VOQ switches */);

  std::unique_ptr<HwAsic> dualStageRdswAsic, dualStageEdswAsic;
  // Update inbandPortId based on InterfaceNodeRole
  if (isDualStage3Q2QMode()) {
    cfg::SwitchInfo dualStageRdswInfo = switchInfo;
    dualStageRdswInfo.inbandPortId() =
        myAsic->getRecyclePortInfo(HwAsic::InterfaceNodeRole::IN_CLUSTER_NODE)
            .inbandPortId;
    dualStageRdswAsic = HwAsic::makeAsic(
        *firstDsfNode.switchId(),
        dualStageRdswInfo,
        std::nullopt,
        std::nullopt /* fabricNodeRole is N/A for VOQ switches */);

    cfg::SwitchInfo dualStageEdswInfo = switchInfo;
    dualStageEdswInfo.inbandPortId() =
        myAsic
            ->getRecyclePortInfo(
                HwAsic::InterfaceNodeRole::DUAL_STAGE_EDGE_NODE)
            .inbandPortId;
    dualStageEdswAsic = HwAsic::makeAsic(
        *firstDsfNode.switchId(),
        dualStageEdswInfo,
        std::nullopt,
        std::nullopt /* fabricNodeRole is N/A for VOQ switches */);
  }

  int numCores = myAsic->getNumCores();
  std::optional<PlatformType> platformType{*firstDsfNode.platformType()};

  CHECK(
      !numRemoteNodes.has_value() ||
      numRemoteNodes.value() < getDsfInterfaceNodeCount(platformType));
  int totalNodes = numRemoteNodes.has_value()
      ? numRemoteNodes.value() + curDsfNodes.size()
      : getDsfInterfaceNodeCount(platformType);
  auto lastDsfNode = dsfNodes.rbegin()->second;
  int remoteNodeStart = *lastDsfNode.switchId() + numCores;
  auto firstDsfNodeSysPortRanges =
      *firstDsfNode.systemPortRanges()->systemPortRanges();

  std::string testSwitchNamePrefix = "hwTestSwitch";
  for (int remoteSwitchId = remoteNodeStart;
       remoteSwitchId < totalNodes * numCores;
       remoteSwitchId += numCores) {
    std::optional<int> clusterId;
    HwAsic& hwAsic = *myAsic;
    if (isDualStage3Q2QMode()) {
      if (remoteSwitchId < getMaxRdsw(platformType) * numCores) {
        clusterId = remoteSwitchId / numCores / getRdswPerCluster(platformType);
        CHECK(dualStageRdswAsic);
        hwAsic = *dualStageRdswAsic;
        testSwitchNamePrefix = "rdsw";
      } else {
        clusterId = k2StageEdgePodClusterId;
        CHECK(dualStageEdswAsic);
        hwAsic = *dualStageEdswAsic;
        testSwitchNamePrefix = "edsw";
      }
    }

    auto remoteDsfNodeCfg = dsfNodeConfig(
        hwAsic,
        SwitchID(remoteSwitchId),
        platformType, // Pass the optional platformType variable
        clusterId,
        testSwitchNamePrefix);
    dsfNodes.insert({remoteSwitchId, remoteDsfNodeCfg});
  }
  return dsfNodes;
}

std::pair<int, cfg::DsfNode> getRemoteFabricNodeCfg(
    const std::map<int64_t, cfg::DsfNode>& curDsfNodes,
    int fabricLevel,
    int clusterId,
    cfg::AsicType asicType,
    PlatformType platformType) {
  CHECK(!curDsfNodes.empty());
  auto dsfNodes = curDsfNodes;

  auto getRemoteSwitchID = [](cfg::DsfNode& dsfNode) {
    const auto& lastNodeHwAsic = getHwAsicForAsicType(*dsfNode.asicType());
    return *dsfNode.switchId() + lastNodeHwAsic.getNumCores();
  };

  int remoteSwitchId = getRemoteSwitchID(dsfNodes.rbegin()->second);
  auto nodeName = folly::to<std::string>("fabNode", remoteSwitchId);
  cfg::DsfNode fabricNode;
  fabricNode.name() = nodeName;
  fabricNode.switchId() = remoteSwitchId;
  fabricNode.type() = cfg::DsfNodeType::FABRIC_NODE;
  fabricNode.asicType() = asicType;
  fabricNode.platformType() = platformType;
  fabricNode.clusterId() = clusterId;
  fabricNode.fabricLevel() = fabricLevel;
  return {remoteSwitchId, fabricNode};
}

cfg::DsfNode makeFabricDsfNode(
    int64_t switchId,
    const std::string& name,
    int fabricLevel,
    PlatformType platformType,
    cfg::AsicType asicType) {
  cfg::DsfNode node;
  node.switchId() = switchId;
  node.name() = name;
  node.type() = cfg::DsfNodeType::FABRIC_NODE;
  node.fabricLevel() = fabricLevel;
  node.platformType() = platformType;
  node.asicType() = asicType;
  return node;
}

void addFabricDsfNode(
    cfg::SwitchConfig& config,
    int64_t switchId,
    const std::string& name,
    int fabricLevel,
    PlatformType platformType,
    cfg::AsicType asicType) {
  (*config.dsfNodes())[switchId] =
      makeFabricDsfNode(switchId, name, fabricLevel, platformType, asicType);
}

std::map<PortID, std::pair<int64_t, int64_t>> collectFabricLinkMonitoringStats(
    FabricLinkMonitoringManager* mgr,
    const std::vector<PortID>& fabricPorts,
    size_t numPortsToCheck) {
  std::map<PortID, std::pair<int64_t, int64_t>> stats;
  if (!mgr) {
    return stats;
  }
  auto allPortStats = mgr->getAllFabricLinkMonPortStats();

  for (size_t i = 0; i < numPortsToCheck && i < fabricPorts.size(); i++) {
    const auto& portId = fabricPorts[i];
    auto it = allPortStats.find(portId);
    if (it != allPortStats.end()) {
      stats[portId] = {*it->second.txCount(), *it->second.rxCount()};
    } else {
      stats[portId] = {0, 0};
    }
  }
  return stats;
}

bool allPortsHaveFabricLinkMonitoringCounterIncrements(
    FabricLinkMonitoringManager* mgr,
    const std::vector<PortID>& fabricPorts,
    const std::map<PortID, std::pair<int64_t, int64_t>>& initialStats,
    size_t numPortsToCheck,
    int64_t minIncrement) {
  if (!mgr) {
    return false;
  }
  auto allPortStats = mgr->getAllFabricLinkMonPortStats();

  for (size_t i = 0; i < numPortsToCheck && i < fabricPorts.size(); i++) {
    auto portId = fabricPorts[i];
    auto it = initialStats.find(portId);
    CHECK(it != initialStats.end())
        << "Port " << portId << " should exist in initialStats";
    auto [initialTx, initialRx] = it->second;

    int64_t currentTx = 0, currentRx = 0;
    auto statsIt = allPortStats.find(portId);
    if (statsIt != allPortStats.end()) {
      currentTx = *statsIt->second.txCount();
      currentRx = *statsIt->second.rxCount();
    }

    int64_t txIncrement = currentTx - initialTx;
    int64_t rxIncrement = currentRx - initialRx;

    XLOG(DBG3) << "Port " << portId << ": TX increment=" << txIncrement
               << ", RX increment=" << rxIncrement;

    if (txIncrement < minIncrement || rxIncrement < minIncrement) {
      return false;
    }
  }
  return true;
}

bool allPortsHaveFabricLinkMonitoringFsdbStats(
    SwSwitch* sw,
    const std::vector<PortID>& fabricPorts,
    const std::map<PortID, std::pair<int64_t, int64_t>>& initialStats,
    size_t numPortsToCheck,
    int64_t minIncrement) {
  const auto agentStats = sw->fillFsdbStats();
  const auto state = sw->getState();
  for (size_t i = 0; i < numPortsToCheck && i < fabricPorts.size(); ++i) {
    const auto& portId = fabricPorts[i];
    const auto port = state->getPorts()->getNodeIf(portId);
    if (!port) {
      return false;
    }
    const auto it = agentStats.hwPortStats()->find(port->getName());
    if (it == agentStats.hwPortStats()->end()) {
      return false;
    }
    const auto& stats = it->second;
    auto tx = stats.fabricLinkMonitoringTxPackets_();
    auto rx = stats.fabricLinkMonitoringRxPackets_();
    auto dropped = stats.fabricLinkMonitoringDroppedPackets_();
    auto invalid = stats.fabricLinkMonitoringInvalidPayloadPackets_();
    auto noPending = stats.fabricLinkMonitoringNoPendingSeqNumPackets_();
    if (!tx.has_value() || !rx.has_value() || !dropped.has_value() ||
        !invalid.has_value() || !noPending.has_value()) {
      return false;
    }
    if (*tx < 0 || *rx < 0 || *dropped < 0 || *invalid < 0 || *noPending < 0) {
      return false;
    }
    const auto initial = initialStats.find(portId);
    if (initial == initialStats.end()) {
      XLOG(DBG3) << "Port " << portId
                 << " has no initial Fabric Link Monitoring stats";
      return false;
    }
    const auto [initialTx, initialRx] = initial->second;
    if (*tx - initialTx < minIncrement || *rx - initialRx < minIncrement) {
      return false;
    }
  }
  return true;
}

} // namespace facebook::fboss::utility
