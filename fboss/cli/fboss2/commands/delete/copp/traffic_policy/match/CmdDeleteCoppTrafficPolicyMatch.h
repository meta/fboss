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

#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/CmdDeleteCoppTrafficPolicy.h"

namespace facebook::fboss {

struct CmdDeleteCoppTrafficPolicyMatchTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteCoppTrafficPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "matcher_name",
           args,
           "Name of the ACL matcher entry to target in "
           "cpuTrafficPolicy.trafficPolicy.matchToAction")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = traffic_policy::MatcherName;
  using RetType = std::string;
};

class CmdDeleteCoppTrafficPolicyMatch
    : public CmdHandler<
          CmdDeleteCoppTrafficPolicyMatch,
          CmdDeleteCoppTrafficPolicyMatchTraits> {
 public:
  using ObjectArgType = CmdDeleteCoppTrafficPolicyMatchTraits::ObjectArgType;
  using RetType = CmdDeleteCoppTrafficPolicyMatchTraits::RetType;

  RetType queryClient(
      const HostInfo& /* hostInfo */,
      const ObjectArgType& /* matcherName */) {
    throw std::runtime_error(
        "Incomplete command, please use one of the subcommands");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
