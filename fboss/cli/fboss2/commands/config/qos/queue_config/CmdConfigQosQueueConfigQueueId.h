/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/commands/config/qos/queue_config/CmdConfigQosQueueConfig.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

struct CmdConfigQosQueueConfigQueueIdTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigQosQueueConfig;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "queue_config",
        args,
        "Queue ID followed by key-value pairs: <queue-id> <attr> <value> "
        "[<attr> <value> ...] where <attr> is one of: " +
            utils::validQueueAttrs());
  }
  using ObjectArgType = utils::QueueIdAndAttributes;
  using RetType = std::string;
};

class CmdConfigQosQueueConfigQueueId
    : public CmdHandler<
          CmdConfigQosQueueConfigQueueId,
          CmdConfigQosQueueConfigQueueIdTraits> {
 public:
  using ObjectArgType = CmdConfigQosQueueConfigQueueIdTraits::ObjectArgType;
  using RetType = CmdConfigQosQueueConfigQueueIdTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::QueueConfigName& name,
      const ObjectArgType& config);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
