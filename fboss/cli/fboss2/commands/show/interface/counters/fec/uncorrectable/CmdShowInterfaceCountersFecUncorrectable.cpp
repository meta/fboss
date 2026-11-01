// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/counters/fec/uncorrectable/CmdShowInterfaceCountersFecUncorrectable.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <sstream>
#include <unordered_set>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

namespace facebook::fboss {

namespace {

utils::Table::StyledCell styledUc(int64_t uc) {
  std::ostringstream outStringStream;
  outStringStream << std::to_string(uc);
  if (uc >= 100) {
    return utils::Table::StyledCell(
        outStringStream.str(), utils::Table::Style::ERROR);
  }
  if (uc >= 1) {
    return utils::Table::StyledCell(
        outStringStream.str(), utils::Table::Style::WARN);
  }
  return utils::Table::StyledCell(
      outStringStream.str(), utils::Table::Style::GOOD);
}

cli::ShowInterfaceCountersFecUncorrectableModel createModel(
    std::vector<std::string>& ifNames,
    const std::map<std::string, int64_t>& iPhyIngressUcSum,
    const std::map<std::string, int64_t>& xPhyIngressUcSum,
    const std::map<std::string, int64_t>& tcvrIngressUcSum,
    const std::map<std::string, int64_t>& xPhyEgressUcSum,
    const std::map<std::string, int64_t>& tcvrEgressUcSum,
    const std::vector<std::string>& queriedIfs,
    const phy::Direction direction) {
  cli::ShowInterfaceCountersFecUncorrectableModel model;
  model.direction() = direction;

  for (const auto& interface :
       utils::getQueriedInterfaceNames(ifNames, queriedIfs)) {
    std::string counterName;

    counterName = interface + ".fec_uncorrectable_errors.sum";
    if (iPhyIngressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::ASIC]
          .totalCount() = iPhyIngressUcSum.at(counterName);
    }

    counterName = interface + ".fec_uncorrectable_errors.sum.600";
    if (iPhyIngressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::ASIC]
          .tenMinuteCount() = iPhyIngressUcSum.at(counterName);
    }

    counterName = interface + ".xphy.line.fec_uncorrectable.sum";
    if (xPhyIngressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::GB_LINE]
          .totalCount() = xPhyIngressUcSum.at(counterName);
    }

    counterName = interface + ".xphy.line.fec_uncorrectable.sum.600";
    if (xPhyIngressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::GB_LINE]
          .tenMinuteCount() = xPhyIngressUcSum.at(counterName);
    }

    counterName = "qsfp.interface." + interface + ".media.uncorrectable.sum";
    if (tcvrIngressUcSum.contains(counterName)) {
      model
          .uncorrectableFrames()[interface]
                                [phy::PortComponent::TRANSCEIVER_LINE]
          .totalCount() = tcvrIngressUcSum.at(counterName);
    }

    counterName =
        "qsfp.interface." + interface + ".media.uncorrectable.sum.600";
    if (tcvrIngressUcSum.contains(counterName)) {
      model
          .uncorrectableFrames()[interface]
                                [phy::PortComponent::TRANSCEIVER_LINE]
          .tenMinuteCount() = tcvrIngressUcSum.at(counterName);
    }

    counterName = interface + ".xphy.system.fec_uncorrectable.sum";
    if (xPhyEgressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::GB_SYSTEM]
          .totalCount() = xPhyEgressUcSum.at(counterName);
    }

    counterName = interface + ".xphy.system.fec_uncorrectable.sum.600";
    if (xPhyEgressUcSum.contains(counterName)) {
      model.uncorrectableFrames()[interface][phy::PortComponent::GB_SYSTEM]
          .tenMinuteCount() = xPhyEgressUcSum.at(counterName);
    }

    counterName = "qsfp.interface." + interface + ".host.uncorrectable.sum";
    if (tcvrEgressUcSum.contains(counterName)) {
      model
          .uncorrectableFrames()[interface]
                                [phy::PortComponent::TRANSCEIVER_SYSTEM]
          .totalCount() = tcvrEgressUcSum.at(counterName);
    }

    counterName = "qsfp.interface." + interface + ".host.uncorrectable.sum.600";
    if (tcvrEgressUcSum.contains(counterName)) {
      model
          .uncorrectableFrames()[interface]
                                [phy::PortComponent::TRANSCEIVER_SYSTEM]
          .tenMinuteCount() = tcvrEgressUcSum.at(counterName);
    }
  }

  return model;
}

} // namespace

