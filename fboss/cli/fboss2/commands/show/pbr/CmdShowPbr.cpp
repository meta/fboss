// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/pbr/CmdShowPbr.h"

#include <fmt/format.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

namespace {

std::map<std::string, AclEntryThrift> getNameToPbrAclEntry(
    const AclTableThrift& aclTables) {
  const auto& tableName = cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
  auto it = aclTables.aclTableEntries()->find(tableName);
  if (it == aclTables.aclTableEntries()->end()) {
    throw std::runtime_error(fmt::format("ACL table {} not found", tableName));
  }
  std::map<std::string, AclEntryThrift> nameToEntry;
  for (const auto& entry : it->second) {
    nameToEntry[*entry.name()] = entry;
  }
  return nameToEntry;
}

cli::PbrClassEntry makeClassEntry(
    const std::string& policyName,
    ForwardingClass forwardingClass,
    const std::string& nhgName) {
  cli::PbrClassEntry classEntry;
  classEntry.forwardingClass() =
      apache::thrift::util::enumNameSafe(forwardingClass);
  classEntry.redirectNextHopGroup() = nhgName;
  classEntry.aclEntry() =
      fmt::format("{}_tc{}", policyName, static_cast<int>(forwardingClass));
  return classEntry;
}

} // namespace

CmdShowPbr::RetType CmdShowPbr::queryClient(const HostInfo& hostInfo) {
  std::vector<Policy> policies;
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  client->sync_getAllPolicies(policies);
  if (policies.empty()) {
    return {};
  }
  AclTableThrift aclTables;
  client->sync_getAclTableGroup(aclTables);
  return createModel(policies, aclTables);
}

CmdShowPbr::RetType CmdShowPbr::createModel(
    const std::vector<Policy>& policies,
    const AclTableThrift& aclTables) {
  const auto nameToPbrAclEntry = getNameToPbrAclEntry(aclTables);
  RetType model;
  for (const auto& policy : policies) {
    const auto& cbfPolicy = *policy.cbfPolicy();
    cli::PbrPolicyEntry entry;
    entry.name() = *cbfPolicy.name();
    entry.defaultNextHopGroup() = *cbfPolicy.defaultNexthopGroup();
    for (const auto& [forwardingClass, nhgName] :
         *cbfPolicy.class2NamedNextHopGroup()) {
      entry.classes()->push_back(
          makeClassEntry(*cbfPolicy.name(), forwardingClass, nhgName));
    }
    entry.programmed() = !entry.classes()->empty() &&
        std::all_of(
            entry.classes()->begin(),
            entry.classes()->end(),
            [&](const auto& classEntry) {
              return nameToPbrAclEntry.contains(*classEntry.aclEntry());
            });
    model.policies()->push_back(std::move(entry));
  }
  return model;
}

} // namespace facebook::fboss
