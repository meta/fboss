/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/rollback/CmdConfigRollback.h"
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigRollbackTraits::RetType CmdConfigRollback::queryClient(
    const HostInfo& hostInfo,
    const utils::RevisionList& revisions) {
  auto& session = ConfigSession::getInstance();

  // Validate arguments
  if (revisions.size() > 1) {
    throw std::invalid_argument(
        "Too many arguments. Expected 0 or 1 commit SHA.");
  }

  if (!revisions.empty() && revisions[0] == "current") {
    throw std::invalid_argument(
        "Cannot rollback to 'current'. Please specify a commit SHA.");
  }

  try {
    // An empty revision rolls back to the previous commit.
    std::string newCommitSha = session.rollback(
        hostInfo,
        revisions.empty() ? "" : revisions[0],
        CmdGlobalOptions::getInstance()->isForce());
    if (!newCommitSha.empty()) {
      return "Successfully rolled back. New commit: " +
          Git::shortSha1(newCommitSha) + ". Config reloaded.";
    } else {
      return "Failed to create a new commit after rollback.";
    }
  } catch (const std::exception& ex) {
    throw std::runtime_error(
        "Failed to rollback config: " + std::string(ex.what()));
  }
}

void CmdConfigRollback::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigRollback, CmdConfigRollbackTraits>::run();

} // namespace facebook::fboss
