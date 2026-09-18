/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/port_channel/member/CmdConfigPortChannelMember.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>

#include "fboss/cli/fboss2/commands/config/port_channel/CmdConfigPortChannel.h"
#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

PortChannelMemberArgs::PortChannelMemberArgs(std::vector<std::string> v)
    : BaseObjectArgType(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No member arguments provided. {}", portChannelMemberSetUsage()));
  }

  const std::string first = toLowerCopy(v[0]);
  if (first == kPortChannelMemberOpAdd || first == kPortChannelMemberOpRemove) {
    op_ = (first == kPortChannelMemberOpAdd) ? Op::ADD : Op::REMOVE;
    memberNames_.assign(v.begin() + 1, v.end());
    if (memberNames_.empty()) {
      throw std::invalid_argument(
          fmt::format(
              "'member {}' needs at least one interface name. {}",
              first,
              portChannelMemberSetUsage()));
    }
    return;
  }

  // <interface> <attribute> <value>
  memberNames_ = {v[0]};
  if (v.size() < 2) {
    throw std::invalid_argument(
        fmt::format(
            "Missing member attribute after interface '{}'. {}",
            v[0],
            portChannelMemberSetUsage()));
  }

  auto match = matchPortChannelMemberAttr(v, 1);
  if (!match) {
    // Attributes span up to two tokens; include the second token when the
    // first opens a two-token attribute group (e.g. "lacp timeout").
    std::string unknown = v[1];
    if (v.size() > 2 && isPortChannelMemberAttrStart(v[1])) {
      unknown += " " + v[2];
    }
    throw std::invalid_argument(
        fmt::format(
            "Unknown member attribute '{}'. {}",
            unknown,
            portChannelMemberSetUsage()));
  }
  op_ = Op::SET_ATTR;
  attr_ = std::move(match->first);
  const auto& ops = portChannelMemberAttrTable().find(attr_)->second;
  if (v.size() != 1 + match->second + 1) {
    throw std::invalid_argument(
        fmt::format(
            "'member <interface> {}' takes exactly one value. {}",
            attr_,
            portChannelMemberSetUsage()));
  }
  value_ = v.back();
  // Rejects a malformed value at parse time, before any config is touched.
  ops.validate(value_);
}

namespace {

// Finds a port-channel or creates one (validating that its derived key is not
// already taken). Returns (portChannel, created).
std::pair<cfg::AggregatePort*, bool> findOrCreatePortChannel(
    cfg::SwitchConfig& swConfig,
    const std::string& name) {
  cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);
  if (portChannel) {
    return {portChannel, false};
  }

  int16_t key = parsePortChannelKey(name);
  for (const auto& aggPort : *swConfig.aggregatePorts()) {
    if (*aggPort.key() == key) {
      throw std::invalid_argument(
          fmt::format(
              "Port-channel ID {} is already used by '{}'",
              key,
              *aggPort.name()));
    }
  }

  cfg::AggregatePort newPortChannel;
  newPortChannel.key() = key;
  newPortChannel.name() = name;
  newPortChannel.description() = "";
  swConfig.aggregatePorts()->push_back(std::move(newPortChannel));
  return {&swConfig.aggregatePorts()->back(), true};
}

// A leaf subcommand runs alone: attributes given to the parent command on the
// same line would be silently dropped, so reject the combination.
void rejectParentAttributes(const PortChannelConfigArgs& portChannelConfig) {
  if (portChannelConfig.hasAttributes()) {
    throw std::invalid_argument(
        "Port-channel attributes cannot be combined with the member "
        "subcommand; run them as separate commands");
  }
}

} // namespace

