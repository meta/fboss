// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/init/CmdConfigInit.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigInitTraits::RetType CmdConfigInit::queryClient(
    const HostInfo& /* hostInfo */) {
  // ReadOnly: initializing the history must never stage a session.
  auto& session =
      ConfigSession::getInstance(ConfigSession::SessionInit::ReadOnly);
  if (auto sha = session.initializeHistory()) {
    return fmt::format(
        "Committed the live config to the config history as {}.",
        Git::shortSha1(*sha));
  }
  // Creating the session may itself have made the first commit (see
  // ConfigSession::initializeGit()), so report HEAD rather than "no change".
  return fmt::format(
      "Config history matches the live config at {}.",
      Git::shortSha1(session.getGit().getHead()));
}

void CmdConfigInit::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigInit, CmdConfigInitTraits>::run();

} // namespace facebook::fboss
