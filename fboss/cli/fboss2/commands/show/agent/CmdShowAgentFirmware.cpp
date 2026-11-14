// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "CmdShowAgentFirmware.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

using RetType = CmdShowAgentFirmwareTraits::RetType;

CmdShowAgentFirmwareTraits::RetType CmdShowAgentFirmware::queryClient(
    const HostInfo& hostInfo) {
  auto client = utils::createAgentClient(hostInfo);

  CmdShowAgentFirmwareTraits::RetType model{};
  std::vector<FirmwareInfo> entries;

  auto hwAgentQueryFn =
      [&entries](apache::thrift::Client<facebook::fboss::FbossHwCtrl>& client) {
        std::vector<FirmwareInfo> firmwareInfoList;
        client.sync_getAllHwFirmwareInfo(firmwareInfoList);
        entries.insert(
            entries.end(), firmwareInfoList.begin(), firmwareInfoList.end());
      };
  utils::runOnAllHwAgents(hostInfo, hwAgentQueryFn);

  return createModel(entries);
}

RetType CmdShowAgentFirmware::createModel(
    const std::vector<FirmwareInfo>& firmwareInfoList) {
  RetType model;

  for (const auto& firmwareInfo : firmwareInfoList) {
    cli::AgentFirmwareEntry entry;

    entry.version() = firmwareInfo.version().value();
    entry.opStatus() =
        apache::thrift::util::enumNameSafe(firmwareInfo.opStatus().value());
    entry.funcStatus() =
        apache::thrift::util::enumNameSafe(firmwareInfo.funcStatus().value());

    model.firmwareEntries()->push_back(entry);
  }

  return model;
}

void CmdShowAgentFirmware::printOutput(
    const CmdShowAgentFirmwareTraits::RetType& model,
    std::ostream& out) {
  if (model.firmwareEntries()->empty()) {
    out << "No firmware information available" << std::endl;
    return;
  }
  for (const auto& entry : *model.firmwareEntries()) {
    out << "Version: " << entry.version().value() << std::endl;
    out << "Operational Status: " << entry.opStatus().value() << std::endl;
    out << "Functional Status: " << entry.funcStatus().value() << std::endl;
  }
}

std::string_view CmdShowAgentFirmwareTraits::description() {
  return "Displays the agent firmware version and its operational and "
         "functional status. Use it to check firmware state. Prints 'No "
         "firmware information available' when no hw agent reports firmware. "
         "A hw agent that does not answer is skipped, so the same line can "
         "appear after a failed query.";
}

CmdShowAgentFirmware::RetType CmdShowAgentFirmware::sampleModel() {
  RetType model;
  cli::AgentFirmwareEntry entry;
  entry.version() = "1.0.0-EA1";
  entry.opStatus() = "RUNNING";
  entry.funcStatus() = "MONITORING";
  model.firmwareEntries()->push_back(entry);
  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowAgentFirmware, CmdShowAgentFirmwareTraits>::run();

} // namespace facebook::fboss
