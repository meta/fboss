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
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/CmdDeleteProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `delete protocol bgp policy routing-policy <name>`, validated at
// construction. The name is the policy's identity, matched exactly the same
// way the config command stores it; the whole policy statement is deleted.
// Mirrors BgpCommunityListRef minus the nested member level.
class BgpRoutingPolicyRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyRef(std::vector<std::string> v);
  const std::string& policyName() const {
    return policyName_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string policyName_;
};

struct CmdDeleteProtocolBgpPolicyRoutingPolicyTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<name>");
  }
  using ObjectArgType = BgpRoutingPolicyRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPolicyRoutingPolicy
    : public CmdHandler<
          CmdDeleteProtocolBgpPolicyRoutingPolicy,
          CmdDeleteProtocolBgpPolicyRoutingPolicyTraits> {
 public:
  using ObjectArgType =
      CmdDeleteProtocolBgpPolicyRoutingPolicyTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPolicyRoutingPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
