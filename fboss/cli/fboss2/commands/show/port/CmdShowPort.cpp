/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowPort.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/cli/fboss2/commands/show/port/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <algorithm>
#include <array>

namespace facebook::fboss {
using utils::Table;
using ObjectArgType = CmdShowPortTraits::ObjectArgType;
using RetType = CmdShowPortTraits::RetType;

namespace {

constexpr auto kWindowedCounterRegex =
    R"(.*\.(in_bytes|out_bytes|in_unicast_pkts|in_multicast_pkts|in_broadcast_pkts|out_unicast_pkts|out_multicast_pkts|out_broadcast_pkts|in_errors|in_ipv4_header_errors|in_ipv6_header_errors|in_discards|out_errors|out_discards|out_congestion_discards|in_pause_frames|out_pause_frames)\.sum\.(60|600|3600)$|.*\.queue[0-9]+\..*\.(out_bytes|out_congestion_discards)\.sum\.(60|600|3600)$)";

std::string formatHwCounter(int64_t value) {
  return value == hardware_stats_constants::STAT_UNINITIALIZED()
      ? "n/a"
      : folly::to<std::string>(value);
}

std::optional<cli::WindowedCounter> getWindowedCounter(
    const std::map<std::string, int64_t>& counters,
    const std::string& counterName,
    int64_t multiplier = 1,
    bool perSecond = false) {
  cli::WindowedCounter result;
  bool found = false;

  const auto getValue = [&](int window) -> std::optional<int64_t> {
    const auto iter =
        counters.find(fmt::format("{}.sum.{}", counterName, window));
    if (iter == counters.end()) {
      return std::nullopt;
    }
    const auto value = iter->second * multiplier;
    return perSecond ? value / window : value;
  };

  if (auto value = getValue(60)) {
    result.oneMinute() = *value;
    found = true;
  }
  if (auto value = getValue(600)) {
    result.tenMinutes() = *value;
    found = true;
  }
  if (auto value = getValue(3600)) {
    result.oneHour() = *value;
    found = true;
  }
  return found ? std::optional<cli::WindowedCounter>(std::move(result))
               : std::nullopt;
}

std::string formatRate(const std::optional<int64_t>& rate, bool bitsPerSecond) {
  if (!rate.has_value()) {
    return "--";
  }

  struct Unit {
    double scale;
    std::string_view suffix;
  };
  const std::array<Unit, 4> units = bitsPerSecond
      ? std::array<
            Unit,
            4>{Unit{1e12, "Tbps"}, Unit{1e9, "Gbps"}, Unit{1e6, "Mbps"}, Unit{1e3, "Kbps"}}
      : std::array<Unit, 4>{
            Unit{1e9, "Gpps"},
            Unit{1e6, "Mpps"},
            Unit{1e3, "Kpps"},
            Unit{1, "pps"}};

  for (const auto& unit : units) {
    if (*rate >= unit.scale) {
      return fmt::format("{:.2f} {}", *rate / unit.scale, unit.suffix);
    }
  }
  return fmt::format("{} {}", *rate, bitsPerSecond ? "bps" : "pps");
}

enum class WindowedValueType {
  BITS_PER_SECOND,
  PACKETS,
  PACKETS_PER_SECOND,
};

std::string formatWindowedValue(
    const std::optional<int64_t>& value,
    WindowedValueType type) {
  if (type == WindowedValueType::PACKETS) {
    return value.has_value() ? folly::to<std::string>(*value) : "--";
  }
  return formatRate(value, type == WindowedValueType::BITS_PER_SECOND);
}

void appendWindowedCounter(
    std::vector<std::string>& output,
    std::string_view label,
    const std::optional<cli::WindowedCounter>& counter,
    WindowedValueType type) {
  if (!counter.has_value()) {
    return;
  }
  output.emplace_back(
      fmt::format(
          "\t{:<36}{:>16}{:>16}{:>16}",
          label,
          formatWindowedValue(counter->oneMinute().to_optional(), type),
          formatWindowedValue(counter->tenMinutes().to_optional(), type),
          formatWindowedValue(counter->oneHour().to_optional(), type)));
}

void appendWindowedQueueCounters(
    std::vector<std::string>& output,
    const std::map<int16_t, cli::WindowedCounter>& rates,
    const std::map<int16_t, std::string>& queueIdToName,
    WindowedValueType type) {
  for (const auto& [queueId, rate] : rates) {
    const auto queueNameIter = queueIdToName.find(queueId);
    const auto queueLabel = queueNameIter == queueIdToName.end()
        ? fmt::format("    Queue {}", queueId)
        : fmt::format("    Queue {} ({})", queueId, queueNameIter->second);
    appendWindowedCounter(output, queueLabel, rate, type);
  }
}

} // namespace

std::string getAdminStateStr(PortAdminState adminState) {
  switch (adminState) {
    case PortAdminState::DISABLED:
      return "Disabled";
    case PortAdminState::ENABLED:
      return "Enabled";
  }

  throw std::runtime_error(
      "Unsupported AdminState: " +
      std::to_string(static_cast<int>(adminState)));
}
std::string getOptionalIntStr(std::optional<int> val) {
  if (val.has_value()) {
    return folly::to<std::string>(*val);
  }
  return "--";
}

std::string getOperStateStr(PortOperState operState) {
  switch (operState) {
    case PortOperState::DOWN:
      return "Down";
    case PortOperState::UP:
      return "Up";
  }

  throw std::runtime_error(
      "Unsupported LinkState: " + std::to_string(static_cast<int>(operState)));
}

Table::StyledCell getStyledActiveState(
    const std::string& activeState,
    bool isMismatch) {
  if (isMismatch) {
    return Table::StyledCell(activeState + " (Mismatch)", Table::Style::ERROR);
  } else if (activeState == "Inactive") {
    return Table::StyledCell(activeState, Table::Style::ERROR);
  } else if (activeState == "Active") {
    return Table::StyledCell(activeState, Table::Style::GOOD);
  }
  return Table::StyledCell("--", Table::Style::NONE);
}

Table::StyledCell getStyledLinkState(std::string linkState) {
  if (linkState == "Down") {
    return Table::StyledCell("Down", Table::Style::ERROR);
  } else {
    return Table::StyledCell("Up", Table::Style::GOOD);
  }

  throw std::runtime_error("Unsupported LinkState: " + linkState);
}

std::string getActiveStateStr(PortActiveState* activeState) {
  if (activeState) {
    switch (*activeState) {
      case PortActiveState::INACTIVE:
        return "Inactive";
      case PortActiveState::ACTIVE:
        return "Active";
    }

    throw std::runtime_error(
        "Unsupported ActiveState: " +
        std::to_string(static_cast<int>(*activeState)));
  } else {
    return "--";
  }
}

std::string getTransceiverStr(
    const std::map<int32_t, facebook::fboss::TransceiverInfo>&
        transceiverEntries,
    int32_t transceiverId) {
  auto entry = transceiverEntries.find(transceiverId);
  if (entry == transceiverEntries.end()) {
    return "";
  }

  if (*entry->second.tcvrState()->present()) {
    return "Present";
  }
  return "Absent";
}

std::string getCbfcStr(const CbfcInfo& cbfc) {
  auto cbfcStr = fmt::format(
      "{}, VC {}",
      *cbfc.configName(),
      folly::join(" ", *cbfc.virtualChannels()));
  if (cbfc.senderCreditSize() && cbfc.senderPacketOverhead()) {
    cbfcStr += fmt::format(
        ", sender {} B / {} B",
        *cbfc.senderCreditSize(),
        *cbfc.senderPacketOverhead());
  }
  if (cbfc.senderCreditLimit()) {
    cbfcStr += fmt::format(", sender limit {}", *cbfc.senderCreditLimit());
  }
  if (cbfc.receiverCreditLimit()) {
    cbfcStr += fmt::format(", receiver limit {}", *cbfc.receiverCreditLimit());
  }
  return cbfcStr;
}

Table::StyledCell getStyledErrors(std::string errors) {
  if (errors == "--") {
    return Table::StyledCell(errors, Table::Style::NONE);
  } else {
    return Table::StyledCell(errors, Table::Style::ERROR);
  }
}

std::unordered_map<std::string, std::vector<std::string>>
CmdShowPort::getAcceptedFilterValues() {
  return {
      {"adminState", {"Enabled", "Disabled"}},
      {"linkState", {"Up", "Down"}},
      {"activeState", {"Active", "Inactive", "--"}},
      {"userMetaData", {"Unconstrained", "Restricted", "Blocked", "--"}},
  };
}

PeerInfo CmdShowPort::getFabPortPeerInfo(const auto& hostInfo) const {
  auto fabricEntries = utils::getFabricEndpoints(hostInfo);
  std::unordered_map<std::string, Endpoint> portToPeer;
  std::unordered_set<std::string> peers;
  for (auto const& [localPort, endpoint] : fabricEntries) {
    Endpoint ep;
    ep.isAttached = endpoint.isAttached().value();
    if (endpoint.expectedSwitchName()) {
      ep.expectedSwitchName = *endpoint.expectedSwitchName();
    }
    if (endpoint.isAttached().value() && endpoint.switchName()) {
      ep.attachedSwitchName = *endpoint.switchName();
      peers.insert(*endpoint.switchName());
    }
    if (endpoint.isAttached().value() && endpoint.portName()) {
      ep.attachedRemotePortName = *endpoint.portName();
    }
    portToPeer[localPort] = ep;
  }

  PeerInfo peerInfo;
  peerInfo.fabPort2Peer = portToPeer;
  peerInfo.allPeers = peers;
  return peerInfo;
}

PeerDrainState CmdShowPort::asyncGetDrainState(
    std::shared_ptr<apache::thrift::Client<FbossCtrl>> client) const {
  PeerDrainState entries;
  try {
    client->sync_getActualSwitchDrainState(entries);
  } catch (const std::exception&) {
    // Continue
  }
  return entries;
}

std::unordered_map<std::string, cfg::SwitchDrainState>
CmdShowPort::getPeerDrainStates(const PeerInfo& peerInfo) {
  // Launch futures
  std::unordered_set<std::string> peersChecked;
  std::unordered_map<std::string, std::shared_future<PeerDrainState>> futures;
  for (const auto& peer : peerInfo.allPeers) {
    if (!clients_.contains(peer)) {
      clients_[peer] = utils::createClient<apache::thrift::Client<FbossCtrl>>(
          HostInfo(peer), peerTimeout);
    }
    futures[peer] = std::async(
        std::launch::async,
        &CmdShowPort::asyncGetDrainState,
        this,
        clients_[peer]);
  };

  // Get results
  std::unordered_map<std::string, cfg::SwitchDrainState> peerToDrainState;
  for (const auto& [peer, f] : futures) {
    auto entries = f.get();
    if (!entries.empty()) {
      peerToDrainState[peer] = entries.begin()->second;
    }
  }

  // Map local port to peer drain state
  std::unordered_map<std::string, cfg::SwitchDrainState> portToPeerDrainState;
  for (const auto& [localPort, peer] : peerInfo.fabPort2Peer) {
    auto& attachedPeer = peer.attachedSwitchName;
    if (peerToDrainState.contains(attachedPeer)) {
      portToPeerDrainState[localPort] = peerToDrainState[attachedPeer];
    }
  }
  return portToPeerDrainState;
}

PortIdToInfo CmdShowPort::asyncGetPortInfo(
    std::shared_ptr<apache::thrift::Client<FbossCtrl>> client) const {
  PortIdToInfo entries;
  try {
    client->sync_getAllPortInfo(entries);
  } catch (const std::exception&) {
    // Continue
  }
  return entries;
}

std::unordered_map<std::string, PortNameToInfo> CmdShowPort::getPeerToPorts(
    const std::unordered_set<std::string>& hosts) {
  // Launch futures
  std::unordered_map<std::string, std::shared_future<PortIdToInfo>> futures;
  for (const auto& host : hosts) {
    if (!clients_.contains(host)) {
      clients_[host] = utils::createClient<apache::thrift::Client<FbossCtrl>>(
          HostInfo(host), peerTimeout);
    }
    futures[host] = std::async(
        std::launch::async,
        &CmdShowPort::asyncGetPortInfo,
        this,
        clients_[host]);
  }

  // Get results
  std::unordered_map<std::string, PortNameToInfo> peerToPorts;
  for (const auto& [peer, f] : futures) {
    auto& portIdToInfo = f.get();
    if (portIdToInfo.empty()) {
      continue;
    }

    // Remap key from portId to portName
    PortNameToInfo portNameToInfo{};
    for (const auto& [_, portInfo] : portIdToInfo) {
      portNameToInfo[portInfo.name().value()] = portInfo;
    }
    peerToPorts[peer] = portNameToInfo;
  }
  return peerToPorts;
}

std::unordered_map<std::string, PeerPortInfo> CmdShowPort::getPeerPortInfo(
    const PeerInfo& peerInfo) {
  auto peerToPorts = getPeerToPorts(peerInfo.allPeers);

  // Populate peer port states
  std::unordered_map<std::string, PeerPortInfo> peerPortInfo;
  for (const auto& [localPort, peer] : peerInfo.fabPort2Peer) {
    if (peer.attachedSwitchName.empty() ||
        peer.attachedRemotePortName.empty()) {
      continue;
    }

    auto peerPortMapIt = peerToPorts.find(peer.attachedSwitchName);
    if (peerPortMapIt == peerToPorts.end()) {
      continue;
    }
    auto peerPortNameToInfo = peerPortMapIt->second;

    auto peerPortInfoIt = peerPortNameToInfo.find(peer.attachedRemotePortName);
    if (peerPortInfoIt == peerPortNameToInfo.end()) {
      continue;
    }
    auto peerPortInfoThrift = peerPortInfoIt->second;

    PeerPortInfo portInfo;
    portInfo.drainedOrDown =
        (peerPortInfoThrift.operState().value() == PortOperState::DOWN ||
         peerPortInfoThrift.adminState().value() == PortAdminState::DISABLED ||
         peerPortInfoThrift.isDrained().value() == true);
    if (auto cableLenMeters = peerPortInfoThrift.cableLengthMeters()) {
      portInfo.cableLenMeters = *cableLenMeters;
    }
    peerPortInfo[localPort] = portInfo;
  }
  return peerPortInfo;
}

RetType CmdShowPort::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedPorts) {
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::map<int32_t, facebook::fboss::TransceiverInfo> transceiverEntries;
  std::map<std::string, facebook::fboss::HwPortStats> portStats;
  std::map<std::string, int64_t> fb303Counters;

  std::vector<int32_t> requiredTransceiverEntries;
  std::vector<std::string> bgpDrainedInterfaces;

  auto client =
      utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
  client->sync_getAllPortInfo(portEntries);

  auto opt = CmdGlobalOptions::getInstance();
  if (opt->isDetailed()) {
    client->sync_getHwPortStats(portStats);
    fb303Counters =
        utils::getAgentFb303RegexCounters(hostInfo, kWindowedCounterRegex);
  }

  try {
    auto qsfpService =
        utils::createClient<facebook::fboss::QsfpServiceAsyncClient>(hostInfo);

    qsfpService->sync_getTransceiverInfo(
        transceiverEntries, requiredTransceiverEntries);
  } catch (apache::thrift::transport::TTransportException&) {
    std::cerr << "Cannot connect to qsfp_service\n";
  }

  // Get peer drain state
  std::unordered_map<std::string, Endpoint> portToPeer;
  std::unordered_map<std::string, cfg::SwitchDrainState> peerDrainStates;
  std::unordered_map<std::string, PeerPortInfo> peerPortInfo;
  if (utils::isVoqOrFabric(utils::getSwitchType(*client))) {
    auto peerInfo = getFabPortPeerInfo(hostInfo);
    portToPeer = peerInfo.fabPort2Peer;
    peerDrainStates = getPeerDrainStates(peerInfo);
    peerPortInfo = getPeerPortInfo(peerInfo);
  }

  return createModel(
      portEntries,
      transceiverEntries,
      queriedPorts.data(),
      portStats,
      portToPeer,
      peerDrainStates,
      peerPortInfo,
      utils::getBgpDrainedInterafces(hostInfo),
      fb303Counters);
}

