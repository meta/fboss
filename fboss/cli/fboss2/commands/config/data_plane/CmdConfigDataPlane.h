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

namespace facebook::fboss {

// The `data-plane` parent node dispatches to its subcommand leaves. It needs
// a handler, not a pure branch, so addCommandBranch() increments depth before
// descending into the leaves. Without that, sibling leaves would register
// their positional args at the same CmdArgsLists slot.
struct CmdConfigDataPlaneTraits : public WriteCommandTraits {
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdConfigDataPlane
    : public CmdHandler<CmdConfigDataPlane, CmdConfigDataPlaneTraits> {
 public:
  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use a subcommand (e.g. 'traffic-policy')");
  }
  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
