// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/CmdDeleteTunnelSrv6.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

template void CmdHandler<CmdDeleteTunnelSrv6, CmdDeleteTunnelSrv6Traits>::run();

} // namespace facebook::fboss
