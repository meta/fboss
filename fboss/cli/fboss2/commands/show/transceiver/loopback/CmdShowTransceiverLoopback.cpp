// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/transceiver/loopback/CmdShowTransceiverLoopback.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/LoopbackUtils.h"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include <fmt/format.h>

namespace facebook::fboss {

using namespace loopback_utils;

namespace {

std::string showLoopbackForPort(
    apache::thrift::Client<FbossCtrl>* agent,
    apache::thrift::Client<QsfpService>* qsfpService,
    const std::string& portName,
    const std::map<int32_t, PortInfoThrift>& portEntries) {
  int32_t transceiverId = resolveTransceiverId(agent, portName, portEntries);
  auto cap = fetchLoopbackCapability(qsfpService, transceiverId);

  std::string output;
  output += fmt::format("Port: {}\n", portName);
  output += fmt::format("Transceiver ID: {}\n", transceiverId);

  output += "\nCapability:\n";
  auto addCapability = [&output](std::string_view name, bool supported) {
    output += fmt::format(
        "  {:<34}{}\n", name, supported ? "supported" : "not-supported");
  };
  addCapability("system input (host input):", cap.systemInput);
  addCapability("system output (host output):", cap.systemOutput);
  addCapability("line input (media input):", cap.lineInput);
  addCapability("line output (media output):", cap.lineOutput);

  if (!cap.any()) {
    output += "\nLoopback not supported by this module.\n";
    return output;
  }

  try {
    auto state = readLoopbackState(qsfpService, transceiverId);
    output += "\nState:\n";
    output += formatState(state);
  } catch (const std::exception& ex) {
    output += fmt::format("\nError reading state: {}\n", ex.what());
  }

  return output;
}

} // namespace

CmdShowTransceiverLoopback::RetType CmdShowTransceiverLoopback::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedPorts) {
  if (queriedPorts.empty()) {
    return "Usage: show transceiver <port> loopback\n"
           "\nShows loopback capability and current state.\n"
           "\nExample:\n"
           "  show transceiver eth1/25/1 loopback\n";
  }

  auto agent = utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  auto qsfpService =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
  auto portEntries = fetchAllPortInfo(agent.get());

  std::string output;
  for (const auto& portName : queriedPorts) {
    try {
      output += showLoopbackForPort(
          agent.get(), qsfpService.get(), portName, portEntries);
      if (queriedPorts.size() > 1) {
        output += "\n";
      }
    } catch (const std::exception& ex) {
      output += fmt::format("Error ({}): {}\n", portName, ex.what());
    }
  }

  return output;
}

void CmdShowTransceiverLoopback::printOutput(
    const RetType& output,
    std::ostream& out) {
  out << output;
}

std::string_view CmdShowTransceiverLoopbackTraits::description() {
  return "Displays a transceiver's loopback capability and current state for the four CMIS loopbacks: system (host side) input/output and line (media side) input/output. Use it to check optic loopback support and whether loopback is enabled.";
}

CmdShowTransceiverLoopback::RetType CmdShowTransceiverLoopback::sampleModel() {
  return R"(Port: eth1/1/1
Transceiver ID: 0

Capability:
  system input (host input):        supported
  system output (host output):      supported
  line input (media input):         supported
  line output (media output):       supported

State:
  system input (host input):        0x00  disabled
  system output (host output):      0x00  disabled
  line input (media input):         0x00  disabled
  line output (media output):       0x00  disabled
)";
}

template void
CmdHandler<CmdShowTransceiverLoopback, CmdShowTransceiverLoopbackTraits>::run();

} // namespace facebook::fboss
