/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/errors/CmdShowInterfaceErrors.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"

namespace facebook::fboss {

CmdShowInterfaceErrors::RetType CmdShowInterfaceErrors::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  RetType countersEntries;

  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::map<int32_t, facebook::fboss::PortInfoThrift> portCounters;
  client->sync_getAllPortInfo(portCounters);

  return createModel(portCounters, queriedIfs);
}

CmdShowInterfaceErrors::RetType CmdShowInterfaceErrors::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portCounters,
    const std::vector<std::string>& queriedIfs) {
  RetType ret;

  for (const auto* port : utils::getQueriedPorts(portCounters, queriedIfs)) {
    const auto& portInfo = *port;
    cli::ErrorCounters counter;

    counter.interfaceName() = portInfo.name().value();
    counter.inputErrors() =
        folly::copy(portInfo.input().value().errors().value().errors().value());
    counter.inputDiscards() = folly::copy(
        portInfo.input().value().errors().value().discards().value());
    counter.outputErrors() = folly::copy(
        portInfo.output().value().errors().value().errors().value());
    counter.outputDiscards() = folly::copy(
        portInfo.output().value().errors().value().discards().value());

    ret.error_counters()->push_back(counter);
  }

  return ret;
}

Table::StyledCell CmdShowInterfaceErrors::stylizeCounterValue(
    int64_t counterValue) {
  return counterValue == 0
      ? Table::StyledCell(std::to_string(counterValue), Table::Style::NONE)
      : Table::StyledCell(std::to_string(counterValue), Table::Style::ERROR);
}

void CmdShowInterfaceErrors::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table table;
  table.setHeader(
      {"Interface Name",
       "Input Errors",
       "Input Discards",
       "Output Errors",
       "Output Discards"});

  for (const auto& counter : model.error_counters().value()) {
    table.addRow({
        counter.interfaceName().value(),
        stylizeCounterValue(folly::copy(counter.inputErrors().value())),
        stylizeCounterValue(folly::copy(counter.inputDiscards().value())),
        stylizeCounterValue(folly::copy(counter.outputErrors().value())),
        stylizeCounterValue(folly::copy(counter.outputDiscards().value())),
    });
  }
  out << table << std::endl;
}

std::string_view CmdShowInterfaceErrorsTraits::description() {
  return "Displays per-interface error and discard counters: input errors, input discards, output errors, and output discards. Use it to spot ports dropping or corrupting traffic.";
}

CmdShowInterfaceErrors::RetType CmdShowInterfaceErrors::sampleModel() {
  RetType model;

  cli::ErrorCounters counter1;
  counter1.interfaceName() = "eth1/1/1";
  counter1.inputErrors() = 0;
  counter1.inputDiscards() = 7;
  counter1.outputErrors() = 0;
  counter1.outputDiscards() = 2836;
  model.error_counters()->push_back(counter1);

  cli::ErrorCounters counter2;
  counter2.interfaceName() = "eth1/2/1";
  counter2.inputErrors() = 0;
  counter2.inputDiscards() = 164;
  counter2.outputErrors() = 0;
  counter2.outputDiscards() = 84;
  model.error_counters()->push_back(counter2);

  cli::ErrorCounters counter3;
  counter3.interfaceName() = "eth1/11/1";
  counter3.inputErrors() = 0;
  counter3.inputDiscards() = 0;
  counter3.outputErrors() = 0;
  counter3.outputDiscards() = 5079;
  model.error_counters()->push_back(counter3);

  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowInterfaceErrors, CmdShowInterfaceErrorsTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceErrors,
    CmdShowInterfaceErrorsTraits>::getValidFilters();

} // namespace facebook::fboss
