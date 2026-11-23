// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/CmdConfigTunnelSrv6.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

template void CmdHandler<CmdConfigTunnelSrv6, CmdConfigTunnelSrv6Traits>::run();

} // namespace facebook::fboss
