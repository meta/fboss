/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/reload/CmdReloadConfigAgent.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

CmdReloadConfigAgent::RetType CmdReloadConfigAgent::queryClient(
    const HostInfo& hostInfo) {
  auto agentClient =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  agentClient->sync_reloadConfig();
  return "Agent config reloaded";
}

template void
CmdHandler<CmdReloadConfigAgent, CmdReloadConfigAgentTraits>::run();

} // namespace facebook::fboss
