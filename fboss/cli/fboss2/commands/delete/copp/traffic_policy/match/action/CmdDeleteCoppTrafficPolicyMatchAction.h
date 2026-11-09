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
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/match/CmdDeleteCoppTrafficPolicyMatch.h"

namespace facebook::fboss {

struct CmdDeleteCoppTrafficPolicyMatchActionTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteCoppTrafficPolicyMatch;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "action_type",
           args,
           "CPU-plane action to delete from the matcher entry; see "
           "`config copp traffic-policy` for the list")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = traffic_policy::MatchActionArg;
  using RetType = std::string;
};

class CmdDeleteCoppTrafficPolicyMatchAction
    : public CmdHandler<
          CmdDeleteCoppTrafficPolicyMatchAction,
          CmdDeleteCoppTrafficPolicyMatchActionTraits> {
 public:
  using ObjectArgType =
      CmdDeleteCoppTrafficPolicyMatchActionTraits::ObjectArgType;
  using RetType = CmdDeleteCoppTrafficPolicyMatchActionTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const traffic_policy::MatcherName& matcherName,
      const ObjectArgType& actionType);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