RetType CmdShowPort::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
    const std::map<int32_t, facebook::fboss::TransceiverInfo>&
        transceiverEntries,
    const ObjectArgType& queriedPorts,
    const std::map<std::string, facebook::fboss::HwPortStats>& portStats,
    const std::unordered_map<std::string, Endpoint>& portToPeer,
    const std::unordered_map<std::string, cfg::SwitchDrainState>&
        peerDrainStates,
    const std::unordered_map<std::string, PeerPortInfo>& peerPortInfo,
    const std::vector<std::string>& drainedInterfaces,
    const std::map<std::string, int64_t>& fb303Counters) {
  RetType model;
  std::unordered_set<std::string> queriedSet(
      queriedPorts.begin(), queriedPorts.end());

  for (const auto& entry : portEntries) {
    auto portInfo = entry.second;
    auto portName = portInfo.name().value();
    auto operState = getOperStateStr(folly::copy(portInfo.operState().value()));
    auto activeState =
        getActiveStateStr(apache::thrift::get_pointer(portInfo.activeState()));

    if (queriedPorts.size() == 0 || queriedSet.count(portName)) {
      bool isPortDetached = false;
      if (auto peer = portToPeer.find(portName); peer != portToPeer.end()) {
        isPortDetached = !peer->second.isAttached;
      }
      bool isPortDisabled =
          (portInfo.adminState().value() == PortAdminState::DISABLED);
      bool isPortDrained =
          (std::find(
               drainedInterfaces.begin(), drainedInterfaces.end(), portName) !=
           drainedInterfaces.end()) ||
          portInfo.isDrained().value();

      std::optional<bool> isActive;
      if (portInfo.activeState().has_value()) {
        isActive = (portInfo.activeState().value() == PortActiveState::ACTIVE);
      }
      std::optional<bool> isPeerDrained;
      if (peerDrainStates.contains(portName)) {
        isPeerDrained =
            (peerDrainStates.at(portName) == cfg::SwitchDrainState::DRAINED);
      }
      std::optional<bool> isPeerPortDrainedOrDown;
      std::optional<uint64_t> peerPortCableLenMeters;
      auto it = peerPortInfo.find(portName);
      if (it != peerPortInfo.end()) {
        isPeerPortDrainedOrDown = it->second.drainedOrDown;
        if (it->second.cableLenMeters) {
          peerPortCableLenMeters = *it->second.cableLenMeters;
        }
      }

      bool canDetermineExpectedActiveState =
          (isActive.has_value() && isPeerDrained.has_value() &&
           isPeerPortDrainedOrDown.has_value());

      bool activeStateMismatch = false;
      if (canDetermineExpectedActiveState) {
        bool expectedActive = !isPortDetached && !isPortDisabled &&
            !isPortDrained && !isPeerDrained.value() &&
            !isPeerPortDrainedOrDown.value();
        activeStateMismatch = (isActive.value() != expectedActive);
      }
      std::string cableLenMeters = "--";
      if (auto cableLen = portInfo.cableLengthMeters()) {
        cableLenMeters = folly::to<std::string>(*cableLen);
      } else if (peerPortCableLenMeters.has_value()) {
        cableLenMeters = folly::to<std::string>(*peerPortCableLenMeters);
      }

      cli::PortEntry portDetails;
      portDetails.id() = folly::copy(portInfo.portId().value());
      portDetails.name() = portInfo.name().value();
      portDetails.adminState() =
          getAdminStateStr(folly::copy(portInfo.adminState().value()));
      portDetails.linkState() = operState;
      portDetails.activeState() = activeState;
      portDetails.activeStateMismatch() = activeStateMismatch;
      portDetails.cableLengthMeters() = cableLenMeters;
      // Left unset unless the port has an LLR profile bound on an LLR-capable
      // ASIC, so the detail view omits the rows entirely rather than showing
      // them empty on the platforms that do not run LLR.
      if (auto llrTxStatus = portInfo.llrTxStatus()) {
        portDetails.llrTxStatus() =
            apache::thrift::util::enumNameSafe(*llrTxStatus);
      }
      if (auto llrRxStatus = portInfo.llrRxStatus()) {
        portDetails.llrRxStatus() =
            apache::thrift::util::enumNameSafe(*llrRxStatus);
      }
      if (auto cbfc = apache::thrift::get_pointer(portInfo.cbfc())) {
        portDetails.cbfc() = getCbfcStr(*cbfc);
      }
      portDetails.userMetaData() = utils::getAclLookupClassPortStr(
          portInfo.userMetaData().to_optional());
      portDetails.ingressAclTable() =
          portInfo.ingressAclTableName().to_optional().value_or("--");
      portDetails.speed() =
          utils::getSpeedGbps(folly::copy(portInfo.speedMbps().value()));
      portDetails.profileId() = portInfo.profileID().value();
      portDetails.coreId() = getOptionalIntStr(portInfo.coreId().to_optional());
      portDetails.virtualDeviceId() =
          getOptionalIntStr(portInfo.virtualDeviceId().to_optional());
      if (portInfo.activeErrors()->size()) {
        std::vector<std::string> errorStrs;
        std::for_each(
            portInfo.activeErrors()->begin(),
            portInfo.activeErrors()->end(),
            [&errorStrs](auto error) {
              errorStrs.push_back(apache::thrift::util::enumNameSafe(error));
            });
        portDetails.activeErrors() = folly::join(",", errorStrs);
      } else {
        portDetails.activeErrors() = "--";
      }
      if (auto hwLogicalPortId = portInfo.hwLogicalPortId()) {
        portDetails.hwLogicalPortId() = *hwLogicalPortId;
      }
      portDetails.isDrained() = "No";
      if ((std::find(
               drainedInterfaces.begin(), drainedInterfaces.end(), portName) !=
           drainedInterfaces.end()) ||
          folly::copy(portInfo.isDrained().value())) {
        portDetails.isDrained() = "Yes";
      }
      portDetails.peerSwitchDrained() = isPeerDrained.has_value()
          ? (isPeerDrained.value() ? "Yes" : "No")
          : "--";
      portDetails.peerPortDrainedOrDown() = isPeerPortDrainedOrDown.has_value()
          ? (isPeerPortDrainedOrDown.value() ? "Yes" : "No")
          : "--";
      if (auto tcvrId = portInfo.transceiverIdx()) {
        const auto transceiverId = folly::copy(tcvrId->transceiverId().value());
        portDetails.tcvrID() = transceiverId;
        portDetails.tcvrPresent() =
            getTransceiverStr(transceiverEntries, transceiverId);
      }
      if (auto pfc = apache::thrift::get_pointer(portInfo.pfc())) {
        std::string pfcString;
        if (*pfc->tx()) {
          pfcString = "TX ";
        }
        if (*pfc->rx()) {
          pfcString += "RX ";
        }
        if (*pfc->watchdog()) {
          pfcString += "WD";
        }
        portDetails.pfc() = pfcString;
      } else {
        std::string pauseString;
        if (folly::copy(portInfo.txPause().value())) {
          pauseString = "TX ";
        }
        if (folly::copy(portInfo.rxPause().value())) {
          pauseString += "RX";
        }
        portDetails.pause() = pauseString;
      }
      portDetails.numUnicastQueues() = portInfo.portQueues().value().size();
      for (const auto& queue : portInfo.portQueues().value()) {
        if (!queue.name().value().empty()) {
          portDetails.queueIdToName()->insert(
              {folly::copy(queue.id().value()), queue.name().value()});
        }
      }

      const auto& iter = portStats.find(portName);
      if (iter != portStats.end()) {
        auto portHwStatsEntry = iter->second;
        cli::PortHwStatsEntry cliPortStats;
        cliPortStats.inUnicastPkts() =
            folly::copy(portHwStatsEntry.inUnicastPkts_().value());
        cliPortStats.inMulticastPkts() =
            folly::copy(portHwStatsEntry.inMulticastPkts_().value());
        cliPortStats.inBroadcastPkts() =
            folly::copy(portHwStatsEntry.inBroadcastPkts_().value());
        cliPortStats.inDiscardPkts() =
            folly::copy(portHwStatsEntry.inDiscards_().value());
        cliPortStats.inErrorPkts() =
            folly::copy(portHwStatsEntry.inErrors_().value());
        cliPortStats.inIpv4HeaderErrorPkts() =
            folly::copy(portHwStatsEntry.inIpv4HdrErrors_().value());
        cliPortStats.inIpv6HeaderErrorPkts() =
            folly::copy(portHwStatsEntry.inIpv6HdrErrors_().value());
        cliPortStats.outUnicastPkts() =
            folly::copy(portHwStatsEntry.outUnicastPkts_().value());
        cliPortStats.outMulticastPkts() =
            folly::copy(portHwStatsEntry.outMulticastPkts_().value());
        cliPortStats.outBroadcastPkts() =
            folly::copy(portHwStatsEntry.outBroadcastPkts_().value());
        cliPortStats.outErrorPkts() =
            folly::copy(portHwStatsEntry.outErrors_().value());
        cliPortStats.outDiscardPkts() =
            folly::copy(portHwStatsEntry.outDiscards_().value());
        cliPortStats.outCongestionDiscardPkts() =
            folly::copy(portHwStatsEntry.outCongestionDiscardPkts_().value());
        cliPortStats.queueOutDiscardBytes() =
            portHwStatsEntry.queueOutDiscardBytes_().value();
        cliPortStats.queueOutDiscardPackets() =
            portHwStatsEntry.queueOutDiscardPackets_().value();
        cliPortStats.inCongestionDiscards() =
            folly::copy(portHwStatsEntry.inCongestionDiscards_().value());
        cliPortStats.queueOutBytes() =
            portHwStatsEntry.queueOutBytes_().value();
        for (const auto& queue : portInfo.portQueues().value()) {
          const auto queueId = folly::copy(queue.id().value());
          const auto queueName = queue.name().value().empty()
              ? fmt::format("queue{}", queueId)
              : queue.name().value();
          const auto counterPrefix =
              fmt::format("{}.queue{}.{}", portName, queueId, queueName);
          const auto queueOutBytes =
              portHwStatsEntry.queueOutBytes_()->find(queueId);
          if (queueOutBytes != portHwStatsEntry.queueOutBytes_()->end() &&
              (queueOutBytes->second != 0 || !queue.name()->empty())) {
            if (auto rates = getWindowedCounter(
                    fb303Counters, counterPrefix + ".out_bytes", 8, true)) {
              cliPortStats.queueOutBitsPerSecond()[queueId] = std::move(*rates);
            }
          }
          const auto queueOutDiscardPackets =
              portHwStatsEntry.queueOutDiscardPackets_()->find(queueId);
          if (queueOutDiscardPackets !=
                  portHwStatsEntry.queueOutDiscardPackets_()->end() &&
              (queueOutDiscardPackets->second != 0 || !queue.name()->empty())) {
            if (auto rates = getWindowedCounter(
                    fb303Counters,
                    counterPrefix + ".out_congestion_discards",
                    1,
                    true)) {
              cliPortStats
                  .queueOutCongestionDiscardPacketsPerSecond()[queueId] =
                  std::move(*rates);
            }
          }
        }
        if (apache::thrift::get_pointer(portInfo.pfc())) {
          cliPortStats.outPfcPriorityPackets() =
              portHwStatsEntry.outPfc_().value();
          cliPortStats.inPfcPriorityPackets() =
              portHwStatsEntry.inPfc_().value();
          cliPortStats.outPfcPackets() =
              folly::copy(portHwStatsEntry.outPfcCtrl_().value());
          cliPortStats.inPfcPackets() =
              folly::copy(portHwStatsEntry.inPfcCtrl_().value());
        } else {
          cliPortStats.outPausePackets() =
              folly::copy(portHwStatsEntry.outPause_().value());
          cliPortStats.inPausePackets() =
              folly::copy(portHwStatsEntry.inPause_().value());
        }
        cliPortStats.ingressBytes() =
            folly::copy(portHwStatsEntry.inBytes_().value());
        cliPortStats.egressBytes() =
            folly::copy(portHwStatsEntry.outBytes_().value());
        cli::PortWindowedStatsEntry windowedStats;
        windowedStats.ingressBitsPerSecond().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_bytes", 8, true));
        windowedStats.egressBitsPerSecond().from_optional(getWindowedCounter(
            fb303Counters, portName + ".out_bytes", 8, true));
        windowedStats.inUnicastPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_unicast_pkts"));
        windowedStats.inMulticastPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_multicast_pkts"));
        windowedStats.inBroadcastPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_broadcast_pkts"));
        windowedStats.outUnicastPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".out_unicast_pkts"));
        windowedStats.outMulticastPkts().from_optional(getWindowedCounter(
            fb303Counters, portName + ".out_multicast_pkts"));
        windowedStats.outBroadcastPkts().from_optional(getWindowedCounter(
            fb303Counters, portName + ".out_broadcast_pkts"));
        windowedStats.inErrorPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_errors"));
        windowedStats.inIpv4HeaderErrorPkts().from_optional(getWindowedCounter(
            fb303Counters, portName + ".in_ipv4_header_errors"));
        windowedStats.inIpv6HeaderErrorPkts().from_optional(getWindowedCounter(
            fb303Counters, portName + ".in_ipv6_header_errors"));
        windowedStats.inDiscardPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_discards"));
        windowedStats.outErrorPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".out_errors"));
        windowedStats.outDiscardPkts().from_optional(
            getWindowedCounter(fb303Counters, portName + ".out_discards"));
        windowedStats.outCongestionDiscardPkts().from_optional(
            getWindowedCounter(
                fb303Counters, portName + ".out_congestion_discards"));
        windowedStats.inPausePackets().from_optional(
            getWindowedCounter(fb303Counters, portName + ".in_pause_frames"));
        windowedStats.outPausePackets().from_optional(
            getWindowedCounter(fb303Counters, portName + ".out_pause_frames"));
        cliPortStats.windowedStats() = std::move(windowedStats);
        cliPortStats.cbfcCfUpdateTx().from_optional(
            portHwStatsEntry.cbfcCfUpdateTx_().to_optional());
        cliPortStats.cbfcCfUpdateRx().from_optional(
            portHwStatsEntry.cbfcCfUpdateRx_().to_optional());
        cliPortStats.cbfcCcUpdateTx().from_optional(
            portHwStatsEntry.cbfcCcUpdateTx_().to_optional());
        portDetails.hwPortStats() = cliPortStats;
      }
      model.portEntries()->push_back(portDetails);
    }
  }

  std::sort(
      model.portEntries()->begin(),
      model.portEntries()->end(),
      [&](const cli::PortEntry& a, const cli::PortEntry& b) {
        return utils::comparePortNameOrId(
            a.name().value(),
            folly::copy(a.id().value()),
            b.name().value(),
            folly::copy(b.id().value()));
      });

  return model;
}

