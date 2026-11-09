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
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/switch/CmdDeleteSwitch.h"

namespace facebook::fboss {

struct CmdDeleteIcmpV4UnavailableSrcAddrTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteSwitch;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdDeleteIcmpV4UnavailableSrcAddr
    : public CmdHandler<
          CmdDeleteIcmpV4UnavailableSrcAddr,
          CmdDeleteIcmpV4UnavailableSrcAddrTraits> {
 public:
  using RetType = CmdDeleteIcmpV4UnavailableSrcAddrTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
