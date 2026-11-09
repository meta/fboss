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
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

// Removes the load-balancer entry matching `id` from
// swConfig.loadBalancers, returning a human-readable result string. If no
// entry with that ID is configured, returns a warning and changes nothing.
std::string removeLoadBalancer(
    cfg::SwitchConfig& swConfig,
    cfg::LoadBalancerID id);

struct CmdDeleteLoadBalancingTraits : public WriteCommandTraits {
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdDeleteLoadBalancing
    : public CmdHandler<CmdDeleteLoadBalancing, CmdDeleteLoadBalancingTraits> {
 public:
  using ObjectArgType = CmdDeleteLoadBalancingTraits::ObjectArgType;
  using RetType = CmdDeleteLoadBalancingTraits::RetType;

  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use 'ecmp' or 'lag' subcommand");
  }

  void printOutput(const RetType& /* model */) {}
};

// ECMP and LAG share the removal logic — they differ only in which
// LoadBalancerID they target. The two handler classes are thin wrappers that
// exist so the command tree can register them at distinct paths
// (`delete load-balancing ecmp` and `delete load-balancing lag`), mirroring
// the config-side CmdConfigLoadBalancing{Ecmp,Lag} split.

struct CmdDeleteLoadBalancingEcmpTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteLoadBalancing;
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdDeleteLoadBalancingEcmp : public CmdHandler<
                                       CmdDeleteLoadBalancingEcmp,
                                       CmdDeleteLoadBalancingEcmpTraits> {
 public:
  using ObjectArgType = CmdDeleteLoadBalancingEcmpTraits::ObjectArgType;
  using RetType = CmdDeleteLoadBalancingEcmpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& logMsg);
};

struct CmdDeleteLoadBalancingLagTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteLoadBalancing;
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdDeleteLoadBalancingLag : public CmdHandler<
                                      CmdDeleteLoadBalancingLag,
                                      CmdDeleteLoadBalancingLagTraits> {
 public:
  using ObjectArgType = CmdDeleteLoadBalancingLagTraits::ObjectArgType;
  using RetType = CmdDeleteLoadBalancingLagTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
