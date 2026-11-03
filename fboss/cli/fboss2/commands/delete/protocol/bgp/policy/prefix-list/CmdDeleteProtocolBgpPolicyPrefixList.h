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

#include <optional>
#include <string>
#include <vector>
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/CmdDeleteProtocolBgpPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `delete protocol bgp policy prefix-list <name> [prefix <prefix/len>]`,
// validated at construction. The list name and the entry's base prefix are
// the identities the config command stores. Without the `prefix` selector
// the whole list is deleted; with it, only that entry. Mirrors
// BgpCommunityListRef, with a prefix-keyed second level in place of the
// value-keyed one.
class BgpPrefixListRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpPrefixListRef(std::vector<std::string> v);
  const std::string& listName() const {
    return listName_;
  }
  bool hasPrefix() const {
    return basePrefix_.has_value();
  }
  const std::string& basePrefix() const {
    return *basePrefix_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string listName_;
  std::optional<std::string> basePrefix_;
};

struct CmdDeleteProtocolBgpPolicyPrefixListTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgpPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<name> [prefix <prefix/len>]");
  }
  using ObjectArgType = BgpPrefixListRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPolicyPrefixList
    : public CmdHandler<
          CmdDeleteProtocolBgpPolicyPrefixList,
          CmdDeleteProtocolBgpPolicyPrefixListTraits> {
 public:
  using ObjectArgType =
      CmdDeleteProtocolBgpPolicyPrefixListTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPolicyPrefixListTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
