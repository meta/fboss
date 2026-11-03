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
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/CmdDeleteProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `delete protocol bgp policy as-path-list <name> [regex <regex>]`,
// validated at construction. The name is the list's identity, matched exactly
// the same way the config command stores it; the optional `regex` selector
// removes one as_paths pattern instead of the whole list. Mirrors
// BgpPeerGroupRef.
class BgpAsPathListRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpAsPathListRef(std::vector<std::string> v);
  const std::string& listName() const {
    return listName_;
  }
  bool hasRegex() const {
    return hasRegex_;
  }
  const std::string& regex() const {
    return regex_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string listName_;
  bool hasRegex_{false};
  std::string regex_;
};

struct CmdDeleteProtocolBgpPolicyAsPathListTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<name> [regex <regex>]");
  }
  using ObjectArgType = BgpAsPathListRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPolicyAsPathList
    : public CmdHandler<
          CmdDeleteProtocolBgpPolicyAsPathList,
          CmdDeleteProtocolBgpPolicyAsPathListTraits> {
 public:
  using ObjectArgType =
      CmdDeleteProtocolBgpPolicyAsPathListTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPolicyAsPathListTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
