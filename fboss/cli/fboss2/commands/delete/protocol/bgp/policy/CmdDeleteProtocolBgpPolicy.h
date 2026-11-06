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

#include <variant>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/CmdDeleteProtocolBgp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Grouping node for `delete protocol bgp policy <object-type> <name>`. Holds no
// state of its own; the per-object-type delete commands are its subcommands.
struct CmdDeleteProtocolBgpPolicyTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgp;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPolicy : public CmdHandler<
                                       CmdDeleteProtocolBgpPolicy,
                                       CmdDeleteProtocolBgpPolicyTraits> {
 public:
  using ObjectArgType = CmdDeleteProtocolBgpPolicyTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
