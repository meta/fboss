/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/acl/table/CmdConfigAclTable.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/acl/AclConfigUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kAclTableAttrGroup = "group";
constexpr std::string_view kAclTableAttrPriority = "priority";
} // namespace

AclTableConfigArgs::AclTableConfigArgs(std::vector<std::string> v) {
  if (v.size() != 5) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <table-name> group <group-name> priority <value>, got {} argument(s)",
            v.size()));
  }
  if (v[1] != kAclTableAttrGroup) {
    throw std::invalid_argument(
        fmt::format(
            "Expected keyword 'group' after the table name, got '{}'", v[1]));
  }
  if (v[3] != kAclTableAttrPriority) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown attribute '{}' for acl table. Valid attrs: priority",
            v[3]));
  }

  int16_t parsed = 0;
  try {
    parsed = folly::to<int16_t>(v[4]);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Value for 'priority' must be an integer, got '{}'", v[4]));
  }
  if (parsed < 0) {
    throw std::invalid_argument(
        fmt::format(
            "Value for 'priority' must be non-negative, got {}", parsed));
  }

  tableName_ = v[0];
  groupName_ = v[2];
  priority_ = parsed;
  data_ = std::move(v);
}

CmdConfigAclTableTraits::RetType CmdConfigAclTable::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();

  acl_utils::requireAclTableGroupMode(config);

  auto& group =
      acl_utils::findAclTableGroupOrThrow(swConfig, args.getGroupName());

  // Table names are resolved across all groups elsewhere (acl rule commands
  // never name a group), so the same name appearing in two groups would make
  // those lookups ambiguous. One lookup answers both questions: does the
  // table exist, and in which group.
  auto existing = acl_utils::findAclTable(swConfig, args.getTableName());
  if (existing && existing->second != args.getGroupName()) {
    throw std::runtime_error(
        fmt::format(
            "AclTable '{}' already exists in group '{}'; table names must be "
            "unique across groups",
            args.getTableName(),
            existing->second));
  }

  auto& tables = *group.aclTables();
  bool created = false;
  if (!existing) {
    // actionTypes, qualifiers and udfGroups stay empty on purpose: the agent
    // expands an empty list to the ASIC's full supported set, so a partial
    // list would silently narrow what the table can match.
    cfg::AclTable fresh;
    fresh.name() = args.getTableName();
    fresh.priority() = args.getPriority();
    tables.push_back(std::move(fresh));
    created = true;
  } else {
    existing->first->priority() = args.getPriority();
  }

  // A config reload applies it, so HITLESS -- though a priority change still
  // recreates the table (see the returned message).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  if (created) {
    return fmt::format(
        "Created acl table '{}' in group '{}' with priority {}",
        args.getTableName(),
        args.getGroupName(),
        args.getPriority());
  }
  return fmt::format(
      "Set acl table '{}' (group '{}') priority to {}; the table is torn down "
      "and rebuilt in hardware when this is committed",
      args.getTableName(),
      args.getGroupName(),
      args.getPriority());
}

void CmdConfigAclTable::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigAclTable, CmdConfigAclTableTraits>::run();

} // namespace facebook::fboss