CmdShowInterfaceCountersFecUncorrectable::RetType
CmdShowInterfaceCountersFecUncorrectable::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs,
    const utils::LinkDirection& direction) {
  // Get all the counters from Fbagent/Fb303. We will use regex to filter out
  // our desired counters
  auto qsfpClient =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::map<std::string, int64_t> fb303CountersIPhyIngressUcSum;
  std::map<std::string, int64_t> fb303CountersXPhyIngressUcSum;
  std::map<std::string, int64_t> fb303CountersTcvrIngressUcSum;
  std::map<std::string, int64_t> fb303CountersXPhyEgressUcSum;
  std::map<std::string, int64_t> fb303CountersTcvrEgressUcSum;

  fb303CountersIPhyIngressUcSum = utils::getAgentFb303RegexCounters(
      hostInfo, "^(eth|fab).*fec_uncorrectable_errors.sum.*");
  qsfpClient->sync_getRegexCounters(
      fb303CountersXPhyIngressUcSum, ".*xphy.line.fec_uncorrectable.sum.*");
  qsfpClient->sync_getRegexCounters(
      fb303CountersTcvrIngressUcSum, "^qsfp.*media.uncorrectable.sum.*");
  qsfpClient->sync_getRegexCounters(
      fb303CountersXPhyEgressUcSum, ".*xphy.system.fec_uncorrectable.sum.*");
  qsfpClient->sync_getRegexCounters(
      fb303CountersTcvrEgressUcSum, "^qsfp.*host.uncorrectable.sum.*");

  std::unordered_set<std::string> distinctInterfaceNames;
  for (const auto& counter : fb303CountersIPhyIngressUcSum) {
    std::vector<std::string> result;
    boost::split(result, counter.first, boost::is_any_of("."));
    if (!result.empty()) {
      distinctInterfaceNames.insert(result[0]);
    }
  }
  std::vector<std::string> ifNames(
      distinctInterfaceNames.begin(), distinctInterfaceNames.end());

  return createModel(
      ifNames,
      fb303CountersIPhyIngressUcSum,
      fb303CountersXPhyIngressUcSum,
      fb303CountersTcvrIngressUcSum,
      fb303CountersXPhyEgressUcSum,
      fb303CountersTcvrEgressUcSum,
      queriedIfs,
      direction.direction);
}

