/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// OSS implementations of the BGP show-util functions that diverge from the
// internal (facebook/) versions. The internal build compiles
// facebook/CmdShowUtils.cpp instead of this file; this file is compiled only by
// the OSS CMake build (see fboss/github/cmake/CliFboss2.cmake). Functions whose
// behavior is identical across builds live in
// commands/show/bgp/CmdShowUtils.cpp and are compiled by both builds.

#include <utility>

#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/oss/NetTools.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "folly/IPAddress.h"
#include "folly/Range.h"
#include "folly/String.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;
using apache::thrift::util::enumNameSafe;
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TBgpAddPathNegotiated;
using facebook::neteng::fboss::bgp::thrift::TBgpSessionDetail;
using folly::IPAddress;
using neteng::fboss::bgp::thrift::TBgpPeerState;

void cmdShowVersion(CmdShowVersionTraits::RetType& binaryVersion) {
  std::cout << "Package Name: " << binaryVersion["build_package_name"]
            << std::endl;
  std::cout << "Package Info: " << binaryVersion["build_package_info"]
            << std::endl;

  auto package_version = binaryVersion["build_package_version"];

  std::cout << "Package Version: " << package_version << std::endl;
  std::cout << "Build Details: " << std::endl;
  std::cout << "\t Host: " << binaryVersion["build_host"] << std::endl;
  std::cout << "\t Time: " << binaryVersion["build_time"] << std::endl;
  std::cout << "\t User: " << binaryVersion["build_user"] << std::endl;
  std::cout << "\t Path: " << binaryVersion["build_path"] << std::endl;
  std::cout << "\t Platform: " << binaryVersion["build_platform"] << std::endl;
  std::cout << "\t Revision: " << binaryVersion["build_revision"] << std::endl;
}

std::string getPackageId(
    const std::string& packageVersion,
    const std::string& /* packageInfo */) {
  return packageVersion;
}

const std::string printExtCommunities(
    const std::vector<TBgpExtCommunity>& extCommunities) {
  if (extCommunities.empty()) {
    return "";
  }

  std::vector<std::string> commStrs;
  for (const auto& community : extCommunities) {
    const auto& communityUnion = community.u().value();
    if (communityUnion.getType() != TBgpExtCommUnion::Type::two_byte_asn) {
      commStrs.emplace_back("non-two-byte-asn");
      continue;
    }

    const auto& extCommunity = communityUnion.get_two_byte_asn();
    if (folly::copy(extCommunity.type().value()) ==
        static_cast<int>(TBgpTwoByteAsnExtCommType::LINK_BANDWIDTH_TYPE)) {
      if (folly::copy(extCommunity.sub_type().value()) ==
          static_cast<int>(
              TBgpTwoByteAsnExtCommSubType::LINK_BANDWIDTH_SUB_TYPE)) {
        auto decodeLbwExtComm = CmdLocalOptions::getInstance()->getLocalOption(
            "show_bgp_neighbors", kGar);
        if (decodeLbwExtComm.empty()) {
          decodeLbwExtComm = CmdLocalOptions::getInstance()->getLocalOption(
              "show_bgp_table", kGar);
        }
        if (decodeLbwExtComm == "true") {
          commStrs.emplace_back(
              fmt::format(
                  "LBW{}:AS({}):RawValue({}):LbwValue({})",
                  ((folly::copy(extCommunity.type().value()) & 0x40) == 0)
                      ? "(T)"
                      : "",
                  folly::copy(extCommunity.asn().value()),
                  folly::copy(extCommunity.value().value()),
                  utils::formatBandwidth(translateLinkBandwidthValue(
                      folly::copy(extCommunity.value().value())))));
        } else {
          commStrs.emplace_back(
              fmt::format(
                  "LBW{}:AS({}):RawValue({}):LbwValue({})",
                  ((folly::copy(extCommunity.type().value()) & 0x40) == 0)
                      ? "(T)"
                      : "",
                  folly::copy(extCommunity.asn().value()),
                  folly::copy(extCommunity.value().value()),
                  utils::formatBandwidth(translateLinkBandwidthValue(
                      folly::copy(extCommunity.value().value())))));
        }
      } else {
        commStrs.emplace_back(
            fmt::format(
                "Type({}):SubType({}):AS({}):Value({})",
                folly::copy(extCommunity.type().value()),
                folly::copy(extCommunity.sub_type().value()),
                folly::copy(extCommunity.asn().value()),
                folly::copy(extCommunity.value().value())));
      }
    }
  }
  return folly::join(" ", commStrs);
}

