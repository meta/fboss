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
#include <vector>
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/CmdDeleteProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `delete protocol bgp policy routing-policy <name> term <seq-num>`,
// validated at construction. The seq-num is the term's identity within the
// policy named by the parent command's args; the whole term is deleted.
class BgpRoutingPolicyTermRef : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyTermRef(std::vector<std::string> v);
  int64_t seqNum() const {
    return seqNum_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  int64_t seqNum_{0};
};

// The term level as its own CLI11 subcommand, mirroring the config side
// (CmdConfigProtocolBgpPolicyRoutingPolicyTerm); the parent's parsed args
// arrive through the ancestor-args tuple.
struct CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdDeleteProtocolBgpPolicyRoutingPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("args", args, "<seq-num>");
  }
  using ObjectArgType = BgpRoutingPolicyTermRef;
  using RetType = std::string;
};

class CmdDeleteProtocolBgpPolicyRoutingPolicyTerm
    : public CmdHandler<
          CmdDeleteProtocolBgpPolicyRoutingPolicyTerm,
          CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits> {
 public:
  using ObjectArgType =
      CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits::ObjectArgType;
  using RetType = CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpRoutingPolicyRef& policyArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
