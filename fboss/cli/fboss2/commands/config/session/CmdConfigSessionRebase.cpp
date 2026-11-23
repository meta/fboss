/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionRebase.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <ostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigSessionRebaseTraits::RetType CmdConfigSessionRebase::queryClient(
    const HostInfo& /* hostInfo */) {
  auto& session = ConfigSession::getInstance();
  session.rebase(); // raises a runtime_error if we fail
  return "Session successfully rebased onto current HEAD. You can now commit.";
}

void CmdConfigSessionRebase::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigSessionRebase, CmdConfigSessionRebaseTraits>::run();

} // namespace facebook::fboss
