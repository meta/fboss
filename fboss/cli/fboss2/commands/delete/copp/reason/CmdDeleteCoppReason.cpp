/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/copp/reason/CmdDeleteCoppReason.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/copp/CoppUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CoppReasonDeleteArgs::CoppReasonDeleteArgs(std::vector<std::string> v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format("Expected exactly one <reason-name>, got {}", v.size()));
  }
  reason_ = copp_reason::parseReason(v[0]);
  data_ = std::move(v);
}

CmdDeleteCoppReasonTraits::RetType CmdDeleteCoppReason::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  const auto reasonName = apache::thrift::util::enumNameSafe(args.getReason());

  const auto noMapping = [&reasonName] {
    return fmt::format(
        "Warning: no rxReason -> queue mapping for {}, nothing to delete",
        reasonName);
  };

  if (!swConfig.cpuTrafficPolicy().has_value() ||
      !swConfig.cpuTrafficPolicy()->rxReasonToQueueOrderedList().has_value()) {
    return noMapping();
  }
  auto& list = *swConfig.cpuTrafficPolicy()->rxReasonToQueueOrderedList();

  auto it = std::find_if(
      list.begin(), list.end(), [&args](const cfg::PacketRxReasonToQueue& e) {
        return *e.rxReason() == args.getReason();
      });
  if (it == list.end()) {
    return noMapping();
  }

  const auto queueId = *it->queueId();
  list.erase(it);

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Deleted reason {} -> queue {} mapping", reasonName, queueId);
}

void CmdDeleteCoppReason::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdDeleteCoppReason, CmdDeleteCoppReasonTraits>::run();

} // namespace facebook::fboss
