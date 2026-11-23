/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string_view>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/reboot_history/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss {

struct CmdShowRebootHistoryTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = utils::NoneArgType;
  using RetType = cli::ShowRebootHistoryModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowRebootHistory
    : public CmdHandler<CmdShowRebootHistory, CmdShowRebootHistoryTraits> {
 public:
  using RetType = CmdShowRebootHistoryTraits::RetType;

  // Reads the records reboot_cause_finder writes on this switch. Only the
  // local switch can be shown: the records live on its filesystem.
  RetType queryClient(const HostInfo& hostInfo);
  // The directory is a parameter so tests can point it at a temporary one.
  RetType readHistory(const std::string& historyDir);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
