/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/traffic/CmdShowInterfaceTraffic.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/Table.h"

#include <boost/algorithm/string/split.hpp>
#include <fmt/color.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>

namespace facebook::fboss {

CmdShowInterfaceTraffic::RetType CmdShowInterfaceTraffic::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  folly::IOThreadPoolExecutor executor(2);

  // Gather port stats asynchrounously
  auto portInfos =
      client->semifuture_getAllPortInfo().via(executor.getEventBase());

  std::map<std::string, int64_t> counters;
  if (utils::isMultiSwitchEnabled(hostInfo)) {
    auto hwAgentQueryFn =
        [&counters](
            apache::thrift::Client<facebook::fboss::FbossHwCtrl>& client) {
          std::map<std::string, int64_t> hwAgentCounters;
          try {
#ifndef IS_OSS
            apache::thrift::Client<facebook::thrift::Monitor> monitoringClient{
                client.getChannelShared()};
            monitoringClient.sync_getCounters(hwAgentCounters);
#else
            // FbossHwCtrl does not extend FacebookService; the OSS HwAgent
            // multiplex serves fb303 methods via a FacebookBase2 handler, so
            // reuse the channel with an FbossCtrl client (which carries
            // getCounters) to fetch HwAgent counters.
            apache::thrift::Client<facebook::fboss::FbossCtrl> fb303Client{
                client.getChannelShared()};
            fb303Client.sync_getCounters(hwAgentCounters);
#endif
          } catch (const std::exception& ex) {
            // runOnAllHwAgents swallows what escapes here, leaving every rate
            // at zero with no hint as to why.
            XLOG(ERR) << "could not read HwAgent counters: " << ex.what();
            return;
          }
          counters.merge(hwAgentCounters);
        };
    utils::runOnAllHwAgents(hostInfo, hwAgentQueryFn);
  } else {
    auto entries =
        client->semifuture_getCounters().via(executor.getEventBase());
    entries.wait();
    counters = entries.value();
  }

  portInfos.wait();

  return createModel(portInfos.value(), counters, queriedIfs);
}

CmdShowInterfaceTraffic::RetType CmdShowInterfaceTraffic::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portCounters,
    std::map<std::string, int64_t> intCounters,
    const std::vector<std::string>& queriedIfs) {
  RetType ret;

  std::unordered_set<std::string> queriedSet(
      queriedIfs.begin(), queriedIfs.end());

  // Create counter object for each interface
  for (const auto& portEntry : portCounters) {
    auto portInfo = portEntry.second;
    if (queriedIfs.size() == 0 || queriedSet.count(portInfo.name().value())) {
      cli::TrafficErrorCounters errorCounters;

      std::string pname = portInfo.name().value();

      // FBOSS devices only have 2 error counters, so extract those here
      // The other counters are placeholders to match EOS output
      int64_t inputErrors = folly::copy(
          portInfo.input().value().errors().value().errors().value());
      int64_t outputErrors = folly::copy(
          portInfo.output().value().errors().value().errors().value());

      const auto operState = folly::copy(portInfo.operState().value());

      // As mentioned above, only tx and rx errors are meaningful to FBOSS.
      // The other fields are statically set just to provide output parity
      // with EOS
      errorCounters.interfaceName() = pname;
      errorCounters.peerIf() =
          extractExpectedPort(portInfo.description().value());
      errorCounters.ifStatus() =
          (operState == facebook::fboss::PortOperState::UP) ? "up" : "down";
      errorCounters.fcsErrors() = 0;
      errorCounters.alignErrors() = 0;
      errorCounters.symbolErrors() = 0;
      errorCounters.rxErrors() = inputErrors;
      errorCounters.runtErrors() = 0;
      errorCounters.giantErrors() = 0;
      errorCounters.txErrors() = outputErrors;

      cli::TrafficCounters trafficCounters;
      // Getting various counters and converting to Mbps
      int64_t portSpeed = folly::copy(portInfo.speedMbps().value());

      bool countersAvailable = false;
      auto rate = [&](std::string_view stat) -> int64_t {
        auto itr = intCounters.find(fmt::format("{}.{}.rate.60", pname, stat));
        if (itr == intCounters.end()) {
          return 0;
        }
        countersAvailable = true;
        return itr->second;
      };

      long inSpeedBps = rate("in_bytes") * 8;

      double inSpeedMbps = (double)inSpeedBps / 1000000;

      // Unicast + multicast + broadcast = PPS
      int64_t inPPS = rate("in_unicast_pkts") + rate("in_multicast_pkts") +
          rate("in_broadcast_pkts");

      long outSpeedBps = rate("out_bytes") * 8;

      double outSpeedMbps = (double)outSpeedBps / 1000000;

      // Unicast + multicast + broadcast = PPS
      int64_t outPPS = rate("out_unicast_pkts") + rate("out_multicast_pkts") +
          rate("out_broadcast_pkts");

      trafficCounters.interfaceName() = pname;
      trafficCounters.peerIf() =
          extractExpectedPort(portInfo.description().value());
      trafficCounters.inMbps() = inSpeedMbps;
      trafficCounters.inPct() =
          calculateUtilizationPercent(inSpeedMbps, portSpeed);
      trafficCounters.inKpps() = inPPS / 1000;
      trafficCounters.outMbps() = outSpeedMbps;
      trafficCounters.outPct() =
          calculateUtilizationPercent(outSpeedMbps, portSpeed);
      trafficCounters.outKpps() = outPPS / 1000;
      trafficCounters.portSpeed() = portSpeed;
      trafficCounters.countersAvailable() = countersAvailable;

      // As in fb_toolkit, only interfaces with errors make the error table;
      // there is no "show zero" option to turn that off yet.
      if (isNonZeroErrors(errorCounters)) {
        ret.error_counters()->push_back(errorCounters);
      }

      if (operState == facebook::fboss::PortOperState::UP ||
          queriedSet.count(pname) || isInterestingTraffic(trafficCounters)) {
        ret.traffic_counters()->push_back(trafficCounters);
      }
    }
  }
  std::sort(
      ret.error_counters()->begin(),
      ret.error_counters()->end(),
      [](cli::TrafficErrorCounters& a, cli::TrafficErrorCounters b) {
        return a.interfaceName().value() < b.interfaceName().value();
      });

  std::sort(
      ret.traffic_counters()->begin(),
      ret.traffic_counters()->end(),
      [](cli::TrafficCounters& a, cli::TrafficCounters b) {
        return a.interfaceName().value() < b.interfaceName().value();
      });
  return ret;
}

