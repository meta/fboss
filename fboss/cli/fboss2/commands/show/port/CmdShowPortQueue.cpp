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

#include <fmt/core.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::fboss {

namespace {

std::string formatAqm(const ActiveQueueManagement& aqm) {
  auto behavior = apache::thrift::util::enumNameSafe(aqm.behavior().value());
  if (const auto& linear = aqm.detection()->linear()) {
    return fmt::format(
        "{}  minLength={}  maxLength={}  probability={}%",
        behavior,
        linear->minimumLength().value(),
        linear->maximumLength().value(),
        linear->probability().value());
  }
  return fmt::format("{}  detection=unknown", behavior);
}

void printQueueDetails(const PortQueueThrift& queue, std::ostream& out) {
  constexpr auto kIndent = "       ";
  if (!queue.aqms() || queue.aqms()->empty()) {
    out << kIndent << "AQM: none\n";
    return;
  }
  for (const auto& aqm : *queue.aqms()) {
    out << kIndent << "AQM: " << formatAqm(aqm) << "\n";
  }
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
  constexpr auto fmtString = "{:<7}{:<20}{:<25}{:<10}{:<15}{:<15}\n";
  const bool detailed = CmdGlobalOptions::getInstance()->isDetailed();

  for (auto const& [portId, portInfo] : portId2PortInfoThrift) {
    std::ignore = portId;

    out << portInfo.name().value() << "\n";
    out << std::string(10, '=') << std::endl;

    out << fmt::format(
        fmtString,
        "ID",
        "Name",
        "Mode",
        "Weight",
        "ReservedBytes",
        "ScalingFactor");
    out << std::string(90, '-') << std::endl;

    for (auto const& queue : portInfo.portQueues().value()) {
      out << fmt::format(
          fmtString,
          folly::copy(queue.id().value()),
          queue.name().value(),
          queue.mode().value(),
          queue.weight() ? std::to_string(*queue.weight()) : "",
          queue.reservedBytes() ? std::to_string(*queue.reservedBytes()) : "",
          queue.scalingFactor() ? *queue.scalingFactor() : "");
      if (detailed) {
        printQueueDetails(queue, out);
      }
    }

    out << "\n";
  }
}

std::string_view CmdShowPortQueueTraits::description() {
  return "Displays the egress queue configuration of each port: per queue, its ID, traffic class name, scheduling mode, weight, reserved bytes and dynamic threshold scaling factor. Weighted round robin queues report a weight while strict priority queues leave it blank. With --detail, each queue also lists its active queue management (AQM) entries: the congestion behavior (ECN or EARLY_DROP) and the linear detection thresholds (minimum length, maximum length, probability), or 'none' when no AQM is configured. Use it to verify QoS queue, scheduler and ECN/WRED configuration on the ports.";
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
