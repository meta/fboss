/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/dhcp/reply_source_override/CmdConfigDhcpReplySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Verify that ipStr appears as the host address of at least one interface IP
// entry (stored as CIDR in swConfig.interfaces[*].ipAddresses). Throws
// std::invalid_argument listing the valid addresses if the check fails.
template <typename IPVersionType>
void requireInterfaceIp(
    const cfg::SwitchConfig& swConfig,
    const std::string& ipStr) {
  // DhcpSourceOverrideArgs already validated ipStr, so construction won't
  // throw.
  const auto newIp = IPVersionType(ipStr);
  std::vector<std::string> validAddresses;
  for (const auto& intf : *swConfig.interfaces()) {
    for (const auto& cidr : *intf.ipAddresses()) {
      try {
        // applyMask=false: keep the host IP, not the network address.
        auto [hostIp, _] = folly::IPAddress::createNetwork(cidr, -1, false);
        if constexpr (std::is_same_v<IPVersionType, folly::IPAddressV4>) {
          if (!hostIp.isV4()) {
            continue;
          }
          if (hostIp.asV4() == newIp) {
            return;
          }
          validAddresses.push_back(hostIp.str());
        } else {
          if (!hostIp.isV6()) {
            continue;
          }
          if (hostIp.asV6() == newIp) {
            return;
          }
          validAddresses.push_back(hostIp.str());
        }
      } catch (const std::exception& e) {
        XLOG(WARN) << "Skipping malformed interface IP CIDR '" << cidr
                   << "': " << e.what();
      }
    }
  }
  throw std::invalid_argument(
      fmt::format(
          "Reply source override address '{}' does not match any configured "
          "interface IP. Valid addresses: {}",
          ipStr,
          folly::join(", ", validAddresses)));
}

} // namespace

CmdConfigDhcpReplySourceOverrideTraits::RetType
CmdConfigDhcpReplySourceOverride::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  const auto& family = args.getFamily();
  const auto& ipAddress = args.getIpAddress();

  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  // The agent requires the reply-src to match a configured interface IP.
  // Validate here to surface a clear error with the list of valid addresses
  // rather than a cryptic agent reject at apply time.
  if (family == DhcpSourceOverrideArgs::kFamilyIpv4) {
    requireInterfaceIp<folly::IPAddressV4>(swConfig, ipAddress);
  } else {
    requireInterfaceIp<folly::IPAddressV6>(swConfig, ipAddress);
  }

  auto msg = applyDhcpSourceOverride(swConfig, "reply", family, ipAddress);
  // Applied hitlessly via SwitchSettings::setDhcpV{4,6}ReplySrc
  // (no SAI ChangeProhibited guard).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigDhcpReplySourceOverride::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigDhcpReplySourceOverride,
    CmdConfigDhcpReplySourceOverrideTraits>::run();

std::vector<std::string> CmdConfigDhcpReplySourceOverrideTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(typed, {dhcpFamilies()});
}

} // namespace facebook::fboss
