// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/transceiver/CmdShowInterfaceTransceiver.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

// This node only exists to host its subcommands, so it always throws.
[[noreturn]] CmdShowInterfaceTransceiver::RetType
CmdShowInterfaceTransceiver::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::PortList& /* queriedIfs */) {
  throw std::runtime_error(
      "Incomplete command, please use one the subcommands");
}

void CmdShowInterfaceTransceiver::printOutput(const RetType& /* model */) {}

// Template instantiations
template void CmdHandler<
    CmdShowInterfaceTransceiver,
    CmdShowInterfaceTransceiverTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceTransceiver,
    CmdShowInterfaceTransceiverTraits>::getValidFilters();

} // namespace facebook::fboss
