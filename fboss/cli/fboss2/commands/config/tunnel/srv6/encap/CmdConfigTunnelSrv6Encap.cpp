/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/encap/CmdConfigTunnelSrv6Encap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <unordered_set>
#include <utility>

namespace facebook::fboss {

namespace {
const std::unordered_set<std::string> kEncapAttrs = {
    std::string(srv6_tunnel_utils::kAttrSource),
    std::string(srv6_tunnel_utils::kAttrTtlMode),
    std::string(srv6_tunnel_utils::kAttrDscpMode),
    std::string(srv6_tunnel_utils::kAttrEcnMode),
    std::string(srv6_tunnel_utils::kAttrTerminationType),
    std::string(srv6_tunnel_utils::kAttrUnderlayIntfId),
};
} // namespace

TunnelSrv6EncapConfig::TunnelSrv6EncapConfig(std::vector<std::string> values)
    : TunnelSrv6ConfigArgsBase(std::move(values), kEncapAttrs) {}

CmdConfigTunnelSrv6EncapTraits::RetType CmdConfigTunnelSrv6Encap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& tunnelConfig) {
  return srv6_tunnel_utils::configureTunnel(
      TunnelType::SRV6_ENCAP,
      tunnelConfig.getTunnelId(),
      tunnelConfig.getAttrs());
}

void CmdConfigTunnelSrv6Encap::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdConfigTunnelSrv6Encap, CmdConfigTunnelSrv6EncapTraits>::run();

} // namespace facebook::fboss
