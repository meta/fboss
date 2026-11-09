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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/CmdConfigProtocolBgp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `config protocol bgp neighbor <ip> [<attribute> <value> ...]`,
// validated at construction. The neighbor address is normalized via
// folly::IPAddress so different spellings of the same address (e.g.
// 2001:DB8::1 vs 2001:db8::1) key the same peer. Attributes may span two
// tokens (e.g. `timers hold-time`); the dispatch table is matched
// longest-prefix-first. A bare `neighbor <ip>` (no attribute) creates the
// peer.
class BgpNeighborConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpNeighborConfig(std::vector<std::string> v);
  const std::string& peerAddr() const {
    return peerAddr_;
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
  std::string peerAddr_;
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// The `<attribute> <value>` list the neighbor command accepts, rendered one
// attribute per line for `neighbor --help`. Generated from the dispatch table
// in the .cpp, so it cannot drift from what the command actually parses.
std::string bgpNeighborAttributeHelp();

// The dispatch keys (e.g. "remote-asn", "timers hold-time"), sorted.
std::vector<std::string> bgpNeighborAttributeNames();

// Single handler for the whole `config protocol bgp neighbor` family. The
// first positional token is the neighbor address, the next one or two tokens
// name the attribute, and the remaining tokens are its value(s); dispatch and
// per-attribute parsing live in the .cpp so adding a new neighbor tunable is
// a one-entry change rather than a new command class. Mirrors
// CmdConfigProtocolBgpGlobal.
struct CmdConfigProtocolBgpNeighborTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // positionals_at_end: once the first positional (the neighbor address,
    // which never collides with a command name) is consumed, CLI11 stops
    // classifying later tokens as subcommands. Without this, an attribute
    // token that matches a sibling command name (e.g. `peer-group`) is
    // stolen by CLI11's parent-chain subcommand fallthrough and the
    // remaining tokens are misparsed.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<ip-address> [<attribute> <value> ...]");
    // The attribute list is long, so it lives in this subcommand's own help
    // footer rather than in the one-line description shown by the parent.
    cmd.footer(bgpNeighborAttributeHelp());
  }
  using ObjectArgType = BgpNeighborConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpNeighbor : public CmdHandler<
                                         CmdConfigProtocolBgpNeighbor,
                                         CmdConfigProtocolBgpNeighborTraits> {
 public:
  using ObjectArgType = CmdConfigProtocolBgpNeighborTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpNeighborTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
