// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/qos/priority_group_policy/CmdConfigQosPriorityGroupPolicy.h"
#include "fboss/cli/fboss2/commands/delete/qos/CmdDeleteQos.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// `delete qos priority-group-policy <name>` removes an entry from
// sw.portPgConfigs. It refuses while any port's pfc.portPgConfigName still
// names the policy; unbind those ports first with
// `delete interface <name> pfc-config`.
struct CmdDeleteQosPriorityGroupPolicyTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteQos;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "priority_group_policy_name",
           args,
           "Priority group policy to remove")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = PriorityGroupPolicyName;
  using RetType = std::string;
};

class CmdDeleteQosPriorityGroupPolicy
    : public CmdHandler<
          CmdDeleteQosPriorityGroupPolicy,
          CmdDeleteQosPriorityGroupPolicyTraits> {
 public:
  using ObjectArgType = CmdDeleteQosPriorityGroupPolicyTraits::ObjectArgType;
  using RetType = CmdDeleteQosPriorityGroupPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& name);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