CmdConfigPortChannelMemberTraits::RetType
CmdConfigPortChannelMember::queryClient(
    const HostInfo& /* hostInfo */,
    const PortChannelConfigArgs& portChannelConfig,
    const ObjectArgType& memberArgs) {
  rejectParentAttributes(portChannelConfig);

  const std::string& name = portChannelConfig.getName();
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  if (memberArgs.getOp() == PortChannelMemberArgs::Op::ADD) {
    cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);

    // Validate every member before mutating the config, so a bad name never
    // leaves a half-created (in particular memberless) port-channel behind.
    std::vector<std::pair<std::string, int32_t>> toAdd;
    std::vector<std::string> alreadyMembers;
    // The agent binds a LAG to the ingress VLAN of its first member (0 = no
    // VLAN, a routed LAG) and assumes every member shares it; nothing
    // downstream checks, so reject a mismatch here.
    std::optional<int32_t> memberVlan = portChannel
        ? portChannelMemberVlan(swConfig, *portChannel)
        : std::nullopt;
    for (const auto& memberName : memberArgs.getMemberNames()) {
      cfg::Port* port = resolveMemberPort(memberName);
      int32_t portId = *port->logicalID();
      if (portChannel && findPortChannelMember(*portChannel, portId)) {
        alreadyMembers.push_back(memberName);
        continue;
      }
      if (auto other = portChannelForMember(swConfig, portId)) {
        throw std::invalid_argument(
            fmt::format(
                "Interface '{}' is already a member of port-channel '{}'",
                memberName,
                *other));
      }
      int32_t vlan = *port->ingressVlan();
      if (memberVlan && vlan != *memberVlan) {
        throw std::invalid_argument(
            fmt::format(
                "Interface '{}' has ingress VLAN {} but the members of "
                "port-channel '{}' have ingress VLAN {}; all members of a "
                "port-channel must share one ingress VLAN (0 for a routed "
                "port-channel)",
                memberName,
                vlan,
                name,
                *memberVlan));
      }
      memberVlan = vlan;
      bool duplicate =
          std::any_of(toAdd.begin(), toAdd.end(), [portId](const auto& entry) {
            return entry.second == portId;
          });
      if (!duplicate) {
        toAdd.emplace_back(memberName, portId);
      }
    }

    if (toAdd.empty()) {
      return fmt::format(
          "Interface(s) {} already member(s) of port-channel '{}'",
          folly::join(", ", alreadyMembers),
          name);
    }

    bool created = false;
    if (!portChannel) {
      std::tie(portChannel, created) = findOrCreatePortChannel(swConfig, name);
    }

    std::vector<std::string> added;
    for (const auto& [memberName, portId] : toAdd) {
      cfg::AggregatePortMember member;
      member.memberPortID() = portId;
      member.priority() = kPortChannelDefaultMemberPriority;
      portChannel->memberPorts()->push_back(std::move(member));
      added.push_back(memberName);
    }

    session.saveConfig();

    std::string message = fmt::format(
        "Successfully added member(s) {} to port-channel '{}'",
        folly::join(", ", added),
        name);
    if (created) {
      message += fmt::format(" (port-channel '{}' created)", name);
    }
    return message;
  }

  cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);
  if (!portChannel) {
    throw std::invalid_argument(
        fmt::format("Port-channel '{}' not found", name));
  }

  if (memberArgs.getOp() == PortChannelMemberArgs::Op::REMOVE) {
    auto removed =
        removePortChannelMembers(*portChannel, memberArgs.getMemberNames());
    session.saveConfig();
    return fmt::format(
        "Successfully removed member(s) {} from port-channel '{}'",
        folly::join(", ", removed),
        name);
  }

  // SET_ATTR targets a single existing member.
  const std::string& memberName = memberArgs.getMemberName();
  int32_t portId = *resolveMemberPort(memberName)->logicalID();
  cfg::AggregatePortMember* member =
      findPortChannelMember(*portChannel, portId);
  if (!member) {
    throw std::invalid_argument(
        fmt::format(
            "Interface '{}' is not a member of port-channel '{}'; add it "
            "first with: config port-channel {} member add {}",
            memberName,
            name,
            name,
            memberName));
  }

  // The attribute is guaranteed valid: PortChannelMemberArgs' constructor
  // rejects an unknown attribute before we get here.
  auto result = portChannelMemberAttrTable()
                    .find(memberArgs.getAttr())
                    ->second.set(*member, memberArgs.getValue());

  if (!result.changed) {
    return fmt::format(
        "No changes to member '{}' of port-channel '{}': {} already set",
        memberName,
        name,
        result.applied);
  }

  session.saveConfig();

  return fmt::format(
      "Successfully set {} on member '{}' of port-channel '{}'",
      result.applied,
      memberName,
      name);
}

void CmdConfigPortChannelMember::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigPortChannelMember, CmdConfigPortChannelMemberTraits>::run();

} // namespace facebook::fboss
