/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/switch/icmpv4_unavailable_src_addr/CmdDeleteIcmpV4UnavailableSrcAddr.h"

#include <fmt/format.h>
#include <iostream>

#include "fboss/cli/fboss2/CmdHandler.cpp" // NOLINT(facebook-unused-include-check)
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

CmdDeleteIcmpV4UnavailableSrcAddrTraits::RetType
CmdDeleteIcmpV4UnavailableSrcAddr::queryClient(const HostInfo& /* hostInfo */) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  auto addrRef = swConfig.icmpV4UnavailableSrcAddress();
  if (!addrRef.has_value()) {
    return "Warning: no ICMPv4 unavailable source address configured, "
           "nothing to delete";
  }
  const std::string oldAddr = *addrRef;
  addrRef.reset();

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully removed ICMPv4 unavailable source address {}; "
      "the agent will use the RFC 7600 default (192.0.0.8)",
      oldAddr);
}

void CmdDeleteIcmpV4UnavailableSrcAddr::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteIcmpV4UnavailableSrcAddr,
    CmdDeleteIcmpV4UnavailableSrcAddrTraits>::run();

} // namespace facebook::fboss
