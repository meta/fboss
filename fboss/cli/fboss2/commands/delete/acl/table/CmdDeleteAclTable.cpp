/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/acl/table/CmdDeleteAclTable.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/acl/AclConfigUtils.h"

#include <fmt/format.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

AclTableDeleteArgs::AclTableDeleteArgs(std::vector<std::string> v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format("Expected <table-name>, got {} argument(s)", v.size()));
  }
  tableName_ = v[0];
  data_ = std::move(v);
}

CmdDeleteAclTableTraits::RetType CmdDeleteAclTable::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();

  acl_utils::requireAclTableGroupMode(config);

  auto [table, groupName] =
      acl_utils::resolveAclTable(swConfig, args.getTableName());

  std::size_t totalTables = 0;
  if (swConfig.aclTableGroups().has_value()) {
    for (const auto& group : *swConfig.aclTableGroups()) {
      totalTables += group.aclTables()->size();
    }
  }
  if (totalTables == 1) {
    throw std::runtime_error(
        fmt::format(
            "AclTable '{}' is the last ACL table in the config; deleting it "
            "would drop every ACL and CoPP classification rule. Edit the "
            "agent config directly if that is intended",
            args.getTableName()));
  }

  // Deleting the rules orphans any traffic-policy matcher naming them, and
  // the agent rejects a config with a dangling matcher.
  const auto ruleNames = acl_utils::ruleNamesOfTable(*table);
  const auto entryCount = ruleNames.size();
  const auto strippedMatchers =
      acl_utils::stripMatchersForRules(swConfig, ruleNames);

  auto& tables =
      *acl_utils::findAclTableGroupOrThrow(swConfig, groupName).aclTables();
  tables.erase(
      std::remove_if(
          tables.begin(),
          tables.end(),
          [&](const cfg::AclTable& t) {
            return *t.name() == args.getTableName();
          }),
      tables.end());

  // A hardware teardown, but a config reload applies it, so HITLESS.
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Deleted acl table '{}' from group '{}' ({} rule(s), {} traffic-policy "
      "action entr(ies)); the table is torn down in hardware when this is "
      "committed",
      args.getTableName(),
      groupName,
      entryCount,
      strippedMatchers);
}

void CmdDeleteAclTable::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template void CmdHandler<CmdDeleteAclTable, CmdDeleteAclTableTraits>::run();

} // namespace facebook::fboss
