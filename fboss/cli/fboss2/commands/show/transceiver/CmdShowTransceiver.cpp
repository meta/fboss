/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowTransceiver.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>

namespace facebook::fboss {

CmdShowTransceiver::RetType CmdShowTransceiver::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedPorts) {
  auto qsfpService =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
  auto agent = utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  // TODO: explore performance improvement if we make all this parallel.
  auto portEntries = queryPortInfo(agent.get());
  auto portStatusEntries = queryPortStatus(agent.get(), portEntries);
  auto transceiverEntries = queryTransceiverInfo(qsfpService.get());
  auto transceiverValidationEntries =
      queryTransceiverValidationInfo(qsfpService.get(), portStatusEntries);

  return createModel(
      std::set(queriedPorts.begin(), queriedPorts.end()),
      portStatusEntries,
      transceiverEntries,
      portEntries,
      transceiverValidationEntries);
}

void CmdShowTransceiver::printOutput(const RetType& model, std::ostream& out) {
  Table outTable;

  outTable.setHeader(
      {"Interface",
       "Status",
       "Transceiver",
       "CfgValidated",
       "Reason",
       "Vendor",
       "Serial",
       "Part Number",
       "FW App Version",
       "FW DSP Version",
       "Temperature (C)",
       "Voltage (V)",
       "Current (mA)",
       "Tx Power (dBm)",
       "Rx Power (dBm)",
       "Rx SNR"});

  for (const auto& [portId, details] : model.transceivers().value()) {
    const bool isZr =
        details.mediaInterface().value() == MediaInterfaceCode::ZR_800G;
    outTable.addRow({
        details.name().value(),
        statusToString(
            details.isUp().has_value()
                ? std::make_optional<>(folly::copy(details.isUp().value()))
                : std::nullopt),
        (folly::copy(details.isPresent().value()))
            ? apache::thrift::util::enumNameSafe(
                  details.mediaInterface().value())
            : "Absent",
        details.validationStatus().value(),
        details.notValidatedReason().value(),
        details.vendor().value(),
        details.serial().value(),
        details.partNumber().value(),
        details.appFwVer().value(),
        details.dspFwVer().value(),
        coloredSensorValue(
            fmt::format("{:.2f}", folly::copy(details.temperature().value())),
            details.tempFlags().value()),
        coloredSensorValue(
            fmt::format("{:.2f}", folly::copy(details.voltage().value())),
            details.vccFlags().value()),
        listToString(
            details.currentMA().value(), LOW_CURRENT_WARN, LOW_CURRENT_ERR),
        listToString(details.txPower().value(), LOW_POWER_WARN, LOW_POWER_ERR),
        listToString(details.rxPower().value(), LOW_POWER_WARN, LOW_POWER_ERR),
        listToString(
            details.rxSnr().value(),
            isZr ? ZR_LOW_SNR_WARN : LOW_SNR_WARN,
            isZr ? ZR_LOW_SNR_ERR : LOW_SNR_ERR),
    });
  }
  out << outTable << std::endl;
}

std::map<int, PortInfoThrift> CmdShowTransceiver::queryPortInfo(
    apache::thrift::Client<FbossCtrl>* agent) const {
  // Query all ports here. Any filtering should be done in createModel
  std::map<int, PortInfoThrift> portEntries;
  agent->sync_getAllPortInfo(portEntries);
  return portEntries;
}

Table::StyledCell CmdShowTransceiver::statusToString(
    std::optional<bool> isUp) const {
  Table::Style cellStyle = Table::Style::GOOD;
  // Bypass modules don't have an associated port, so the concept of
  // 'port up/down' doesn't apply
  std::string valueStr = "Bypass";
  if (isUp.has_value()) {
    valueStr = *isUp ? "Up" : "Down";
    if (!*isUp) {
      cellStyle = Table::Style::ERROR;
    }
  }
  return Table::StyledCell(valueStr, cellStyle);
}