double CmdShowInterfaceTraffic::calculateUtilizationPercent(
    double speedMbps,
    int bandwidth) {
  return (speedMbps / bandwidth) * 100;
}

std::string CmdShowInterfaceTraffic::extractExpectedPort(
    const std::string& portDescription) {
  if (portDescription.empty()) {
    return "--";
  }
  std::vector<std::string> results;
  // Minipack style: "<local>: <peer-host>.<peer-port> (<attrs>)"
  boost::split(results, portDescription, [](char c) { return c == ' '; });
  if (results.size() > 1) {
    return results[1];
  }
  // Hostname style, dot separated. Each branch names the tokens it needs; a
  // description that lacks them is shown verbatim instead of indexed.
  boost::split(results, portDescription, [](char c) { return c == '.'; });
  static const RE2 sswRegex(".*ssw.*");
  static const RE2 fswRegex("^fsw.*");
  if (RE2::FullMatch(portDescription, sswRegex) && results.size() > 2) {
    return results[1] + "." + results[2];
  }
  if (RE2::FullMatch(portDescription, fswRegex) && results.size() > 1) {
    return results[0] + "." + results[1];
  }
  if (results.size() > 1) {
    return results[1];
  }
  return portDescription;
}

bool CmdShowInterfaceTraffic::isInterestingTraffic(cli::TrafficCounters& tc) {
  if (folly::copy(tc.inMbps().value()) < 0.1 &&
      folly::copy(tc.inPct().value()) < 0.1 &&
      folly::copy(tc.inKpps().value()) < 0.1 &&
      folly::copy(tc.outMbps().value()) < 0.1 &&
      folly::copy(tc.outPct().value()) < 0.1 &&
      folly::copy(tc.outKpps().value()) < 0.1) {
    return false;
  }
  return true;
}

bool CmdShowInterfaceTraffic::isNonZeroErrors(cli::TrafficErrorCounters& ec) {
  if (folly::copy(ec.rxErrors().value()) <= 0 &&
      folly::copy(ec.txErrors().value()) <= 0) {
    return false;
  }
  return true;
}

std::vector<double> CmdShowInterfaceTraffic::getTrafficTotals(
    const std::vector<cli::TrafficCounters>& trafficCounters) {
  double inMbpsT = 0.0;
  double inPctT = 0.0;
  double inKppsT = 0.0;
  double outMbpsT = 0.0;
  double outPctT = 0.0;
  double outKppsT = 0.0;
  double totalBW = 0.0;

  for (const auto& tc : trafficCounters) {
    // Counting its speed would understate the utilization of the rest.
    if (!folly::copy(tc.countersAvailable().value())) {
      continue;
    }
    inMbpsT += folly::copy(tc.inMbps().value());
    inKppsT += folly::copy(tc.inKpps().value());
    outMbpsT += folly::copy(tc.outMbps().value());
    outKppsT += folly::copy(tc.outKpps().value());
    totalBW += folly::copy(tc.portSpeed().value());
  }

  if (totalBW > 0) {
    inPctT = (inMbpsT / totalBW) * 100;
    outPctT = (outMbpsT / totalBW) * 100;
  }

  std::vector<double> rv{inMbpsT, inPctT, inKppsT, outMbpsT, outPctT, outKppsT};
  return rv;
}

