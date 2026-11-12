/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/counters/CmdShowInterfaceCounters.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <limits>

#include "fboss/cli/fboss2/utils/Table.h"

namespace {

int64_t accumulateCounter(int64_t total, int64_t value) {
  const auto uninitialized =
      facebook::fboss::hardware_stats_constants::STAT_UNINITIALIZED();
  if (total == uninitialized || value == uninitialized) {
    return uninitialized;
  }
  if (value > 0 && total > std::numeric_limits<int64_t>::max() - value) {
    return std::numeric_limits<int64_t>::max();
  }
  return total + value;
}

void addPortCounters(
    facebook::fboss::cli::InterfaceCounters& aggregateCounters,
    const facebook::fboss::PortInfoThrift& portInfo) {
  aggregateCounters.inputBytes() = accumulateCounter(
      *aggregateCounters.inputBytes(), *portInfo.input()->bytes());
  aggregateCounters.inputUcastPkts() = accumulateCounter(
      *aggregateCounters.inputUcastPkts(), *portInfo.input()->ucastPkts());
  aggregateCounters.inputMulticastPkts() = accumulateCounter(
      *aggregateCounters.inputMulticastPkts(),
      *portInfo.input()->multicastPkts());
  aggregateCounters.inputBroadcastPkts() = accumulateCounter(
      *aggregateCounters.inputBroadcastPkts(),
      *portInfo.input()->broadcastPkts());
  aggregateCounters.outputBytes() = accumulateCounter(
      *aggregateCounters.outputBytes(), *portInfo.output()->bytes());
  aggregateCounters.outputUcastPkts() = accumulateCounter(
      *aggregateCounters.outputUcastPkts(), *portInfo.output()->ucastPkts());
  aggregateCounters.outputMulticastPkts() = accumulateCounter(
      *aggregateCounters.outputMulticastPkts(),
      *portInfo.output()->multicastPkts());
  aggregateCounters.outputBroadcastPkts() = accumulateCounter(
      *aggregateCounters.outputBroadcastPkts(),
      *portInfo.output()->broadcastPkts());
}

facebook::fboss::cli::InterfaceCounters makeCounters(
    const std::string& interfaceName,
    int64_t value) {
  facebook::fboss::cli::InterfaceCounters counter;
  counter.interfaceName() = interfaceName;
  counter.inputBytes() = value;
  counter.inputUcastPkts() = value;
  counter.inputMulticastPkts() = value;
  counter.inputBroadcastPkts() = value;
  counter.outputBytes() = value;
  counter.outputUcastPkts() = value;
  counter.outputMulticastPkts() = value;
  counter.outputBroadcastPkts() = value;
  return counter;
}

} // namespace

namespace facebook::fboss {

CmdShowInterfaceCounters::RetType CmdShowInterfaceCounters::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  RetType countersEntries;

  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::map<int32_t, facebook::fboss::PortInfoThrift> portCounters;
  std::vector<facebook::fboss::AggregatePortThrift> aggregatePorts;
  client->sync_getAllPortInfo(portCounters);
  client->sync_getAggregatePortTable(aggregatePorts);

  return createModel(portCounters, aggregatePorts, queriedIfs);
}

CmdShowInterfaceCounters::RetType CmdShowInterfaceCounters::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portCounters,
    const std::vector<facebook::fboss::AggregatePortThrift>& aggregatePorts,
    const std::vector<std::string>& queriedIfs) {
  RetType ret;

  std::unordered_set<std::string> queriedSet(
      queriedIfs.begin(), queriedIfs.end());

  for (const auto& port : portCounters) {
    auto portInfo = port.second;
    if (queriedIfs.size() == 0 || queriedSet.count(portInfo.name().value())) {
      cli::InterfaceCounters counter;

      counter.interfaceName() = portInfo.name().value();
      counter.inputBytes() =
          folly::copy(portInfo.input().value().bytes().value());
      counter.inputUcastPkts() =
          folly::copy(portInfo.input().value().ucastPkts().value());
      counter.inputMulticastPkts() =
          folly::copy(portInfo.input().value().multicastPkts().value());
      counter.inputBroadcastPkts() =
          folly::copy(portInfo.input().value().broadcastPkts().value());
      counter.outputBytes() =
          folly::copy(portInfo.output().value().bytes().value());
      counter.outputUcastPkts() =
          folly::copy(portInfo.output().value().ucastPkts().value());
      counter.outputMulticastPkts() =
          folly::copy(portInfo.output().value().multicastPkts().value());
      counter.outputBroadcastPkts() =
          folly::copy(portInfo.output().value().broadcastPkts().value());

      ret.int_counters()->push_back(counter);
    }
  }

  for (const auto& aggregatePort : aggregatePorts) {
    const auto& aggregatePortName = *aggregatePort.name();
    if (!queriedIfs.empty() && !queriedSet.count(aggregatePortName)) {
      continue;
    }

    auto counter = makeCounters(aggregatePortName, 0);
    for (const auto& memberPort : *aggregatePort.memberPorts()) {
      const auto portIt = portCounters.find(*memberPort.memberPortID());
      if (portIt == portCounters.end()) {
        counter = makeCounters(
            aggregatePortName, hardware_stats_constants::STAT_UNINITIALIZED());
        break;
      }
      addPortCounters(counter, portIt->second);
    }
    ret.int_counters()->push_back(std::move(counter));
  }

  std::sort(
      ret.int_counters()->begin(),
      ret.int_counters()->end(),
      [](cli::InterfaceCounters& a, cli::InterfaceCounters b) {
        return a.interfaceName().value() < b.interfaceName().value();
      });

  return ret;
}

