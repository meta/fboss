/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
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

// Parsed `prefix-list <name> [<attribute> <value> ...]`, validated at
// construction. A PrefixList is keyed by <name>. Its prefixes[] entries are
// their own subcommand (CmdConfigProtocolBgpPolicyPrefixListEntry), not
// attributes. Mirrors BgpAsPathListConfig.
//
// Grammar (from the FBOSS proposed syntax):
//   prefix-list <name>                       (create/select list)
//   prefix-list <name> boolean-operator OR   (list attribute; bgpd accepts
//                                             only OR, and no list-level
//                                             compare-operator at all)
//   prefix-list <name> description <string>  (list attribute)
//   prefix-list <name> ip-version <v4|v6>    (list attribute)
class BgpPrefixListConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpPrefixListConfig(std::vector<std::string> v);
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

// Single handler for the whole `config protocol bgp policy prefix-list`
// family; dispatch and per-attribute parsing live in the .cpp so a new tunable
// is a one-entry change. Mirrors CmdConfigProtocolBgpPolicyAsPathList.
struct CmdConfigProtocolBgpPolicyPrefixListTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // No positionals_at_end() here: CLI11 must stay free to classify the
    // `prefix` token as this command's subcommand rather than swallowing it
    // into args. See CmdConfigProtocolBgpPolicyAsPathListTraits.
    cmd.add_option("args", args, "<name> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpPrefixListConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyPrefixList
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyPrefixList,
          CmdConfigProtocolBgpPolicyPrefixListTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyPrefixListTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPolicyPrefixListTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
