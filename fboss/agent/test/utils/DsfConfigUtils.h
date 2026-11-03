// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/types.h"

#include <map>

namespace facebook::fboss {
class FabricLinkMonitoringManager;
class SwSwitch;
} // namespace facebook::fboss

namespace facebook::fboss::utility {

// Common constants for DSF fabric link monitoring tests
constexpr int64_t kFabricLinkMonitoringMinCounterIncrement = 3;
constexpr int kL1FabricLevel = 1;
constexpr int kL2FabricLevel = 2;
constexpr int kSwitchIdIncrement = 4;

// Returns config with remote DSF node added. If numRemoteNodes is not
// specified, it will check the asic type and use max DSF node count.
std::optional<std::map<int64_t, cfg::DsfNode>> addRemoteIntfNodeCfg(
    const std::map<int64_t, cfg::DsfNode>& curDsfNodes,
    std::optional<int> numRemoteNodes = std::nullopt);

std::pair<int, cfg::DsfNode> getRemoteFabricNodeCfg(
    const std::map<int64_t, cfg::DsfNode>& curDsfNodes,
    int fabricLevel,
    int clusterId,
    cfg::AsicType asicType,
    PlatformType platformType);

// DSF Fabric link monitoring test utilities

// Create a fabric DSF node
cfg::DsfNode makeFabricDsfNode(
    int64_t switchId,
    const std::string& name,
    int fabricLevel,
    PlatformType platformType = PlatformType::PLATFORM_MERU800BFA,
    cfg::AsicType asicType = cfg::AsicType::ASIC_TYPE_RAMON3);

// Add a fabric DSF node to the config
void addFabricDsfNode(
    cfg::SwitchConfig& config,
    int64_t switchId,
    const std::string& name,
    int fabricLevel,
    PlatformType platformType,
    cfg::AsicType asicType);

// Collect fabric link monitoring TX/RX stats for the given ports.
// Returns a map of portID -> {txCount, rxCount}
std::map<PortID, std::pair<int64_t, int64_t>> collectFabricLinkMonitoringStats(
    FabricLinkMonitoringManager* mgr,
    const std::vector<PortID>& fabricPorts,
    size_t numPortsToCheck);

// Check if all ports have incremented TX/RX counters by at least minIncrement.
// Returns true if all ports have incremented counters, false otherwise.
bool allPortsHaveFabricLinkMonitoringCounterIncrements(
    FabricLinkMonitoringManager* mgr,
    const std::vector<PortID>& fabricPorts,
    const std::map<PortID, std::pair<int64_t, int64_t>>& initialStats,
    size_t numPortsToCheck,
    int64_t minIncrement = kFabricLinkMonitoringMinCounterIncrement);

// Validates the assembled FSDB payload without requiring an FSDB server.
bool allPortsHaveFabricLinkMonitoringFsdbStats(
    SwSwitch* sw,
    const std::vector<PortID>& fabricPorts,
    const std::map<PortID, std::pair<int64_t, int64_t>>& initialStats,
    size_t numPortsToCheck,
    int64_t minIncrement = kFabricLinkMonitoringMinCounterIncrement);

} // namespace facebook::fboss::utility