Table::StyledCell CmdShowTransceiver::listToString(
    std::vector<double> listToPrint,
    double lowWarningThreshold,
    double lowErrorThreshold) {
  std::string result;
  Table::Style cellStyle = Table::Style::GOOD;
  for (size_t i = 0; i < listToPrint.size(); ++i) {
    result += fmt::format("{:.2f}", listToPrint[i]) +
        ((i != listToPrint.size() - 1) ? "," : "");

    if (listToPrint[i] <= lowWarningThreshold) {
      cellStyle = Table::Style::WARN;
    }
    if (listToPrint[i] <= lowErrorThreshold) {
      cellStyle = Table::Style::ERROR;
    }
  }
  return Table::StyledCell(result, cellStyle);
}

Table::StyledCell CmdShowTransceiver::coloredSensorValue(
    std::string value,
    FlagLevels flags) {
  if (folly::copy(flags.alarm().value().high().value()) ||
      folly::copy(flags.alarm().value().low().value())) {
    return Table::StyledCell(value, Table::Style::ERROR);
  } else if (
      folly::copy(flags.warn().value().high().value()) ||
      folly::copy(flags.warn().value().low().value())) {
    return Table::StyledCell(value, Table::Style::WARN);
  } else {
    return Table::StyledCell(value, Table::Style::GOOD);
  }
}

std::map<int, PortStatus> CmdShowTransceiver::queryPortStatus(
    apache::thrift::Client<FbossCtrl>* agent,
    std::map<int, PortInfoThrift> portEntries) const {
  std::vector<int32_t> requiredPortStatus;
  for (const auto& portItr : portEntries) {
    requiredPortStatus.push_back(portItr.first);
  }
  std::map<int, PortStatus> portStatusEntries;
  agent->sync_getPortStatus(portStatusEntries, requiredPortStatus);

  return portStatusEntries;
}

std::map<int, TransceiverInfo> CmdShowTransceiver::queryTransceiverInfo(
    apache::thrift::Client<QsfpService>* qsfpService) const {
  std::vector<int32_t> requiredTransceiverEntries;
  std::map<int, TransceiverInfo> transceiverEntries;
  // Query all transceivers here. Any filtering should be done in createModel
  qsfpService->sync_getTransceiverInfo(transceiverEntries, {});
  return transceiverEntries;
}

std::map<int32_t, std::string>
CmdShowTransceiver::queryTransceiverValidationInfo(
    apache::thrift::Client<QsfpService>* qsfpService,
    std::map<int, PortStatus> portStatusEntries) const {
  std::set<int32_t> uniqueTransceiverIds;
  for (const auto& portStatusItr : portStatusEntries) {
    if (auto tidx = portStatusItr.second.transceiverIdx()) {
      uniqueTransceiverIds.insert(folly::copy(tidx->transceiverId().value()));
    }
  }
  const std::vector<int32_t> requiredTransceiverEntries(
      uniqueTransceiverIds.begin(), uniqueTransceiverIds.end());

  std::map<int, std::string> transceiverValidationEntries;
  try {
    qsfpService->sync_getTransceiverConfigValidationInfo(
        transceiverValidationEntries, requiredTransceiverEntries, false);
  } catch (apache::thrift::TException&) {
    std::cerr << "Exception while calling getTransceiverConfigValidationInfo()."
              << std::endl;
  }
  return transceiverValidationEntries;
}

const std::pair<std::string, std::string>
CmdShowTransceiver::getTransceiverValidationStrings(
    std::map<int32_t, std::string>& transceiverEntries,
    int32_t transceiverId) const {
  if (transceiverEntries.find(transceiverId) == transceiverEntries.end()) {
    return std::make_pair("--", "--");
  }

  return transceiverEntries[transceiverId] == ""
      ? std::make_pair("Validated", "--")
      : std::make_pair("Not Validated", transceiverEntries[transceiverId]);
}

