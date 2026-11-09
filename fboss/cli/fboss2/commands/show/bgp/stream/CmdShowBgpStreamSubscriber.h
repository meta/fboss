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

#include <fmt/core.h>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;

struct CmdShowBgpStreamSubscriberTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PEERID_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::vector<std::string>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Selects a BGP stream subscriber by id so one of the 'pre-policy' or 'post-policy' subcommands can show the routes being streamed to it. This level of the command does no lookup of its own: run without a subscriber id it prints the usage line, and run with an id but no policy subcommand it prints which subcommand is missing. Subscriber ids come from 'show bgp stream summary', which lists the clients currently subscribed to the switch's route stream. Use 'pre-policy' for what the switch selected for the subscriber and 'post-policy' for what the export policy actually let through.";
  }
};

class CmdShowBgpStreamSubscriber : public CmdHandler<
                                       CmdShowBgpStreamSubscriber,
                                       CmdShowBgpStreamSubscriberTraits> {
 public:
  using RetType = CmdShowBgpStreamSubscriberTraits::RetType;
  using ObjectArgType = CmdShowBgpStreamSubscriberTraits::ObjectArgType;
  RetType queryClient(const HostInfo&, const ObjectArgType& peerIds) {
    if (peerIds.empty()) {
      std::cout
          << "No subscriber id entered. Usage: fboss2 show bgp stream subscriber <subscriber id> pre-policy/post-policy"
          << std::endl;
    }
    return peerIds;
  }

  // Canned, synthetic model (no real switch data): the subscriber id list this
  // level of the command echoes back before a policy subcommand narrows it.
  static RetType sampleModel() {
    return {"1"};
  }

  void printOutput(RetType& peerIds) {
    if (!peerIds.empty()) {
      std::cout
          << fmt::format(
                 "Missing policy argument: fboss2 show bgp stream subscriber {} pre-policy/post-policy",
                 peerIds[0])
          << std::endl;
    }
  }
};

} // namespace facebook::fboss