void CmdShowPort::printOutput(const RetType& model, std::ostream& out) {
  std::vector<std::string> detailedOutput;
  auto opt = CmdGlobalOptions::getInstance();

  if (opt->isDetailed()) {
    for (auto const& portInfo : model.portEntries().value()) {
      std::string hwLogicalPortId;
      if (auto portId = portInfo.hwLogicalPortId()) {
        hwLogicalPortId = folly::to<std::string>(*portId);
      }

      const auto& portHwStats = portInfo.hwPortStats().value();
      detailedOutput.emplace_back("");
      detailedOutput.emplace_back(
          fmt::format("Name:           \t\t {}", portInfo.name().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "ID:             \t\t {}",
              folly::to<std::string>(folly::copy(portInfo.id().value()))));
      detailedOutput.emplace_back(
          fmt::format(
              "Admin State:    \t\t {}", portInfo.adminState().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "Active Errors:  \t\t {}", portInfo.activeErrors().value()));
      detailedOutput.emplace_back(
          fmt::format("Speed:          \t\t {}", portInfo.speed().value()));
      detailedOutput.emplace_back(
          fmt::format("LinkState:      \t\t {}", portInfo.linkState().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "TcvrID:         \t\t {}",
              folly::to<std::string>(folly::copy(portInfo.tcvrID().value()))));
      detailedOutput.emplace_back(
          fmt::format(
              "Transceiver:    \t\t {}", portInfo.tcvrPresent().value()));
      detailedOutput.emplace_back(
          fmt::format("ProfileID:      \t\t {}", portInfo.profileId().value()));
      detailedOutput.emplace_back(
          fmt::format("ProfileID:      \t\t {}", hwLogicalPortId));
      detailedOutput.emplace_back(
          fmt::format(
              "Core ID:             \t\t {}", portInfo.coreId().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "Virtual device ID:    \t\t {}",
              portInfo.virtualDeviceId().value()));
      if (apache::thrift::get_pointer(portInfo.pause())) {
        detailedOutput.emplace_back(
            fmt::format(
                "Pause:          \t\t {}",
                *apache::thrift::get_pointer(portInfo.pause())));
      } else if (apache::thrift::get_pointer(portInfo.pfc())) {
        detailedOutput.emplace_back(
            fmt::format(
                "PFC:            \t\t {}",
                *apache::thrift::get_pointer(portInfo.pfc())));
      }
      if (apache::thrift::get_pointer(portInfo.llrTxStatus())) {
        detailedOutput.emplace_back(
            fmt::format(
                "LLR TX status:  \t\t {}",
                *apache::thrift::get_pointer(portInfo.llrTxStatus())));
      }
      if (apache::thrift::get_pointer(portInfo.llrRxStatus())) {
        detailedOutput.emplace_back(
            fmt::format(
                "LLR RX status:  \t\t {}",
                *apache::thrift::get_pointer(portInfo.llrRxStatus())));
      }
      if (apache::thrift::get_pointer(portInfo.cbfc())) {
        detailedOutput.emplace_back(
            fmt::format(
                "CBFC:           \t\t {}",
                *apache::thrift::get_pointer(portInfo.cbfc())));
      }
      detailedOutput.emplace_back(
          fmt::format(
              "User metadata:  \t\t {}", portInfo.userMetaData().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "Ingress ACL table:\t\t {}", portInfo.ingressAclTable().value()));
      detailedOutput.emplace_back(
          fmt::format(
              "Unicast queues: \t\t {}",
              folly::to<std::string>(
                  folly::copy(portInfo.numUnicastQueues().value()))));
      detailedOutput.emplace_back("    Cumulative hardware counters");
      detailedOutput.emplace_back(
          fmt::format(
              "    Ingress (bytes)               \t\t {}",
              formatHwCounter(portHwStats.ingressBytes().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Egress (bytes)                \t\t {}",
              formatHwCounter(portHwStats.egressBytes().value())));
      for (const auto& queueBytes : portHwStats.queueOutBytes().value()) {
        const auto iter =
            portInfo.queueIdToName().value().find(queueBytes.first);
        std::string queueName;
        if (iter != portInfo.queueIdToName().value().end()) {
          queueName = folly::to<std::string>("(", iter->second, ")");
        }
        // print either if the queue is valid or queue has non zero traffic
        if (queueBytes.second || !queueName.empty()) {
          detailedOutput.emplace_back(
              fmt::format(
                  "\tQueue {} {:12}    \t\t {}",
                  queueBytes.first,
                  queueName,
                  queueBytes.second));
        }
      }
      detailedOutput.emplace_back(
          fmt::format(
              "    Received Unicast (pkts)       \t\t {}",
              formatHwCounter(portHwStats.inUnicastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Received Multicast (pkts)     \t\t {}",
              formatHwCounter(portHwStats.inMulticastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Received Broadcast (pkts)     \t\t {}",
              formatHwCounter(portHwStats.inBroadcastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Sent Unicast (pkts)           \t\t {}",
              formatHwCounter(portHwStats.outUnicastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Sent Multicast (pkts)         \t\t {}",
              formatHwCounter(portHwStats.outMulticastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Sent Broadcast (pkts)         \t\t {}",
              formatHwCounter(portHwStats.outBroadcastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In Errors (pkts)              \t\t {}",
              formatHwCounter(portHwStats.inErrorPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In IPv4 Header Errors (pkts)  \t\t {}",
              formatHwCounter(portHwStats.inIpv4HeaderErrorPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In IPv6 Header Errors (pkts)  \t\t {}",
              formatHwCounter(portHwStats.inIpv6HeaderErrorPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In Discards (pkts)            \t\t {}",
              formatHwCounter(portHwStats.inDiscardPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Out Errors (pkts)             \t\t {}",
              formatHwCounter(portHwStats.outErrorPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Out Discards (pkts)           \t\t {}",
              formatHwCounter(portHwStats.outDiscardPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Out Congestion Discards (pkts)\t\t {}",
              formatHwCounter(portHwStats.outCongestionDiscardPkts().value())));
      for (const auto& queueDiscardPackets :
           portHwStats.queueOutDiscardPackets().value()) {
        const auto iter =
            portInfo.queueIdToName().value().find(queueDiscardPackets.first);
        std::string queueName;
        if (iter != portInfo.queueIdToName().value().end()) {
          queueName = folly::to<std::string>("(", iter->second, ")");
        }
        // print either if the queue is valid or queue has non zero traffic
        if (queueDiscardPackets.second || !queueName.empty()) {
          detailedOutput.emplace_back(
              fmt::format(
                  "\tQueue {} {:12}    \t\t {}",
                  queueDiscardPackets.first,
                  queueName,
                  queueDiscardPackets.second));
        }
      }
      detailedOutput.emplace_back(
          fmt::format(
              "    In Congestion Discards (pkts)\t\t {}",
              formatHwCounter(portHwStats.inCongestionDiscards().value())));
      if (apache::thrift::get_pointer(portHwStats.outPfcPackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    PFC Output (pkts)             \t\t {}",
                formatHwCounter(*apache::thrift::get_pointer(
                    portHwStats.outPfcPackets()))));
        if (apache::thrift::get_pointer(portHwStats.outPfcPriorityPackets())) {
          for (const auto& pfcPriortyCounter : *apache::thrift::get_pointer(
                   portHwStats.outPfcPriorityPackets())) {
            detailedOutput.emplace_back(
                fmt::format(
                    "\tPriority {}                 \t\t {}",
                    pfcPriortyCounter.first,
                    pfcPriortyCounter.second));
          }
        }
      }
      if (apache::thrift::get_pointer(portHwStats.inPfcPackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    PFC Input (pkts)              \t\t {}",
                formatHwCounter(
                    *apache::thrift::get_pointer(portHwStats.inPfcPackets()))));
        if (apache::thrift::get_pointer(portHwStats.inPfcPriorityPackets())) {
          for (const auto& pfcPriortyCounter : *apache::thrift::get_pointer(
                   portHwStats.inPfcPriorityPackets())) {
            detailedOutput.emplace_back(
                fmt::format(
                    "\tPriority {}                 \t\t {}",
                    pfcPriortyCounter.first,
                    pfcPriortyCounter.second));
          }
        }
      }
      if (apache::thrift::get_pointer(portHwStats.outPausePackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    Pause Output (pkts)           \t\t {}",
                formatHwCounter(*apache::thrift::get_pointer(
                    portHwStats.outPausePackets()))));
      }
      if (apache::thrift::get_pointer(portHwStats.inPausePackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    Pause Input (pkts)            \t\t {}",
                formatHwCounter(*apache::thrift::get_pointer(
                    portHwStats.inPausePackets()))));
      }
      if (apache::thrift::get_pointer(portHwStats.cbfcCfUpdateTx())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    CBFC CF Update Output         \t\t {}",
                *apache::thrift::get_pointer(portHwStats.cbfcCfUpdateTx())));
      }
      if (apache::thrift::get_pointer(portHwStats.cbfcCfUpdateRx())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    CBFC CF Update Input          \t\t {}",
                *apache::thrift::get_pointer(portHwStats.cbfcCfUpdateRx())));
      }
      if (apache::thrift::get_pointer(portHwStats.cbfcCcUpdateTx())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    CBFC CC Update Output         \t\t {}",
                *apache::thrift::get_pointer(portHwStats.cbfcCcUpdateTx())));
      }

      const auto& windowedStats = portHwStats.windowedStats().value();
      detailedOutput.emplace_back("    Windowed FB303 counters");
      detailedOutput.emplace_back(
          fmt::format(
              "\t{:<36}{:>16}{:>16}{:>16}",
              "Time interval",
              "1 minute",
              "10 minutes",
              "1 hour"));
      appendWindowedCounter(
          detailedOutput,
          "Ingress",
          windowedStats.ingressBitsPerSecond().to_optional(),
          WindowedValueType::BITS_PER_SECOND);
      appendWindowedCounter(
          detailedOutput,
          "Egress",
          windowedStats.egressBitsPerSecond().to_optional(),
          WindowedValueType::BITS_PER_SECOND);
      appendWindowedQueueCounters(
          detailedOutput,
          portHwStats.queueOutBitsPerSecond().value(),
          portInfo.queueIdToName().value(),
          WindowedValueType::BITS_PER_SECOND);
      appendWindowedCounter(
          detailedOutput,
          "Received Unicast (pkts)",
          windowedStats.inUnicastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Received Multicast (pkts)",
          windowedStats.inMulticastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Received Broadcast (pkts)",
          windowedStats.inBroadcastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Sent Unicast (pkts)",
          windowedStats.outUnicastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Sent Multicast (pkts)",
          windowedStats.outMulticastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Sent Broadcast (pkts)",
          windowedStats.outBroadcastPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "In Errors (pkts)",
          windowedStats.inErrorPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "In IPv4 Header Errors (pkts)",
          windowedStats.inIpv4HeaderErrorPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "In IPv6 Header Errors (pkts)",
          windowedStats.inIpv6HeaderErrorPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "In Discards (pkts)",
          windowedStats.inDiscardPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Out Errors (pkts)",
          windowedStats.outErrorPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Out Discards (pkts)",
          windowedStats.outDiscardPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Out Congestion Discards (pkts)",
          windowedStats.outCongestionDiscardPkts().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedQueueCounters(
          detailedOutput,
          portHwStats.queueOutCongestionDiscardPacketsPerSecond().value(),
          portInfo.queueIdToName().value(),
          WindowedValueType::PACKETS_PER_SECOND);
      appendWindowedCounter(
          detailedOutput,
          "Pause Input (pkts)",
          windowedStats.inPausePackets().to_optional(),
          WindowedValueType::PACKETS);
      appendWindowedCounter(
          detailedOutput,
          "Pause Output (pkts)",
          windowedStats.outPausePackets().to_optional(),
          WindowedValueType::PACKETS);
    }
    out << folly::join("\n", detailedOutput) << std::endl;
  } else {
    Table table;
    table.setHeader({
        "ID",
        "Name",
        "AdminState",
        "LinkState",
        "ActiveState",
        "Transceiver",
        "TcvrID",
        "Speed",
        "ProfileID",
        "HwLogicalPortId",
        "Drained",
        "PeerSwitchDrained",
        "PeerPortDrainedOrDown",
        "Errors",
        "Core Id",
        "Virtual device Id",
        "Cable Len meters",
        "UserMetaData",
        "IngressAclTable",
    });

    for (auto const& portInfo : model.portEntries().value()) {
      std::string hwLogicalPortId;
      if (auto portId = portInfo.hwLogicalPortId()) {
        hwLogicalPortId = folly::to<std::string>(*portId);
      }

      bool activeStateMismatch = portInfo.activeStateMismatch().value();
      table.addRow(
          {folly::to<std::string>(folly::copy(portInfo.id().value())),
           portInfo.name().value(),
           portInfo.adminState().value(),
           getStyledLinkState(portInfo.linkState().value()),
           getStyledActiveState(
               portInfo.activeState().value(), activeStateMismatch),
           portInfo.tcvrPresent().value(),
           folly::to<std::string>(folly::copy(portInfo.tcvrID().value())),
           portInfo.speed().value(),
           portInfo.profileId().value(),
           hwLogicalPortId,
           portInfo.isDrained().value(),
           portInfo.peerSwitchDrained().value(),
           portInfo.peerPortDrainedOrDown().value(),
           getStyledErrors(portInfo.activeErrors().value()),
           portInfo.coreId().value(),
           portInfo.virtualDeviceId().value(),
           portInfo.cableLengthMeters().value(),
           portInfo.userMetaData().value(),
           portInfo.ingressAclTable().value()});
    }
    out << table << std::endl;
  }
}

std::string_view CmdShowPortTraits::description() {
  return "Displays low-level port state: admin/link/active state, transceiver presence and ID, speed and port profile, hardware logical port ID, drain state, error flags, and cable length. With --detail, also displays cumulative hardware counters and FB303 traffic rates and packet/error counts over 1-minute, 10-minute and 1-hour windows. Use it for detailed port and hardware debugging.";
}

CmdShowPort::RetType CmdShowPort::sampleModel() {
  RetType model;

  cli::PortEntry port1;
  port1.id() = 1;
  port1.name() = "eth1/1/1";
  port1.adminState() = "Enabled";
  port1.linkState() = "Up";
  port1.activeState() = "--";
  port1.tcvrPresent() = "Present";
  port1.tcvrID() = 0;
  port1.speed() = "400G";
  port1.profileId() = "PROFILE_400G_4_PAM4_RS544X2N_OPTICAL";
  port1.hwLogicalPortId() = 28;
  port1.isDrained() = "No";
  port1.peerSwitchDrained() = "--";
  port1.peerPortDrainedOrDown() = "--";
  port1.activeErrors() = "--";
  port1.coreId() = "--";
  port1.virtualDeviceId() = "--";
  port1.cableLengthMeters() = "--";
  port1.activeStateMismatch() = false;
  port1.userMetaData() = "Restricted";
  port1.ingressAclTable() = "AccessPolicyBlockTable";

  cli::PortEntry port2;
  port2.id() = 3;
  port2.name() = "eth1/2/1";
  port2.adminState() = "Enabled";
  port2.linkState() = "Up";
  port2.activeState() = "--";
  port2.tcvrPresent() = "Present";
  port2.tcvrID() = 1;
  port2.speed() = "400G";
  port2.profileId() = "PROFILE_400G_4_PAM4_RS544X2N_OPTICAL";
  port2.hwLogicalPortId() = 30;
  port2.isDrained() = "No";
  port2.peerSwitchDrained() = "--";
  port2.peerPortDrainedOrDown() = "--";
  port2.activeErrors() = "--";
  port2.coreId() = "--";
  port2.virtualDeviceId() = "--";
  port2.cableLengthMeters() = "--";
  port2.activeStateMismatch() = false;
  port2.userMetaData() = "--";
  port2.ingressAclTable() = "--";

  cli::PortEntry port3;
  port3.id() = 5;
  port3.name() = "eth1/3/1";
  port3.adminState() = "Enabled";
  port3.linkState() = "Up";
  port3.activeState() = "--";
  port3.tcvrPresent() = "Present";
  port3.tcvrID() = 2;
  port3.speed() = "200G";
  port3.profileId() = "PROFILE_200G_4_PAM4_RS544X2N_OPTICAL";
  port3.hwLogicalPortId() = 32;
  port3.isDrained() = "No";
  port3.peerSwitchDrained() = "--";
  port3.peerPortDrainedOrDown() = "--";
  port3.activeErrors() = "--";
  port3.coreId() = "--";
  port3.virtualDeviceId() = "--";
  port3.cableLengthMeters() = "--";
  port3.activeStateMismatch() = false;
  port3.userMetaData() = "Unconstrained";
  port3.ingressAclTable() = "--";

  model.portEntries() = {port1, port2, port3};
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowPort, CmdShowPortTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowPort, CmdShowPortTraits>::getValidFilters();
template const ValidAggMapType
CmdHandler<CmdShowPort, CmdShowPortTraits>::getValidAggs();

} // namespace facebook::fboss
