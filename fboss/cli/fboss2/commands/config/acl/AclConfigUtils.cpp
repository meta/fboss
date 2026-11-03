/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/acl/AclConfigUtils.h"

#include <fmt/format.h>

#include <folly/String.h>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss::acl_utils {

void requireAclTableGroupMode(const cfg::AgentConfig& config) {
  const auto& args = *config.defaultCommandLineArgs();
  auto it = args.find("enable_acl_table_group");
  if (it == args.end() || it->second != "true") {
    throw std::runtime_error(
        "ACL table-groups are disabled on this device. Set "
        "defaultCommandLineArgs[\"enable_acl_table_group\"] = \"true\" in "
        "the agent config and restart the agent");
  }
}

cfg::AclTableGroup& findAclTableGroupOrThrow(
    cfg::SwitchConfig& swConfig,
    const std::string& groupName) {
  if (swConfig.aclTableGroups().has_value()) {
    auto& groups = *swConfig.aclTableGroups();
    auto it = std::find_if(
        groups.begin(), groups.end(), [&](const cfg::AclTableGroup& g) {
          return *g.name() == groupName;
        });
    if (it != groups.end()) {
      return *it;
    }
  }
  throw std::runtime_error(
      fmt::format("AclTableGroup '{}' not found", groupName));
}

bool findOrCreateAclTableGroup(
    cfg::SwitchConfig& swConfig,
    const std::string& groupName,
    cfg::AclStage stage) {
  // ensure() materializes the list when unset: creating the first group on a
  // config that has none is the bootstrap case, legal because callers have
  // already passed requireAclTableGroupMode().
  auto& groups = swConfig.aclTableGroups().ensure();

  auto it = std::find_if(
      groups.begin(), groups.end(), [&](const cfg::AclTableGroup& g) {
        return *g.name() == groupName;
      });
  if (it != groups.end()) {
    // Observed on a TH4 device: committing a stage move restarts the agent
    // and the stage comes back unchanged, so the CLI must not offer it.
    if (*it->stage() != stage) {
      throw std::runtime_error(
          fmt::format(
              "AclTableGroup '{}' is at stage {} and cannot be moved to stage "
              "{}: the agent keys table-groups by stage, so this is a teardown "
              "of one group and creation of another, which it does not apply. "
              "Delete the group and recreate it at the new stage instead",
              groupName,
              apache::thrift::util::enumNameSafe(*it->stage()),
              apache::thrift::util::enumNameSafe(stage)));
    }
    return false;
  }

  auto clash = std::find_if(
      groups.begin(), groups.end(), [&](const cfg::AclTableGroup& g) {
        return *g.stage() == stage;
      });
  if (clash != groups.end()) {
    throw std::runtime_error(
        fmt::format(
            "AclTableGroup '{}' already occupies stage {}; a second group on "
            "the same stage would silently replace it (AclTableGroupMap is "
            "keyed by stage)",
            *clash->name(),
            apache::thrift::util::enumNameSafe(stage)));
  }

  cfg::AclTableGroup fresh;
  fresh.name() = groupName;
  fresh.stage() = stage;
  groups.push_back(std::move(fresh));
  return true;
}

std::optional<std::pair<cfg::AclTable*, std::string>> findAclTable(
    cfg::SwitchConfig& swConfig,
    const std::string& tableName) {
  if (!swConfig.aclTableGroups()) {
    return std::nullopt;
  }
  for (auto& group : *swConfig.aclTableGroups()) {
    auto& tables = *group.aclTables();
    auto it =
        std::find_if(tables.begin(), tables.end(), [&](const cfg::AclTable& t) {
          return *t.name() == tableName;
        });
    if (it != tables.end()) {
      return std::make_pair(&*it, *group.name());
    }
  }
  return std::nullopt;
}

std::pair<cfg::AclTable*, std::string> resolveAclTable(
    cfg::SwitchConfig& swConfig,
    const std::optional<std::string>& tableName) {
  if (tableName.has_value()) {
    if (auto found = findAclTable(swConfig, *tableName)) {
      return *found;
    }
    throw std::runtime_error(
        fmt::format(
            "AclTable '{}' not found in any AclTableGroup", *tableName));
  }

  // Name omitted: usable only when the choice is unambiguous.
  cfg::AclTable* only = nullptr;
  std::string onlyGroup;
  std::vector<std::string> candidates;
  if (swConfig.aclTableGroups().has_value()) {
    for (auto& group : *swConfig.aclTableGroups()) {
      for (auto& table : *group.aclTables()) {
        candidates.push_back(*table.name());
        if (only == nullptr) {
          only = &table;
          onlyGroup = *group.name();
        }
      }
    }
  }
  if (candidates.empty()) {
    throw std::runtime_error("No AclTable found in any AclTableGroup");
  }
  if (candidates.size() > 1) {
    throw std::runtime_error(
        fmt::format(
            "Config has {} AclTables ({}); name the one to use",
            candidates.size(),
            folly::join(", ", candidates)));
  }
  return {only, onlyGroup};
}

std::set<std::string> ruleNamesOfTable(const cfg::AclTable& table) {
  std::set<std::string> names;
  for (const auto& entry : *table.aclEntries()) {
    names.insert(*entry.name());
  }
  return names;
}

std::size_t stripMatchersForRules(
    cfg::SwitchConfig& swConfig,
    const std::set<std::string>& ruleNames) {
  std::size_t removed = 0;
  auto strip = [&](cfg::TrafficPolicyConfig& policy) {
    auto& mtaList = *policy.matchToAction();
    auto it = std::remove_if(
        mtaList.begin(), mtaList.end(), [&](const cfg::MatchToAction& mta) {
          return ruleNames.count(*mta.matcher()) > 0;
        });
    removed += static_cast<std::size_t>(std::distance(it, mtaList.end()));
    mtaList.erase(it, mtaList.end());
  };

  if (auto dataPlane = swConfig.dataPlaneTrafficPolicy()) {
    strip(*dataPlane);
  }
  // cpuTrafficPolicy nests the policy one level deeper, and trafficPolicy is
  // optional inside it: a config may carry rxReasonToQueueOrderedList alone.
  if (auto cpu = swConfig.cpuTrafficPolicy()) {
    if (auto cpuPolicy = cpu->trafficPolicy()) {
      strip(*cpuPolicy);
    }
  }
  return removed;
}

} // namespace facebook::fboss::acl_utils
