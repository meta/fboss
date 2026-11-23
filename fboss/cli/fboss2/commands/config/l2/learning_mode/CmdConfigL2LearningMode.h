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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/l2/CmdConfigL2.h"

namespace facebook::fboss {

// Custom type for L2 learning mode argument with validation
class L2LearningModeArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ L2LearningModeArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  cfg::L2LearningMode getL2LearningMode() const {
    return l2LearningMode_;
  }

 private:
  cfg::L2LearningMode l2LearningMode_ = cfg::L2LearningMode::HARDWARE;
};

struct CmdConfigL2LearningModeTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigL2;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "learning_mode", args, "L2 learning mode (hardware|software|disabled)");
  }
  using ObjectArgType = L2LearningModeArg;
  using RetType = std::string;
};

class CmdConfigL2LearningMode : public CmdHandler<
                                    CmdConfigL2LearningMode,
                                    CmdConfigL2LearningModeTraits> {
 public:
  using ObjectArgType = CmdConfigL2LearningModeTraits::ObjectArgType;
  using RetType = CmdConfigL2LearningModeTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& learningMode);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
