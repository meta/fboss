// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/set/transceiver/loopback/CmdSetTransceiverLoopback.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/LoopbackUtils.h"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include <fmt/format.h>

namespace facebook::fboss {

using namespace loopback_utils;

CmdSetTransceiverLoopback::RetType CmdSetTransceiverLoopback::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedPorts,
    const ObjectArgType& action) {
  if (queriedPorts.empty()) {
    return "Error: at least one port must be specified\n"
           "Usage: set transceiver <port> loopback <system|line> [input|output] <enable|disable>\n"
           "       set transceiver <port> loopback disable\n";
  }

  auto agent = utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  auto qsfpService =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
  auto portEntries = fetchAllPortInfo(agent.get());

  std::string output;
  for (const auto& portName : queriedPorts) {
    try {
      output += setTransceiverLoopbackForPort(
          agent.get(), qsfpService.get(), portName, action, portEntries);
      if (queriedPorts.size() > 1) {
        output += "\n";
      }
    } catch (const std::exception& ex) {
      output += fmt::format("Error ({}): {}\n", portName, ex.what());
    }
  }

  return output;
}

void CmdSetTransceiverLoopback::printOutput(
    const RetType& output,
    std::ostream& out) {
  out << output;
}

template void
CmdHandler<CmdSetTransceiverLoopback, CmdSetTransceiverLoopbackTraits>::run();

} // namespace facebook::fboss
