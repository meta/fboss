/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/decap/CmdDeleteTunnelSrv6Decap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>

namespace facebook::fboss {

CmdDeleteTunnelSrv6DecapTraits::RetType CmdDeleteTunnelSrv6Decap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  return srv6_tunnel_delete_utils::deleteTunnel(
      TunnelType::SRV6_DECAP, args.getTunnelId(), args.getAttrs());
}

void CmdDeleteTunnelSrv6Decap::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteTunnelSrv6Decap, CmdDeleteTunnelSrv6DecapTraits>::run();

} // namespace facebook::fboss
