// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/counters/fec/CmdShowInterfaceCountersFec.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

CmdShowInterfaceCountersFec::RetType CmdShowInterfaceCountersFec::queryClient(
    const HostInfo& /* hostInfo */,
    const std::vector<std::string>& /* queriedIfs */,
    const ObjectArgType& /* system|line */) {
  throw std::runtime_error(
      "Incomplete command, please use one of the subcommands");
}

void CmdShowInterfaceCountersFec::printOutput(const RetType& /* model */) {}

// Template instantiations
template void CmdHandler<
    CmdShowInterfaceCountersFec,
    CmdShowInterfaceCountersFecTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCountersFec,
    CmdShowInterfaceCountersFecTraits>::getValidFilters();

} // namespace facebook::fboss
