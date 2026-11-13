// Copyright (c) Meta Platforms, Inc. and affiliates.

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
