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
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// The `acl` parent node dispatches to its subcommand leaves. It needs a
// handler (rather than being a pure branch) so that addCommandBranch()
// increments depth before descending into the leaves — without that, sibling
// leaves would register their positional args at the same CmdArgsLists slot.
struct CmdConfigAclTraits : public WriteCommandTraits {
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdConfigAcl : public CmdHandler<CmdConfigAcl, CmdConfigAclTraits> {
 public:
  using ObjectArgType = CmdConfigAclTraits::ObjectArgType;
  using RetType = CmdConfigAclTraits::RetType;

  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use a subcommand: "
        "'rule', 'table' or 'table-group'");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
