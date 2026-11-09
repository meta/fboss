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

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <folly/IPAddress.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {
class HwAsic;
namespace utility {

constexpr uint8_t kNetworkControlDscp = 48;

/*
 * Utilities for generating a basic, ASIC-aware control-plane policing config.
 * Deployment-specific policies such as role-based MPLS handling, QoS policy
 * attachment, and rollout workarounds remain outside this library.
 */

cfg::Range getRange(uint32_t minimum, uint32_t maximum);
cfg::StreamType getCpuDefaultStreamType(const HwAsic* hwAsic);
cfg::QueueScheduling getCpuDefaultQueueScheduling(const HwAsic* hwAsic);
cfg::PortQueueRate getPortQueueRate(const HwAsic* hwAsic, uint16_t queueId);
cfg::ToCpuAction getCpuActionType(const HwAsic* hwAsic);

cfg::MatchAction genCoppQueueAction(
    bool enableUserDefinedTrap,
    const HwAsic& asic,
    int queueId,
    std::optional<cfg::ToCpuAction> toCpuAction = std::nullopt);

const folly::CIDRNetwork& kIPv6LinkLocalMcastNetwork();
const folly::CIDRNetwork& kIPv6LinkLocalUcastNetwork();

cfg::AclEntry genLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork);
cfg::AclEntry genCpuPortLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork);
cfg::AclEntry genNetworkControlLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork);

// Generates the six IPv6 link-local CoPP ACLs. Consumers may select a subset,
// supply backend-specific queue actions, or add platform-specific ACLs.
std::vector<std::pair<cfg::AclEntry, cfg::MatchAction>> genLinkLocalCoppAcls(
    const HwAsic& asic,
    const cfg::MatchAction& highPriorityAction,
    const cfg::MatchAction& midPriorityAction);

// Adds high, mid, default, and low CPU queues. Unsupported and non-NPU ASICs
// receive no CPU queue configuration.
void addDefaultCpuQueueConfig(cfg::SwitchConfig& config, const HwAsic& asic);

// Adds common RX-reason mappings and a small IPv6 link-local policing policy.
// CoPP ACLs are inserted directly into the default ingress ACL table; this
// function never writes the legacy flat ACL list.
void addDefaultCpuTrafficPolicyConfig(
    cfg::SwitchConfig& config,
    const HwAsic& asic,
    bool enableUserDefinedTrap = false);

} // namespace utility
} // namespace facebook::fboss
