// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/pbr/CmdShowPbr.h"

#include <fmt/format.h>
#include <folly/MapUtil.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/Table.h"

namespace facebook::fboss {

namespace {

std::string counterValue(const std::optional<int64_t>& value) {
  return value ? std::to_string(*value) : "--";
}

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
    const std::string& nhgName,
    const std::map<std::string, AclEntryThrift>& nameToPbrAclEntry,
    const std::map<std::string, int64_t>& counters) {
  cli::PbrClassEntry classEntry;
  classEntry.forwardingClass() =
      apache::thrift::util::enumNameSafe(forwardingClass);
  classEntry.redirectNextHopGroup() = nhgName;
  classEntry.aclEntry() =
      fmt::format("{}_tc{}", policyName, static_cast<int>(forwardingClass));
  auto aclEntry =
      folly::get_optional(nameToPbrAclEntry, *classEntry.aclEntry());
  if (aclEntry && aclEntry->counterName()) {
    const auto& counterName = *aclEntry->counterName();
    if (auto packets =
            folly::get_optional(counters, counterName + ".packets.sum")) {
      classEntry.packets() = *packets;
    }
    if (auto bytes =
            folly::get_optional(counters, counterName + ".bytes.sum")) {
      classEntry.bytes() = *bytes;
    }
  }
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
  std::vector<std::string> counterKeys;
  for (const auto& [_, entry] : getNameToPbrAclEntry(aclTables)) {
    if (entry.counterName()) {
      counterKeys.push_back(*entry.counterName() + ".packets.sum");
      counterKeys.push_back(*entry.counterName() + ".bytes.sum");
    }
  }
  if (counterKeys.empty()) {
    return createModel(policies, aclTables, {});
  }
  return createModel(
      policies, aclTables, utils::sumAgentFb303Counters(hostInfo, counterKeys));
}

CmdShowPbr::RetType CmdShowPbr::createModel(
    const std::vector<Policy>& policies,
    const AclTableThrift& aclTables,
    const std::map<std::string, int64_t>& counters) {
  const auto nameToPbrAclEntry = getNameToPbrAclEntry(aclTables);
  RetType model;
  for (const auto& policy : policies) {
    const auto& cbfPolicy = *policy.cbfPolicy();
    cli::PbrPolicyEntry entry;
    entry.name() = *cbfPolicy.name();
    entry.defaultNextHopGroup() = *cbfPolicy.defaultNexthopGroup();
    for (const auto& [forwardingClass, nhgName] :
         *cbfPolicy.class2NamedNextHopGroup()) {
      entry.classes()->push_back(makeClassEntry(
          *cbfPolicy.name(),
          forwardingClass,
          nhgName,
          nameToPbrAclEntry,
          counters));
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

void CmdShowPbr::printOutput(const RetType& model, std::ostream& out) {
  utils::Table table;
  table.setHeader(
      {"Policy",
       "Default NHG",
       "Programmed",
       "Class",
       "Redirect NHG",
       "ACL Entry",
       "Packets",
       "Bytes"});
  for (const auto& policy : *model.policies()) {
    const std::string programmed = *policy.programmed() ? "yes" : "no";
    if (policy.classes()->empty()) {
      table.addRow(
          {*policy.name(),
           *policy.defaultNextHopGroup(),
           programmed,
           "--",
           "--",
           "--",
           "--",
           "--"});
    }
    for (const auto& classEntry : *policy.classes()) {
      table.addRow(
          {*policy.name(),
           *policy.defaultNextHopGroup(),
           programmed,
           *classEntry.forwardingClass(),
           *classEntry.redirectNextHopGroup(),
           *classEntry.aclEntry(),
           counterValue(classEntry.packets().to_optional()),
           counterValue(classEntry.bytes().to_optional())});
    }
  }
  out << table << std::endl;
}

std::string_view CmdShowPbrTraits::description() {
  return "Displays the switch's PBR (class-based forwarding) policies: each policy's default next hop group, the next hop group each traffic class is redirected to, the PbrAclTable entry that implements it with its packet and byte counters, and whether the policy is programmed. Policies that redirect the same traffic class to the same next hop group share a counter. Programmed means every traffic class has its PbrAclTable entry, which the agent installs once a route names the policy's default next hop group. Use it to check PBR policy installation.";
}

CmdShowPbr::RetType CmdShowPbr::sampleModel() {
  RetType model;

  cli::PbrPolicyEntry policy;
  policy.name() = "lspgrp_rb01-01_dc1-dc2-gold-class";
  policy.defaultNextHopGroup() = "lspgrp_rb01-01_dc1-dc2-gold-class";
  policy.programmed() = true;
  cli::PbrClassEntry silver;
  silver.forwardingClass() = "CLASS_2";
  silver.redirectNextHopGroup() = "lspgrp_rb01-01_dc1-dc2-silver-class";
  silver.aclEntry() = "lspgrp_rb01-01_dc1-dc2-gold-class_tc2";
  silver.packets() = 1234567;
  silver.bytes() = 987654321;
  policy.classes()->push_back(silver);
  model.policies()->push_back(policy);

  return model;
}

template void CmdHandler<CmdShowPbr, CmdShowPbrTraits>::run();

} // namespace facebook::fboss
