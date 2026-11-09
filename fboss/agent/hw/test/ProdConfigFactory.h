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

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/test/TestEnsembleIf.h"

#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"

namespace facebook::fboss::utility {

void addOlympicQosToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch,
    bool enableStrictPriority);
void addNetworkAIQosToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch);

void addOlympicQosToConfig(
    cfg::SwitchConfig& config,
    const HwAsic* asic,
    bool enableStrictPriority);
void addNetworkAIQosToConfig(cfg::SwitchConfig& config, const HwAsic* hwSwitch);

void addLoadBalancerToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch,
    LBHash hashType);

void addMplsConfig(cfg::SwitchConfig& config);

uint16_t uplinksCountFromSwitch(const HwSwitch* hwSwitch);
uint16_t uplinksCountFromSwitch(PlatformType mode);

cfg::SwitchConfig createProdRswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai = false,
    bool enableStrictPriority = false);

cfg::SwitchConfig createProdRswConfig(
    const std::vector<const HwAsic*>& asics,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai = false,
    bool enableStrictPriority = false);

cfg::SwitchConfig createProdFswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai = false,
    bool enableStrictPriority = false);

cfg::SwitchConfig createProdFswConfig(
    const HwAsic* hwAsic,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai = false,
    bool enableStrictPriority = false);

cfg::PortSpeed getPortSpeed(const HwSwitch* hwSwitch);
cfg::PortSpeed getPortSpeed(PlatformType platformType);

cfg::SwitchConfig createProdRswMhnicConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai);

cfg::SwitchConfig createProdRswMhnicConfig(
    const HwAsicTable& table,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai);

cfg::SwitchConfig createProdRswMhnicConfig(
    const HwAsic* hwAsic,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai);

cfg::SwitchConfig createProdRtswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    const HwSwitchEnsemble* ensemble);

// Roles running mmu-lossless, whose uplink/downlink split comes from PFC
// portPgConfigName rather than ingress vlan.
enum class ProdMmuLosslessRole {
  RTSW,
  FTSW,
  STSW,
  SUSW,
};

// Approximates a prod RTSW/FTSW/STSW/SUSW config, for when no prod agent.conf
// is supplied. Ports split 1:1 for RTSW/FTSW, all-downlink for STSW/SUSW.
cfg::SwitchConfig createProdMmuLosslessRoleConfig(
    const std::vector<const HwAsic*>& asics,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    const TestEnsembleIf* ensemble,
    ProdMmuLosslessRole role,
    bool isSai);

} // namespace facebook::fboss::utility
