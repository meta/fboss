/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/CmdDeleteProtocolBgp.h"
#include <iostream>

#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteProtocolBgpTraits::RetType CmdDeleteProtocolBgp::queryClient(
    const HostInfo& /* hostInfo */) {
  return "Delete BGP configuration objects. Use subcommands: neighbor";
}

void CmdDeleteProtocolBgp::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteProtocolBgp, CmdDeleteProtocolBgpTraits>::run();

} // namespace facebook::fboss
