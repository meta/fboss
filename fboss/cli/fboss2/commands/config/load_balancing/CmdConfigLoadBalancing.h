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
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Argument for `config load-balancing <ecmp|lag> <attr> <value>`.
// Validates that v[0] is one of the valid load-balancing attribute names.
// Value parsing/validation is deferred to applyLoadBalancerConfig() because
// the accepted value shape depends on the attribute.
class LoadBalancingConfigArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ LoadBalancingConfigArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getAttribute() const {
    return attribute_;
  }

  const std::string& getValue() const {
    return value_;
  }

 private:
  std::string attribute_;
  std::string value_;
};

// CLI display name ("ecmp"/"lag") for a LoadBalancerID, shared by the
// config and delete load-balancing command trees.
std::string lbIdToString(cfg::LoadBalancerID id);

// Mutate `swConfig`'s load-balancer entry matching `id` to apply
// `<attr> <value>`, inserting a new entry if none exists. Throws
// std::invalid_argument for unknown attrs or malformed values.
// Returns a human-readable description of the change for display.
std::string applyLoadBalancerConfig(
    cfg::SwitchConfig& swConfig,
    cfg::LoadBalancerID id,
    const std::string& attr,
    const std::string& value);

// ECMP and LAG share all dispatch/mutation logic — they differ only in which
// LoadBalancerID they target. The two handler classes are thin wrappers that
// exist so the command tree can register them at distinct paths
// (`config load-balancing ecmp` and `config load-balancing lag`).

struct CmdConfigLoadBalancingEcmpTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "load_balancing_attr_value",
        args,
        "<attr> <value> where <attr> is one of: "
        "hash-fields-ipv4 (src-ip,dst-ip or none), "
        "hash-fields-ipv6 (src-ip,dst-ip,flow-label or none), "
        "hash-fields-transport (src-port,dst-port or none), "
        "hash-fields-mpls (top,second,third or none), "
        "hash-algorithm (crc16-ccitt|crc32-lo|crc32-hi|crc32-ethernet-lo|"
        "crc32-ethernet-hi|crc32-koopman-lo|crc32-koopman-hi|crc), "
        "hash-seed (integer or 'default' to unset)");
  }
  using ObjectArgType = LoadBalancingConfigArgs;
  using RetType = std::string;
};

class CmdConfigLoadBalancingEcmp : public CmdHandler<
                                       CmdConfigLoadBalancingEcmp,
                                       CmdConfigLoadBalancingEcmpTraits> {
 public:
  using ObjectArgType = CmdConfigLoadBalancingEcmpTraits::ObjectArgType;
  using RetType = CmdConfigLoadBalancingEcmpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

struct CmdConfigLoadBalancingLagTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "load_balancing_attr_value",
        args,
        "<attr> <value> where <attr> is one of: "
        "hash-fields-ipv4 (src-ip,dst-ip or none), "
        "hash-fields-ipv6 (src-ip,dst-ip,flow-label or none), "
        "hash-fields-transport (src-port,dst-port or none), "
        "hash-fields-mpls (top,second,third or none), "
        "hash-algorithm (crc16-ccitt|crc32-lo|crc32-hi|crc32-ethernet-lo|"
        "crc32-ethernet-hi|crc32-koopman-lo|crc32-koopman-hi|crc), "
        "hash-seed (integer or 'default' to unset)");
  }
  using ObjectArgType = LoadBalancingConfigArgs;
  using RetType = std::string;
};

class CmdConfigLoadBalancingLag : public CmdHandler<
                                      CmdConfigLoadBalancingLag,
                                      CmdConfigLoadBalancingLagTraits> {
 public:
  using ObjectArgType = CmdConfigLoadBalancingLagTraits::ObjectArgType;
  using RetType = CmdConfigLoadBalancingLagTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
