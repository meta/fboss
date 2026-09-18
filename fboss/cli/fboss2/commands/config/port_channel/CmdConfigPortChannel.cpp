/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/port_channel/CmdConfigPortChannel.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>

#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

PortChannelConfigArgs::PortChannelConfigArgs(const std::vector<std::string>& v)
    : utils::MultiArgsConfigType(
          utils::MultiArgsConfigType::Spec{
              portChannelAttrNameSet(),
              /* valuelessAttrs */ {},
              "port-channel",
              "attribute",
              validPortChannelAttrs()}) {
  auto names = parseTokens(v);
  if (names.size() != 1) {
    throw std::invalid_argument(
        fmt::format(
            "Expected exactly one port-channel name, got: {}",
            folly::join(", ", names)));
  }
  name_ = std::move(names[0]);
  // The name's shape (numeric suffix, length) only matters when a
  // port-channel is created (member add); existing aggregate ports may carry
  // any name.
  // Rejects malformed values at parse time, before any config is touched:
  // MultiArgsConfigType already rejected unknown attributes, so every
  // attribute has a table row.
  const auto& attrTable = portChannelAttrTable();
  for (const auto& [attr, value] : getAttributes()) {
    attrTable.find(attr)->second.validate(value);
  }
}

CmdConfigPortChannelTraits::RetType CmdConfigPortChannel::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& portChannelConfig) {
  const std::string& name = portChannelConfig.getName();
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);
  if (!portChannel) {
    // An aggregate port without members cannot be applied by the agent, so
    // creation happens when the first member is added.
    throw std::invalid_argument(
        fmt::format(
            "Port-channel '{}' does not exist. A port-channel needs at "
            "least one member; create it with: config port-channel {} "
            "member add <interface> [...]",
            name,
            name));
  }

  if (!portChannelConfig.hasAttributes()) {
    return fmt::format("Port-channel '{}' already exists", name);
  }

  // Attributes and values were validated by PortChannelConfigArgs.
  const auto& attrTable = portChannelAttrTable();
  bool changed = false;
  std::vector<std::string> applied;
  for (const auto& [attr, value] : portChannelConfig.getAttributes()) {
    auto result = attrTable.find(attr)->second.set(*portChannel, value);
    changed |= result.changed;
    applied.push_back(std::move(result.applied));
  }

  if (!changed) {
    return fmt::format(
        "No changes to port-channel '{}': {} already set",
        name,
        folly::join(", ", applied));
  }

  // Aggregate port changes are applied live through the agent's normal
  // state-delta pipeline (SaiLagManager::changeLag), so HITLESS is correct.
  session.saveConfig();

  return fmt::format(
      "Successfully configured port-channel '{}': {}",
      name,
      folly::join(", ", applied));
}

void CmdConfigPortChannel::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigPortChannel, CmdConfigPortChannelTraits>::run();

} // namespace facebook::fboss
