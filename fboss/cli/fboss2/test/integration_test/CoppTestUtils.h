// Copyright (c) Meta Platforms, Inc. and affiliates.

// Helpers for CoPP tests that create, edit and then delete a scratch CPU
// queue, so a live CoPP queue is never modified. Shared by ConfigCoppTest and
// DeleteCoppTest.

#pragma once

#include <folly/json/dynamic.h>
#include <optional>
#include <string>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

// The stream type to create a scratch queue with: `config copp queue`
// requires one, and the value is per-platform (XGS ships MULTICAST,
// Jericho/Chenab UNICAST). Follow the box's existing cpu queues, falling
// back to MULTICAST on a config that carries none.
inline std::string scratchCpuQueueStreamType(const folly::dynamic& config) {
  const auto& sw = config["sw"];
  if (sw.count("cpuQueues") && sw["cpuQueues"].isArray() &&
      !sw["cpuQueues"].empty()) {
    const auto& queue = sw["cpuQueues"][0];
    if (queue.count("streamType")) {
      const auto& streamType = queue["streamType"];
      return streamType.isInt()
          ? apache::thrift::util::enumNameSafe(
                static_cast<cfg::StreamType>(streamType.asInt()))
          : streamType.asString();
    }
  }
  return "MULTICAST";
}

// Return the smallest queue id in [0, 9] that has no cpuQueues entry, or
// std::nullopt when the range is fully populated.
inline std::optional<int> findUnusedCpuQueueId(const folly::dynamic& config) {
  const auto& sw = config["sw"];
  const bool hasQueues = sw.count("cpuQueues") && sw["cpuQueues"].isArray();
  for (int id = 0; id <= 9; ++id) {
    bool used = false;
    if (hasQueues) {
      for (const auto& q : sw["cpuQueues"]) {
        if (q.count("id") && q["id"].asInt() == id) {
          used = true;
          break;
        }
      }
    }
    if (!used) {
      return id;
    }
  }
  return std::nullopt;
}

} // namespace facebook::fboss
