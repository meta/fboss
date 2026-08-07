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

#include <string>
#include <vector>
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `match from <attribute> <value> ...`, validated at construction.
// The policy and term this match belongs to are supplied by the ancestor
// commands' args.
//
// Grammar (the subset of the FBOSS proposed syntax bgpd supports):
//   ... term <seq-num> match from as-path-list <name>
//   ... term <seq-num> match from origin <IGP|EGP|INCOMPLETE>
//   ... term <seq-num> match from prefix-list <name>
//
// `from community-list`, `from local-pref`, `from med` and `from next-hop`
// are documented but not offered — bgpd either has no match case for the
// atomic type or cannot resolve the reference. See the .cpp for the detail.
class BgpRoutingPolicyTermMatchConfig
    : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyTermMatchConfig(std::vector<std::string> v);
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string attr_; // matched dispatch key (never empty; no bare create)
  std::vector<std::string> values_;
};

// The match level of a routing-policy term as its own CLI11 subcommand,
// like `action`; the policy and term args arrive through the ancestor-args
// tuple.
struct CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicyRoutingPolicyTerm;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Match has no nested subcommands; stop CLI11's parent-chain
    // fallthrough from stealing a value token that spells `match`/`action`.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "from <attribute> <value> ...");
  }
  using ObjectArgType = BgpRoutingPolicyTermMatchConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch,
          CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits::ObjectArgType;
  using RetType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpRoutingPolicyConfig& policyArgs,
      const BgpRoutingPolicyTermConfig& termArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
