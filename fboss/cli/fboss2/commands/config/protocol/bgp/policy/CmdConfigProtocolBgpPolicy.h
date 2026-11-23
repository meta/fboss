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
#include <variant>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/CmdConfigProtocolBgp.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Grouping node for `config protocol bgp policy <object-type> ...`. Holds no
// state of its own; the per-object-type dispatchers (as-path-list,
// community-list, prefix-list, routing-policy) are its subcommands.
// Mirrors CmdConfigProtocolBgp.
struct CmdConfigProtocolBgpPolicyTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgp;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicy : public CmdHandler<
                                       CmdConfigProtocolBgpPolicy,
                                       CmdConfigProtocolBgpPolicyTraits> {
 public:
  using ObjectArgType = CmdConfigProtocolBgpPolicyTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