void CmdShowInterfaceCountersFecUncorrectable::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;

  if (model.direction() == phy::Direction::RECEIVE) {
    table.setHeader(
        {"Interface Name",
         "ASIC",
         "ASIC_10min",
         "XPHY_LINE",
         "XPHY_LINE_10min",
         "TRANSCEIVER_LINE",
         "TRANSCEIVER_LINE_10min"});
  } else {
    table.setHeader(
        {"Interface Name",
         "XPHY_SYSTEM",
         "XPHY_SYSTEM_10min",
         "TRANSCEIVER_SYSTEM",
         "TRANSCEIVER_SYSTEM_10min"});
  }

  for (const auto& [interfaceName, ucCounters] : *model.uncorrectableFrames()) {
    if (model.direction() == phy::Direction::RECEIVE) {
      std::optional<int64_t> iphyUc, iphyUc10m, xphyUc, xphyUc10m, tcvrUc,
          tcvrUc10m;

      if (ucCounters.contains(phy::PortComponent::ASIC)) {
        iphyUc = ucCounters.at(phy::PortComponent::ASIC).totalCount().value();
        iphyUc10m =
            ucCounters.at(phy::PortComponent::ASIC).tenMinuteCount().value();
      }

      if (ucCounters.contains(phy::PortComponent::GB_LINE)) {
        xphyUc =
            ucCounters.at(phy::PortComponent::GB_LINE).totalCount().value();
        xphyUc10m =
            ucCounters.at(phy::PortComponent::GB_LINE).tenMinuteCount().value();
      }

      if (ucCounters.contains(phy::PortComponent::TRANSCEIVER_LINE)) {
        tcvrUc = ucCounters.at(phy::PortComponent::TRANSCEIVER_LINE)
                     .totalCount()
                     .value();
        tcvrUc10m = ucCounters.at(phy::PortComponent::TRANSCEIVER_LINE)
                        .tenMinuteCount()
                        .value();
      }

      table.addRow({
          interfaceName,
          iphyUc.has_value()
              ? styledUc(*iphyUc)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          iphyUc10m.has_value()
              ? styledUc(*iphyUc10m)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          xphyUc.has_value()
              ? styledUc(*xphyUc)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          xphyUc10m.has_value()
              ? styledUc(*xphyUc10m)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          tcvrUc.has_value()
              ? styledUc(*tcvrUc)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          tcvrUc10m.has_value()
              ? styledUc(*tcvrUc10m)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
      });
    } else {
      std::optional<int64_t> xphyUc, xphyUc10m, tcvrUc, tcvrUc10m;

      if (ucCounters.contains(phy::PortComponent::GB_SYSTEM)) {
        xphyUc =
            ucCounters.at(phy::PortComponent::GB_SYSTEM).totalCount().value();
        xphyUc10m = ucCounters.at(phy::PortComponent::GB_SYSTEM)
                        .tenMinuteCount()
                        .value();
      }

      if (ucCounters.contains(phy::PortComponent::TRANSCEIVER_SYSTEM)) {
        tcvrUc = ucCounters.at(phy::PortComponent::TRANSCEIVER_SYSTEM)
                     .totalCount()
                     .value();
        tcvrUc10m = ucCounters.at(phy::PortComponent::TRANSCEIVER_SYSTEM)
                        .tenMinuteCount()
                        .value();
      }

      table.addRow({
          interfaceName,
          xphyUc.has_value()
              ? styledUc(*xphyUc)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          xphyUc10m.has_value()
              ? styledUc(*xphyUc10m)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          tcvrUc.has_value()
              ? styledUc(*tcvrUc)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
          tcvrUc10m.has_value()
              ? styledUc(*tcvrUc10m)
              : utils::Table::StyledCell("-", utils::Table::Style::NONE),
      });
    }
  }
  out << table << std::endl;
}

// CLI reference wiki hooks
std::string_view CmdShowInterfaceCountersFecUncorrectableTraits::description() {
  return "Displays per-interface uncorrectable FEC codeword counts (total and over the last 10 minutes), by side. system shows XPHY-system and transceiver-system counts; line shows ASIC, XPHY-line, and transceiver-line counts. Use it to spot links taking uncorrectable FEC errors.";
}

CmdShowInterfaceCountersFecUncorrectable::RetType
CmdShowInterfaceCountersFecUncorrectable::sampleModel() {
  cli::ShowInterfaceCountersFecUncorrectableModel model;
  model.direction() = phy::Direction::RECEIVE;

  // eth2/1/1: ASIC and GB_LINE present, TRANSCEIVER_LINE absent
  cli::uncorrectableCount asic1;
  asic1.totalCount() = 0;
  asic1.tenMinuteCount() = 0;
  model.uncorrectableFrames()["eth2/1/1"][phy::PortComponent::ASIC] = asic1;

  cli::uncorrectableCount gbLine1;
  gbLine1.totalCount() = 0;
  gbLine1.tenMinuteCount() = 0;
  model.uncorrectableFrames()["eth2/1/1"][phy::PortComponent::GB_LINE] =
      gbLine1;

  // eth2/14/1: ASIC has 3 total, 0 in 10min; GB_LINE is 0/0
  cli::uncorrectableCount asic14;
  asic14.totalCount() = 3;
  asic14.tenMinuteCount() = 0;
  model.uncorrectableFrames()["eth2/14/1"][phy::PortComponent::ASIC] = asic14;

  cli::uncorrectableCount gbLine14;
  gbLine14.totalCount() = 0;
  gbLine14.tenMinuteCount() = 0;
  model.uncorrectableFrames()["eth2/14/1"][phy::PortComponent::GB_LINE] =
      gbLine14;

  return model;
}

// Template instantiations
template void CmdHandler<
    CmdShowInterfaceCountersFecUncorrectable,
    CmdShowInterfaceCountersFecUncorrectableTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCountersFecUncorrectable,
    CmdShowInterfaceCountersFecUncorrectableTraits>::getValidFilters();

} // namespace facebook::fboss
