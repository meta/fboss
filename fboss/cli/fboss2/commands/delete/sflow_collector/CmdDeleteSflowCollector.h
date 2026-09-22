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
#include "fboss/cli/fboss2/commands/config/sflow_collector/CmdConfigSflowCollector.h"

namespace facebook::fboss {

struct CmdDeleteSflowCollectorTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "ip_and_port",
           args,
           "Collector IP address followed by UDP port (1-32767)")
        ->expected(2);
  }
  using ObjectArgType = SflowCollectorArg;
  using RetType = std::string;
};

class CmdDeleteSflowCollector : public CmdHandler<
                                    CmdDeleteSflowCollector,
                                    CmdDeleteSflowCollectorTraits> {
 public:
  using ObjectArgType = CmdDeleteSflowCollectorTraits::ObjectArgType;
  using RetType = CmdDeleteSflowCollectorTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& collector);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
