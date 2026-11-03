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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/CmdConfigProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `routing-policy <name> [<attribute> <value> ...]`, validated at
// construction. A routing-policy (bgp_policy.BgpPolicyStatement) is keyed by
// <name>; anything after the name is a policy-level attribute. Mirrors
// BgpCommunityListConfig minus the nested member level — the seq-num-keyed
// `term <seq-num>` level lands as a follow-up.
//
// Grammar (from the FBOSS proposed syntax):
//   routing-policy <name>                       (create/select policy)
//   routing-policy <name> description <string>  (policy attribute)
class BgpRoutingPolicyConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyConfig(std::vector<std::string> v);
  const std::string& policyName() const {
    return policyName_;
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
  std::string policyName_;
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// Single handler for the whole `config protocol bgp policy routing-policy`
// family; dispatch and per-attribute parsing live in the .cpp so a new tunable
// is a one-entry change. Mirrors CmdConfigProtocolBgpPolicyCommunityList.
struct CmdConfigProtocolBgpPolicyRoutingPolicyTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stops CLI11 from classifying attribute tokens as subcommands once the
    // policy name is consumed. See CmdConfigProtocolBgpNeighborTraits.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<name> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpRoutingPolicyConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyRoutingPolicy
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyRoutingPolicy,
          CmdConfigProtocolBgpPolicyRoutingPolicyTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyRoutingPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
