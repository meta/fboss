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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

/**
 * The action level of a routing-policy term. `action` itself is a pure
 * grouping node; `result` and `set` are its subcommands, each with its own
 * handler (the policy and term args arrive through the ancestor-args tuple).
 *
 * `set` MUST be a real subcommand rather than a parsed token: CLI11's
 * _valid_subcommand walks the parent chain unconditionally, so a bare `set`
 * arg token inside `action` would be classified as the top-level `set` verb
 * and stolen. A local subcommand named `set` shadows the verb because
 * _find_subcommand checks the local scope first.
 *
 * Grammar (from the FBOSS proposed syntax):
 *   ... term <seq-num> action result <ACCEPT|REJECT|CONTINUE>
 *   ... term <seq-num> action set as-path prepend <asn> [<asn> ...]
 *   ... term <seq-num> action set community <community-string> [additive]
 *   ... term <seq-num> action set local-pref <value>
 *   ... term <seq-num> action set med <value>
 *   ... term <seq-num> action set next-hop <ip-address>
 *   ... term <seq-num> action set origin <IGP|EGP|INCOMPLETE>
 *   ... term <seq-num> action set weight <value>
 */
namespace facebook::fboss {

// Parsed `action result <value>`, validated at construction.
class BgpRoutingPolicyTermActionResultConfig
    : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyTermActionResultConfig(
      std::vector<std::string> v);
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::vector<std::string> values_;
};

// Parsed `action set <attribute> <value> ...`, validated at construction.
// Attribute names may be composed of more than one CLI token
// (`as-path prepend`), matched as a unit.
class BgpRoutingPolicyTermActionSetConfig
    : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpRoutingPolicyTermActionSetConfig(
      std::vector<std::string> v);
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string attr_; // matched dispatch key (never empty; no bare create)
  std::vector<std::string> values_;
};

struct CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicyRoutingPolicyTerm;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stop CLI11's parent-chain fallthrough from stealing a value token
    // that spells an ancestor subcommand name.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<ACCEPT|REJECT|CONTINUE>");
  }
  using ObjectArgType = BgpRoutingPolicyTermActionResultConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult,
          CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits::
          ObjectArgType;
  using RetType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpRoutingPolicyConfig& policyArgs,
      const BgpRoutingPolicyTermConfig& termArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

struct CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgpPolicyRoutingPolicyTerm;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stop CLI11's parent-chain fallthrough from stealing a value token
    // that spells an ancestor subcommand name.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<attribute> <value> ...");
  }
  using ObjectArgType = BgpRoutingPolicyTermActionSetConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet
    : public CmdHandler<
          CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet,
          CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits::ObjectArgType;
  using RetType =
      CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const BgpRoutingPolicyConfig& policyArgs,
      const BgpRoutingPolicyTermConfig& termArgs,
      const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
