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

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

struct CmdDeleteDataPlaneTraits : public WriteCommandTraits {
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdDeleteDataPlane
    : public CmdHandler<CmdDeleteDataPlane, CmdDeleteDataPlaneTraits> {
 public:
  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use one of the subcommands");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
