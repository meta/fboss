/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/encap/CmdDeleteTunnelIpInIpEncap.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/TunnelIpInIpConfigUtils.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/TunnelIpInIpDeleteUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Optional fields that can be reset on an encap tunnel. termination-type is
// decap-only; source is required for encap (the agent reads it
// unconditionally when programming the encap), so neither is resettable.
const std::unordered_set<std::string> kResettableAttrs = {
    std::string(tunnel_utils::kAttrTtlMode),
    std::string(tunnel_utils::kAttrDscpMode),
    std::string(tunnel_utils::kAttrEcnMode),
};

// Required fields — cannot be reset individually; delete the whole tunnel.
const std::unordered_set<std::string> kRequiredAttrs = {
    std::string(tunnel_utils::kAttrDestination),
    std::string(tunnel_utils::kAttrUnderlayIntfId),
    std::string(tunnel_utils::kAttrSource),
};

// Deterministic display order for error messages.
const std::vector<std::string> kResettableAttrsDisplay = {
    "ttl-mode",
    "dscp-mode",
    "ecn-mode",
};

} // namespace

TunnelIpInIpEncapDeleteArgs::TunnelIpInIpEncapDeleteArgs(
    std::vector<std::string> v)
    : tunnel_delete_utils::TunnelIpInIpDeleteArgsBase(
          std::move(v),
          kResettableAttrs,
          kRequiredAttrs,
          kResettableAttrsDisplay) {}

CmdDeleteTunnelIpInIpEncapTraits::RetType
CmdDeleteTunnelIpInIpEncap::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& deleteArgs) {
  return tunnel_delete_utils::deleteTunnel(
      TunnelType::IP_IN_IP_ENCAP,
      deleteArgs.getTunnelId(),
      deleteArgs.getAttributes());
}

void CmdDeleteTunnelIpInIpEncap::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteTunnelIpInIpEncap, CmdDeleteTunnelIpInIpEncapTraits>::run();

std::vector<std::string> CmdDeleteTunnelIpInIpEncapTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completeAttrList(
      typed, kResettableAttrsDisplay, /* minObjects */ 1);
}

} // namespace facebook::fboss
