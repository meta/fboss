/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionCommit.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <vector>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"

namespace facebook::fboss {

CmdConfigSessionCommitTraits::RetType CmdConfigSessionCommit::queryClient(
    const HostInfo& hostInfo) {
  auto& session = ConfigSession::getInstance();

  if (!session.hasActiveSession()) {
    return "No config session exists. Make a config change first.";
  }

  auto result = session.commit(hostInfo);

  // Check if this was an empty commit (no changes to commit)
  if (result.commitSha.empty()) {
    return "Nothing to commit. Config session is clean.";
  }

  // Categorize services by action type
  std::vector<std::string> restartedServices;
  std::vector<std::string> reloadedServices;

  for (const auto& [service, level] : result.actions) {
    // Get the actual service names that were restarted/reloaded
    auto it = result.serviceNames.find(service);
    if (it == result.serviceNames.end()) {
      continue; // Skip if no service names found
    }
    const auto& serviceNamesList = it->second;

    switch (level) {
      case cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART:
      case cli::ConfigActionLevel::SERVICE_RESTART:
        // Label with what the level meant for this service (agent: warmboot
        // or coldboot; bgpd: a plain restart).
        for (const auto& serviceName : serviceNamesList) {
          restartedServices.push_back(
              fmt::format(
                  "{} ({})",
                  serviceName,
                  FbossServiceUtil::restartTypeName(service, level)));
        }
        break;
      case cli::ConfigActionLevel::HITLESS:
        for (const auto& serviceName : serviceNamesList) {
          reloadedServices.push_back(serviceName);
        }
        break;
    }
  }

  // Build message based on what actions were taken
  std::string message;
  std::string shortSha = result.commitSha.substr(0, 7);
  if (restartedServices.empty() && reloadedServices.empty()) {
    message =
        fmt::format("Config session committed successfully as {}.", shortSha);
  } else if (restartedServices.empty()) {
    message = fmt::format(
        "Config session committed successfully as {} and config reloaded for {}.",
        shortSha,
        folly::join(", ", reloadedServices));
  } else if (reloadedServices.empty()) {
    message = fmt::format(
        "Config session committed successfully as {} and {} restarted.",
        shortSha,
        folly::join(", ", restartedServices));
  } else {
    message = fmt::format(
        "Config session committed successfully as {}, {} restarted, and config reloaded for {}.",
        shortSha,
        folly::join(", ", restartedServices),
        folly::join(", ", reloadedServices));
  }

  return message;
}

void CmdConfigSessionCommit::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigSessionCommit, CmdConfigSessionCommitTraits>::run();

} // namespace facebook::fboss
