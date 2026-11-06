/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include <vector>
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/CmdConfigProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `prefix <prefix/len> [<attribute> <value> ...]`, validated at
// construction. An entry (routing_policy.PrefixListEntry in
// PrefixList.prefixes[]) is keyed by its base_prefix: that is the identity
// bgpd keeps (it merges entries by prefix and rejects seq_num), so the
// documented `entry <seq-num>` level is spelled `prefix <prefix/len>` and the
// prefix doubles as the entry's base_prefix. The list it belongs to is
// supplied by the parent command's args.
//
// Grammar:
//   ... prefix-list <name> prefix <prefix/len>                (create/select)
//   ... prefix-list <name> prefix <prefix/len> communities <community-string>
//   ... prefix-list <name> prefix <prefix/len> description <string>
//   ... prefix-list <name> prefix <prefix/len> match-logic EQUAL
//       (bgpd rejects NOT_EQUAL)
//   ... prefix-list <name> prefix <prefix/len> max-allowed-subnet-count <value>
//       (bgpd reads it only for golden-prefix policies)
//   ... prefix-list <name> prefix <prefix/len> prefix-len-range
//       compare-operator <EQ|GE|LE|NE|GT|LT>  (bgpd rejects RG)
//   ... prefix-list <name> prefix <prefix/len> prefix-len-range value <0-128>
//   ... prefix-list <name> prefix <prefix/len> regex <string>
class BgpPrefixListEntryConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpPrefixListEntryConfig(std::vector<std::string> v);
  const std::string& basePrefix() const {
    return basePrefix_;
  }
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string basePrefix_;
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// The entry level of the prefix-list family as its own CLI11 subcommand; the
// parent's parsed args arrive through the ancestor-args tuple. Prefix-list
// entries are nested (unlike as-path-list/community-list values) because bgpd
// reads them as structured objects, not a flat string list.
struct CmdConfigProtocolBgpPolicyPrefixListEntryTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicyPrefixList;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // The prefix level has no nested subcommands; stop CLI11's parent-chain
    // fallthrough from stealing a value token that spells `prefix` (e.g. in a
    // description).
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<prefix/len> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpPrefixListEntryConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyPrefixListEntry
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyPrefixListEntry,
          CmdConfigProtocolBgpPolicyPrefixListEntryTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyPrefixListEntryTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyPrefixListEntryTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpPrefixListConfig& listArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
