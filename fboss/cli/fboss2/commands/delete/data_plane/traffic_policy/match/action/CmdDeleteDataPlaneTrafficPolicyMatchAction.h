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
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/match/CmdDeleteDataPlaneTrafficPolicyMatch.h"

namespace facebook::fboss {

struct CmdDeleteDataPlaneTrafficPolicyMatchActionTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdDeleteDataPlaneTrafficPolicyMatch;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "action_type",
           args,
           "Data-plane action to delete from the matcher entry; see "
           "`config data-plane traffic-policy` for the list")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = traffic_policy::MatchActionArg;
  using RetType = std::string;
};

class CmdDeleteDataPlaneTrafficPolicyMatchAction
    : public CmdHandler<
          CmdDeleteDataPlaneTrafficPolicyMatchAction,
          CmdDeleteDataPlaneTrafficPolicyMatchActionTraits> {
 public:
  using ObjectArgType =
      CmdDeleteDataPlaneTrafficPolicyMatchActionTraits::ObjectArgType;
  using RetType = CmdDeleteDataPlaneTrafficPolicyMatchActionTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const traffic_policy::MatcherName& matcherName,
      const ObjectArgType& actionType);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
