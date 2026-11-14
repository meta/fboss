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
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/copp/CmdDeleteCopp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

// Argument for `delete copp reason <reason-name>`.
//
// <reason-name> is matched case-insensitively against the
// cfg::PacketRxReason enum (e.g. arp, ndp, bgp, lacp, lldp, dhcp, ...),
// same as `config copp reason`.
class CoppReasonDeleteArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ CoppReasonDeleteArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  cfg::PacketRxReason getReason() const {
    return reason_;
  }

 private:
  cfg::PacketRxReason reason_{};
};

struct CmdDeleteCoppReasonTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdDeleteCopp;
  using ObjectArgType = CoppReasonDeleteArgs;
  using RetType = std::string;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // required() + expected(1) forces CLI11 to route the positional to this
    // option even when the reason name would otherwise classify as a
    // subcommand elsewhere in the tree (e.g. `arp`, `ndp` under other
    // command roots) — same rationale as CmdConfigCoppReasonTraits.
    cmd.add_option(
           "copp_reason_delete",
           args,
           "<reason-name> where <reason-name> is a cfg::PacketRxReason "
           "(e.g. arp, ndp, bgp, bgpv6, lacp, lldp, dhcp, dhcpv6, ttl_1, "
           "...)")
        ->required()
        ->expected(1);
  }
};

class CmdDeleteCoppReason
    : public CmdHandler<CmdDeleteCoppReason, CmdDeleteCoppReasonTraits> {
 public:
  using ObjectArgType = CmdDeleteCoppReasonTraits::ObjectArgType;
  using RetType = CmdDeleteCoppReasonTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
