/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/switch/hostname/CmdConfigHostname.h"

#include "fboss/cli/fboss2/CmdHandler.cpp" // NOLINT(facebook-unused-include-check)

#include <fmt/format.h>
#include <re2/re2.h>
#include <iostream>
#include <stdexcept>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {
// RFC 952/1123: dot-separated labels, each 1-63 chars of [a-zA-Z0-9-] with no
// leading/trailing hyphen. Total length (checked separately) max 253 chars.
bool isValidHostname(const std::string& hostname) {
  static const re2::RE2 kHostnamePattern(
      "^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?"
      "(\\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$");
  return hostname.length() <= 253 &&
      re2::RE2::FullMatch(hostname, kHostnamePattern);
}
} // namespace

HostnameConfigArgs::HostnameConfigArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("Hostname is required");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format("Expected exactly one hostname, got {}", v.size()));
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Hostname must not be empty");
  }
  if (!isValidHostname(v[0])) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid hostname '{}': must be RFC 952/1123 compliant (alphanumeric + "
            "hyphens, max 63 chars per label, total max 253 chars)",
            v[0]));
  }
  data_ = std::move(v);
}

CmdConfigHostnameTraits::RetType CmdConfigHostname::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();

  const auto& newName = args.getName();

  if (swConfig.hostname().has_value() && *swConfig.hostname() == newName) {
    return fmt::format("Hostname is already set to '{}'", newName);
  }

  swConfig.hostname() = newName;

  // hostname is applied hitlessly via ThriftConfigApplier::updateSwitchSettings
  // (newSwitchSettings->setHostname); no SAI ChangeProhibited guard exists.
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format("Successfully set hostname to '{}'", newName);
}

void CmdConfigHostname::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigHostname, CmdConfigHostnameTraits>::run();

} // namespace facebook::fboss
