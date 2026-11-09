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

#include <folly/json/dynamic.h>
#include <string_view>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

// Traits for the "show config running bgp" command
struct CmdShowConfigRunningBgpTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = folly::dynamic;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    // Split per topic rather than one long literal: this prose is reviewed and
    // amended far more often than the code around it.
    return
        // What it shows.
        "Displays the BGP configuration the daemon is actually running, as "
        "pretty-printed JSON. This is the daemon's own view rather than the "
        "file on disk, so it is the authoritative answer to 'what config is "
        "this switch running right now' after a reload or a config push. "
        // What the document contains.
        "The document carries the daemon-wide settings, the named community "
        "and local-preference mnemonics that the rest of the CLI resolves "
        "against (which is why 'show bgp table' can print names instead of "
        "raw asn:value pairs), the peer groups and their policies, and the "
        "policy statements themselves. "
        // Reading it.
        "Output is large on a production switch; page it or pipe it through a "
        "JSON filter rather than reading it end to end. "
        // Command spelling, which differs by build.
        "The command is reached as 'show bgp config' in the internal build, "
        "where 'show bgp config raw' gives the config-file rendering instead "
        "and 'show config running bgp' is an equivalent alias; in the "
        "open-source build it is spelled 'show bgp config running' and "
        "neither the raw variant nor the 'show config running bgp' alias is "
        "registered. "
        // The two builds also differ in the SHAPE of what they return.
        "The two builds also differ in what they render: the internal build "
        "returns the structured BgpConfig document illustrated below, while "
        "the open-source build returns the daemon's raw running-config JSON, "
        "whose top-level keys differ. Treat the example here as the internal "
        "rendering.";
  }
};

} // namespace facebook::fboss
