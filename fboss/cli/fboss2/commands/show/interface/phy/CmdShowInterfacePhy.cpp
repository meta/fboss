// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/phy/CmdShowInterfacePhy.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <folly/String.h>
#include "fboss/cli/fboss2/utils/Table.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {

CmdShowInterfacePhy::RetType CmdShowInterfacePhy::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const utils::PhyChipType& phyChipType) {
  return createModel(hostInfo, queriedIfs, phyChipType);
}

void CmdShowInterfacePhy::printOutput(const RetType& model, std::ostream& out) {
  for (const auto& info : *model.phyInfo()) {
    auto ifName = info.first;
    auto phyInfo = info.second;
    if (phyInfo.find(phy::DataPlanePhyChipType::IPHY) != phyInfo.end()) {
      printPhyInfo(out, phyInfo[phy::DataPlanePhyChipType::IPHY], ifName);
    }
    if (phyInfo.find(phy::DataPlanePhyChipType::XPHY) != phyInfo.end()) {
      printPhyInfo(out, phyInfo[phy::DataPlanePhyChipType::XPHY], ifName);
    }
    out << std::endl;
  }
}

void CmdShowInterfacePhy::printPhyInfo(
    std::ostream& out,
    phy::PhyInfo& phyInfo,
    const std::string& ifName) {
  auto& phyState = *phyInfo.state();
  auto& phyStats = *phyInfo.stats();
  auto phyType =
      apache::thrift::util::enumNameSafe(*phyState.phyChip()->type());
  std::string prefix = phyType + "-";
  Table table;
  table.setHeader({"Interface", ifName});
  table.addRow({"PhyChipType", phyType});
  auto linkState = phyState.linkState();
  if (linkState.has_value()) {
    if (*linkState) {
      table.addRow({"Link State", Table::StyledCell("UP", Table::Style::GOOD)});
    } else {
      table.addRow(
          {"Link State", Table::StyledCell("DOWN", Table::Style::ERROR)});
    }
  }
  auto linkFlapCount = phyStats.linkFlapCount();
  if (linkFlapCount.has_value()) {
    table.addRow({"Link Flap Count", std::to_string(*linkFlapCount)});
  }
  auto speed = phyState.speed();
  table.addRow({"Speed", apache::thrift::util::enumNameSafe(*speed)});
  auto timeCollected = phyState.timeCollected();
  table.addRow(
      {phyType + " Data Collected",
       utils::getPrettyElapsedTime(*timeCollected) + " ago"});
  out << table;
  if (auto systemState = phyState.system()) {
    printSideStateAndStat(
        out, *systemState, phyStats.system().ensure(), prefix + "System ");
  }
  printSideStateAndStat(
      out, *phyState.line(), *phyStats.line(), prefix + "Line ");
}

