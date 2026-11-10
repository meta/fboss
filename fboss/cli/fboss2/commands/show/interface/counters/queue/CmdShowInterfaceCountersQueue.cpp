// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/show/interface/counters/queue/CmdShowInterfaceCountersQueue.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <set>

#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"

namespace {

int64_t getQueueCounter(
    const std::map<int16_t, int64_t>& queueCounters,
    int16_t queueId) {
  const auto it = queueCounters.find(queueId);
  return it == queueCounters.end()
      ? facebook::fboss::hardware_stats_constants::STAT_UNINITIALIZED()
      : it->second;
}

facebook::fboss::cli::InterfaceQueueCounters makeQueueCounters(
    const std::string& interfaceName,
    int16_t queueId,
    const std::string& queueName,
    int64_t outPackets,
    int64_t outBytes,
    int64_t outDiscardPackets,
    int64_t outDiscardBytes) {
  facebook::fboss::cli::InterfaceQueueCounters counters;
  counters.interfaceName() = interfaceName;
  counters.queueId() = queueId;
  counters.queueName() = queueName;
  counters.outPackets() = outPackets;
  counters.outBytes() = outBytes;
  counters.outDiscardPackets() = outDiscardPackets;
  counters.outDiscardBytes() = outDiscardBytes;
  return counters;
}

} // namespace

namespace facebook::fboss {

CmdShowInterfaceCountersQueue::RetType
CmdShowInterfaceCountersQueue::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::map<std::string, facebook::fboss::HwPortStats> portStats;
  client->sync_getAllPortInfo(portEntries);
  client->sync_getHwPortStats(portStats);

  return createModel(portEntries, portStats, queriedIfs);
}

CmdShowInterfaceCountersQueue::RetType
CmdShowInterfaceCountersQueue::createModel(
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
    const std::map<std::string, facebook::fboss::HwPortStats>& portStats,
    const std::vector<std::string>& queriedIfs) {
  RetType ret;

  // A port has no stats entry until the agent first collects its counters,
  // e.g. right after it starts. Its configured queues are then reported with
  // every counter n/a. Ports with neither queues nor stats (e.g. fabric ports)
  // produce no rows.
  static const HwPortStats kNoStats;
  for (const auto* portInfo : utils::getQueriedPorts(portEntries, queriedIfs)) {
    const auto& portName = portInfo->name().value();
    const auto statsIt = portStats.find(portName);
    const auto& hwStats =
        (statsIt == portStats.end()) ? kNoStats : statsIt->second;
    const auto& outPackets = hwStats.queueOutPackets_().value();
    const auto& outBytes = hwStats.queueOutBytes_().value();
    const auto& outDiscardPackets = hwStats.queueOutDiscardPackets_().value();
    const auto& outDiscardBytes = hwStats.queueOutDiscardBytes_().value();

    // Report every queue that is either configured or counted by the
    // hardware, so idle queues show up as zero rather than being hidden.
    // PortQueueThrift ids are i32 but HwPortStats keys queues by i16.
    std::map<int16_t, std::string> queueIdToName;
    std::set<int16_t> queueIds;
    for (const auto& queue : portInfo->portQueues().value()) {
      const auto queueId =
          static_cast<int16_t>(folly::copy(queue.id().value()));
      queueIdToName.emplace(queueId, queue.name().value());
      queueIds.insert(queueId);
    }
    for (const auto* queueCounters :
         {&outPackets, &outBytes, &outDiscardPackets, &outDiscardBytes}) {
      for (const auto& [queueId, value] : *queueCounters) {
        queueIds.insert(queueId);
      }
    }

    for (const auto queueId : queueIds) {
      const auto nameIt = queueIdToName.find(queueId);
      ret.queueCounters()->push_back(makeQueueCounters(
          portName,
          queueId,
          nameIt == queueIdToName.end() ? "" : nameIt->second,
          getQueueCounter(outPackets, queueId),
          getQueueCounter(outBytes, queueId),
          getQueueCounter(outDiscardPackets, queueId),
          getQueueCounter(outDiscardBytes, queueId)));
    }
  }

  return ret;
}

void CmdShowInterfaceCountersQueue::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;
  table.setHeader(
      {"Interface",
       "Queue",
       "Name",
       "Out Pkts",
       "Out Bytes",
       "Discard Pkts",
       "Discard Bytes"});

  auto makeStr = [](int64_t counterVal) -> std::string {
    return counterVal == hardware_stats_constants::STAT_UNINITIALIZED()
        ? "n/a"
        : std::to_string(counterVal);
  };
  for (const auto& counters : model.queueCounters().value()) {
    table.addRow({
        counters.interfaceName().value(),
        std::to_string(folly::copy(counters.queueId().value())),
        counters.queueName().value(),
        makeStr(folly::copy(counters.outPackets().value())),
        makeStr(folly::copy(counters.outBytes().value())),
        makeStr(folly::copy(counters.outDiscardPackets().value())),
        makeStr(folly::copy(counters.outDiscardBytes().value())),
    });
  }

  out << table << std::endl;
}

std::string_view CmdShowInterfaceCountersQueueTraits::description() {
  return "Displays per-queue egress counters of each interface: for every queue, its ID, configured name, transmitted packets and bytes, and discarded packets and bytes. Every configured queue is listed, including idle ones, and counters the hardware does not report show as n/a (-1 with --fmt json). Use it to see how egress traffic is distributed across a port's queues, for example to confirm that QoS classification landed traffic on the expected queue, or to find which queue is dropping. Narrow the output with --filter on interfaceName or queueId.";
}

CmdShowInterfaceCountersQueue::RetType
CmdShowInterfaceCountersQueue::sampleModel() {
  RetType model;
  model.queueCounters() = {
      makeQueueCounters("eth1/1/1", 0, "ncnf", 1203, 154000, 0, 0),
      makeQueueCounters("eth1/1/1", 1, "bronze", 0, 0, 0, 0),
      makeQueueCounters(
          "eth1/1/1", 2, "silver", 241044633532, 751980313621180, 12, 15360),
      makeQueueCounters("eth1/1/1", 7, "nc", 5079, 457110, 0, 0),
      makeQueueCounters("eth1/2/1", 0, "ncnf", 980, 125440, 0, 0),
      makeQueueCounters("eth1/2/1", 1, "bronze", 0, 0, 0, 0),
      makeQueueCounters(
          "eth1/2/1", 2, "silver", 142109732536, 538242637301265, 0, 0),
      makeQueueCounters("eth1/2/1", 7, "nc", 4710, 423900, 0, 0),
  };
  return model;
}

// Explicit template instantiation
template void CmdHandler<
    CmdShowInterfaceCountersQueue,
    CmdShowInterfaceCountersQueueTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCountersQueue,
    CmdShowInterfaceCountersQueueTraits>::getValidFilters();

} // namespace facebook::fboss