void printBgpNeighborsOutput(
    const std::vector<TBgpSession>& neighbors,
    std::ostream& out) {
  constexpr int kKeepAliveRobustCount = 3;
  if (neighbors.empty()) {
    return;
  }
  uint32_t neighborIndex = 0;
  for (auto const& neighbor : neighbors) {
    const std::string& myAddr = *neighbor.my_addr();
    const std::string& peerAddr = *neighbor.peer_addr();
    // Make a mutable copy so we can trim whitespace
    std::string description =
        folly::trimWhitespace(*neighbor.description()).str();
    apache::thrift::optional_field_ref<const TBgpSessionDetail&> details =
        neighbor.details();
    const TBgpPeer& peer = *neighbor.peer();
    const TBgpPeerState peerState = *peer.peer_state();
    const int holdTime = *peer.hold_time();

    if (myAddr.size() == 0 || !details.has_value()) {
      out << "Missing details or IP Address: " << myAddr << " " << peerAddr;
      return;
    }

    // prevent printing number with comma, e.g: 65,000
    out.imbue(std::locale("C"));
    if (neighbors.size() > 1) {
      out << "------------------------------------------------------------------"
          << std::endl;
      out << fmt::format(
                 "neighbor {} of {}", neighborIndex + 1, neighbors.size())
          << std::endl;
    }

    // TODO(michaeluw): T113736668 deprecate i32 asns field
    uint32_t remote_as = *peer.remote_as_4_byte() > 0 ? *peer.remote_as_4_byte()
                                                      : *peer.remote_as();
    out << "BGP neighbor is " << peerAddr << ", remote AS " << remote_as
        << ", ";
    out << "Confed Peer: " << (*details->confed_peer() ? "" : "not ")
        << "configured" << std::endl;
    out << "  Description: " << description << std::endl;
    if (neighbor.ingress_policy_name().has_value()) {
      out << "  Ingress Policy: " << *neighbor.ingress_policy_name()
          << std::endl;
    }
    if (neighbor.egress_policy_name().has_value()) {
      out << "  Egress Policy: " << *neighbor.egress_policy_name() << std::endl;
    }
    out << "  BGP version 4, remote router ID "
        << IPAddress::fromLong(*details->remote_bgp_id()) << std::endl;
    if (neighbor.rib_version().has_value() && *neighbor.rib_version() > 0) {
      out << "  RIB version: " << *neighbor.rib_version() << std::endl;
    }
    out << "  Hold time is " << holdTime << "s, KeepAlive interval is "
        << holdTime / kKeepAliveRobustCount << "s" << std::endl;

    if (peerState == neteng::fboss::bgp::thrift::TBgpPeerState::ESTABLISHED) {
      out << "  BGP state is " << enumNameSafe(peerState) << ", up for "
          << utils::getPrettyElapsedTime(
                 utils::getEpochFromDuration(*neighbor.uptime()))
          << std::endl;

      if (*neighbor.num_resets() > 0) {
        out << "  Flapped " << *neighbor.num_resets() << " times, last flap "
            << utils::getPrettyElapsedTime(
                   utils::getEpochFromDuration(*neighbor.reset_time()))
            << " ago" << std::endl;
        out << "  Last reset reason: " << *neighbor.last_reset_reason()
            << std::endl;
      }
    } else {
      out << "  BGP state is " << enumNameSafe(peerState) << std::endl;
      if (peerState >= neteng::fboss::bgp::thrift::TBgpPeerState::ACTIVE &&
          details->active_time().has_value()) {
        out << "  The TCP socket has been up for "
            << utils ::getPrettyElapsedTime(
                   utils::getEpochFromDuration(*details->active_time()))
            << "." << std::endl;
      }
      if (*neighbor.num_resets() > 0) {
        out << "  Flapped " << *neighbor.num_resets() << " times";
        if (*neighbor.reset_time() > 0) {
          out << ", last went down "
              << utils ::getPrettyElapsedTime(
                     utils::getEpochFromDuration(*neighbor.reset_time()))
              << " ago";
        }
        out << std::endl;
        out << "  Last reset reason: " << *neighbor.last_reset_reason()
            << std::endl;
      }
    }

    out << "  UCMP Link Bandwidth:" << std::endl;
    out << "    Advertise: "
        << enumNameSafe(*neighbor.advertise_link_bandwidth()) << std::endl;
    out << "    Receive  : " << enumNameSafe(*neighbor.receive_link_bandwidth())
        << std::endl;
    if (neighbor.link_bandwidth_bps().has_value()) {
      out << "    Value    : "
          << utils::formatBandwidth(*neighbor.link_bandwidth_bps())
          << std::endl;
    }

    printBgpCapabilities(*details, out);
    out << "    Add Path: ";
    if (peer.add_path().has_value()) {
      out << "Configured - " << enumNameSafe(*peer.add_path());
      printAddPathCapability(*details->add_path_capabilities(), out);
      out << std::endl;
    } else {
      out << "DISABLED\n" << std::endl;
    }

    // Counter telemetry (prefix-level + PDU-level) is only meaningful for an
    // established session; stats are cleared on session-down, so skip both
    // tables for non-established peers to avoid all-zero noise.
    if (peerState == TBgpPeerState::ESTABLISHED) {
      printBgpPrefixTelemetry(neighbor, *details, out);
      printBgpMessageCounters(*details, out);
    }
    if (auto enforceFirstAsRejects = *details->enforce_first_as_rejects()) {
      out << "Number of enforce-first-as validation rejections: "
          << enforceFirstAsRejects << std::endl;
    }
    // TODO(michaeluw): T113736668 deprecate i32 asns field
    uint32_t local_as = *peer.local_as_4_byte() > 0 ? *peer.local_as_4_byte()
                                                    : *peer.local_as();
    out << "Local AS is " << local_as << ", ";
    out << "local router ID " << *details->local_router_id() << std::endl;
    out << "Local TCP address is " << myAddr << ", local port is "
        << ntohs(*details->local_port()) << std::endl;
    out << "Remote TCP address is " << peerAddr << ", remote port is "
        << ntohs(*details->peer_port()) << std::endl;

    out << "TCP connection-mode is " << enumNameSafe(*details->connect_mode())
        << std::endl;
    if (details->ttl_security_enabled().has_value() &&
        *details->ttl_security_enabled() &&
        details->ttl_security_hops().has_value()) {
      out << "TTL Security (GTSM): enabled, hops: "
          << *details->ttl_security_hops() << std::endl;
    }
    if (auto flaps = *details->num_of_flaps()) {
      out << "Number of session terminations: " << flaps << std::endl;
    }

    if (const auto lastResetHoldTimer = *peer.lastResetHoldTimer()) {
      printTimestamp(lastResetHoldTimer, "HoldTimer last reset at", out);
    }
    if (const auto lastSentKeepAlive = *peer.lastSentKeepAlive()) {
      printTimestamp(lastSentKeepAlive, "KeepAlive last received at", out);
    }
    if (const auto lastRcvdKeepAlive = *peer.lastRcvdKeepAlive()) {
      printTimestamp(lastRcvdKeepAlive, "KeepAlive last sent at", out);
    }
    if (peerState == TBgpPeerState::ESTABLISHED) {
      if (const auto eorSentTime = *details->eor_sent_time()) {
        printTimestamp(eorSentTime, "EOR Sent at", out);
      } else {
        out << "EOR not yet sent" << std::endl;
      }
      if (const auto eorRcvdTime = *details->eor_received_time()) {
        printTimestamp(eorRcvdTime, "EOR Received at", out);
      } else {
        out << "EOR not yet received" << std::endl;
      }
    }
    if (neighbors.size() > 1) {
      out << "------------------------------------------------------------------"
          << std::endl;
    }
    out << std::endl;
    ++neighborIndex;
  }
}