void CmdShowInterfacePhy::printSideStateAndStat(
    std::ostream& out,
    phy::PhySideState& sideState,
    phy::PhySideStats& sideStats,
    const std::string& prefix) {
  if (auto rs = sideState.rs()) {
    Table rsTable;
    rsTable.setHeader({prefix + "Reconciliation Sublayer", ""});
    rsTable.addRow(
        {prefix + "Local Fault Live",
         std::to_string(*(*rs).faultStatus()->localFault())});
    rsTable.addRow(
        {prefix + "Remote Fault Live",
         std::to_string(*(*rs).faultStatus()->remoteFault())});
    rsTable.addRow(
        {prefix + "High CRC Error Rate Live",
         std::to_string(*(*rs).faultStatus()->highCrcErrorRateLive())});
    rsTable.addRow(
        {prefix + "High CRC Error Rate Changed",
         std::to_string(*(*rs).faultStatus()->highCrcErrorRateChangedCount())});
    out << rsTable;
  }
  if (sideState.pcs().has_value() || sideStats.pcs().has_value()) {
    Table pcsTable;
    Table rsFecTable;
    Table rsFecStateTable;
    Table rsFecCodewordStatsTable;
    bool hasPcsData{false}, hasRsFecData{false}, hasRsFecState{false},
        hasRsFecCodewordStats{false};
    if (sideState.pcs().has_value()) {
      if (auto pcsRxStatusLive = sideState.pcs()->pcsRxStatusLive()) {
        pcsTable.setHeader({prefix + "PCS", ""});
        pcsTable.addRow(
            {prefix + "PCS RX Link Status Live",
             makeColorCellForLiveFlag(std::to_string(*pcsRxStatusLive))});
        if (auto pcsRxStatusLatched = sideState.pcs()->pcsRxStatusLatched()) {
          pcsTable.addRow(
              {prefix + "PCS RX Link Status Changed",
               std::to_string(*pcsRxStatusLatched)});
        }
        hasPcsData = true;
      }
      if (auto rsFecState = sideState.pcs()->rsFecState()) {
        for (auto& fecLaneState : *rsFecState->lanes()) {
          if (!hasRsFecState) {
            // Display the header only once
            rsFecStateTable.setHeader(
                {prefix + "RS FEC State",
                 "Lane",
                 "Alignment Lock Live",
                 "Alignment Lock Changed"});
          }
          std::string fecAmLive = "N/A";
          std::string fecAmChanged = "N/A";
          if (auto fecAmLiveState =
                  fecLaneState.second.fecAlignmentLockLive()) {
            fecAmLive = std::to_string(*fecAmLiveState);
          }
          if (auto fecAmChangedState =
                  fecLaneState.second.fecAlignmentLockChanged()) {
            fecAmChanged = std::to_string(*fecAmChangedState);
          }
          rsFecStateTable.addRow(
              {"",
               std::to_string(*(fecLaneState.second.lane())),
               makeColorCellForLiveFlag(fecAmLive),
               fecAmChanged});
          hasRsFecState = true;
        }
      }
    }
    if (sideStats.pcs().has_value()) {
      if (auto rsFec = sideStats.pcs()->rsFec()) {
        rsFecTable.setHeader({prefix + "RS FEC", ""});
        rsFecTable.addRow(
            {prefix + "Corrected codewords",
             std::to_string(*(rsFec->correctedCodewords()))});
        rsFecTable.addRow(
            {prefix + "Uncorrected codewords",
             std::to_string(*(rsFec->uncorrectedCodewords()))});
        std::ostringstream outStringStream;
        outStringStream << *rsFec->preFECBer();
        rsFecTable.addRow({prefix + "Pre-FEC BER", outStringStream.str()});
        if (rsFec->preFECBerSource().has_value()) {
          rsFecTable.addRow(
              {prefix + "Pre-FEC BER Source",
               apache::thrift::util::enumNameSafe(*rsFec->preFECBerSource())});
        }
        if (rsFec->fecTail().has_value()) {
          rsFecTable.addRow(
              {prefix + "FEC Tail", std::to_string(rsFec->fecTail().value())});
        }
        hasRsFecData = true;
        if (!rsFec->codewordStats()->empty()) {
          rsFecCodewordStatsTable.setHeader(
              {prefix + "Codeword stats", "Symbol Errors", "# of codewords"});
          for (auto& [symbolErrors, numCodewords] : *rsFec->codewordStats()) {
            rsFecCodewordStatsTable.addRow(
                {"",
                 std::to_string(symbolErrors),
                 std::to_string(numCodewords)});
          }
          hasRsFecCodewordStats = true;
        }
      }
      if (hasPcsData) {
        out << pcsTable;
      }
      if (hasRsFecData) {
        out << rsFecTable;
      }
      if (hasRsFecCodewordStats) {
        out << rsFecCodewordStatsTable;
      }
      if (hasRsFecState) {
        out << rsFecStateTable;
      }
    }
  }

  std::set<int> pmdLanes;
  for (auto it : *sideState.pmd()->lanes()) {
    pmdLanes.insert(it.first);
  }
  for (auto it : *sideStats.pmd()->lanes()) {
    pmdLanes.insert(it.first);
  }
  if (auto loopback = sideState.loopback()) {
    out << prefix
        << "Loopback: " << apache::thrift::util::enumNameSafe(*loopback)
        << std::endl;
  }
  if (auto intfType = sideState.interfaceType()) {
    out << prefix
        << "Interface Type: " << apache::thrift::util::enumNameSafe(*intfType)
        << std::endl;
  }
  printLinkTrainingInfo(out, *sideState.pmd(), prefix);
  if (!pmdLanes.empty()) {
    printPmdLaneRxInfo(out, sideState, sideStats, pmdLanes, prefix);
    printPmdLaneTxInfo(out, sideState, pmdLanes, prefix);
    printSerdesParametersInfo(out, *sideState.pmd(), prefix);
  }
}

