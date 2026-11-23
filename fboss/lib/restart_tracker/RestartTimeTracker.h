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
#include <string>
/**
 * This module exports restart timing info. The main interface is the
 * mark() method, which can be called from any point in our code to
 * track when an event occurs. The trickiest part of this is that the
 * tracking actually occurs across process invocations, so we save
 * some timestamps during the shutdown path.
 *
 * NOTE: the implementation here is NOT very portable and relies on
 * the behavior of std::chrono::steady_clock using boot time as the
 * epoch. Nothing in the standard guarantees this, but most
 * implementations use boot time as the epoch in order to guarantee
 * monotonicity. An implementation that uses process_time as epoch is
 * also possible, which would break the duration calculation in this
 * module.
 */

namespace facebook::fboss {

enum class RestartEvent : uint16_t {
  SIGNAL_RECEIVED,
  SHUTDOWN,
  PARENT_PROCESS_STARTED,
  PROCESS_STARTED,
  INITIALIZED,
  CONFIGURED,
  FIB_SYNCED_BGPD,
  FIB_SYNCED_OPENR,
  VRFS_SYNCED,
};

namespace restart_time {
void init(
    const std::string& warmBootDir,
    bool warmBoot,
    const std::optional<std::string>& serviceName = std::nullopt);
void mark(RestartEvent event);
void stop();
}; // namespace restart_time

} // namespace facebook::fboss