std::string CmdShowInterfaceTraffic::getThresholdColor(double pct) {
  if (pct >= 70) {
    return "ERROR";
  } else if (pct >= 50) {
    return "WARN";
  } else if (pct > 0) {
    return "GOOD";
  }
  return "NONE";
}

std::vector<std::string> CmdShowInterfaceTraffic::getRowColors(
    double inPct,
    double outPct) {
  // First get color based on percent threshold
  std::string inColor = getThresholdColor(inPct);
  std::string outColor = getThresholdColor(outPct);

  // Now decide what to color the interface name
  std::string ifNameColor;
  if (inPct > outPct) {
    ifNameColor = inColor;
  } else {
    ifNameColor = outColor;
  }

  // Colors as a warning if we seem to have traffic in a single direction
  const int UNIDRECTIONAL_TRAFFIC_THRESHOLD = 1;

  if ((inPct >= UNIDRECTIONAL_TRAFFIC_THRESHOLD &&
       outPct < UNIDRECTIONAL_TRAFFIC_THRESHOLD) ||
      (inPct < UNIDRECTIONAL_TRAFFIC_THRESHOLD &&
       outPct >= UNIDRECTIONAL_TRAFFIC_THRESHOLD)) {
    if (ifNameColor != "ERROR") {
      ifNameColor = "WARN";
    }
  }

  std::vector<std::string> rowColors = {inColor, outColor, ifNameColor};
  return rowColors;
}

Table::StyledCell CmdShowInterfaceTraffic::makeColorCell(
    const std::string& val,
    const std::string& level) {
  if (level == "ERROR") {
    return Table::StyledCell(val, Table::Style::ERROR);
  } else if (level == "GOOD") {
    return Table::StyledCell(val, Table::Style::GOOD);
  } else if (level == "INFO") {
    return Table::StyledCell(val, Table::Style::INFO);
  } else if (level == "WARN") {
    return Table::StyledCell(val, Table::Style::WARN);
  }
  return Table::StyledCell(val, Table::Style::NONE);
}

