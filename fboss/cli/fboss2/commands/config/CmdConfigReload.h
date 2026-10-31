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
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

// Optional positional argument controlling how the reload is applied:
//   (omitted) or "hitless" -> sync_reloadConfig() over thrift (default)
//   "warmboot"             -> systemctl restart agent services
//   "coldboot"             -> create coldboot marker + systemctl restart
class BootTypeArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ BootTypeArg();

  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BootTypeArg(std::vector<std::string> v);

  cli::ConfigActionLevel level() const {
    return level_;
  }

 private:
  cli::ConfigActionLevel level_ = cli::ConfigActionLevel::HITLESS;
};

struct CmdConfigReloadTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "boot_type",
        args,
        "Optional boot type controlling how the reload is applied:\n"
        "  hitless   (default) Apply config diff in-place via the running "
        "agent.\n"
        "  warmboot  Restart the FBOSS agent service(s); on startup the "
        "agent reads agent.conf and the warmboot state cache from the "
        "previous run. Local-only.\n"
        "  coldboot  Restart the FBOSS agent service(s) and reprogram the "
        "ASIC from scratch from agent.conf. Local-only.");
  }
  using ObjectArgType = BootTypeArg;
  using RetType = std::string;
};

class CmdConfigReload
    : public CmdHandler<CmdConfigReload, CmdConfigReloadTraits> {
 public:
  using ObjectArgType = CmdConfigReloadTraits::ObjectArgType;
  using RetType = CmdConfigReloadTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const BootTypeArg& bootType);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
