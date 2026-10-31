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
#include "fboss/cli/fboss2/commands/show/port/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <algorithm>
#include <initializer_list>

namespace facebook::fboss {
using utils::Table;
using ObjectArgType = CmdShowPortTraits::ObjectArgType;
using RetType = CmdShowPortTraits::RetType;

namespace {
// Sums packet counters, treating counters the hardware does not report
// (STAT_UNINITIALIZED) as zero.
int64_t sumPktCounters(std::initializer_list<int64_t> counters) {
  int64_t total = 0;
  for (auto counter : counters) {
    total += std::max<int64_t>(counter, 0);
  }
  return total;
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
    cbfcStr += fmt::format(", limit {}", *cbfc.senderCreditLimit());
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

  std::vector<int32_t> requiredTransceiverEntries;
  std::vector<std::string> bgpDrainedInterfaces;

  auto client =
      utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
  client->sync_getAllPortInfo(portEntries);

  auto opt = CmdGlobalOptions::getInstance();
  if (opt->isDetailed()) {
    client->sync_getHwPortStats(portStats);
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
      utils::getBgpDrainedInterafces(hostInfo));
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
    const std::vector<std::string>& drainedInterfaces) {
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
        cliPortStats.inDiscardPkts() =
            folly::copy(portHwStatsEntry.inDiscards_().value());
        cliPortStats.inErrorPkts() =
            folly::copy(portHwStatsEntry.inErrors_().value());
        cliPortStats.outDiscardPkts() =
            folly::copy(portHwStatsEntry.outDiscards_().value());
        cliPortStats.outCongestionDiscardPkts() =
            folly::copy(portHwStatsEntry.outCongestionDiscardPkts_().value());
        cliPortStats.queueOutDiscardBytes() =
            portHwStatsEntry.queueOutDiscardBytes_().value();
        cliPortStats.inCongestionDiscards() =
            folly::copy(portHwStatsEntry.inCongestionDiscards_().value());
        cliPortStats.queueOutBytes() =
            portHwStatsEntry.queueOutBytes_().value();
        cliPortStats.queueOutPackets() =
            portHwStatsEntry.queueOutPackets_().value();
        cliPortStats.queueOutDiscardPackets() =
            portHwStatsEntry.queueOutDiscardPackets_().value();
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
        cliPortStats.ingressPkts() = sumPktCounters(
            {folly::copy(portHwStatsEntry.inUnicastPkts_().value()),
             folly::copy(portHwStatsEntry.inMulticastPkts_().value()),
             folly::copy(portHwStatsEntry.inBroadcastPkts_().value())});
        cliPortStats.egressPkts() = sumPktCounters(
            {folly::copy(portHwStatsEntry.outUnicastPkts_().value()),
             folly::copy(portHwStatsEntry.outMulticastPkts_().value()),
             folly::copy(portHwStatsEntry.outBroadcastPkts_().value())});
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
        return utils::comparePortName(a.name().value(), b.name().value());
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
      detailedOutput.emplace_back(
          fmt::format(
              "    Ingress (pkts)                \t\t {}",
              folly::copy(portHwStats.ingressPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Egress (pkts)                 \t\t {}",
              folly::copy(portHwStats.egressPkts().value())));
      for (const auto& queuePkts : portHwStats.queueOutPackets().value()) {
        const auto iter =
            portInfo.queueIdToName().value().find(queuePkts.first);
        std::string queueName;
        if (iter != portInfo.queueIdToName().value().end()) {
          queueName = folly::to<std::string>("(", iter->second, ")");
        }
        // print either if the queue is valid or queue has non zero traffic
        if (queuePkts.second || !queueName.empty()) {
          detailedOutput.emplace_back(
              fmt::format(
                  "\tQueue {} {:12}    \t\t {}",
                  queuePkts.first,
                  queueName,
                  queuePkts.second));
        }
      }
      detailedOutput.emplace_back(
          fmt::format(
              "    Received Unicast (pkts)       \t\t {}",
              folly::copy(portHwStats.inUnicastPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In Errors (pkts)              \t\t {}",
              folly::copy(portHwStats.inErrorPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    In Discards (pkts)            \t\t {}",
              folly::copy(portHwStats.inDiscardPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Out Discards (pkts)           \t\t {}",
              folly::copy(portHwStats.outDiscardPkts().value())));
      detailedOutput.emplace_back(
          fmt::format(
              "    Out Congestion Discards (pkts)\t\t {}",
              folly::copy(portHwStats.outCongestionDiscardPkts().value())));
      for (const auto& queueDiscardPkts :
           portHwStats.queueOutDiscardPackets().value()) {
        const auto iter =
            portInfo.queueIdToName().value().find(queueDiscardPkts.first);
        std::string queueName;
        if (iter != portInfo.queueIdToName().value().end()) {
          queueName = folly::to<std::string>("(", iter->second, ")");
        }
        // print either if the queue is valid or queue has non zero traffic
        if (queueDiscardPkts.second || !queueName.empty()) {
          detailedOutput.emplace_back(
              fmt::format(
                  "\tQueue {} {:12}    \t\t {}",
                  queueDiscardPkts.first,
                  queueName,
                  queueDiscardPkts.second));
        }
      }
      detailedOutput.emplace_back(
          fmt::format(
              "    In Congestion Discards (pkts)\t\t {}",
              folly::copy(portHwStats.inCongestionDiscards().value())));
      if (apache::thrift::get_pointer(portHwStats.outPfcPackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    PFC Output (pkts)             \t\t {}",
                *apache::thrift::get_pointer(portHwStats.outPfcPackets())));
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
                *apache::thrift::get_pointer(portHwStats.inPfcPackets())));
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
                *apache::thrift::get_pointer(portHwStats.outPausePackets())));
      }
      if (apache::thrift::get_pointer(portHwStats.inPausePackets())) {
        detailedOutput.emplace_back(
            fmt::format(
                "    Pause Input (pkts)            \t\t {}",
                *apache::thrift::get_pointer(portHwStats.inPausePackets())));
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
  return "Displays low-level port state: admin/link/active state, transceiver presence and ID, speed and port profile, hardware logical port ID, drain state, error flags, and cable length. Use it for detailed port and hardware debugging.";
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
