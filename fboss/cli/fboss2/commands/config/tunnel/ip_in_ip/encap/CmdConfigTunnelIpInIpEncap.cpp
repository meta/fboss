/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/encap/CmdConfigTunnelIpInIpEncap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/TunnelIpInIpConfigUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Encap accepts every config attribute except termination-type, which is
// meaningful only for decap (tunnel termination) tunnels.
const std::unordered_set<std::string> kEncapAttrs = {
    std::string(tunnel_utils::kAttrDestination),
    std::string(tunnel_utils::kAttrSource),
    std::string(tunnel_utils::kAttrTtlMode),
    std::string(tunnel_utils::kAttrDscpMode),
    std::string(tunnel_utils::kAttrEcnMode),
    std::string(tunnel_utils::kAttrUnderlayIntfId),
};

} // namespace

TunnelIpInIpEncapConfig::TunnelIpInIpEncapConfig(std::vector<std::string> v)
    : tunnel_utils::TunnelIpInIpConfigArgsBase(std::move(v), kEncapAttrs) {}

CmdConfigTunnelIpInIpEncapTraits::RetType
CmdConfigTunnelIpInIpEncap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& tunnelConfig) {
  return tunnel_utils::configureTunnel(
      TunnelType::IP_IN_IP_ENCAP,
      tunnelConfig.getTunnelId(),
      tunnelConfig.getAttrs());
}

void CmdConfigTunnelIpInIpEncap::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigTunnelIpInIpEncap, CmdConfigTunnelIpInIpEncapTraits>::run();

std::vector<std::string> CmdConfigTunnelIpInIpEncapTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return tunnel_utils::completeTunnelConfigArgs(typed, kEncapAttrs);
}

} // namespace facebook::fboss
