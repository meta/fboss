/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/copp/CoppUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace facebook::fboss {

namespace copp_queue {

int16_t parseQueueId(const std::string& s, std::string_view context) {
  int16_t parsed = 0;
  try {
    parsed = folly::to<int16_t>(s);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Queue ID ({}) must be an integer, got '{}'", context, s));
  }
  if (parsed < 0 || parsed > kMaxCpuQueueId) {
    throw std::invalid_argument(
        fmt::format(
            "Queue ID ({}) must be in [0, {}], got {}",
            context,
            kMaxCpuQueueId,
            parsed));
  }
  return parsed;
}

std::vector<cfg::PortQueue>::iterator findQueue(
    std::vector<cfg::PortQueue>& queues,
    int16_t id) {
  return std::find_if(
      queues.begin(), queues.end(), [id](const cfg::PortQueue& q) {
        return *q.id() == id;
      });
}

} // namespace copp_queue

namespace copp_reason {

std::string normalizeReason(const std::string& v) {
  std::string out;
  out.reserve(v.size());
  for (unsigned char c : v) {
    out.push_back(c == '-' ? '_' : std::toupper(c));
  }
  return out;
}

std::string validReasonNames() {
  std::vector<std::string> names;
  for (auto value : apache::thrift::TEnumTraits<cfg::PacketRxReason>::values) {
    names.push_back(apache::thrift::util::enumNameSafe(value));
  }
  return folly::join(", ", names);
}

std::vector<std::string> reasonNames() {
  std::vector<std::string> names;
  for (auto value : apache::thrift::TEnumTraits<cfg::PacketRxReason>::values) {
    auto name = apache::thrift::util::enumNameSafe(value);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
      return c == '_' ? '-' : std::tolower(c);
    });
    names.push_back(std::move(name));
  }
  return names;
}

cfg::PacketRxReason parseReason(const std::string& s) {
  cfg::PacketRxReason reason{};
  if (!apache::thrift::TEnumTraits<cfg::PacketRxReason>::findValue(
          normalizeReason(s), &reason)) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown reason name '{}'. Valid: {}", s, validReasonNames()));
  }
  return reason;
}

} // namespace copp_reason

} // namespace facebook::fboss