void CmdShowInterfacePhy::printLinkTrainingInfo(
    std::ostream& out,
    phy::PmdState& pmdState,
    const std::string& prefix) {
  auto ltStatus = *pmdState.linkTrainingStatus();
  out << prefix << "Link Training" << std::endl;
  out << "  " << prefix << "Enabled                  "
      << (*ltStatus.linkTrainingEnabled() ? "True" : "False") << std::endl;
  if (ltStatus.rxStatus().has_value()) {
    out << "  " << prefix << "RX Trained Status        "
        << apache::thrift::util::enumNameSafe(*ltStatus.rxStatus())
        << std::endl;
  }
}

void CmdShowInterfacePhy::printPmdLaneRxInfo(
    std::ostream& out,
    phy::PhySideState& sideState,
    phy::PhySideStats& sideStats,
    const std::set<int>& pmdLanes,
    const std::string& prefix) {
  Table pmdRxTable;
  pmdRxTable.setHeader(
      {prefix + "RX PMD",
       "Lane",
       "RX Signal Detect Live",
       "RX Signal Detect Changed",
       "RX CDR Lock Live",
       "RX CDR Lock Changed",
       "Eye Heights",
       "Eye Widths",
       "Rx PPM",
       "RX SNR"});
  for (auto pmdLane : pmdLanes) {
    auto laneState = (*sideState.pmd()->lanes())[pmdLane];
    auto laneStat = (*sideStats.pmd()->lanes())[pmdLane];
    std::string sigDetLive = "N/A";
    std::string cdrLockLive = "N/A";
    std::string sigDetChanged = "N/A";
    std::string cdrLockChanged = "N/A";
    std::string rxPPM = "N/A";
    std::string rxSNR = "N/A";
    std::vector<float> eyeHeights = {};
    std::vector<float> eyeWidths = {};
    if (auto rxSigDetLive = laneState.signalDetectLive()) {
      sigDetLive = std::to_string(*rxSigDetLive);
    }
    if (auto rxSigDetChanged = laneStat.signalDetectChangedCount()) {
      sigDetChanged = std::to_string(*rxSigDetChanged);
    }
    if (auto rxCdrLockLive = laneState.cdrLockLive()) {
      cdrLockLive = std::to_string(*rxCdrLockLive);
    }
    if (auto rxCdrLockChanged = laneStat.cdrLockChangedCount()) {
      cdrLockChanged = std::to_string(*rxCdrLockChanged);
    }
    if (auto rxFreqPPM = laneState.rxFrequencyPPM()) {
      rxPPM = std::to_string(*rxFreqPPM);
    }
    if (auto rxLaneSNR = laneStat.snr()) {
      rxSNR = std::to_string(*rxLaneSNR);
    }
    if (auto eyes = laneStat.eyes()) {
      for (const auto& eye : *eyes) {
        if (auto eyeW = eye.width()) {
          eyeWidths.push_back(*eyeW);
        }
        if (auto eyeH = eye.height()) {
          eyeHeights.push_back(*eyeH);
        }
      }
    }
    pmdRxTable.addRow(
        {"",
         std::to_string(pmdLane),
         makeColorCellForLiveFlag(sigDetLive),
         sigDetChanged,
         makeColorCellForLiveFlag(cdrLockLive),
         cdrLockChanged,
         folly::join(",", eyeHeights),
         folly::join(",", eyeWidths),
         rxPPM,
         rxSNR});
  }
  out << pmdRxTable;
}

