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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/ptp/CmdConfigPtp.h"

namespace facebook::fboss {

class PtpTransparentClockArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ PtpTransparentClockArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  bool getEnable() const {
    return enable_;
  }

 private:
  bool enable_{false};
};

struct CmdConfigPtpTransparentClockTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigPtp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "state", args, "PTP transparent clock state (enable|disable)");
  }
  using ObjectArgType = PtpTransparentClockArg;
  using RetType = std::string;
};

class CmdConfigPtpTransparentClock : public CmdHandler<
                                         CmdConfigPtpTransparentClock,
                                         CmdConfigPtpTransparentClockTraits> {
 public:
  using ObjectArgType = CmdConfigPtpTransparentClockTraits::ObjectArgType;
  using RetType = CmdConfigPtpTransparentClockTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& state);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
