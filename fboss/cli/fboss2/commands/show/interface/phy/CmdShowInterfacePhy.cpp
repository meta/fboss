// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/phy/CmdShowInterfacePhy.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <folly/String.h>
#include <glog/logging.h>
#include <algorithm>
#include <variant>
#include "fboss/cli/fboss2/utils/Table.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {

namespace {

constexpr auto kNotApplicable = "N/A";

// One column of a per-lane table: its header plus one cell per lane.
struct LaneColumn {
  std::string header;
  std::vector<Table::RowData> cells;
};

std::vector<LaneColumn> makeLaneColumns(
    const std::vector<std::string>& headers) {
  std::vector<LaneColumn> columns;
  columns.reserve(headers.size());
  for (const auto& header : headers) {
    columns.push_back(LaneColumn{header, {}});
  }
  return columns;
}

void addLaneCells(
    std::vector<LaneColumn>& columns,
    const std::vector<Table::RowData>& cells) {
  CHECK_EQ(columns.size(), cells.size());
  for (size_t i = 0; i < cells.size(); ++i) {
    columns[i].cells.push_back(cells[i]);
  }
}

template <typename OptionalField>
std::string optionalStr(const OptionalField& field) {
  return field.has_value() ? std::to_string(*field) : kNotApplicable;
}

// A FIR tap, from the i32 fir* field on platforms that populate it and the
// legacy i16 everywhere else. Only the Cisco SiliconOne path fills fir*, where
// the i16 would wrap above 32767; every other platform takes the fallback.
template <typename OptionalField>
std::string txTapStr(const OptionalField& wide, std::string narrow) {
  return wide.has_value() ? std::to_string(*wide) : std::move(narrow);
}

// Renders a per-lane list, e.g. eye heights, as N/A when nothing was reported
// so that an empty column can be dropped like any other inapplicable one.
std::string joinedStr(const std::vector<float>& values) {
  return values.empty() ? kNotApplicable : folly::join(",", values);
}

bool isNotApplicable(const Table::RowData& cell) {
  return std::visit(
      [](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return data == kNotApplicable;
        } else {
          return data.getData() == kNotApplicable;
        }
      },
      cell);
}

// Prints one row per lane, dropping every column that is N/A on all lanes.
// Those parameters are not applicable to the ASIC being queried, and listing
// them only pushes the columns that do have values off the screen.
void printLaneTable(
    std::ostream& out,
    const std::string& title,
    const std::vector<int>& lanes,
    const std::vector<LaneColumn>& columns) {
  std::vector<const LaneColumn*> applicable;
  std::vector<Table::RowData> header{title, "Lane"};
  for (const auto& column : columns) {
    if (std::any_of(
            column.cells.begin(), column.cells.end(), [](const auto& cell) {
              return !isNotApplicable(cell);
            })) {
      applicable.push_back(&column);
      header.emplace_back(column.header);
    }
  }

  Table table;
  table.setHeader(header);
  for (size_t row = 0; row < lanes.size(); ++row) {
    std::vector<Table::RowData> cells{"", std::to_string(lanes[row])};
    for (const auto* column : applicable) {
      cells.emplace_back(column->cells[row]);
    }
    table.addRow(cells);
  }
  out << table;
}

} // namespace

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
  // phyState/phyStats.system() are thrift field accessors; clang-tidy's
  // bugprone-unsafe-functions matches the name against ::system().
  // NOLINTNEXTLINE(bugprone-unsafe-functions)
  if (auto systemState = phyState.system()) {
    // NOLINTNEXTLINE(bugprone-unsafe-functions)
    auto& systemStats = phyStats.system().ensure();
    printSideStateAndStat(out, *systemState, systemStats, prefix + "System ");
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
  auto columns = makeLaneColumns(
      {"RX Signal Detect Live",
       "RX Signal Detect Changed",
       "RX CDR Lock Live",
       "RX CDR Lock Changed",
       "Eye Heights",
       "Eye Widths",
       "Rx PPM",
       "RX SNR"});

  std::vector<int> lanes;
  for (auto pmdLane : pmdLanes) {
    lanes.push_back(pmdLane);
    auto laneState = (*sideState.pmd()->lanes())[pmdLane];
    auto laneStat = (*sideStats.pmd()->lanes())[pmdLane];
    std::vector<float> eyeHeights = {};
    std::vector<float> eyeWidths = {};
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

    addLaneCells(
        columns,
        {makeColorCellForLiveFlag(optionalStr(laneState.signalDetectLive())),
         optionalStr(laneStat.signalDetectChangedCount()),
         makeColorCellForLiveFlag(optionalStr(laneState.cdrLockLive())),
         optionalStr(laneStat.cdrLockChangedCount()),
         joinedStr(eyeHeights),
         joinedStr(eyeWidths),
         optionalStr(laneState.rxFrequencyPPM()),
         optionalStr(laneStat.snr())});
  }

  printLaneTable(out, prefix + "RX PMD", lanes, columns);
}