CmdShowTransceiver::RetType CmdShowTransceiver::createModel(
    const std::set<std::string>& queriedPorts,
    std::map<int, PortStatus> portStatusEntries,
    std::map<int, TransceiverInfo> transceiverEntries,
    std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries,
    std::map<int32_t, std::string> transceiverValidationEntries) const {
  RetType model;

  // Create lookup maps for interface name to ID
  std::map<std::string, int32_t> interfaceToPortId;
  for (const auto& [portId, portInfo] : portEntries) {
    interfaceToPortId[portInfo.name().value()] = portId;
  }

  auto getPortEntry = [&](const auto& intf) -> std::optional<PortStatus> {
    std::optional<PortStatus> portEntry;
    if (auto portIdIter = interfaceToPortId.find(intf);
        portIdIter != interfaceToPortId.end()) {
      if (auto portStatusIter = portStatusEntries.find(portIdIter->second);
          portStatusIter != portStatusEntries.end()) {
        portEntry = portStatusIter->second;
      }
    }
    return portEntry;
  };

  // Iterate over transceiver entries, since there are some transceivers
  // (bypass modules) that won't have port entries
  for (const auto& tcvrEntry : transceiverEntries) {
    const auto& transceiver = tcvrEntry.second;
    // First check if any interface on the transceiver has a corresponding
    // port on the agent. If not, we can assume it's a bypass module
    auto isBypassModule = true;
    for (const auto& intf : *transceiver.tcvrState()->interfaces()) {
      isBypassModule &= !getPortEntry(intf).has_value();
    }

    for (const auto& intf : *transceiver.tcvrState()->interfaces()) {
      if (!queriedPorts.empty() &&
          queriedPorts.find(intf) == queriedPorts.end()) {
        continue;
      }

      // For non-bypass modules, only consider the interfaces that have a
      // corresponding agent port (otherwise we'd print unused interfaces on
      // multi-port modules)
      const auto& portEntry = getPortEntry(intf);
      if (!isBypassModule && !portEntry.has_value()) {
        continue;
      }

      cli::TransceiverDetail details;
      details.name() = intf;

      const auto& tcvrState = *transceiver.tcvrState();
      const auto& tcvrStats = *transceiver.tcvrStats();

      if (portEntry.has_value()) {
        details.isUp() = folly::copy(portEntry->up().value());
      }

      details.isPresent() = folly::copy(tcvrState.present().value());
      details.mediaInterface() = tcvrState.moduleMediaInterface().value_or({});
      const auto& validationStringPair = getTransceiverValidationStrings(
          transceiverValidationEntries, *transceiver.tcvrState()->port());
      details.validationStatus() = validationStringPair.first;
      details.notValidatedReason() = validationStringPair.second;
      if (const auto& vendor = tcvrState.vendor()) {
        details.vendor() = vendor->name().value();
        details.serial() = vendor->serialNumber().value();
        details.partNumber() = vendor->partNumber().value();
        details.temperature() = folly::copy(
            apache::thrift::get_pointer(tcvrStats.sensor())
                ->temp()
                .value()
                .value()
                .value());
        details.voltage() = folly::copy(
            apache::thrift::get_pointer(tcvrStats.sensor())
                ->vcc()
                .value()
                .value()
                .value());
        details.tempFlags() = apache::thrift::get_pointer(tcvrStats.sensor())
                                  ->temp()
                                  .value()
                                  .flags()
                                  .value_or({});
        details.vccFlags() = apache::thrift::get_pointer(tcvrStats.sensor())
                                 ->vcc()
                                 .value()
                                 .flags()
                                 .value_or({});

        if (const auto& moduleStatus = tcvrState.status()) {
          if (const auto& fwStatus = moduleStatus->fwStatus()) {
            auto appFwVer = fwStatus->version().value_or("N/A");
            // If the firmware build number is available, append it to the app
            // firmware version as version.buildNumber (e.g. 1.0.7680).
            if (const auto& buildNumber = fwStatus->buildNumber()) {
              appFwVer = fmt::format("{}.{}", appFwVer, *buildNumber);
            }
            details.appFwVer() = appFwVer;
            details.dspFwVer() = fwStatus->dspFwVer().value_or("N/A");
          }
        }
        std::vector<double> current;
        std::vector<double> txPower;
        std::vector<double> rxPower;
        std::vector<double> rxSnr;
        for (const auto& channel : tcvrStats.channels().value()) {
          // Need to filter out the lanes for this specific port
          auto portToLaneMapIt = tcvrStats.portNameToMediaLanes()->find(intf);
          // If the port in question exists in the map and if the
          // channel number is not in the list, skip printing information
          // about this channel
          if (portToLaneMapIt != tcvrStats.portNameToMediaLanes()->end() &&
              std::find(
                  portToLaneMapIt->second.begin(),
                  portToLaneMapIt->second.end(),
                  folly::copy(channel.channel().value())) ==
                  portToLaneMapIt->second.end()) {
            continue;
          }
          // If the port doesn't exist in the map, it's likely not
          // configured yet. Display all channels in this case
          current.push_back(
              folly::copy(
                  channel.sensors().value().txBias().value().value().value()));
          txPower.push_back(
              folly::copy(
                  apache::thrift::get_pointer(
                      channel.sensors().value().txPwrdBm())
                      ->value()
                      .value()));
          rxPower.push_back(
              folly::copy(
                  apache::thrift::get_pointer(
                      channel.sensors().value().rxPwrdBm())
                      ->value()
                      .value()));
          if (const auto& snr = channel.sensors().value().rxSnr()) {
            rxSnr.push_back(folly::copy(snr->value().value()));
          }
        }
        details.currentMA() = current;
        details.txPower() = txPower;
        details.rxPower() = rxPower;
        details.rxSnr() = rxSnr;
      }
      model.transceivers()->emplace(intf, std::move(details));
    }
  }

  return model;
}

