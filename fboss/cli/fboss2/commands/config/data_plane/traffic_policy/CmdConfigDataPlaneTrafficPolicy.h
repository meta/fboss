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
#include "fboss/cli/fboss2/commands/config/data_plane/CmdConfigDataPlane.h"

namespace facebook::fboss {

// `config data-plane traffic-policy match <rule> action <type> [<value>]`
//
// The action lands on the MatchAction stored against <rule> in
// sw.dataPlaneTrafficPolicy.matchToAction. Parsing, validation and the apply
// itself are shared with the copp verb; see TrafficPolicyUtils.
struct CmdConfigDataPlaneTrafficPolicyTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigDataPlane;
  using ObjectArgType = traffic_policy::TrafficPolicyArgs;
  using RetType = std::string;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // required() + expected(4, 6) keeps the first four tokens as positionals
    // whatever they spell; without it CLI11 reclassifies a value that happens
    // to name a subcommand anywhere up the ancestor chain. allow_extra_args()
    // lets the optional trailing value(s) through.
    cmd.add_option(
           "data_plane_traffic_policy_config",
           args,
           traffic_policy::configHelpText())
        ->required()
        ->expected(4, 6)
        ->allow_extra_args();
  }
};

class CmdConfigDataPlaneTrafficPolicy
    : public CmdHandler<
          CmdConfigDataPlaneTrafficPolicy,
          CmdConfigDataPlaneTrafficPolicyTraits> {
 public:
  using ObjectArgType = CmdConfigDataPlaneTrafficPolicyTraits::ObjectArgType;
  using RetType = CmdConfigDataPlaneTrafficPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);
  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
