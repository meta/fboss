/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"

/*
 * Shared lookup, creation and cleanup helpers for the `acl` command family.
 */
namespace facebook::fboss::acl_utils {

/*
 * Mutable reference to the AclTableGroup named `groupName` in
 * swConfig.aclTableGroups(); throws std::runtime_error when absent.
 */
cfg::AclTableGroup& findAclTableGroupOrThrow(
    cfg::SwitchConfig& swConfig,
    const std::string& groupName);

/*
 * Throws unless the agent config sets
 * defaultCommandLineArgs["enable_acl_table_group"] = "true". With the flag
 * off the agent never reads aclTableGroups, so anything written there is
 * silently discarded. The map only sets the flag's *default*
 * (initFlagDefaults applies it with SET_FLAGS_DEFAULT), so an explicit
 * --enable_acl_table_group on the agent command line overrides what this
 * check sees. Read-only: which mode the agent runs in is the operator's call.
 */
void requireAclTableGroupMode(const cfg::AgentConfig& config);

/*
 * Ensures the group exists at the given stage, creating it when absent --
 * including the first group on a config that has none. Returns true when
 * created. Throws on a stage move or on a stage another group already holds.
 */
bool findOrCreateAclTableGroup(
    cfg::SwitchConfig& swConfig,
    const std::string& groupName,
    cfg::AclStage stage);

/*
 * Find an AclTable by name across every AclTableGroup. Returns the table and
 * its owning group's name, or std::nullopt when absent. Non-throwing.
 */
std::optional<std::pair<cfg::AclTable*, std::string>> findAclTable(
    cfg::SwitchConfig& swConfig,
    const std::string& tableName);

/*
 * Resolve an AclTable by name across every group (names are unique across
 * groups, so callers never name the group), or resolve the single table
 * config-wide when `tableName` is unset. Throws when the name is missing, or
 * when an omitted name is ambiguous or matches nothing.
 */
std::pair<cfg::AclTable*, std::string> resolveAclTable(
    cfg::SwitchConfig& swConfig,
    const std::optional<std::string>& tableName);

// The names of every AclEntry in the table, in the shape
// stripMatchersForRules() takes.
std::set<std::string> ruleNamesOfTable(const cfg::AclTable& table);

/*
 * Remove every MatchToAction whose matcher names one of `ruleNames`, from
 * both dataPlaneTrafficPolicy and cpuTrafficPolicy.trafficPolicy. Callers
 * that delete an AclEntry must run this: the agent rejects a config whose
 * matcher references a rule that no longer exists, so a leftover fails the
 * next commit. Returns the number of matchers removed.
 */
std::size_t stripMatchersForRules(
    cfg::SwitchConfig& swConfig,
    const std::set<std::string>& ruleNames);

} // namespace facebook::fboss::acl_utils
