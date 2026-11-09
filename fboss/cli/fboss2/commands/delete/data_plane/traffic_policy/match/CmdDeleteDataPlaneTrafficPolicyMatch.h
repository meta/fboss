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

#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/CmdDeleteDataPlaneTrafficPolicy.h"

namespace facebook::fboss {

struct CmdDeleteDataPlaneTrafficPolicyMatchTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteDataPlaneTrafficPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "matcher_name",
           args,
           "Name of the ACL matcher entry to target in "
           "dataPlaneTrafficPolicy.matchToAction")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = traffic_policy::MatcherName;
  using RetType = std::string;
};

class CmdDeleteDataPlaneTrafficPolicyMatch
    : public CmdHandler<
          CmdDeleteDataPlaneTrafficPolicyMatch,
          CmdDeleteDataPlaneTrafficPolicyMatchTraits> {
 public:
  using ObjectArgType =
      CmdDeleteDataPlaneTrafficPolicyMatchTraits::ObjectArgType;
  using RetType = CmdDeleteDataPlaneTrafficPolicyMatchTraits::RetType;

  RetType queryClient(
      const HostInfo& /* hostInfo */,
      const ObjectArgType& /* matcherName */) {
    throw std::runtime_error(
        "Incomplete command, please use one of the subcommands");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