void CmdShowInterfacePhy::printPmdLaneTxInfo(
    std::ostream& out,
    phy::PhySideState& sideState,
    const std::set<int>& pmdLanes,
    const std::string& prefix) {
  auto columns = makeLaneColumns(
      {"Pre3",
       "Pre2",
       "Pre1",
       "Main",
       "Post1",
       "Post2",
       "Post3",
       "Precoding",
       "DriverSwing",
       "DigGain",
       "DiffEncoderEn",
       "LdoBypass"});

  std::vector<int> lanes;
  for (auto pmdLane : pmdLanes) {
    lanes.push_back(pmdLane);
    auto laneState = (*sideState.pmd()->lanes())[pmdLane];
    auto txSettings = *laneState.txSettings();
    addLaneCells(
        columns,
        {txTapStr(txSettings.firPre3(), optionalStr(txSettings.pre3())),
         txTapStr(txSettings.firPre2(), std::to_string(*txSettings.pre2())),
         txTapStr(txSettings.firPre1(), std::to_string(*txSettings.pre())),
         txTapStr(txSettings.firMain(), std::to_string(*txSettings.main())),
         txTapStr(txSettings.firPost1(), std::to_string(*txSettings.post())),
         txTapStr(txSettings.firPost2(), std::to_string(*txSettings.post2())),
         txTapStr(txSettings.firPost3(), std::to_string(*txSettings.post3())),
         optionalStr(txSettings.precoding()),
         optionalStr(txSettings.driverSwing()),
         optionalStr(txSettings.digGain()),
         optionalStr(txSettings.diffEncoderEn()),
         optionalStr(txSettings.ldoBypass())});
  }

  printLaneTable(out, prefix + "TX PMD", lanes, columns);
}

void CmdShowInterfacePhy::printSerdesParametersInfo(
    std::ostream& out,
    phy::PmdState& pmdState,
    const std::string& prefix) {
  auto columns = makeLaneColumns(
      {"RVga",
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
       "RxPrecoding",
       "RxCtleCode",
       "RxDspMode",
       "RxAfeTrim",
       "RxDiffEncoderEn",
       "RxInstgBoost1Start",
       "RxInstgBoost1Step",
       "RxInstgBoost1Stop",
       "RxInstgBoost2OrHrStart",
       "RxInstgBoost2OrHrStep",
       "RxInstgBoost2OrHrStop",
       "RxInstgC1Start1p7",
       "RxInstgC1Step1p7",
       "RxInstgC1Stop1p7",
       "RxInstgDfeStart1p7",
       "RxInstgDfeStep1p7",
       "RxInstgDfeStop1p7",
       "RxInstgEnableScan",
       "RxInstgScanUseSrSettings",
       "RxFfeLengthBitmap",
       "RxFfeLmsDynamicGatingEn"});

  std::vector<int> lanes;
  for (const auto& [laneId, laneState] : *pmdState.lanes()) {
    lanes.push_back(laneId);
    auto serdesParams = laneState.serdesParameters();

    std::string rxReach = kNotApplicable;
    if (auto rxReachVal = serdesParams->rxReach()) {
      rxReach = apache::thrift::util::enumNameSafe(*rxReachVal);
    }

    addLaneCells(
        columns,
        {optionalStr(serdesParams->rvga()),
         optionalStr(serdesParams->dco()),
         optionalStr(serdesParams->tpChn0()),
         optionalStr(serdesParams->tpChn1()),
         optionalStr(serdesParams->tpChn2()),
         optionalStr(serdesParams->rxPf()),
         optionalStr(serdesParams->rxPfLfq()),
         optionalStr(serdesParams->rxPfHfq()),
         optionalStr(serdesParams->rxFltM()),
         optionalStr(serdesParams->rxFltS()),
         optionalStr(serdesParams->rxTap1()),
         optionalStr(serdesParams->rxTap2()),
         optionalStr(serdesParams->rxEq3()),
         optionalStr(serdesParams->rxEq2()),
         optionalStr(serdesParams->rxEq1()),
         optionalStr(serdesParams->rxEqM()),
         optionalStr(serdesParams->rxEqP1()),
         optionalStr(serdesParams->rxEqP2()),
         rxReach,
         optionalStr(serdesParams->rxPrecoding()),
         optionalStr(serdesParams->rxCtleCode()),
         optionalStr(serdesParams->rxDspMode()),
         optionalStr(serdesParams->rxAfeTrim()),
         optionalStr(serdesParams->rxDiffEncoderEn()),
         optionalStr(serdesParams->rxInstgBoost1Start()),
         optionalStr(serdesParams->rxInstgBoost1Step()),
         optionalStr(serdesParams->rxInstgBoost1Stop()),
         optionalStr(serdesParams->rxInstgBoost2OrHrStart()),
         optionalStr(serdesParams->rxInstgBoost2OrHrStep()),
         optionalStr(serdesParams->rxInstgBoost2OrHrStop()),
         optionalStr(serdesParams->rxInstgC1Start1p7()),
         optionalStr(serdesParams->rxInstgC1Step1p7()),
         optionalStr(serdesParams->rxInstgC1Stop1p7()),
         optionalStr(serdesParams->rxInstgDfeStart1p7()),
         optionalStr(serdesParams->rxInstgDfeStep1p7()),
         optionalStr(serdesParams->rxInstgDfeStop1p7()),
         optionalStr(serdesParams->rxInstgEnableScan()),
         optionalStr(serdesParams->rxInstgScanUseSrSettings()),
         optionalStr(serdesParams->rxFfeLengthBitmap()),
         optionalStr(serdesParams->rxFfeLmsDynamicGatingEn())});
  }

  printLaneTable(out, prefix + "Serdes Parameters", lanes, columns);
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