void printRIBEntries(
    std::ostream& out,
    TRibEntryWithHost& data,
    const bool detail,
    const std::string& originator,
    bool fbnetsource) {
  out << kRibEntryMarkersLegend << std::endl;
  out << "Acronyms: ASP - AS Path, LP - Local Preference, LBW - Link Bandwidth, LM - Last Modified"
      << std::endl;
  auto& entries = *data.tRibEntries();
  sortEntries(entries);
  const HostInfo hostInfo(
      *data.host(), *data.oobName(), folly::IPAddress(*data.ip()));
  const auto& prefMnemonics = getLocalPrefMnemonics(hostInfo);

  for (const auto& entry : entries) {
    const auto& bestGroup = entry.best_group().value();
    const auto& bestNextHop = entry.best_next_hop().value();
    const bool entryHasBestPath = hasBestPath(entry);
    int totalPaths = 0, ecmpPaths = 0, inactivePaths = 0;
    std::vector<std::string> pathsToPrint;

    for (const auto& [group, paths] : entry.paths().value()) {
      // Accounting for ecmp and total paths
      const bool isEcmpPath = (group == bestGroup);
      totalPaths += paths.size();
      if (isEcmpPath) {
        ecmpPaths += paths.size();
      }

      for (const auto& path : paths) {
        const auto& nextHop = path.next_hop().value();

        // If the TRibEntry contains a best path as indicated by bgp++'s RIB
        // then use best_path indicator; otherwise infer from best_next_hop on
        // the TBgpPath obj. This only applies for ECMP paths.
        const bool bestEcmpPath = entryHasBestPath
            ? path.is_best_path().value_or(false)
            : (isEcmpPath && bestNextHop == nextHop);

        const bool isInactivePath = path.is_inactive().value_or(false);
        if (isInactivePath) {
          ++inactivePaths;
        }

        std::string marker = (isEcmpPath) ? "*" : " ";
        marker += (bestEcmpPath) ? "@" : " ";
        marker += (isInactivePath) ? "!" : " ";

        std::string peerIdStr = "--";
        if (const auto& peerId = apache::thrift::get_pointer(path.peer_id())) {
          peerIdStr = IPAddress::fromBinary(
                          folly::ByteRange(
                              folly::StringPiece(peerId->prefix_bin().value())))
                          .str();
        }

        std::string peerDescriptionStr =
            path.peer_description() ? *path.peer_description() : "--";

        std::string asPathStr = "[]";
        const auto& asPath = path.as_path().value();
        if (!asPath.empty()) {
          asPathStr = printAsPath(asPath);
        }

        // Router or Originator ID - Set the following to originator id
        // if populated (i.e. route-reflector case) else use router-id.
        std::string routerOriginatorIdStr = "--";
        if (const auto& originatorId =
                apache::thrift::get_pointer(path.originator_id())) {
          routerOriginatorIdStr =
              IPAddress::fromLongHBO((uint32_t)*originatorId).str();
        } else if (
            const auto& routerId =
                apache::thrift::get_pointer(path.router_id())) {
          routerOriginatorIdStr =
              IPAddress::fromLongHBO((uint32_t)*routerId).str();
        }

        if (!originator.empty() && originator != routerOriginatorIdStr) {
          continue;
        }

        const auto& clusterList =
            apache::thrift::get_pointer(path.cluster_list());
        std::string clusterListStr = (clusterList && !clusterList->empty())
            ? fmt::format("[{}]", printClusterList(*clusterList))
            : "[]";

        const auto& nextHopStr = (folly::copy(nextHop.num_bits().value()) != 0)
            ? IPAddress::fromBinary(
                  folly::ByteRange(
                      folly::StringPiece(nextHop.prefix_bin().value())))
                  .str()
            : "SELF";

        const std::string originStr =
            (apache::thrift::get_pointer(path.origin()))
            ? formatBgpOrigin(*apache::thrift::get_pointer(path.origin()))
            : nettools::bgplib::kNullMessage;

        std::string lbwStr = "None";
        std::string extCommunitiesStr;
        if (const auto& extCommunities =
                apache::thrift::get_pointer(path.extCommunities())) {
          float linkBandwidth =
              getLinkBandwidthFromExtCommunities(*extCommunities);
          if (linkBandwidth > 0.0f) {
            lbwStr = utils::formatBandwidth(linkBandwidth);
          }
          extCommunitiesStr = printExtCommunities(*extCommunities);
        }

        std::string backupAddrStr;
        if (const auto* backupAddr =
                apache::thrift::get_pointer(path.backup_addr())) {
          backupAddrStr = fmt::format(
              " (backup: {})",
              IPAddress::fromBinary(
                  folly::ByteRange(
                      folly::StringPiece(*backupAddr->prefix_bin())))
                  .str());
        }

        std::string pathToPrint = fmt::format(
            "{} from {} ({}) via {}{} | LBW: {} | Origin: {} | "
            "LP: {} | ASP: {} | LM: {} | NH Weight: {} | MED: {} | ID: {} (rcvd) {} (sent) | Weight: {}{} | IgpCost: {}",
            marker,
            peerIdStr,
            peerDescriptionStr,
            nextHopStr,
            backupAddrStr,
            lbwStr,
            (originStr != nettools::bgplib::kNullMessage)
                ? originStr.substr(11) // Trim BGP_ORIGIN_ from origin string
                : originStr,
            printLocalPrefs(
                *apache::thrift::get_pointer(path.local_pref()), prefMnemonics),
            asPathStr,
            utils::getPrettyElapsedTime(
                folly::copy(path.last_modified_time().value()) / 1000000),
            path.next_hop_weight() ? std::to_string(*path.next_hop_weight())
                                   : "N/A",
            path.med().has_value()
                ? std::to_string(*path.med())
                : (path.multi_exit_disc().has_value()
                       ? std::to_string(*path.multi_exit_disc())
                       : "N/A"),
            path.path_id() ? std::to_string(*path.path_id()) : "N/A",
            path.path_id_to_send() ? std::to_string(*path.path_id_to_send())
                                   : "N/A",
            path.weight() ? std::to_string(*path.weight()) : "0",
            path.topologyInfo() ? fmt::format(
                                      " | {}",
                                      nettools::bgplib::BgpPathC::topoInfoToStr(
                                          *path.topologyInfo()))
                                : "",
            path.igp_cost() ? std::to_string(*path.igp_cost()) : "N/A");

        std::string communityToPrint;
        {
          const HostInfo hostInfo(
              *data.host(), *data.oobName(), folly::IPAddress(*data.ip()));
          communityToPrint = printCommunities(
              *apache::thrift::get_pointer(path.communities()), hostInfo);
        }

        if (detail) {
          pathToPrint += fmt::format(
              "\n    Router/Originator: {} | ClusterList: {}"
              "\n    Communities: {}",
              routerOriginatorIdStr,
              clusterListStr,
              communityToPrint);
          if (!extCommunitiesStr.empty()) {
            pathToPrint +=
                fmt::format("\n    ExtCommunities: {}", extCommunitiesStr);
          }

          const auto& filterDescrp =
              apache::thrift::get_pointer(path.bestpath_filter_descr());
          if (filterDescrp && !filterDescrp->empty()) {
            pathToPrint += fmt::format(
                "\n    BestPath Rejection Reason: {}", *filterDescrp);
          }
        }
        pathsToPrint.emplace_back(pathToPrint);
      }
    }
    const auto entryIp = IPAddress::fromBinary(
        folly::ByteRange(
            folly::StringPiece(entry.prefix().value().prefix_bin().value())));
    const auto prefix = fmt::format(
        "{}/{}",
        entryIp.str(),
        folly::copy(entry.prefix().value().num_bits().value()));
    // Check if path selection is pending. This can happen when IGP cost
    // has changed but best-path hasn't been recalculated yet. The "%" marker
    // indicates the path-selection results displayed are in a transient state.
    // Wait for the final result after the marker is cleared.
    std::string pendingMarker;
    if (entry.path_selection_pending().has_value() &&
        entry.path_selection_pending().value()) {
      pendingMarker = "%";
    }
    out << fmt::format(
        "\n>{} {}, Selected {}/{} paths ({} active, {} inactive)\n",
        pendingMarker,
        prefix,
        ecmpPaths,
        totalPaths,
        totalPaths - inactivePaths,
        inactivePaths);
    out << folly::join('\n', pathsToPrint) << std::endl;
    if (entry.fib_out().has_value()) {
      printFibOut(out, entry);
    }
  }
}

} // namespace facebook::fboss
