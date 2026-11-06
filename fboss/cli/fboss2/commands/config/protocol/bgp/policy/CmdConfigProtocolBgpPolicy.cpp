/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/CmdConfigProtocolBgpPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

CmdConfigProtocolBgpPolicyTraits::RetType
CmdConfigProtocolBgpPolicy::queryClient(const HostInfo& /* hostInfo */) {
  return "BGP policy configuration. Use subcommands: as-path-list";
}

void CmdConfigProtocolBgpPolicy::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigProtocolBgpPolicy, CmdConfigProtocolBgpPolicyTraits>::run();

} // namespace facebook::fboss
