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

// Parsed `delete protocol bgp peer-group <name>`, validated at construction.
// The group name is the group's identity (an arbitrary string), matched
// exactly the same way the config command stores it. Mirrors BgpNeighborRef.
class BgpPeerGroupRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpPeerGroupRef(std::vector<std::string> v);
  const std::string& groupName() const {
    return groupName_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string groupName_;
};

struct CmdDeleteProtocolBgpPeerGroupTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<name>");
  }
  using ObjectArgType = BgpPeerGroupRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPeerGroup : public CmdHandler<
                                          CmdDeleteProtocolBgpPeerGroup,
                                          CmdDeleteProtocolBgpPeerGroupTraits> {
 public:
  using ObjectArgType = CmdDeleteProtocolBgpPeerGroupTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPeerGroupTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
