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
#include <string>
#include <string_view>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

// Shared vocabulary for the copp queue commands, used by both the
// `config` and `delete` command trees so queue-id parsing and lookup live
// in one place.
namespace copp_queue {

// CPU queue IDs are a small platform-bounded set; reject anything that is
// clearly out of range before touching the config. The actual per-ASIC cap
// is enforced by the agent (SaiHostifManager::getMaxCpuQueues) at apply
// time.
constexpr int16_t kMaxCpuQueueId = 255;

int16_t parseQueueId(const std::string& s, std::string_view context);

// Return an iterator to the cpuQueues entry with `id`, or end().
std::vector<cfg::PortQueue>::iterator findQueue(
    std::vector<cfg::PortQueue>& queues,
    int16_t id);

} // namespace copp_queue

// Shared vocabulary for the copp reason commands, used by both the `config`
// and `delete` command trees so reason-name parsing stays consistent.
namespace copp_reason {

// Normalize a user-typed reason name: uppercase + dashes->underscores, so
// that "arp", "ARP", "ttl-1", "ttl_1" all match the cfg::PacketRxReason
// enum names ("ARP", "TTL_1", ...).
std::string normalizeReason(const std::string& v);

std::string validReasonNames();

// Every cfg::PacketRxReason name in the spelling parseReason() accepts from
// the command line (lowercase, dashes), for shell completion.
std::vector<std::string> reasonNames();

cfg::PacketRxReason parseReason(const std::string& s);

} // namespace copp_reason

} // namespace facebook::fboss
