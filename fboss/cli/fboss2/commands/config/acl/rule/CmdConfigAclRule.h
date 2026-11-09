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

#include <functional>
#include <string>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/acl/CmdConfigAcl.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Returns the description shown by `--help` for `config acl rule`. The
// list of supported <attr> values is generated from the same ordered
// list used for validation, so the three stay in sync.
std::string aclRuleConfigHelpText();

// Argument for `config acl rule <table-name> <rule-name> <attr> <value>
// [<extra>]`.
//
// <table-name> matches an AclTable inside any AclTableGroup in
//   sw.aclTableGroups (field 56). The deprecated aclTableGroup (field 45)
//   is not supported.
// <rule-name>  the AclEntry to upsert. Created with default fields (just
//              `name`) plus the supplied attribute if it doesn't already
//              exist in the matching table; otherwise the attribute is
//              applied to the existing entry.
// <attr>       must be one of the supported attributes (see AclRuleAttrs.h).
// <value>      attribute-dependent — see attribute-specific parsing in the
//              constructor. For `action`, this is the action sub-attribute
//              (permit/deny/deny-data-and-control-plane), all of which set
//              AclEntry.actionType.
// <extra>      attribute-dependent trailing token. Only valid for
//              `ttl <value> [<mask>]` (mask, defaults 0xFF).
class AclRuleConfigArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AclRuleConfigArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getTableName() const {
    return tableName_;
  }

  const std::string& getRuleName() const {
    return ruleName_;
  }

  const std::string& getAttribute() const {
    return attribute_;
  }

  const std::string& getRawValue() const {
    return rawValue_;
  }

  // Apply the parsed attribute value to the given AclEntry. Throws if the
  // rule's existing state is incompatible with the change (none of the
  // current attributes carry such a constraint, but kept as a hook).
  void applyTo(cfg::AclEntry& rule) const;

 private:
  std::string tableName_;
  std::string ruleName_;
  std::string attribute_;
  std::string rawValue_;

  // The parsed mutation, captured at parse time and replayed by applyTo().
  // Keeping parse-and-apply together (one lambda per attribute) means each
  // attribute lives in exactly one place instead of a parse chain plus a
  // parallel apply chain.
  std::function<void(cfg::AclEntry&)> applyEntryFn_;
};

struct CmdConfigAclRuleTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigAcl;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // expected_min=4 (table, rule, attr, value) is required to shield
    // attribute names against CLI11's subcommand-priority override at
    // App.hpp:_parse_subcommand: once an option's count reaches its
    // expected_min, CLI11 will route any subsequent token that matches a
    // subcommand-name anywhere up the tree (e.g. `protocol`, `vlan` —
    // siblings of `acl` under `config`) to that subcommand instead of the
    // positional, which strands the attribute value as an extra and
    // teleports the parser into the wrong subtree. expected_max=5
    // accommodates the optional ttl mask; every action form is exactly 4
    // tokens (<table> <rule> action <sub>). allow_extra_args() is needed so
    // CLI11's positional consume loop keeps eating past expected_min.
    // AclRuleConfigArgs validates exact arity per attr.
    cmd.add_option("acl_rule_config", args, aclRuleConfigHelpText())
        ->required()
        ->expected(4, 5)
        ->allow_extra_args();
  }
  using ObjectArgType = AclRuleConfigArgs;
  using RetType = std::string;
};

class CmdConfigAclRule
    : public CmdHandler<CmdConfigAclRule, CmdConfigAclRuleTraits> {
 public:
  using ObjectArgType = CmdConfigAclRuleTraits::ObjectArgType;
  using RetType = CmdConfigAclRuleTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