std::string_view CmdShowTransceiverTraits::description() {
  return "Displays each interface's transceiver: presence and status, config-validation result, vendor/serial/part number, firmware versions, and live optics readings (temperature, voltage, current, Tx/Rx power, SNR). Use it to check optic health and inventory.";
}

CmdShowTransceiver::RetType CmdShowTransceiver::sampleModel() {
  RetType model;

  cli::TransceiverDetail entry1;
  entry1.name() = "eth1/11/1";
  entry1.isUp() = true;
  entry1.isPresent() = true;
  entry1.mediaInterface() = MediaInterfaceCode::DR4_400G;
  entry1.validationStatus() = "--";
  entry1.notValidatedReason() = "--";
  entry1.vendor() = "Arista Networks";
  entry1.serial() = "MOCKSERIAL001";
  entry1.partNumber() = "FB-P800G-2XDR4-1";
  entry1.appFwVer() = "3.7.0";
  entry1.dspFwVer() = "0.0";
  entry1.temperature() = 62.09;
  entry1.voltage() = 3.28;
  entry1.tempFlags() = FlagLevels{};
  entry1.vccFlags() = FlagLevels{};
  entry1.currentMA() = {80.20, 80.20, 64.98, 74.53};
  entry1.txPower() = {1.25, 1.14, 1.07, 1.15};
  entry1.rxPower() = {1.25, 0.78, 1.43, 1.41};
  entry1.rxSnr() = {22.70, 22.63, 22.39, 22.43};

  cli::TransceiverDetail entry2;
  entry2.name() = "eth1/11/5";
  entry2.isUp() = true;
  entry2.isPresent() = true;
  entry2.mediaInterface() = MediaInterfaceCode::DR4_400G;
  entry2.validationStatus() = "--";
  entry2.notValidatedReason() = "--";
  entry2.vendor() = "Arista Networks";
  entry2.serial() = "MOCKSERIAL001";
  entry2.partNumber() = "FB-P800G-2XDR4-1";
  entry2.appFwVer() = "3.7.0";
  entry2.dspFwVer() = "0.0";
  entry2.temperature() = 62.09;
  entry2.voltage() = 3.28;
  entry2.tempFlags() = FlagLevels{};
  entry2.vccFlags() = FlagLevels{};
  entry2.currentMA() = {};
  entry2.txPower() = {};
  entry2.rxPower() = {};
  entry2.rxSnr() = {};

  model.transceivers()->emplace("eth1/11/1", std::move(entry1));
  model.transceivers()->emplace("eth1/11/5", std::move(entry2));

  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowTransceiver, CmdShowTransceiverTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowTransceiver, CmdShowTransceiverTraits>::getValidFilters();

} // namespace facebook::fboss