void CmdShowInterfacePhy::printPmdLaneTxInfo(
    std::ostream& out,
    phy::PhySideState& sideState,
    const std::set<int>& pmdLanes,
    const std::string& prefix) {
  Table pmdTxTable;
  pmdTxTable.setHeader(
      {prefix + "TX PMD",
       "Lane",
       "Pre3",
       "Pre2",
       "Pre1",
       "Main",
       "Post1",
       "Post2",
       "Post3",
       "Precoding"});
  for (auto pmdLane : pmdLanes) {
    auto laneState = (*sideState.pmd()->lanes())[pmdLane];
    auto txSettings = *laneState.txSettings();
    std::string pre3 = "N/A";
    auto txPre3 = txSettings.pre3();
    if (txPre3.has_value()) {
      pre3 = std::to_string(*txPre3);
    }
    std::string pre2 = std::to_string(*txSettings.pre2());
    std::string pre = std::to_string(*txSettings.pre());
    std::string main = std::to_string(*txSettings.main());
    std::string post = std::to_string(*txSettings.post());
    std::string post2 = std::to_string(*txSettings.post2());
    std::string post3 = std::to_string(*txSettings.post3());
    std::string precoding = "N/A";
    if (auto txPrecoding = txSettings.precoding()) {
      precoding = std::to_string(*txPrecoding);
    }
    pmdTxTable.addRow(
        {"",
         std::to_string(pmdLane),
         pre3,
         pre2,
         pre,
         main,
         post,
         post2,
         post3,
         precoding});
  }
  out << pmdTxTable;
}

void CmdShowInterfacePhy::printSerdesParametersInfo(
    std::ostream& out,
    phy::PmdState& pmdState,
    const std::string& prefix) {
  Table serdesTable;
  serdesTable.setHeader(
      {prefix + "Serdes Parameters",
       "Lane",
       "RVga",
       "Dco",
       "TpChn0",
       "TpChn1",
       "TpChn2",
       "RxPf",
       "RxPfLfq",
       "RxPfHfq",
       "RxFltM",
       "RxFltS",
       "RxTap1",
       "RxTap2",
       "RxEq3",
       "RxEq2",
       "RxEq1",
       "RxEqM",
       "RxEqP1",
       "RxEqP2",
       "RxReach",
       "RxPrecoding"});

  for (const auto& [laneId, laneState] : *pmdState.lanes()) {
    auto serdesParams = laneState.serdesParameters();

    std::string rvga = "N/A";
    std::string dco = "N/A";
    std::string tpChn0 = "N/A";
    std::string tpChn1 = "N/A";
    std::string tpChn2 = "N/A";
    std::string rxPf = "N/A";
    std::string rxPfLfq = "N/A";
    std::string rxPfHfq = "N/A";
    std::string rxFltM = "N/A";
    std::string rxFltS = "N/A";
    std::string rxTap1 = "N/A";
    std::string rxTap2 = "N/A";
    std::string rxEq3 = "N/A";
    std::string rxEq2 = "N/A";
    std::string rxEq1 = "N/A";
    std::string rxEqM = "N/A";
    std::string rxEqP1 = "N/A";
    std::string rxEqP2 = "N/A";
    std::string rxReach = "N/A";
    std::string rxPrecoding = "N/A";

    if (auto rvgaVal = serdesParams->rvga()) {
      rvga = std::to_string(*rvgaVal);
    }
    if (auto dcoVal = serdesParams->dco()) {
      dco = std::to_string(*dcoVal);
    }
    if (auto tpChn0Val = serdesParams->tpChn0()) {
      tpChn0 = std::to_string(*tpChn0Val);
    }
    if (auto tpChn1Val = serdesParams->tpChn1()) {
      tpChn1 = std::to_string(*tpChn1Val);
    }
    if (auto tpChn2Val = serdesParams->tpChn2()) {
      tpChn2 = std::to_string(*tpChn2Val);
    }
    if (auto rxPfVal = serdesParams->rxPf()) {
      rxPf = std::to_string(*rxPfVal);
    }
    if (auto rxPfLfqVal = serdesParams->rxPfLfq()) {
      rxPfLfq = std::to_string(*rxPfLfqVal);
    }
    if (auto rxPfHfqVal = serdesParams->rxPfHfq()) {
      rxPfHfq = std::to_string(*rxPfHfqVal);
    }
    if (auto rxFltMVal = serdesParams->rxFltM()) {
      rxFltM = std::to_string(*rxFltMVal);
    }
    if (auto rxFltSVal = serdesParams->rxFltS()) {
      rxFltS = std::to_string(*rxFltSVal);
    }
    if (auto rxTap1Val = serdesParams->rxTap1()) {
      rxTap1 = std::to_string(*rxTap1Val);
    }
    if (auto rxTap2Val = serdesParams->rxTap2()) {
      rxTap2 = std::to_string(*rxTap2Val);
    }
    if (auto rxEq3Val = serdesParams->rxEq3()) {
      rxEq3 = std::to_string(*rxEq3Val);
    }
    if (auto rxEq2Val = serdesParams->rxEq2()) {
      rxEq2 = std::to_string(*rxEq2Val);
    }
    if (auto rxEq1Val = serdesParams->rxEq1()) {
      rxEq1 = std::to_string(*rxEq1Val);
    }
    if (auto rxEqMVal = serdesParams->rxEqM()) {
      rxEqM = std::to_string(*rxEqMVal);
    }
    if (auto rxEqP1Val = serdesParams->rxEqP1()) {
      rxEqP1 = std::to_string(*rxEqP1Val);
    }
    if (auto rxEqP2Val = serdesParams->rxEqP2()) {
      rxEqP2 = std::to_string(*rxEqP2Val);
    }
    if (auto rxReachVal = serdesParams->rxReach()) {
      rxReach = apache::thrift::util::enumNameSafe(*rxReachVal);
    }
    if (auto rxPrecodingVal = serdesParams->rxPrecoding()) {
      rxPrecoding = std::to_string(*rxPrecodingVal);
    }

    serdesTable.addRow({"",      std::to_string(laneId),
                        rvga,    dco,
                        tpChn0,  tpChn1,
                        tpChn2,  rxPf,
                        rxPfLfq, rxPfHfq,
                        rxFltM,  rxFltS,
                        rxTap1,  rxTap2,
                        rxEq3,   rxEq2,
                        rxEq1,   rxEqM,
                        rxEqP1,  rxEqP2,
                        rxReach, rxPrecoding});
  }

  out << serdesTable;
}

