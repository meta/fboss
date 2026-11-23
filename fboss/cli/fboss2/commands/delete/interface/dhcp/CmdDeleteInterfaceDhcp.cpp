// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/interface/dhcp/CmdDeleteInterfaceDhcp.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

// Explicit template instantiation
template void
CmdHandler<CmdDeleteInterfaceDhcp, CmdDeleteInterfaceDhcpTraits>::run();

} // namespace facebook::fboss
