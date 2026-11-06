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

#include <string_view>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_route_types_types.h"

namespace facebook::fboss {

/** Throw the compatibility error used when an older BGP lacks the RPC. */
[[noreturn]] inline neteng::fboss::bgp::thrift::TFibNexthopDatabase
throwFibOutTrackingUnsupported() {
  throw FbossError("The target BGP binary does not support FIB-out tracking.");
}

/** Query the nexthop database and translate UNKNOWN_METHOD for older BGP. */
template <typename Client>
neteng::fboss::bgp::thrift::TFibNexthopDatabase
queryFibNexthopDatabaseWithCompatibility(Client& client) {
  using RetType = neteng::fboss::bgp::thrift::TFibNexthopDatabase;
  return runMethodWithLegacyFallback(
      [&]() {
        RetType database;
        client.sync_getFibNexthopDatabase(database);
        return database;
      },
      throwFibOutTrackingUnsupported);
}

struct CmdShowBgpFibNexthopsTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = neteng::fboss::bgp::thrift::TFibNexthopDatabase;

  /** Return the user-facing description for this command. */
  static std::string_view description();
};

class CmdShowBgpFibNexthops
    : public CmdHandler<CmdShowBgpFibNexthops, CmdShowBgpFibNexthopsTraits> {
 public:
  using RetType = CmdShowBgpFibNexthopsTraits::RetType;

  /** Fetch the canonical FIB-out nexthop-set database from BGP. */
  RetType queryClient(const HostInfo& hostInfo);

  /** Render each canonical nexthop set and its route reference count. */
  void printOutput(const RetType& database, std::ostream& out = std::cout);

  /** Return synthetic canonical-set data for command documentation. */
  static RetType sampleModel();
};

} // namespace facebook::fboss
