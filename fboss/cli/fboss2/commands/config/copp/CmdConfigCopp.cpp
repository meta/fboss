/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/copp/CmdConfigCopp.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/commands/config/copp/CoppUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

using copp_queue::parseQueueId;

// Literal tokens accepted in
// `config copp reason <reason-name> queue <id> [order <n>]`.
constexpr std::string_view kSubCmdQueue = "queue";
constexpr std::string_view kSubCmdOrder = "order";

} // namespace

CoppQueueArgs::CoppQueueArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected <id> [<attr> <value> ...] where <attr> is one of: " +
        utils::validQueueAttrs());
  }
  queueId_ = parseQueueId(v[0], "queue");

  // Every token after <id> is a shared queue attribute, so the grammar cannot
  // drift from `config qos queue-config`'s.
  utils::walkQueueAttributes(v, 1, attributes_, aqmAttributes_);

  data_ = std::move(v);
}

CoppReasonArgs::CoppReasonArgs(std::vector<std::string> v) {
  if (v.size() != 3 && v.size() != 5) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <reason-name> {} <id> [{} <n>], got {} argument(s)",
            kSubCmdQueue,
            kSubCmdOrder,
            v.size()));
  }
  cfg::PacketRxReason reason = copp_reason::parseReason(v[0]);
  if (v[1] != kSubCmdQueue) {
    throw std::invalid_argument(
        fmt::format(
            "Expected '{}' between reason and queue id, got '{}'",
            kSubCmdQueue,
            v[1]));
  }
  reason_ = reason;
  queueId_ = parseQueueId(v[2], "reason");
  if (v.size() == 5) {
    if (v[3] != kSubCmdOrder) {
      throw std::invalid_argument(
          fmt::format(
              "Expected '{}' after the queue id, got '{}'",
              kSubCmdOrder,
              v[3]));
    }
    int32_t parsed = 0;
    try {
      parsed = folly::to<int32_t>(v[4]);
    } catch (const folly::ConversionError&) {
      throw std::invalid_argument(
          fmt::format("order value must be an integer, got '{}'", v[4]));
    }
    if (parsed < 0) {
      throw std::invalid_argument(
          fmt::format("order value must be non-negative, got {}", parsed));
    }
    order_ = static_cast<size_t>(parsed);
  }
  data_ = std::move(v);
}

namespace {

std::string applyCpuQueueConfig(
    cfg::SwitchConfig& swConfig,
    const CoppQueueArgs& args) {
  auto& queues = *swConfig.cpuQueues();
  auto it = copp_queue::findQueue(queues, args.getQueueId());
  const bool existed = it != queues.end();

  // Edit a local copy; splice back only after every attribute validates, so a
  // rejected edit cannot leave a half-built queue in the session config (same
  // pattern as the qos queue-config commands).
  cfg::PortQueue work;
  if (existed) {
    work = *it;
  } else {
    if (!utils::findStreamTypeAttr(args.getAttributes()).has_value()) {
      throw std::invalid_argument(
          fmt::format(
              "Creating queue {} requires stream-type: the agent only installs "
              "cpuQueues entries whose stream type the ASIC's CPU port exposes",
              args.getQueueId()));
    }
    work.id() = args.getQueueId();
  }

  if (!args.getAttributes().empty() || !args.getAqmAttributes().empty()) {
    utils::applyPortQueueConfig(
        work, args.getAttributes(), args.getAqmAttributes());
  }

  // `it` stays valid: nothing above touches `queues`.
  if (existed) {
    *it = work;
  } else {
    queues.push_back(std::move(work));
  }
  if (!args.hasEdits()) {
    return fmt::format("Ensured queue {} exists", args.getQueueId());
  }
  // Echo the applied tokens back (everything after <id>).
  const auto tokens = args.data();
  return fmt::format(
      "Updated queue {}: {}",
      args.getQueueId(),
      folly::join(" ", tokens.begin() + 1, tokens.end()));
}

std::string applyReasonConfig(
    cfg::SwitchConfig& swConfig,
    const CoppReasonArgs& args) {
  if (!swConfig.cpuTrafficPolicy().has_value()) {
    swConfig.cpuTrafficPolicy() = cfg::CPUTrafficPolicyConfig{};
  }
  auto& policy = *swConfig.cpuTrafficPolicy();
  if (!policy.rxReasonToQueueOrderedList().has_value()) {
    policy.rxReasonToQueueOrderedList() =
        std::vector<cfg::PacketRxReasonToQueue>{};
  }
  auto& list = *policy.rxReasonToQueueOrderedList();
  const auto reasonName = apache::thrift::util::enumNameSafe(args.getReason());

  auto it = std::find_if(
      list.begin(), list.end(), [&args](const cfg::PacketRxReasonToQueue& e) {
        return *e.rxReason() == args.getReason();
      });
  const bool existed = it != list.end();

  cfg::PacketRxReasonToQueue entry;
  entry.rxReason() = args.getReason();
  entry.queueId() = args.getQueueId();

  if (!args.getOrder().has_value()) {
    // No order given: an existing reason keeps its position, a new one
    // appends.
    if (existed) {
      *it = entry;
      return fmt::format(
          "Updated reason {} -> queue {}", reasonName, args.getQueueId());
    }
    list.push_back(entry);
    return fmt::format(
        "Mapped reason {} -> queue {}", reasonName, args.getQueueId());
  }

  // The list is position-sensitive; place the entry at the requested 0-based
  // index. The index refers to the final list, so an existing entry doesn't
  // count towards the bound (it is removed before reinsertion).
  const size_t order = *args.getOrder();
  const size_t maxOrder = existed ? list.size() - 1 : list.size();
  if (order > maxOrder) {
    throw std::invalid_argument(
        fmt::format(
            "order {} is out of range: highest valid position is {}",
            order,
            maxOrder));
  }
  if (existed) {
    list.erase(it);
  }
  list.insert(list.begin() + order, entry);
  return fmt::format(
      "{} reason {} -> queue {} at position {}",
      existed ? "Updated" : "Mapped",
      reasonName,
      args.getQueueId(),
      order);
}

} // namespace

CmdConfigCoppQueueTraits::RetType CmdConfigCoppQueue::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = applyCpuQueueConfig(*session.getAgentConfig().sw(), args);
  // cpuQueues edits are processed hitlessly by
  // SaiHostifManager::processHostifDelta -> changeCpuQueue -> QueueManager.
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigCoppQueue::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdConfigCoppReasonTraits::RetType CmdConfigCoppReason::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = applyReasonConfig(*session.getAgentConfig().sw(), args);
  // rxReason -> queue mapping edits are processed hitlessly by
  // SaiHostifManager::processHostifDelta (trap-group update path).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigCoppReason::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdConfigCoppTrafficPolicyTraits::RetType
CmdConfigCoppTrafficPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = traffic_policy::applyAction(
      *session.getAgentConfig().sw(),
      traffic_policy::PolicyKind::Cpu,
      args.getMatcherName(),
      args.getActionTokens());
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigCoppTrafficPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiations
template void CmdHandler<CmdConfigCopp, CmdConfigCoppTraits>::run();
template void CmdHandler<CmdConfigCoppQueue, CmdConfigCoppQueueTraits>::run();
template void CmdHandler<CmdConfigCoppReason, CmdConfigCoppReasonTraits>::run();
template void
CmdHandler<CmdConfigCoppTrafficPolicy, CmdConfigCoppTrafficPolicyTraits>::run();

} // namespace facebook::fboss
