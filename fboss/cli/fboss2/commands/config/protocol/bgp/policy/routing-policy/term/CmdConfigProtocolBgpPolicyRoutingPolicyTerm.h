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

#include <cstdint>
#include <string>
#include <vector>
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `term <seq-num> [<attribute> <value> ...]`, validated at
// construction. A term (bgp_policy.BgpPolicyTerm in
// BgpPolicyStatement.policy_entries[]) is keyed by <seq-num>; the policy it
// belongs to is supplied by the parent command's args. The term's action and
// match levels are their own subcommands (follow-ups), not attributes.
//
// Grammar (from the FBOSS proposed syntax):
//   ... routing-policy <name> term <seq-num>                 (create/select)
//   ... routing-policy <name> term <seq-num> description <string>
class BgpRoutingPolicyTermConfig
    : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyTermConfig(std::vector<std::string> v);
  int64_t seqNum() const {
    return seqNum_;
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
  int64_t seqNum_{0};
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// The term level of the routing-policy family as its own CLI11 subcommand,
// mirroring the sibling policy families (`as-path-list ... entry`,
// `community-list ... community`, `prefix-list ... entry`): the parent's
// parsed args arrive through the ancestor-args tuple, as in the
// `config interface ... switchport` chain.
struct CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicyRoutingPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Term has no nested subcommands; stop CLI11's parent-chain fallthrough
    // from stealing a value token that spells `term` (e.g. in a description).
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<seq-num> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpRoutingPolicyTermConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyRoutingPolicyTerm
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyRoutingPolicyTerm,
          CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpRoutingPolicyConfig& policyArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
