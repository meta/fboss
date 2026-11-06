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
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/CmdDeleteProtocolBgp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `delete protocol bgp neighbor <ip-address>`, validated at
// construction. The address is normalized the same way the config command
// normalizes it, so any spelling of a peer's address deletes that peer.
class BgpNeighborRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpNeighborRef(std::vector<std::string> v);
  const std::string& peerAddr() const {
    return peerAddr_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string peerAddr_;
};

struct CmdDeleteProtocolBgpNeighborTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<ip-address>");
  }
  using ObjectArgType = BgpNeighborRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpNeighbor : public CmdHandler<
                                         CmdDeleteProtocolBgpNeighbor,
                                         CmdDeleteProtocolBgpNeighborTraits> {
 public:
  using ObjectArgType = CmdDeleteProtocolBgpNeighborTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpNeighborTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
