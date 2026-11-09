// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>

#include <set>

namespace facebook::fboss::utility {

void addTrapPacketAcl(
    const HwAsic* asic,
    cfg::SwitchConfig* config,
    PortID port);
void addTrapPacketAcl(
    const HwAsic* asic,
    cfg::SwitchConfig* config,
    const std::set<PortID>& port);
// cpuQueueOnly: punt matched packets to CPU via a user defined trap only, i.e.
// without a setTc/sendToQueue action. On Broadcom the setTc action rewrites the
// forwarded packet's traffic class, which for SRv6 decap traffic clobbers the
// DSCP derived TC and forces every packet onto queue 0 (CS00012463909).
// Requires FLAGS_sai_user_defined_trap, which supplies the copy to CPU action
// that setTc would otherwise have triggered.
void addTrapPacketAcl(
    const HwAsic* asic,
    cfg::SwitchConfig* config,
    const folly::CIDRNetwork& prefix,
    cfg::ToCpuAction toCpuAction = cfg::ToCpuAction::COPY,
    bool cpuQueueOnly = false);
void addTrapPacketAcl(
    const HwAsic* asic,
    cfg::SwitchConfig* config,
    const std::set<folly::CIDRNetwork>& prefixs,
    bool cpuQueueOnly = false);
void addTrapPacketAcl(
    const HwAsic* asic,
    cfg::SwitchConfig* config,
    uint16_t l4DstPort);
void addTrapPacketAcl(cfg::SwitchConfig* config, folly::MacAddress mac);

} // namespace facebook::fboss::utility
