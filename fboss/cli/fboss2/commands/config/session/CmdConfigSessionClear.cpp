/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionClear.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <filesystem>
#include <iostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace fs = std::filesystem;

namespace facebook::fboss {

CmdConfigSessionClearTraits::RetType CmdConfigSessionClear::queryClient(
    const HostInfo& /* hostInfo */) {
  // Remove each staged session file (agent + BGP configs and the metadata).
  // stagedSessionFilePaths() is the single source of truth, so this handles
  // every config domain uniformly -- including a BGP-only session -- without
  // calling getInstance() (which would create a session we are trying to
  // clear). Only individual files are removed; the ~/.fboss2 directory stays.
  bool removedAny = false;
  for (const auto& path : ConfigSession::stagedSessionFilePaths()) {
    if (!fs::exists(path)) {
      continue;
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
      throw std::runtime_error(
          fmt::format(
              "Failed to remove session file {}: {}", path, ec.message()));
    }
    removedAny = true;
  }

  if (removedAny) {
    return "Config session cleared successfully.";
  }
  return "No config session exists. Nothing to clear.";
}

void CmdConfigSessionClear::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigSessionClear, CmdConfigSessionClearTraits>::run();

} // namespace facebook::fboss