void CmdShowInterfaceCounters::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table table;
  table.setHeader(
      {"Interface Name",
       "Bytes(in)",
       "Unicast Pkts(in)",
       "Multicast Pkts(in)",
       "Broadcast Pkts(in)",
       "Bytes(out)",
       "Unicast Pkts(out)",
       "Multicast Pkts(out)",
       "Broadcast Pkts(out)"});

  auto makeStr = [](auto counterVal) -> std::string {
    const std::string kNA = "n/a";
    return counterVal == hardware_stats_constants::STAT_UNINITIALIZED()
        ? kNA
        : std::to_string(counterVal);
  };
  for (const auto& counter : model.int_counters().value()) {
    table.addRow({
        counter.interfaceName().value(),
        makeStr(folly::copy(counter.inputBytes().value())),
        makeStr(folly::copy(counter.inputUcastPkts().value())),
        makeStr(folly::copy(counter.inputMulticastPkts().value())),
        makeStr(folly::copy(counter.inputBroadcastPkts().value())),
        makeStr(folly::copy(counter.outputBytes().value())),
        makeStr(folly::copy(counter.outputUcastPkts().value())),
        makeStr(folly::copy(counter.outputMulticastPkts().value())),
        makeStr(folly::copy(counter.outputBroadcastPkts().value())),

    });
  }

  out << table << std::endl;
}

std::string_view CmdShowInterfaceCountersTraits::description() {
  return "Displays per-interface traffic counters: inbound/outbound byte counts and unicast/multicast/broadcast packet counts. Use it to check traffic volumes and spot imbalances.";
}

CmdShowInterfaceCounters::RetType CmdShowInterfaceCounters::sampleModel() {
  RetType model;

  cli::InterfaceCounters counter1;
  counter1.interfaceName() = "eth1/1/1";
  counter1.inputBytes() = 673784998014233;
  counter1.inputUcastPkts() = 203100360208;
  counter1.inputMulticastPkts() = 0;
  counter1.inputBroadcastPkts() = 0;
  counter1.outputBytes() = 751980313621180;
  counter1.outputUcastPkts() = 241044633532;
  counter1.outputMulticastPkts() = 0;
  counter1.outputBroadcastPkts() = 0;

  cli::InterfaceCounters counter2;
  counter2.interfaceName() = "eth1/2/1";
  counter2.inputBytes() = 460058508909866;
  counter2.inputUcastPkts() = 130586642176;
  counter2.inputMulticastPkts() = 0;
  counter2.inputBroadcastPkts() = 0;
  counter2.outputBytes() = 538242637301265;
  counter2.outputUcastPkts() = 142109732536;
  counter2.outputMulticastPkts() = 0;
  counter2.outputBroadcastPkts() = 0;

  cli::InterfaceCounters counter3;
  counter3.interfaceName() = "eth1/11/1";
  counter3.inputBytes() = 0;
  counter3.inputUcastPkts() = 0;
  counter3.inputMulticastPkts() = 0;
  counter3.inputBroadcastPkts() = 0;
  counter3.outputBytes() = 457110;
  counter3.outputUcastPkts() = 5079;
  counter3.outputMulticastPkts() = 0;
  counter3.outputBroadcastPkts() = 0;

  model.int_counters() = {counter1, counter2, counter3};
  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowInterfaceCounters, CmdShowInterfaceCountersTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCounters,
    CmdShowInterfaceCountersTraits>::getValidFilters();

} // namespace facebook::fboss
