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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/CmdConfigProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `community-list <name> [<attribute> <value> ...]`, validated at
// construction. A CommunityList is keyed by <name>. Its values are a flat
// string list in the daemon, so they are flat `community` attributes here
// (one per invocation), not nested entries. Mirrors BgpAsPathListConfig.
//
// Grammar (from the FBOSS proposed syntax):
//   community-list <name>                            (create/select list)
//   community-list <name> boolean-operator <AND|OR>  (list attribute)
//   community-list <name> community <community>      (append to communities,
//                                                     the field bgpd matches
//                                                     on)
//   community-list <name> description <string>       (list attribute)
//   community-list <name> exact-match <true|false>   (list attribute)
class BgpCommunityListConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpCommunityListConfig(std::vector<std::string> v);
  const std::string& listName() const {
    return listName_;
  }
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string listName_;
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// Single handler for the whole `config protocol bgp policy community-list`
// family; dispatch and per-attribute parsing live in the .cpp so a new tunable
// is a one-entry change. Mirrors CmdConfigProtocolBgpPolicyAsPathList.
struct CmdConfigProtocolBgpPolicyCommunityListTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stops CLI11 from classifying attribute tokens as subcommands once the
    // list name is consumed. See CmdConfigProtocolBgpNeighborTraits.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<name> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpCommunityListConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyCommunityList
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyCommunityList,
          CmdConfigProtocolBgpPolicyCommunityListTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyCommunityListTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyCommunityListTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
