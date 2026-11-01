// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/flaps/CmdShowInterfaceFlaps.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <unordered_set>

namespace facebook::fboss {

using utils::Table;

CmdShowInterfaceFlapsTraits::RetType createModel(
    std::vector<std::string>& ifNames,
    std::map<std::string, std::int64_t> wedgeCounters,
    const std::vector<std::string>& queriedIfs) {
  CmdShowInterfaceFlapsTraits::RetType ret;

  for (const auto& interface :
       utils::getQueriedInterfaceNames(ifNames, queriedIfs)) {
    cli::FlapCounters counter;

    std::string oneMinute = interface + ".link_state.flap.sum.60";
    std::string tenMinutes = interface + ".link_state.flap.sum.600";
    std::string oneHour = interface + ".link_state.flap.sum.3600";
    std::string totalFlaps = interface + ".link_state.flap.sum";
    std::string totalLinkFaults = interface + ".link_fault.sum";

    counter.interfaceName() = interface;
    counter.oneMinute() = wedgeCounters[oneMinute];
    counter.tenMinute() = wedgeCounters[tenMinutes];
    counter.oneHour() = wedgeCounters[oneHour];
    counter.totalFlaps() = wedgeCounters[totalFlaps];
    counter.totalLinkFaults() = wedgeCounters[totalLinkFaults];

    ret.flap_counters()->push_back(counter);
  }

  return ret;
}

namespace {

Table::Style get_FlapStyle(int64_t counter) {
  if (counter == 0) {
    return Table::Style::NONE;
  } else if (counter > 0 && counter < 100) {
    return Table::Style::WARN;
  } else {
    return Table::Style::ERROR;
  }
}

} // namespace

CmdShowInterfaceFlapsTraits::RetType CmdShowInterfaceFlaps::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  /* Interface flap stats are stored as a FB303 counter.  Will call
     getRegexCounters so we can filter out just the interface counters and
     ignore the multitude of other counters we don't need.
  */
  RetType flapsEntries;

  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::map<std::string, int64_t> wedgeCounters;
  client->sync_getRegexCounters(
      wedgeCounters, "^(eth|fab).*(flap|link_fault).sum.*");
  std::unordered_set<std::string> distinctInterfaceNames;
  for (const auto& counter : wedgeCounters) {
    std::vector<std::string> result;
    boost::split(result, counter.first, boost::is_any_of("."));
    if (!result.empty()) {
      distinctInterfaceNames.insert(result[0]);
    }
  }
  std::vector<std::string> ifNames(
      distinctInterfaceNames.begin(), distinctInterfaceNames.end());
  return createModel(ifNames, wedgeCounters, queriedIfs);
}

void CmdShowInterfaceFlaps::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table table;
  table.setHeader(
      {"Interface Name",
       "1 Min",
       "10 Min",
       "60 Min",
       "Total (since last reboot)",
       "Total Link Faults"});

  for (const auto& counter : model.flap_counters().value()) {
    table.addRow({
        counter.interfaceName().value(),
        Table::StyledCell(
            std::to_string(folly::copy(counter.oneMinute().value())),
            get_FlapStyle(folly::copy(counter.oneMinute().value()))),
        Table::StyledCell(
            std::to_string(folly::copy(counter.tenMinute().value())),
            get_FlapStyle(folly::copy(counter.tenMinute().value()))),
        Table::StyledCell(
            std::to_string(folly::copy(counter.oneHour().value())),
            get_FlapStyle(folly::copy(counter.oneHour().value()))),
        Table::StyledCell(
            std::to_string(folly::copy(counter.totalFlaps().value())),
            get_FlapStyle(folly::copy(counter.totalFlaps().value()))),
        Table::StyledCell(
            std::to_string(folly::copy(counter.totalLinkFaults().value())),
            get_FlapStyle(folly::copy(counter.totalLinkFaults().value()))),
    });
  }

  out << table << std::endl;
}

std::string_view CmdShowInterfaceFlapsTraits::description() {
  return "Displays per-interface link-flap counts over the last 1, 10, and 60 minutes, plus the total since the last reboot. Use it to identify unstable links.";
}

CmdShowInterfaceFlaps::RetType CmdShowInterfaceFlaps::sampleModel() {
  RetType model;

  cli::FlapCounters counter1;
  counter1.interfaceName() = "eth1/1/1";
  counter1.oneMinute() = 0;
  counter1.tenMinute() = 0;
  counter1.oneHour() = 0;
  counter1.totalFlaps() = 13;
  model.flap_counters()->push_back(counter1);

  cli::FlapCounters counter2;
  counter2.interfaceName() = "eth1/2/1";
  counter2.oneMinute() = 0;
  counter2.tenMinute() = 0;
  counter2.oneHour() = 0;
  counter2.totalFlaps() = 1;
  model.flap_counters()->push_back(counter2);

  cli::FlapCounters counter3;
  counter3.interfaceName() = "eth1/11/1";
  counter3.oneMinute() = 0;
  counter3.tenMinute() = 0;
  counter3.oneHour() = 0;
  counter3.totalFlaps() = 0;
  model.flap_counters()->push_back(counter3);

  return model;
}

// Template instantiations
template void
CmdHandler<CmdShowInterfaceFlaps, CmdShowInterfaceFlapsTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceFlaps,
    CmdShowInterfaceFlapsTraits>::getValidFilters();

} // namespace facebook::fboss
