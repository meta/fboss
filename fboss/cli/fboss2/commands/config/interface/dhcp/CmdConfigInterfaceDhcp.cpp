// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/interface/dhcp/CmdConfigInterfaceDhcp.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

// Explicit template instantiation
template void
CmdHandler<CmdConfigInterfaceDhcp, CmdConfigInterfaceDhcpTraits>::run();

} // namespace facebook::fboss
