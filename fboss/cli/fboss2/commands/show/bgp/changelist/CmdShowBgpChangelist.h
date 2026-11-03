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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h" // NOLINT(misc-include-cleaner)
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using neteng::fboss::bgp::thrift::TRibEntry;
using neteng::fboss::bgp::thrift::TRibEntryWithHost;
using neteng::fboss::bgp_attr::TBgpAfi;

struct CmdShowBgpChangelistTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the prefixes currently queued for re-advertisement - the entries BGP has recomputed but not yet pushed out to its peers - rendered in the same summary form as 'show bgp table'. This is a snapshot of work in flight, not a history: an entry appears when its best path changes and disappears once the update has been sent, so on a converged switch the command prints only the marker and acronym legend with nothing under it, and that empty result is the healthy one. Entries that persist across repeated runs are the signal worth chasing - they mean the switch is recomputing a prefix it cannot drain, or a peer is not draining its update queue. Pair with 'show bgp neighbors <peer>' to see whether a particular session's send queue is backing up. Takes no arguments and covers both address families.";
  }
};

class CmdShowBgpChangelist
    : public CmdHandler<CmdShowBgpChangelist, CmdShowBgpChangelistTraits> {
 public:
  using RetType = CmdShowBgpChangelistTraits::RetType;
  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(RetType& entries, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data). A queued entry is an
  // ordinary RIB entry, so this is the shared 'show bgp table' sample; the
  // command renders it without detail.
  static RetType sampleModel() {
    return sampleRibEntriesWithHost();
  }
};
} // namespace facebook::fboss
