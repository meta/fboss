/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/qos/buffer_pool/CmdConfigQosBufferPool.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <iostream>
#include <map>
#include <set>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {

const std::set<std::string> kValidAttrs = {
    "shared-bytes",
    "headroom-bytes",
    "reserved-bytes",
};

void validatePoolName(const std::string& name) {
  // Valid pool name: starts with letter, alphanumeric + underscore/hyphen,
  // 1-64 chars
  static const re2::RE2 kValidPoolNamePattern("^[a-zA-Z][a-zA-Z0-9_-]{0,63}$");
  if (!re2::RE2::FullMatch(name, kValidPoolNamePattern)) {
    throw std::invalid_argument(
        "Invalid buffer pool name: '" + name +
        "'. Name must start with a letter, contain only alphanumeric "
        "characters, underscores, or hyphens, and be 1-64 characters long.");
  }
}

int32_t parseBytes(const std::string& value) {
  try {
    int32_t bytes = folly::to<int32_t>(value);
    if (bytes < 0) {
      throw std::invalid_argument(
          fmt::format("Bytes value must be non-negative, got: {}", bytes));
    }
    return bytes;
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(fmt::format("Invalid bytes value: {}", value));
  }
}

} // namespace

BufferPoolConfig::BufferPoolConfig(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected: <name> <attr> <value> [<attr> <value> ...] where <attr> is "
        "one of: shared-bytes, headroom-bytes, reserved-bytes");
  }

  // First argument is the pool name
  name_ = v[0];
  validatePoolName(name_);
  data_.push_back(name_);

  // Remaining arguments are key-value pairs
  if (v.size() < 3) {
    throw std::invalid_argument(
        "Expected at least one attribute-value pair after pool name. "
        "Valid attributes are: shared-bytes, headroom-bytes, reserved-bytes");
  }

  if ((v.size() - 1) % 2 != 0) {
    throw std::invalid_argument(
        "Attribute-value pairs must come in pairs. Got odd number of "
        "arguments after pool name.");
  }

  for (size_t i = 1; i < v.size(); i += 2) {
    const std::string& attr = v[i];
    const std::string& value = v[i + 1];

    if (kValidAttrs.find(attr) == kValidAttrs.end()) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown attribute: '{}'. Valid attributes are: {}",
              attr,
              folly::join(", ", kValidAttrs)));
    }

    attributes_.emplace_back(attr, value);
    data_.push_back(attr);
    data_.push_back(value);
  }
}

CmdConfigQosBufferPoolTraits::RetType CmdConfigQosBufferPool::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& config) {
  const std::string& poolName = config.getName();

  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  // Get or create the bufferPoolConfigs map
  if (!switchConfig.bufferPoolConfigs()) {
    switchConfig.bufferPoolConfigs() =
        std::map<std::string, cfg::BufferPoolConfig>{};
  }

  auto& bufferPoolConfigs = *switchConfig.bufferPoolConfigs();

  // Get or create the buffer pool config
  auto it = bufferPoolConfigs.find(poolName);
  if (it == bufferPoolConfigs.end()) {
    // Create a new buffer pool config with default sharedBytes
    cfg::BufferPoolConfig newConfig;
    newConfig.sharedBytes() = 0;
    bufferPoolConfigs[poolName] = std::move(newConfig);
    it = bufferPoolConfigs.find(poolName);
  }

  cfg::BufferPoolConfig& targetConfig = it->second;

  // Process each attribute-value pair
  for (const auto& [attr, value] : config.getAttributes()) {
    int32_t bytes = parseBytes(value);
    if (attr == "shared-bytes") {
      targetConfig.sharedBytes() = bytes;
    } else if (attr == "headroom-bytes") {
      targetConfig.headroomBytes() = bytes;
    } else if (attr == "reserved-bytes") {
      targetConfig.reservedBytes() = bytes;
    }
  }

  // Save the updated config - buffer pool changes require agent restart
  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format("Successfully configured buffer-pool '{}'", poolName);
}

void CmdConfigQosBufferPool::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigQosBufferPool, CmdConfigQosBufferPoolTraits>::run();

} // namespace facebook::fboss
