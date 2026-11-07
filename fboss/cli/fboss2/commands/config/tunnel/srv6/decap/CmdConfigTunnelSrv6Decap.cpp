/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/decap/CmdConfigTunnelSrv6Decap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <unordered_set>
#include <utility>

namespace facebook::fboss {

namespace {
const std::unordered_set<std::string> kDecapAttrs = {
    std::string(srv6_tunnel_utils::kAttrTtlMode),
    std::string(srv6_tunnel_utils::kAttrDscpMode),
    std::string(srv6_tunnel_utils::kAttrEcnMode),
    std::string(srv6_tunnel_utils::kAttrTerminationType),
};
} // namespace

TunnelSrv6DecapConfig::TunnelSrv6DecapConfig(std::vector<std::string> values)
    : TunnelSrv6ConfigArgsBase(std::move(values), kDecapAttrs) {}

CmdConfigTunnelSrv6DecapTraits::RetType CmdConfigTunnelSrv6Decap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& tunnelConfig) {
  return srv6_tunnel_utils::configureTunnel(
      TunnelType::SRV6_DECAP,
      tunnelConfig.getTunnelId(),
      tunnelConfig.getAttrs());
}

void CmdConfigTunnelSrv6Decap::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdConfigTunnelSrv6Decap, CmdConfigTunnelSrv6DecapTraits>::run();

} // namespace facebook::fboss
