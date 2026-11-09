// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/transceiver/performancemonitoring/CmdShowInterfaceTransceiverPerformanceMonitoring.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

namespace facebook::fboss {

namespace {

constexpr auto kMediaSide = "Media";
constexpr auto kHostSide = "Host";

cli::VdmPerfMonitorParamVal toParamVal(
    const link::LinkPerfMonitorParamEachSideVal& val) {
  cli::VdmPerfMonitorParamVal out;
  out.min() = *val.min();
  out.max() = *val.max();
  out.avg() = *val.avg();
  out.cur() = *val.cur();
  return out;
}

template <typename FieldRef, typename T>
void setIfPresent(FieldRef dst, const std::map<int32_t, T>& src, int32_t lane) {
  const auto it = src.find(lane);
  if (it != src.end()) {
    dst = it->second;
  }
}

std::vector<cli::VdmLaneStats> buildLaneStats(
    const VdmPerfMonitorPortSideStats& sideStats) {
  // A module only populates the VDM observables it supports, so the lane set
  // is the union of the keys across every per-lane map.
  std::set<int32_t> lanes;
  const auto collectLanes = [&lanes](const auto& laneMap) {
    for (const auto& [lane, _] : laneMap) {
      lanes.insert(lane);
    }
  };
  collectLanes(*sideStats.laneSNR());
  collectLanes(*sideStats.lanePam4Level0SD());
  collectLanes(*sideStats.lanePam4Level1SD());
  collectLanes(*sideStats.lanePam4Level2SD());
  collectLanes(*sideStats.lanePam4Level3SD());
  collectLanes(*sideStats.lanePam4MPI());
  collectLanes(*sideStats.lanePam4LTP());
  collectLanes(*sideStats.lanePam4MPIFlags());

  std::vector<cli::VdmLaneStats> laneStats;
  for (const auto lane : lanes) {
    cli::VdmLaneStats stats;
    stats.lane() = lane;
    setIfPresent(stats.snr(), *sideStats.laneSNR(), lane);
    setIfPresent(stats.pam4Level0SD(), *sideStats.lanePam4Level0SD(), lane);
    setIfPresent(stats.pam4Level1SD(), *sideStats.lanePam4Level1SD(), lane);
    setIfPresent(stats.pam4Level2SD(), *sideStats.lanePam4Level2SD(), lane);
    setIfPresent(stats.pam4Level3SD(), *sideStats.lanePam4Level3SD(), lane);
    setIfPresent(stats.pam4MPI(), *sideStats.lanePam4MPI(), lane);
    setIfPresent(stats.pam4LTP(), *sideStats.lanePam4LTP(), lane);

    const auto& mpiFlags = *sideStats.lanePam4MPIFlags();
    const auto flagIt = mpiFlags.find(lane);
    if (flagIt != mpiFlags.end()) {
      stats.pam4MPIAlarmHigh() = *flagIt->second.alarm()->high();
      stats.pam4MPIAlarmLow() = *flagIt->second.alarm()->low();
      stats.pam4MPIWarnHigh() = *flagIt->second.warn()->high();
      stats.pam4MPIWarnLow() = *flagIt->second.warn()->low();
    }
    laneStats.push_back(std::move(stats));
  }
  return laneStats;
}

cli::VdmCoherentFecPm toCoherentFecPm(const FecPm& fecPm) {
  cli::VdmCoherentFecPm out;
  out.rxBitsPm().from_optional(fecPm.rxBitsPm().to_optional());
  out.rxBitsSubIntPm().from_optional(fecPm.rxBitsSubIntPm().to_optional());
  out.rxCorrBitsPm().from_optional(fecPm.rxCorrBitsPm().to_optional());
  out.rxMinCorrBitsSubIntPm().from_optional(
      fecPm.rxMinCorrBitsSubIntPm().to_optional());
  out.rxMaxCorrBitsSubIntPm().from_optional(
      fecPm.rxMaxCorrBitsSubIntPm().to_optional());
  out.rxFramesPm().from_optional(fecPm.rxFramesPm().to_optional());
  out.rxFramesSubIntPm().from_optional(fecPm.rxFramesSubIntPm().to_optional());
  out.rxFramesUncorrErrPm().from_optional(
      fecPm.rxFramesUncorrErrPm().to_optional());
  out.rxMinFramesUncorrErrSubIntPm().from_optional(
      fecPm.rxMinFramesUncorrErrSubIntPm().to_optional());
  out.rxMaxFramesUncorrErrSubIntPm().from_optional(
      fecPm.rxMaxFramesUncorrErrSubIntPm().to_optional());
  return out;
}

// Emitted in C-CMIS Page 35h order so the CLI output matches the spec table.
std::vector<cli::VdmNamedPerfMonitorParam> toLinkPmParams(
    const LinkPm& linkPm) {
  std::vector<cli::VdmNamedPerfMonitorParam> out;
  const auto addParam = [&out](const std::string& name, const auto& param) {
    if (!param.has_value()) {
      return;
    }
    cli::VdmNamedPerfMonitorParam entry;
    entry.name() = name;
    entry.value() = toParamVal(*param);
    out.push_back(std::move(entry));
  };

  addParam("CD (ps/nm)", linkPm.cd());
  addParam("DGD (ps)", linkPm.dgd());
  addParam("SOPMD (ps^2)", linkPm.sopmd());
  addParam("PDL (dB)", linkPm.pdl());
  addParam("OSNR (dB)", linkPm.osnr());
  addParam("eSNR (dB)", linkPm.esnr());
  addParam("CFO (MHz)", linkPm.cfo());
  addParam("EVM Modem (%)", linkPm.evmModem());
  addParam("Tx Power (dBm)", linkPm.txPower());
  addParam("Rx Power (dBm)", linkPm.rxPower());
  addParam("Rx Sig Power (dBm)", linkPm.rxSigPower());
  addParam("SOP ROC (krad/s)", linkPm.sopcr());
  addParam("MER (dB)", linkPm.mer());
  addParam("Clock Recovery Loop (%)", linkPm.clockRecoveryLoop());
  addParam("SNR Margin (dB)", linkPm.snrMargin());
  addParam("Q-Factor (dB)", linkPm.qFactor());
  addParam("Q-Margin (dB)", linkPm.qMargin());
  return out;
}

cli::VdmCoherentStats toCoherentStats(const CoherentVdmStats& coherent) {
  cli::VdmCoherentStats out;
  out.modulatorBiasXI().from_optional(coherent.modulatorBiasXI().to_optional());
  out.modulatorBiasXQ().from_optional(coherent.modulatorBiasXQ().to_optional());
  out.modulatorBiasYI().from_optional(coherent.modulatorBiasYI().to_optional());
  out.modulatorBiasYQ().from_optional(coherent.modulatorBiasYQ().to_optional());
  out.modulatorBiasXPhase().from_optional(
      coherent.modulatorBiasXPhase().to_optional());
  out.modulatorBiasYPhase().from_optional(
      coherent.modulatorBiasYPhase().to_optional());
  out.cdLowGranularity().from_optional(
      coherent.cdLowGranularity().to_optional());
  out.sopmdLowGranularity().from_optional(
      coherent.sopmdLowGranularity().to_optional());
  if (coherent.fecPm().has_value()) {
    out.fecPm() = toCoherentFecPm(*coherent.fecPm());
  }
  if (coherent.linkPm().has_value()) {
    out.linkPm() = toLinkPmParams(*coherent.linkPm());
  }
  return out;
}

cli::VdmPortSideStats toPortSideStats(
    const std::string& side,
    const VdmPerfMonitorPortSideStats& sideStats) {
  cli::VdmPortSideStats out;
  out.side() = side;
  out.datapathBER() = toParamVal(*sideStats.datapathBER());
  out.datapathErroredFrames() = toParamVal(*sideStats.datapathErroredFrames());
  out.fecTailCurr().from_optional(sideStats.fecTailCurr().to_optional());
  out.fecTailMax().from_optional(sideStats.fecTailMax().to_optional());
  out.maxSupportedFecTail().from_optional(
      sideStats.maxSupportedFecTail().to_optional());
  out.laneStats() = buildLaneStats(sideStats);
  if (sideStats.coherentVdmStats().has_value()) {
    out.coherentStats() = toCoherentStats(*sideStats.coherentVdmStats());
  }
  return out;
}

cli::ShowInterfaceTransceiverPerformanceMonitoringModel createModel(
    const std::map<int, TransceiverInfo>& transceiverInfo,
    const std::vector<std::string>& queriedIfs) {
  const std::unordered_set<std::string> queriedIfSet(
      queriedIfs.begin(), queriedIfs.end());
  const auto isQueried = [&queriedIfSet](const std::string& portName) {
    return queriedIfSet.empty() || queriedIfSet.contains(portName);
  };

  // Ordered by port name so repeated invocations render identically.
  std::map<std::string, cli::VdmPortStats> portToStats;
  const auto addSide = [&portToStats](
                           const std::string& side,
                           const std::string& portName,
                           const VdmPerfMonitorPortSideStats& sideStats,
                           const VdmPerfMonitorStats& vdmStats) {
    auto& portStats = portToStats[portName];
    portStats.portName() = portName;
    portStats.statsCollectionTime() = *vdmStats.statsCollectionTme();
    portStats.intervalStartTime() = *vdmStats.intervalStartTime();
    portStats.sideStats()->push_back(toPortSideStats(side, sideStats));
  };

  for (const auto& [_, tcvrInfo] : transceiverInfo) {
    if (!tcvrInfo.tcvrStats()->vdmPerfMonitorStats().has_value()) {
      continue;
    }
    const auto& vdmStats = *tcvrInfo.tcvrStats()->vdmPerfMonitorStats();
    for (const auto& [portName, sideStats] : *vdmStats.mediaPortVdmStats()) {
      if (isQueried(portName)) {
        addSide(kMediaSide, portName, sideStats, vdmStats);
      }
    }
    for (const auto& [portName, sideStats] : *vdmStats.hostPortVdmStats()) {
      if (isQueried(portName)) {
        addSide(kHostSide, portName, sideStats, vdmStats);
      }
    }
  }

  cli::ShowInterfaceTransceiverPerformanceMonitoringModel model;
  for (auto& [_, portStats] : portToStats) {
    model.portStats()->push_back(std::move(portStats));
  }
  return model;
}

std::string formatTimestamp(int64_t epochSeconds, bool dateOnly) {
  if (epochSeconds == 0) {
    return "-";
  }
  const auto time = static_cast<std::time_t>(epochSeconds);
  std::tm tm{};
  localtime_r(&time, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, dateOnly ? "%H:%M:%S %Z" : "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::string formatInterval(int64_t startTime, int64_t collectionTime) {
  if (startTime == 0 || collectionTime == 0) {
    return formatTimestamp(std::max(startTime, collectionTime), false);
  }
  return fmt::format(
      "{} -> {} ({} s)",
      formatTimestamp(startTime, false),
      formatTimestamp(collectionTime, true),
      collectionTime - startTime);
}

template <typename FieldRef>
std::string formatOptional(const FieldRef& value, int precision) {
  if (!value.has_value()) {
    return "-";
  }
  return fmt::format("{:.{}f}", *value, precision);
}

template <typename FieldRef>
std::string formatOptionalInt(const FieldRef& value) {
  if (!value.has_value()) {
    return "-";
  }
  return fmt::format("{}", *value);
}

bool hasMpiFlags(const cli::VdmLaneStats& lane) {
  return lane.pam4MPIAlarmHigh().has_value() ||
      lane.pam4MPIAlarmLow().has_value() ||
      lane.pam4MPIWarnHigh().has_value() || lane.pam4MPIWarnLow().has_value();
}

std::string formatMpiFlags(const cli::VdmLaneStats& lane) {
  if (!hasMpiFlags(lane)) {
    return "-";
  }
  const auto raised = [](const auto& flag) {
    return flag.has_value() && *flag;
  };
  std::vector<std::string> flags;
  if (raised(lane.pam4MPIAlarmHigh())) {
    flags.emplace_back("alarm-hi");
  }
  if (raised(lane.pam4MPIAlarmLow())) {
    flags.emplace_back("alarm-lo");
  }
  if (raised(lane.pam4MPIWarnHigh())) {
    flags.emplace_back("warn-hi");
  }
  if (raised(lane.pam4MPIWarnLow())) {
    flags.emplace_back("warn-lo");
  }
  return flags.empty() ? "none" : folly::join(",", flags);
}

/*
 * Datapath BER/errored frames and every C-CMIS link PM parameter share the
 * same min/max/avg/cur shape, so they are rendered as rows of one table.
 */
void addParamRow(
    utils::Table& table,
    const std::string& name,
    const cli::VdmPerfMonitorParamVal& val,
    bool scientific) {
  // Left unstyled on purpose: the shared styledBer() thresholds are calibrated
  // for PAM4 datapaths and would flag a healthy coherent pre-FEC BER as an
  // error.
  const auto cell = [scientific](double value) -> utils::Table::RowData {
    return scientific ? fmt::format("{:.3e}", value)
                      : fmt::format("{:.3f}", value);
  };
  table.addRow(
      {name,
       cell(*val.min()),
       cell(*val.max()),
       cell(*val.avg()),
       cell(*val.cur())});
}

void printParamTable(
    const cli::VdmPortSideStats& sideStats,
    std::ostream& out) {
  utils::Table table;
  table.setHeader({"Parameter", "Min", "Max", "Avg", "Cur"});
  addParamRow(table, "Datapath BER", *sideStats.datapathBER(), true);
  addParamRow(
      table, "Errored Frames", *sideStats.datapathErroredFrames(), true);
  if (sideStats.coherentStats().has_value()) {
    for (const auto& param : *sideStats.coherentStats()->linkPm()) {
      addParamRow(table, *param.name(), *param.value(), false);
    }
  }
  out << table << std::endl;
}

/*
 * PAM4 observables do not apply to the coherent side of a DCO module, but the
 * module still publishes the maps, so drop those columns rather than print a
 * block of zeroes. Any column no lane populates is dropped too.
 */
void printLaneStats(const cli::VdmPortSideStats& sideStats, std::ostream& out) {
  const auto& laneStats = *sideStats.laneStats();
  if (laneStats.empty()) {
    return;
  }
  const bool coherent = sideStats.coherentStats().has_value();

  struct LaneColumn {
    std::string header;
    std::function<std::string(const cli::VdmLaneStats&)> format;
    std::function<bool(const cli::VdmLaneStats&)> populated;
  };
  const auto doubleCol =
      [](const std::string& header, auto accessor, int precision) {
        return LaneColumn{
            header,
            [accessor, precision](const cli::VdmLaneStats& lane) {
              return formatOptional(accessor(lane), precision);
            },
            [accessor](const cli::VdmLaneStats& lane) {
              return accessor(lane).has_value();
            }};
      };

  std::vector<LaneColumn> columns;
  columns.push_back(
      doubleCol("SNR (dB)", [](const auto& l) { return l.snr(); }, 2));
  if (!coherent) {
    columns.push_back(doubleCol(
        "PAM4 L0 SD", [](const auto& l) { return l.pam4Level0SD(); }, 3));
    columns.push_back(doubleCol(
        "PAM4 L1 SD", [](const auto& l) { return l.pam4Level1SD(); }, 3));
    columns.push_back(doubleCol(
        "PAM4 L2 SD", [](const auto& l) { return l.pam4Level2SD(); }, 3));
    columns.push_back(doubleCol(
        "PAM4 L3 SD", [](const auto& l) { return l.pam4Level3SD(); }, 3));
    columns.push_back(
        doubleCol("PAM4 MPI", [](const auto& l) { return l.pam4MPI(); }, 3));
    columns.push_back(
        doubleCol("PAM4 LTP", [](const auto& l) { return l.pam4LTP(); }, 3));
    columns.push_back(LaneColumn{"MPI Flags", formatMpiFlags, hasMpiFlags});
  }

  std::vector<utils::Table::RowData> header = {"Lane"};
  std::vector<const LaneColumn*> shown;
  for (const auto& column : columns) {
    const bool anyPopulated = std::any_of(
        laneStats.begin(), laneStats.end(), [&column](const auto& lane) {
          return column.populated(lane);
        });
    if (anyPopulated) {
      header.emplace_back(column.header);
      shown.push_back(&column);
    }
  }
  if (shown.empty()) {
    return;
  }

  utils::Table table;
  table.setHeader(header);
  for (const auto& lane : laneStats) {
    std::vector<utils::Table::RowData> row = {fmt::format("{}", *lane.lane())};
    for (const auto* column : shown) {
      row.emplace_back(column->format(lane));
    }
    table.addRow(row);
  }
  out << table << std::endl;
}

std::string formatFecTail(const cli::VdmPortSideStats& sideStats) {
  if (!sideStats.fecTailCurr().has_value() &&
      !sideStats.fecTailMax().has_value()) {
    return "-";
  }
  return fmt::format(
      "{} / {} (max supported {})",
      formatOptionalInt(sideStats.fecTailCurr()),
      formatOptionalInt(sideStats.fecTailMax()),
      formatOptionalInt(sideStats.maxSupportedFecTail()));
}

void printScalars(const cli::VdmPortSideStats& sideStats, std::ostream& out) {
  out << fmt::format("    FEC tail cur/max:  {}\n", formatFecTail(sideStats));
  if (!sideStats.coherentStats().has_value()) {
    return;
  }
  const auto& coherent = *sideStats.coherentStats();
  out << fmt::format(
      "    Mod bias XI/XQ/YI/YQ (%):  {} / {} / {} / {}   X/Y phase: {} / {}\n",
      formatOptional(coherent.modulatorBiasXI(), 2),
      formatOptional(coherent.modulatorBiasXQ(), 2),
      formatOptional(coherent.modulatorBiasYI(), 2),
      formatOptional(coherent.modulatorBiasYQ(), 2),
      formatOptional(coherent.modulatorBiasXPhase(), 2),
      formatOptional(coherent.modulatorBiasYPhase(), 2));
  out << fmt::format(
      "    Low-granularity CD/SOPMD:  {} ps/nm / {} ps^2\n",
      formatOptional(coherent.cdLowGranularity(), 2),
      formatOptional(coherent.sopmdLowGranularity(), 2));
  if (!coherent.fecPm().has_value()) {
    return;
  }
  const auto& fecPm = *coherent.fecPm();
  out << fmt::format(
      "    FEC PM rx bits/corrected:  {} / {}\n",
      formatOptionalInt(fecPm.rxBitsPm()),
      formatOptionalInt(fecPm.rxCorrBitsPm()));
  out << fmt::format(
      "    FEC PM rx frames/uncorr:   {} / {}   (uncorr sub-interval min/max {} / {})\n",
      formatOptionalInt(fecPm.rxFramesPm()),
      formatOptionalInt(fecPm.rxFramesUncorrErrPm()),
      formatOptionalInt(fecPm.rxMinFramesUncorrErrSubIntPm()),
      formatOptionalInt(fecPm.rxMaxFramesUncorrErrSubIntPm()));
}

void printPortSideStats(
    const cli::VdmPortSideStats& sideStats,
    std::ostream& out) {
  std::string side = *sideStats.side();
  std::transform(side.begin(), side.end(), side.begin(), [](unsigned char c) {
    return std::toupper(c);
  });
  out << fmt::format("{} SIDE\n", side);
  printParamTable(sideStats, out);
  printLaneStats(sideStats, out);
  printScalars(sideStats, out);
  out << std::endl;
}

} // namespace

CmdShowInterfaceTransceiverPerformanceMonitoring::RetType
CmdShowInterfaceTransceiverPerformanceMonitoring::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs) {
  std::map<int, TransceiverInfo> transceiverInfo;
  auto qsfpClient =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
  qsfpClient->sync_getTransceiverInfo(transceiverInfo, {});
  return createModel(transceiverInfo, queriedIfs.data());
}

void CmdShowInterfaceTransceiverPerformanceMonitoring::printOutput(
    const RetType& model,
    std::ostream& out) {
  if (model.portStats()->empty()) {
    out << "No VDM performance monitoring stats available\n";
    return;
  }
  for (const auto& portStats : *model.portStats()) {
    const bool coherent = std::any_of(
        portStats.sideStats()->begin(),
        portStats.sideStats()->end(),
        [](const auto& sideStats) {
          return sideStats.coherentStats().has_value();
        });
    out << fmt::format(
        "Interface: {}{}\n",
        *portStats.portName(),
        coherent ? "   [coherent]" : "");
    out << fmt::format(
        "Interval:  {}\n\n",
        formatInterval(
            *portStats.intervalStartTime(), *portStats.statsCollectionTime()));
    for (const auto& sideStats : *portStats.sideStats()) {
      printPortSideStats(sideStats, out);
    }
  }
}

std::string_view
CmdShowInterfaceTransceiverPerformanceMonitoringTraits::description() {
  return "Displays the transceiver's VDM (Versatile Diagnostics Monitoring) performance monitoring stats "
         "for the media and host sides of an interface, as collected by qsfp_service over the last PM interval. "
         "Reports datapath pre-FEC BER and errored frames (min/max/avg/cur), FEC tail, and per-lane SNR, "
         "PAM4 level standard deviations, MPI and LTP. For coherent (DCO / 400G-800G ZR) modules it additionally "
         "reports the C-CMIS modulator bias, low-granularity CD/SOPMD, lane FEC PM (Page 34h) and link PM "
         "(Page 35h) parameters such as OSNR, DGD, PDL, Q-factor and SNR margin.";
}

CmdShowInterfaceTransceiverPerformanceMonitoring::RetType
CmdShowInterfaceTransceiverPerformanceMonitoring::sampleModel() {
  cli::VdmPerfMonitorParamVal ber;
  ber.min() = 1.2e-8;
  ber.max() = 4.5e-8;
  ber.avg() = 2.1e-8;
  ber.cur() = 1.9e-8;

  cli::VdmPerfMonitorParamVal erroredFrames;
  erroredFrames.min() = 0;
  erroredFrames.max() = 3.0e-6;
  erroredFrames.avg() = 5.0e-7;
  erroredFrames.cur() = 0;

  cli::VdmLaneStats lane;
  lane.lane() = 1;
  lane.snr() = 19.5;
  lane.pam4Level0SD() = 0.021;
  lane.pam4Level1SD() = 0.023;
  lane.pam4Level2SD() = 0.022;
  lane.pam4Level3SD() = 0.024;
  lane.pam4MPI() = 0.031;
  lane.pam4LTP() = 0.014;
  lane.pam4MPIAlarmHigh() = false;
  lane.pam4MPIAlarmLow() = false;
  lane.pam4MPIWarnHigh() = false;
  lane.pam4MPIWarnLow() = false;

  cli::VdmPortSideStats mediaSide;
  mediaSide.side() = kMediaSide;
  mediaSide.datapathBER() = ber;
  mediaSide.datapathErroredFrames() = erroredFrames;
  mediaSide.fecTailCurr() = 3;
  mediaSide.fecTailMax() = 6;
  mediaSide.maxSupportedFecTail() = 15;
  mediaSide.laneStats() = std::vector<cli::VdmLaneStats>{lane};

  cli::VdmPortStats portStats;
  portStats.portName() = "eth1/1/1";
  portStats.statsCollectionTime() = 1735689600;
  portStats.intervalStartTime() = 1735688700;
  portStats.sideStats() = std::vector<cli::VdmPortSideStats>{mediaSide};

  cli::ShowInterfaceTransceiverPerformanceMonitoringModel model;
  model.portStats() = std::vector<cli::VdmPortStats>{portStats};
  return model;
}

// Template instantiations
template void CmdHandler<
    CmdShowInterfaceTransceiverPerformanceMonitoring,
    CmdShowInterfaceTransceiverPerformanceMonitoringTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceTransceiverPerformanceMonitoring,
    CmdShowInterfaceTransceiverPerformanceMonitoringTraits>::getValidFilters();

} // namespace facebook::fboss
