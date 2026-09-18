/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/port_channel/CmdDeletePortChannel.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <algorithm>
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

// Every port-channel attribute is a valueless delete attribute (reset to
// default); the set comes from the shared attribute table, so `config` and
// `delete` cannot drift apart.
PortChannelDeleteArgs::PortChannelDeleteArgs(const std::vector<std::string>& v)
    : utils::MultiArgsConfigType(
          utils::MultiArgsConfigType::Spec{
              portChannelAttrNameSet(),
              portChannelAttrNameSet(),
              "port-channel",
              "delete attribute",
              validPortChannelAttrs()}) {
  auto names = parseTokens(v);
  if (names.size() != 1) {
    throw std::invalid_argument(
        fmt::format(
            "Expected exactly one port-channel name, got: {}",
            folly::join(", ", names)));
  }
  name_ = std::move(names[0]);
}

CmdDeletePortChannelTraits::RetType CmdDeletePortChannel::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& deleteConfig) {
  const std::string& name = deleteConfig.getName();
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);
  if (!portChannel) {
    throw std::invalid_argument(
        fmt::format("Port-channel '{}' not found", name));
  }

  // No attributes => delete the whole port-channel. Removing an aggregate
  // port is applied live by the agent (SaiLagManager::removeLag), so HITLESS
  // is correct.
  if (!deleteConfig.hasAttributes()) {
    auto boundIntfs =
        interfacesBoundToPortChannel(swConfig, *portChannel->key());
    if (!boundIntfs.empty()) {
      throw std::invalid_argument(
          fmt::format(
              "Cannot delete port-channel '{}': still referenced by "
              "interface(s) {}. Delete the interface(s) first",
              name,
              folly::join(", ", boundIntfs)));
    }
    auto& aggregatePorts = *swConfig.aggregatePorts();
    aggregatePorts.erase(
        std::remove_if(
            aggregatePorts.begin(),
            aggregatePorts.end(),
            [&name](const cfg::AggregatePort& aggPort) {
              return *aggPort.name() == name;
            }),
        aggregatePorts.end());
    session.saveConfig();
    return fmt::format("Deleted port-channel '{}'", name);
  }

  // MultiArgsConfigType already rejected unknown attributes, so every
  // attribute has a table row.
  const auto& attrTable = portChannelAttrTable();
  bool changed = false;
  std::vector<std::string> applied;
  for (const auto& [attr, value] : deleteConfig.getAttributes()) {
    auto result = attrTable.find(attr)->second.reset(*portChannel);
    changed |= result.changed;
    applied.push_back(std::move(result.applied));
  }

  if (!changed) {
    return fmt::format(
        "No changes to port-channel '{}': {} already at default",
        name,
        folly::join(", ", applied));
  }

  session.saveConfig();

  return fmt::format(
      "Reset {} of port-channel '{}' to default",
      folly::join(", ", applied),
      name);
}

void CmdDeletePortChannel::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeletePortChannel, CmdDeletePortChannelTraits>::run();

} // namespace facebook::fboss
