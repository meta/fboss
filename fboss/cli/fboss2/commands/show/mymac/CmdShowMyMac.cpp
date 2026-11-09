// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/mymac/CmdShowMyMac.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/Table.h"

#include <algorithm>

namespace facebook::fboss {

using utils::Table;

CmdShowMyMac::RetType CmdShowMyMac::queryClient(const HostInfo& hostInfo) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::vector<cfg::MacAndVlan> myMacs;
  client->sync_getMyMacs(myMacs);
  return createModel(myMacs);
}

CmdShowMyMac::RetType CmdShowMyMac::createModel(
    const std::vector<cfg::MacAndVlan>& myMacs) {
  RetType model;
  for (const auto& myMac : myMacs) {
    cli::MyMacEntry entry;
    entry.vlanID() = folly::copy(myMac.vlanID().value());
    entry.macAddress() = myMac.macAddress().value();
    model.myMacs()->push_back(std::move(entry));
  }
  std::sort(
      model.myMacs()->begin(),
      model.myMacs()->end(),
      [](const cli::MyMacEntry& a, const cli::MyMacEntry& b) {
        return std::tie(*a.vlanID(), *a.macAddress()) <
            std::tie(*b.vlanID(), *b.macAddress());
      });
  return model;
}

void CmdShowMyMac::printOutput(const RetType& model, std::ostream& out) {
  if (model.myMacs()->empty()) {
    out << "No MY_MAC entries configured" << std::endl;
    return;
  }
  Table table;
  table.setHeader({"VLAN", "MAC Address"});
  for (const auto& entry : model.myMacs().value()) {
    table.addRow(
        {folly::to<std::string>(folly::copy(entry.vlanID().value())),
         entry.macAddress().value()});
  }
  out << table << std::endl;
}

std::string_view CmdShowMyMacTraits::description() {
  return "Displays the additional router MACs (MY_MAC) configured in switchSettings.myMacs. Packets destined to one of these MACs on the given VLAN are routed, in addition to packets destined to the interface MAC. Prints a message when no entries are configured, including on ASICs that do not support MY_MAC.";
}

CmdShowMyMac::RetType CmdShowMyMac::sampleModel() {
  RetType model;
  for (const auto& macAddress : {"02:00:00:00:00:01", "02:00:00:00:00:02"}) {
    cli::MyMacEntry entry;
    entry.vlanID() = 2001;
    entry.macAddress() = macAddress;
    model.myMacs()->push_back(std::move(entry));
  }
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowMyMac, CmdShowMyMacTraits>::run();

} // namespace facebook::fboss
