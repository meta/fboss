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
#include "fboss/cli/fboss2/commands/delete/switch/CmdDeleteSwitch.h"

namespace facebook::fboss {

// Parses the single positional argument of
//   delete admin-distance <client-id>
// where <client-id> is a routing ClientID (e.g. 0=BGPD, 700=STATIC_INTERNAL,
// 786=OPENR) whose admin-distance override should be removed from the config.
// ClientIDs 1 (STATIC_ROUTE), 2 (INTERFACE_ROUTE), 3 (LINKLOCAL_ROUTE), and
// 4 (REMOTE_INTERFACE_ROUTE) are forbidden here for the same reason they are
// forbidden in `config switch admin-distance`: the agent hardcodes their
// distances, so their map entries are inert. See
// forbiddenAdminDistanceClients() in CmdConfigAdminDistance.h.
class AdminDistanceDeleteArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AdminDistanceDeleteArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  int32_t getClientId() const {
    return clientId_;
  }

 private:
  int32_t clientId_{0};
};

struct CmdDeleteAdminDistanceTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteSwitch;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "client_id",
        args,
        "<client-id> - routing client whose admin distance entry to remove "
        "(e.g. 0=BGP, 786=OpenR). "
        "Client-ids 1/2/3/4 are hardcoded and cannot be removed.");
  }
  using ObjectArgType = AdminDistanceDeleteArg;
  using RetType = std::string;
};

class CmdDeleteAdminDistance
    : public CmdHandler<CmdDeleteAdminDistance, CmdDeleteAdminDistanceTraits> {
 public:
  using ObjectArgType = CmdDeleteAdminDistanceTraits::ObjectArgType;
  using RetType = CmdDeleteAdminDistanceTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& arg);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
