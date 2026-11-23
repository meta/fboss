// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/encap/CmdDeleteTunnelSrv6Encap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>

namespace facebook::fboss {

CmdDeleteTunnelSrv6EncapTraits::RetType CmdDeleteTunnelSrv6Encap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  return srv6_tunnel_delete_utils::deleteTunnel(
      TunnelType::SRV6_ENCAP, args.getTunnelId(), args.getAttrs());
}

void CmdDeleteTunnelSrv6Encap::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteTunnelSrv6Encap, CmdDeleteTunnelSrv6EncapTraits>::run();

} // namespace facebook::fboss
