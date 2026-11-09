/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/types.h"
#include "fboss/lib/platforms/PlatformMode.h"

#include <optional>

namespace facebook::fboss {

class HwQsfpEnsemble;
class PhyManager;
struct AgentConfig;

namespace phy {
struct PhyPortConfig;
} // namespace phy

namespace utility {
struct IphyAndXphyPorts {
  std::vector<std::pair<PortID, cfg::PortProfileID>> xphyPorts;
  std::vector<std::pair<PortID, cfg::PortProfileID>> iphyPorts;
};
// Verify PhyPortConfig
void verifyPhyPortConfig(
    PortID portID,
    PhyManager* phyManager,
    const phy::PhyPortConfig& expectedConfig,
    PlatformType platformType);

void verifyPhyPortConnector(PortID portID, HwQsfpEnsemble* qsfpEnsemble);

void verifyXphyPort(
    PortID portID,
    cfg::PortProfileID profileID,
    std::optional<TransceiverInfo> tcvrOpt,
    HwQsfpEnsemble* ensemble);

std::optional<TransceiverID> getTranscieverIdx(
    PortID portId,
    const HwQsfpEnsemble* ensemble);
std::vector<TransceiverID> getTransceiverIds(
    const std::vector<PortID>& ports,
    const HwQsfpEnsemble* ensemble,
    bool ignoreNotFound = false);

// Find the available iphy ports and xphy ports from agent config.
// If profile is set, we will also filter the ports based on profile
// Otherwise, we're only looking for cabled ports from the config
IphyAndXphyPorts findAvailableCabledPorts(
    HwQsfpEnsemble* qsfpEnsemble,
    std::optional<cfg::PortProfileID> profile = std::nullopt);

std::map<PortID, cfg::PortProfileID> getCabledPortsAndProfiles(
    const HwQsfpEnsemble* ensemble);
std::set<PortID> getCabledPorts(const HwQsfpEnsemble* ensemble);
std::vector<std::pair<std::string, std::string>> getCabledPairs(
    const HwQsfpEnsemble* ensemble);
std::vector<std::pair<std::string, std::string>> getCabledPairsWithTransceivers(
    const HwQsfpEnsemble* ensemble);
std::vector<TransceiverID> getCabledPortTranceivers(
    const HwQsfpEnsemble* ensemble);
bool match(std::vector<TransceiverID> l, std::vector<TransceiverID> r);
bool containsSubset(
    std::vector<TransceiverID> superset,
    std::vector<TransceiverID> subset);

// Some parts of the code still use int32_t to represent TransceiverID
std::vector<int32_t> legacyTransceiverIds(
    const std::vector<TransceiverID>& ids);
} // namespace utility

} // namespace facebook::fboss
