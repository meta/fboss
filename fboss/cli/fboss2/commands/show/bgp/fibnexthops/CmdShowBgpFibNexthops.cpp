/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/fibnexthops/CmdShowBgpFibNexthops.h"

#include <string_view>

#include <folly/IPAddress.h>

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
CmdShowBgpFibNexthops::RetType CmdShowBgpFibNexthops::queryClient(
    const HostInfo& hostInfo) {
  auto client = utils::createClient<
      apache::thrift::Client<neteng::fboss::bgp::thrift::TBgpService>>(
      hostInfo);
  return queryFibNexthopDatabaseWithCompatibility(*client);
}

void CmdShowBgpFibNexthops::printOutput(
    const RetType& database,
    std::ostream& out) {
  if (!*database.enabled()) {
    out << "FIB-out tracking is disabled.\n";
    return;
  }

  out << "Unique FIB-out nexthop sets: " << database.nexthop_sets()->size()
      << '\n';
  if (database.nexthop_sets()->empty()) {
    out << "FIB-out tracking is enabled; no nexthop sets are currently "
           "referenced.\n";
    return;
  }

  for (const auto& nexthopSet : *database.nexthop_sets()) {
    out << "Nexthop Set | Route References: " << *nexthopSet.ref_count()
        << '\n';
    if (nexthopSet.next_hops()->empty()) {
      out << "    (empty)\n";
      continue;
    }
    for (const auto& nexthop : *nexthopSet.next_hops()) {
      out << "    " << formatBgpIpAddress(*nexthop.next_hop())
          << " | Weight: " << *nexthop.weight()
          << " | Role: " << apache::thrift::util::enumNameSafe(*nexthop.role());
      if (nexthop.is_connected().has_value()) {
        out << " | Connected: " << (*nexthop.is_connected() ? "true" : "false");
      }
      if (nexthop.interface_name().has_value()) {
        out << " | Interface: " << *nexthop.interface_name();
      }
      out << '\n';
    }
  }
}

std::string_view CmdShowBgpFibNexthopsTraits::description() {
  return "Displays each unique complete nexthop set in submitted BGP FIB-out "
         "state and the number of FIB-out routes that reference that set.";
}

CmdShowBgpFibNexthops::RetType CmdShowBgpFibNexthops::sampleModel() {
  RetType database;
  database.enabled() = true;

  neteng::fboss::bgp::thrift::TFibNexthopSet ipv4Set;
  ipv4Set.ref_count() = 128;
  for (const auto address : {"192.0.2.1", "192.0.2.2"}) {
    neteng::fboss::bgp::thrift::TFibOutNextHop nexthop;
    const auto ip = folly::IPAddress(address);
    nexthop.next_hop()->afi() = neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV4;
    nexthop.next_hop()->prefix_bin() =
        std::string(reinterpret_cast<const char*>(ip.bytes()), ip.byteCount());
    nexthop.next_hop()->num_bits() = ip.bitCount();
    nexthop.weight() = 1;
    nexthop.role() = neteng::fboss::bgp::thrift::TFibOutNextHopRole::PRIMARY;
    ipv4Set.next_hops()->push_back(std::move(nexthop));
  }
  database.nexthop_sets()->push_back(std::move(ipv4Set));

  neteng::fboss::bgp::thrift::TFibNexthopSet linkLocalSet;
  linkLocalSet.ref_count() = 64;
  neteng::fboss::bgp::thrift::TFibOutNextHop linkLocal;
  const auto linkLocalAddress = folly::IPAddress("fe80::1");
  linkLocal.next_hop()->afi() = neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV6;
  linkLocal.next_hop()->prefix_bin() = std::string(
      reinterpret_cast<const char*>(linkLocalAddress.bytes()),
      linkLocalAddress.byteCount());
  linkLocal.next_hop()->num_bits() = linkLocalAddress.bitCount();
  linkLocal.weight() = 0;
  linkLocal.role() = neteng::fboss::bgp::thrift::TFibOutNextHopRole::BACKUP;
  linkLocal.interface_name() = "eth1/1";
  linkLocalSet.next_hops()->push_back(std::move(linkLocal));
  database.nexthop_sets()->push_back(std::move(linkLocalSet));

  neteng::fboss::bgp::thrift::TFibNexthopSet emptySet;
  emptySet.ref_count() = 8;
  database.nexthop_sets()->push_back(std::move(emptySet));
  return database;
}

} // namespace facebook::fboss
