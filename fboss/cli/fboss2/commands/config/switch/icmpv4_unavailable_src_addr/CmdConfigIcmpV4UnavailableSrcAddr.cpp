/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/switch/icmpv4_unavailable_src_addr/CmdConfigIcmpV4UnavailableSrcAddr.h"
#include <fmt/format.h>
#include <folly/IPAddressV4.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp" // NOLINT(facebook-unused-include-check)
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

IcmpV4UnavailableSrcAddrArg::IcmpV4UnavailableSrcAddrArg(
    std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("IPv4 address is required (e.g. 192.0.0.8)");
  }
  if (v.size() != 1) {
    throw std::invalid_argument("Expected exactly one argument: ipv4-address");
  }

  auto parsed = folly::IPAddressV4::tryFromString(v[0]);
  if (parsed.hasError()) {
    throw std::invalid_argument(fmt::format("Invalid IPv4 address '{}'", v[0]));
  }

  data_ = std::move(v);
}

CmdConfigIcmpV4UnavailableSrcAddrTraits::RetType
CmdConfigIcmpV4UnavailableSrcAddr::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& srcAddr) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();

  const std::string& newAddr = srcAddr.getAddress();
  auto currentAddrRef = swConfig.icmpV4UnavailableSrcAddress();

  if (currentAddrRef.has_value() && *currentAddrRef == newAddr) {
    return fmt::format(
        "ICMPv4 unavailable source address is already set to {}", newAddr);
  }

  swConfig.icmpV4UnavailableSrcAddress() = newAddr;

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully set ICMPv4 unavailable source address to {}", newAddr);
}

void CmdConfigIcmpV4UnavailableSrcAddr::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigIcmpV4UnavailableSrcAddr,
    CmdConfigIcmpV4UnavailableSrcAddrTraits>::run();

} // namespace facebook::fboss
