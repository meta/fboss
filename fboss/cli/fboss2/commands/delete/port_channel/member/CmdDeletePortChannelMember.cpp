/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/port_channel/member/CmdDeletePortChannelMember.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>

#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/commands/delete/port_channel/CmdDeletePortChannel.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

PortChannelMemberDeleteArgs::PortChannelMemberDeleteArgs(
    std::vector<std::string> v)
    : BaseObjectArgType(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No member arguments provided. {}",
            portChannelMemberDeleteUsage()));
  }

  if (isPortChannelMemberAttrStart(v[0])) {
    throw std::invalid_argument(
        fmt::format(
            "Missing interface name before '{}'. {}",
            v[0],
            portChannelMemberDeleteUsage()));
  }

  if (v.size() < 2 || !isPortChannelMemberAttrStart(v[1])) {
    // Plain interface list => remove those members.
    for (const auto& token : v) {
      if (isPortChannelMemberAttrStart(token)) {
        throw std::invalid_argument(
            fmt::format(
                "Attribute reset takes a single interface. {}",
                portChannelMemberDeleteUsage()));
      }
    }
    op_ = Op::REMOVE;
    memberNames_ = std::move(v);
    return;
  }

  // <interface> <attribute>
  memberNames_ = {v[0]};
  auto match = matchPortChannelMemberAttr(v, 1);
  if (!match) {
    // v[1] opens an attribute group (checked above); include the group's
    // second token when present (e.g. "lacp timeout").
    std::string unknown = v.size() > 2 ? v[1] + " " + v[2] : v[1];
    throw std::invalid_argument(
        fmt::format(
            "Unknown member attribute '{}'. {}",
            unknown,
            portChannelMemberDeleteUsage()));
  }
  op_ = Op::RESET_ATTR;
  attr_ = std::move(match->first);
  if (v.size() != 1 + match->second) {
    throw std::invalid_argument(
        fmt::format(
            "'member <interface> {}' takes no value on delete. {}",
            attr_,
            portChannelMemberDeleteUsage()));
  }
}

CmdDeletePortChannelMemberTraits::RetType
CmdDeletePortChannelMember::queryClient(
    const HostInfo& /* hostInfo */,
    const PortChannelDeleteArgs& deleteConfig,
    const ObjectArgType& memberArgs) {
  if (deleteConfig.hasAttributes()) {
    throw std::invalid_argument(
        "Port-channel delete attributes cannot be combined with the member "
        "subcommand; run them as separate commands");
  }

  const std::string& name = deleteConfig.getName();
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  cfg::AggregatePort* portChannel = findPortChannel(swConfig, name);
  if (!portChannel) {
    throw std::invalid_argument(
        fmt::format("Port-channel '{}' not found", name));
  }

  if (memberArgs.getOp() == PortChannelMemberDeleteArgs::Op::REMOVE) {
    auto removed =
        removePortChannelMembers(*portChannel, memberArgs.getMemberNames());
    session.saveConfig();
    return fmt::format(
        "Removed member(s) {} from port-channel '{}'",
        folly::join(", ", removed),
        name);
  }

  const std::string& memberName = memberArgs.getMemberName();
  int32_t portId = *resolveMemberPort(memberName)->logicalID();
  cfg::AggregatePortMember* member =
      findPortChannelMember(*portChannel, portId);
  if (!member) {
    throw std::invalid_argument(
        fmt::format(
            "Interface '{}' is not a member of port-channel '{}'",
            memberName,
            name));
  }

  // The attribute is guaranteed valid: PortChannelMemberDeleteArgs'
  // constructor rejects an unknown attribute before we get here.
  auto result = portChannelMemberAttrTable()
                    .find(memberArgs.getAttr())
                    ->second.reset(*member);

  if (!result.changed) {
    return fmt::format(
        "No changes to member '{}' of port-channel '{}': {} already at "
        "default",
        memberName,
        name,
        result.applied);
  }

  session.saveConfig();

  return fmt::format(
      "Reset {} of member '{}' of port-channel '{}' to default",
      result.applied,
      memberName,
      name);
}

void CmdDeletePortChannelMember::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeletePortChannelMember, CmdDeletePortChannelMemberTraits>::run();

} // namespace facebook::fboss
