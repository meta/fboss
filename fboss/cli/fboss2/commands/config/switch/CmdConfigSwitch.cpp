/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/switch/CmdConfigSwitch.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>

namespace facebook::fboss {

CmdConfigSwitchTraits::RetType CmdConfigSwitch::queryClient(
    const HostInfo& /* hostInfo */) {
  return "Switch configuration commands. Use subcommands: admin-distance, icmpv4-unavailable-src-addr, hostname, resource-limit";
}

void CmdConfigSwitch::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigSwitch, CmdConfigSwitchTraits>::run();

} // namespace facebook::fboss