Table::StyledCell CmdShowInterfacePhy::makeColorCellForLiveFlag(
    const std::string& flag) {
  if (flag == "0") {
    return Table::StyledCell("False", Table::Style::ERROR);
  } else if (flag == "1") {
    return Table::StyledCell("True", Table::Style::GOOD);
  }
  return Table::StyledCell(flag, Table::Style::INFO);
}

CmdShowInterfacePhy::RetType CmdShowInterfacePhy::createModel(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const utils::PhyChipType& phyChipType) {
  RetType model;
  try {
    if (phyChipType.iphyIncluded) {
      auto agentClient =
          utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
      std::map<std::string, phy::PhyInfo> phyInfo;
      if (queriedIfs.empty()) {
        agentClient->sync_getAllInterfacePhyInfo(phyInfo);
      } else {
        agentClient->sync_getInterfacePhyInfo(phyInfo, queriedIfs.data());
      }
      for (auto& interfacePhyInfo : phyInfo) {
        model.phyInfo()[interfacePhyInfo.first].insert(
            {phy::DataPlanePhyChipType::IPHY, interfacePhyInfo.second});
      }
    }
  } catch (apache::thrift::transport::TTransportException&) {
    std::cerr << "Cannot connect to wedge_agent\n";
  }
  try {
    if (phyChipType.xphyIncluded) {
      auto qsfpClient =
          utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
      std::map<std::string, phy::PhyInfo> phyInfo;
      if (queriedIfs.empty()) {
        qsfpClient->sync_getAllInterfacePhyInfo(phyInfo);
      } else {
        qsfpClient->sync_getInterfacePhyInfo(phyInfo, queriedIfs.data());
      }
      for (auto& interfacePhyInfo : phyInfo) {
        model.phyInfo()[interfacePhyInfo.first].insert(
            {phy::DataPlanePhyChipType::XPHY, interfacePhyInfo.second});
      }
    }
  } catch (apache::thrift::transport::TTransportException&) {
    std::cerr << "Cannot connect to qsfp_service\n";
  }
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowInterfacePhy, CmdShowInterfacePhyTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowInterfacePhy, CmdShowInterfacePhyTraits>::getValidFilters();

} // namespace facebook::fboss
