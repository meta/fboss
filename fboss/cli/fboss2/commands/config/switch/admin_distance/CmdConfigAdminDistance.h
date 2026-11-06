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

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/switch/CmdConfigSwitch.h"

namespace facebook::fboss {

// ClientIDs whose admin distance is hardcoded in the agent, so a
// clientIdToAdminDistance entry for them is never consulted:
//   STATIC_ROUTE (1)           -> AdminDistance::STATIC_ROUTE
//   INTERFACE_ROUTE (2)        -> AdminDistance::DIRECTLY_CONNECTED
//   LINKLOCAL_ROUTE (3)        -> AdminDistance::DIRECTLY_CONNECTED
//   REMOTE_INTERFACE_ROUTE (4) -> AdminDistance::DIRECTLY_CONNECTED
// Maps client-id to the reason it is rejected. Both `config switch
// admin-distance` and `delete switch admin-distance` refuse these ids.
const std::unordered_map<int32_t, std::string>& forbiddenAdminDistanceClients();

// Parses and validates the <client-id> token shared by `config switch
// admin-distance` and `delete switch admin-distance`: must be a non-negative
// integer and not a forbidden client. `action` names the operation for the
// refusal message (e.g. "changing admin distance").
int32_t parseAdminDistanceClientId(
    const std::string& token,
    std::string_view action);

// Parses the two positional arguments of
//   config switch admin-distance <client-id> <distance>
// where <client-id> is a routing ClientID (e.g. 0=BGP, 786=OpenR)
// and <distance> is the administrative distance to associate with it.
// ClientIDs 1 (STATIC_ROUTE), 2 (INTERFACE_ROUTE), 3 (LINKLOCAL_ROUTE),
// and 4 (REMOTE_INTERFACE_ROUTE) are forbidden: their admin distances are
// hardcoded in the agent and cannot be changed via config.
class AdminDistanceArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AdminDistanceArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  int32_t getClientId() const {
    return clientId_;
  }

  int32_t getDistance() const {
    return distance_;
  }

 private:
  int32_t clientId_{0};
  int32_t distance_{0};
};

struct CmdConfigAdminDistanceTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigSwitch;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "client_distance",
        args,
        "<client-id> <distance> - admin distance for a routing client "
        "(client-id: 0=BGP, 786=OpenR; distance: 0-255). "
        "Client-ids 1/2/3/4 are hardcoded and cannot be changed.");
  }
  using ObjectArgType = AdminDistanceArg;
  using RetType = std::string;
};

class CmdConfigAdminDistance
    : public CmdHandler<CmdConfigAdminDistance, CmdConfigAdminDistanceTraits> {
 public:
  using ObjectArgType = CmdConfigAdminDistanceTraits::ObjectArgType;
  using RetType = CmdConfigAdminDistanceTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& arg);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