void CmdShowInterfaceTraffic::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table errorTable;
  Table trafficTable;

  // If Global option for color is yes (default) and we're printing to stdout
  // and stdout is a tty, then print color.  Otherwise suppress color
  bool printColor = false;
  if ((CmdGlobalOptions::getInstance()->getColor() == "yes") &&
      &out == &std::cout && isatty(fileno(stdout))) {
    printColor = true;
  }

  if (model.error_counters().value().size() != 0) {
    const std::string errorMessage = fmt::format(
        "ERRORS {} interfaces, watch for any incrementing counters:\n",
        std::to_string(model.error_counters().value().size()));

    if (printColor) {
      fmt::print(fg(fmt::color::red), "{}", errorMessage);
    } else {
      out << errorMessage;
    }

    errorTable.setHeader({
        "Interface Name",
        "Peer",
        "Status",
        "FCS",
        "Align",
        "Symbol",
        "Rx",
        "Runts",
        "Giants",
        "Tx",
    });

    for (const auto& counter : model.error_counters().value()) {
      errorTable.addRow({
          makeColorCell(counter.interfaceName().value(), "ERROR"),
          makeColorCell(counter.peerIf().value(), "ERROR"),
          makeColorCell(counter.ifStatus().value(), "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.fcsErrors().value())),
              "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.alignErrors().value())),
              "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.symbolErrors().value())),
              "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.rxErrors().value())), "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.runtErrors().value())),
              "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.giantErrors().value())),
              "ERROR"),
          makeColorCell(
              std::to_string(folly::copy(counter.txErrors().value())), "ERROR"),
      });
    }

  } else {
    constexpr std::string_view noErrorString =
        "No interfaces with In/Out errors - all-clear!\n";
    if (printColor) {
      fmt::print(fg(fmt::color::green), "{}", noErrorString);
    } else {
      out << noErrorString;
    }
  }

  trafficTable.setHeader({
      "Interface Name",
      "Peer",
      "Intvl",
      "InMbps",
      "InPct",
      "InKpps",
      "OutMbps",
      "OutPct",
      "OutKpps",
  });

  size_t interfacesWithoutCounters = 0;
  for (const auto& trafficCounter : model.traffic_counters().value()) {
    if (!folly::copy(trafficCounter.countersAvailable().value())) {
      ++interfacesWithoutCounters;
    }
  }
  const bool anyCounters =
      model.traffic_counters()->size() > interfacesWithoutCounters;

  for (const auto& trafficCounter : model.traffic_counters().value()) {
    std::vector<std::string> rowColors = getRowColors(
        folly::copy(trafficCounter.inPct().value()),
        folly::copy(trafficCounter.outPct().value()));

    const std::string& ifNameColor = rowColors[2];
    const std::string& rxColor = rowColors[0];
    const std::string& txColor = rowColors[1];

    const bool haveCounters =
        folly::copy(trafficCounter.countersAvailable().value());
    auto rateCell = [&](double value,
                        const std::string& color,
                        const std::string& suffix = "") {
      return haveCounters
          ? makeColorCell(fmt::format("{:.2f}", value) + suffix, color)
          : makeColorCell("--", "NONE");
    };

    trafficTable.addRow(
        {makeColorCell(trafficCounter.interfaceName().value(), ifNameColor),
         makeColorCell(trafficCounter.peerIf().value(), ifNameColor),
         makeColorCell(haveCounters ? "0:60" : "--", ifNameColor),
         rateCell(folly::copy(trafficCounter.inMbps().value()), rxColor),
         rateCell(folly::copy(trafficCounter.inPct().value()), rxColor, "%"),
         rateCell(folly::copy(trafficCounter.inKpps().value()), rxColor),
         rateCell(folly::copy(trafficCounter.outMbps().value()), txColor),
         rateCell(folly::copy(trafficCounter.outPct().value()), txColor, "%"),
         rateCell(folly::copy(trafficCounter.outKpps().value()), txColor)});
  }

  std::vector<double> totalTraffic =
      getTrafficTotals(model.traffic_counters().value());

  auto totalCell = [&](double value, const std::string& suffix = "") {
    return anyCounters
        ? makeColorCell(fmt::format("{:.2f}", value) + suffix, "INFO")
        : makeColorCell("--", "INFO");
  };

  trafficTable.addRow({
      makeColorCell("Total", "INFO"),
      makeColorCell("--", "INFO"),
      makeColorCell("--", "INFO"),
      totalCell(totalTraffic[0]),
      totalCell(totalTraffic[1], "%"),
      totalCell(totalTraffic[2]),
      totalCell(totalTraffic[3]),
      totalCell(totalTraffic[4], "%"),
      totalCell(totalTraffic[5]),
  });

  out << errorTable << std::endl;

  // Emitted here, not with the messages above: the tables stream at the end,
  // so an earlier write would land above the error table instead.
  if (interfacesWithoutCounters != 0) {
    const std::string noCountersMessage = fmt::format(
        "{} {} no known rate counters.\n",
        interfacesWithoutCounters,
        interfacesWithoutCounters == 1 ? "interface has" : "interfaces have");
    if (printColor) {
      fmt::print(fg(fmt::color::yellow), "{}", noCountersMessage);
    } else {
      out << noCountersMessage;
    }
  }
  if (model.traffic_counters()->empty()) {
    out << "No interfaces to display\n";
  }

  out << trafficTable << std::endl;
}

std::string_view CmdShowInterfaceTrafficTraits::description() {
  return "Displays per-interface traffic rates against the discovered peer: inbound/outbound Mbps, utilization percent, and Kpps over a sampling interval. Use it to gauge link utilization and spot imbalances. Every interface that is up is listed even when idle; a down interface is listed only when named on the command line or when it still has a rate. Rates the agent never published read as `--`, which is how a link nothing was collected for is told apart from a link carrying nothing.";
}

CmdShowInterfaceTraffic::RetType CmdShowInterfaceTraffic::sampleModel() {
  RetType model;

  cli::TrafficCounters counter1;
  counter1.interfaceName() = "eth1/1/1";
  counter1.peerIf() = "peer001";
  counter1.inMbps() = 1768.99;
  counter1.inPct() = 0.44;
  counter1.inKpps() = 54.00;
  counter1.outMbps() = 437.21;
  counter1.outPct() = 0.11;
  counter1.outKpps() = 29.00;
  counter1.portSpeed() = 400000;
  counter1.countersAvailable() = true;
  model.traffic_counters()->push_back(counter1);

  cli::TrafficCounters counter2;
  counter2.interfaceName() = "eth1/2/1";
  counter2.peerIf() = "peer002";
  counter2.inMbps() = 17.99;
  counter2.inPct() = 0.00;
  counter2.inKpps() = 2.00;
  counter2.outMbps() = 18.61;
  counter2.outPct() = 0.00;
  counter2.outKpps() = 3.00;
  counter2.portSpeed() = 400000;
  counter2.countersAvailable() = true;
  model.traffic_counters()->push_back(counter2);

  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowInterfaceTraffic, CmdShowInterfaceTrafficTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceTraffic,
    CmdShowInterfaceTrafficTraits>::getValidFilters();

} // namespace facebook::fboss
