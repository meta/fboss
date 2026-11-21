/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/status/CmdShowInterfaceStatus.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/Table.h"

namespace facebook::fboss {

CmdShowInterfaceStatus::RetType CmdShowInterfaceStatus::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  auto qsfpClient =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::map<int32_t, facebook::fboss::TransceiverInfo> transceiverInfo;

  client->sync_getAllPortInfo(portEntries);
  qsfpClient->sync_getTransceiverInfo(
      transceiverInfo, getRequiredTransceivers(portEntries));

  return createModel(portEntries, transceiverInfo, queriedIfs);
}

std::vector<int32_t> CmdShowInterfaceStatus::getRequiredTransceivers(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries) {
  std::vector<int32_t> requiredTransceivers;
  for (const auto& port : portEntries) {
    if (auto transIdx = port.second.transceiverIdx()) {
      requiredTransceivers.push_back(
          folly::copy(transIdx->transceiverId().value()));
    }
  }
  return requiredTransceivers;
}

Table::StyledCell CmdShowInterfaceStatus::colorStatusCell(
    const std::string& status) {
  Table::Style cellStyle = Table::Style::NONE;
  if (status == "up") {
    cellStyle = Table::Style::GOOD;
  }
  return Table::StyledCell(status, cellStyle);
}

Table::StyledCell CmdShowInterfaceStatus::colorTransCell(
    const std::string& transceiver) {
  Table::Style cellStyle = Table::Style::NONE;
  if (transceiver == "Not Present") {
    cellStyle = Table::Style::INFO;
  }
  return Table::StyledCell(transceiver, cellStyle);
}

CmdShowInterfaceStatus::RetType CmdShowInterfaceStatus::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
    std::map<int32_t, facebook::fboss::TransceiverInfo> transceiverEntries,
    const std::vector<std::string>& queriedIfs) {
  RetType model;
  std::unordered_set<std::string> queriedSet(
      queriedIfs.begin(), queriedIfs.end());
  for (const auto& [portId, portInfo] : portEntries) {
    if (queriedIfs.size() == 0 || queriedSet.count(portInfo.name().value())) {
      cli::InterfaceStatus ifStatus;
      const auto operState = folly::copy(portInfo.operState().value());

      ifStatus.name() = portInfo.name().value();
      ifStatus.portId() = folly::copy(portInfo.portId().value());
      ifStatus.description() = portInfo.description().value();
      ifStatus.status() =
          (operState == facebook::fboss::PortOperState::UP) ? "up" : "down";
      if (portInfo.vlans().value().size()) {
        ifStatus.vlan() = portInfo.vlans().value()[0];
      }
      ifStatus.speed() =
          std::to_string(folly::copy(portInfo.speedMbps().value()) / 1000) +
          "G";

      ifStatus.vendor() = "Not Present";
      ifStatus.mpn() = "Not Present";
      if (auto transceiverIdx = portInfo.transceiverIdx()) {
        int32_t transceiverId =
            folly::copy(transceiverIdx->transceiverId().value());
        const auto& transceiver = transceiverEntries[transceiverId];
        if (apache::thrift::get_pointer(transceiver.tcvrState()->vendor())) {
          ifStatus.vendor() =
              apache::thrift::get_pointer(transceiver.tcvrState()->vendor())
                  ->name()
                  .value();
          ifStatus.mpn() =
              apache::thrift::get_pointer(transceiver.tcvrState()->vendor())
                  ->partNumber()
                  .value();
        }
      }

      model.interfaces()->push_back(ifStatus);
    }
  }
  std::sort(
      model.interfaces()->begin(),
      model.interfaces()->end(),
      [](const cli::InterfaceStatus& a, const cli::InterfaceStatus& b) {
        return utils::comparePortNameOrId(
            a.name().value(),
            folly::copy(a.portId().value()),
            b.name().value(),
            folly::copy(b.portId().value()));
      });
  return model;
}

void CmdShowInterfaceStatus::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table outTable;

  outTable.setHeader({
      "Interface",
      "Description",
      "Status",
      "Vlan",
      "Speed",
      "Vendor",
      "Part Number",
  });

  for (const auto& portStatus : model.interfaces().value()) {
    outTable.addRow({
        portStatus.name().value(),
        portStatus.description().value(),
        colorStatusCell(portStatus.status().value()),
        (portStatus.vlan().has_value()
             ? std::to_string(*apache::thrift::get_pointer(portStatus.vlan()))
             : "--"),
        portStatus.speed().value(),
        colorTransCell(portStatus.vendor().value()),
        colorTransCell(portStatus.mpn().value()),
    });
  }

  out << outTable << std::endl;
}

std::string_view CmdShowInterfaceStatusTraits::description() {
  return "Displays a per-interface status summary: description, link status, VLAN, speed, and the installed transceiver's vendor and part number. Use it to confirm link state and optic inventory at a glance.";
}

CmdShowInterfaceStatus::RetType CmdShowInterfaceStatus::sampleModel() {
  RetType model;

  cli::InterfaceStatus if1;
  if1.name() = "eth1/1/1";
  if1.description() = "neighbor peer001 on eth1/1/1";
  if1.status() = "up";
  if1.vlan() = 2001;
  if1.speed() = "400G";
  if1.vendor() = "Eoptolink";
  if1.mpn() = "EOLO-168HG-02-1B";

  cli::InterfaceStatus if2;
  if2.name() = "eth1/2/1";
  if2.description() = "neighbor peer002 on eth1/2/1";
  if2.status() = "down";
  if2.vlan() = 2003;
  if2.speed() = "200G";
  if2.vendor() = "Eoptolink";
  if2.mpn() = "EOLO-168HG-02-1B";

  model.interfaces() = {if1, if2};
  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowInterfaceStatus, CmdShowInterfaceStatusTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceStatus,
    CmdShowInterfaceStatusTraits>::getValidFilters();

} // namespace facebook::fboss
