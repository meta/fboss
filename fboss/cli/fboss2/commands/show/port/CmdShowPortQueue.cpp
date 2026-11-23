/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowPortQueue.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/Table.h"

#include <fmt/core.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <array>
#include <vector>

namespace facebook::fboss {

namespace {

// Behavior, min length, max length, probability.
using AqmRow = std::array<std::string, 4>;

AqmRow formatAqm(const ActiveQueueManagement& aqm) {
  auto behavior = apache::thrift::util::enumNameSafe(aqm.behavior().value());
  if (const auto& linear = aqm.detection()->linear()) {
    return {
        behavior,
        std::to_string(linear->minimumLength().value()),
        std::to_string(linear->maximumLength().value()),
        fmt::format("{}%", linear->probability().value())};
  }
  return {behavior, "unknown", "unknown", "unknown"};
}

// One row per AQM entry; a queue with ECN and EARLY_DROP yields two rows.
std::vector<AqmRow> aqmRows(const PortQueueThrift& queue) {
  if (!queue.aqms() || queue.aqms()->empty()) {
    return {{"none", "-", "-", "-"}};
  }
  std::vector<AqmRow> rows;
  for (const auto& aqm : *queue.aqms()) {
    rows.push_back(formatAqm(aqm));
  }
  return rows;
}

} // namespace

CmdShowPortQueue::RetType CmdShowPortQueue::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedPorts) {
  RetType portEntries;

  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  client->sync_getAllPortInfo(portEntries);

  if (queriedPorts.size() == 0) {
    return portEntries;
  }

  RetType retVal;
  for (auto const& [portId, portInfo] : portEntries) {
    for (auto const& queriedPort : queriedPorts.data()) {
      if (portInfo.name().value() == queriedPort) {
        retVal.insert(std::make_pair(portId, portInfo));
      }
    }
  }

  return retVal;
}

void CmdShowPortQueue::printOutput(
    const RetType& portId2PortInfoThrift,
    std::ostream& out) {
  const bool detailed = CmdGlobalOptions::getInstance()->isDetailed();

  for (auto const& [portId, portInfo] : portId2PortInfoThrift) {
    std::ignore = portId;

    out << portInfo.name().value() << "\n";
    out << std::string(10, '=') << std::endl;

    // RowData is not assignable, so rows are built as strings first.
    auto toRow = [](const std::vector<std::string>& cells) {
      return std::vector<utils::Table::RowData>(cells.begin(), cells.end());
    };

    utils::Table table;
    std::vector<std::string> header = {
        "ID", "Name", "Mode", "Weight", "ReservedBytes", "ScalingFactor"};
    if (detailed) {
      header.insert(
          header.end(), {"AQM", "MinLength", "MaxLength", "Probability"});
    }
    table.setHeader(toRow(header));

    for (auto const& queue : portInfo.portQueues().value()) {
      std::vector<std::string> row = {
          std::to_string(folly::copy(queue.id().value())),
          queue.name().value(),
          queue.mode().value(),
          queue.weight() ? std::to_string(*queue.weight()) : "",
          queue.reservedBytes() ? std::to_string(*queue.reservedBytes()) : "",
          queue.scalingFactor() ? *queue.scalingFactor() : ""};
      if (!detailed) {
        table.addRow(toRow(row));
        continue;
      }
      const auto queueColumns = row.size();
      for (const auto& aqm : aqmRows(queue)) {
        row.insert(row.end(), aqm.begin(), aqm.end());
        table.addRow(toRow(row));
        // Further AQM entries go on rows with the queue columns blank.
        row.assign(queueColumns, "");
      }
    }

    out << table << "\n";
  }
}

std::string_view CmdShowPortQueueTraits::description() {
  return "Displays the egress queue configuration of each port: per queue, its ID, traffic class name, scheduling mode, weight, reserved bytes and dynamic threshold scaling factor. Weighted round robin queues report a weight while strict priority queues leave it blank. With --detail, four more columns show each queue's active queue management (AQM): the congestion behavior (ECN or EARLY_DROP) and its linear detection thresholds (minimum length, maximum length, probability), one row per AQM entry, or 'none' when no AQM is configured. Use it to verify QoS queue, scheduler and ECN/WRED configuration on the ports.";
}

CmdShowPortQueue::RetType CmdShowPortQueue::sampleModel() {
  auto makeQueue = [](int32_t id,
                      const std::string& name,
                      const std::string& mode,
                      std::optional<int32_t> weight) {
    PortQueueThrift queue;
    queue.id() = id;
    queue.name() = name;
    queue.mode() = mode;
    if (weight.has_value()) {
      queue.weight() = *weight;
    }
    queue.reservedBytes() = 0;
    queue.scalingFactor() = "TWO";
    return queue;
  };
  auto makeAqm = [](QueueCongestionBehavior behavior,
                    int32_t minLength,
                    int32_t maxLength,
                    int32_t probability) {
    ActiveQueueManagement aqm;
    aqm.behavior() = behavior;
    LinearQueueCongestionDetection linear;
    linear.minimumLength() = minLength;
    linear.maximumLength() = maxLength;
    linear.probability() = probability;
    aqm.detection()->linear() = linear;
    return aqm;
  };

  std::vector<PortQueueThrift> queues = {
      makeQueue(0, "ncnf", "STRICT_PRIORITY", std::nullopt),
      makeQueue(1, "bronze", "STRICT_PRIORITY", std::nullopt),
      makeQueue(2, "silver", "STRICT_PRIORITY", std::nullopt),
      makeQueue(3, "gold", "STRICT_PRIORITY", std::nullopt),
      makeQueue(4, "", "WEIGHTED_ROUND_ROBIN", 1),
      makeQueue(5, "", "WEIGHTED_ROUND_ROBIN", 1),
      makeQueue(6, "icp", "STRICT_PRIORITY", std::nullopt),
      makeQueue(7, "nc", "STRICT_PRIORITY", std::nullopt),
  };
  queues[2].aqms() = {
      makeAqm(QueueCongestionBehavior::ECN, 1000000, 5000000, 100),
      makeAqm(QueueCongestionBehavior::EARLY_DROP, 2000000, 6000000, 50),
  };

  RetType model;
  for (const auto& [portId, portName] :
       std::vector<std::pair<int32_t, std::string>>{
           {1, "eth1/2/1"}, {9, "eth1/1/1"}}) {
    PortInfoThrift portInfo;
    portInfo.portId() = portId;
    portInfo.name() = portName;
    portInfo.portQueues() = queues;
    model[portId] = portInfo;
  }
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowPortQueue, CmdShowPortQueueTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowPortQueue, CmdShowPortQueueTraits>::getValidFilters();

} // namespace facebook::fboss
