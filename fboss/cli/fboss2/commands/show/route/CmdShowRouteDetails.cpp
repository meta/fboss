/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/route/CmdShowRouteDetails.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"

#include "fboss/agent/AddressUtil.h"

namespace facebook::fboss {

std::string CmdShowRouteDetails::parseRootPort(const std::string& str) {
  /*
  This function parses out the root port from the port name. Example:
  Port name: eth1/16/1
  rootPort: eth1
   */
  std::string port;
  size_t pos = str.find('/');
  if (pos != std::string::npos) {
    port = str.substr(0, pos);
  }
  return port;
}

void CmdShowRouteDetails::populateAggregatePortMap(
    const std::unique_ptr<apache::thrift::Client<FbossCtrl>>& client) {
  std::map<int32_t, PortInfoThrift> portInfoEntries;
  client->sync_getAllPortInfo(portInfoEntries);

  std::vector<::facebook::fboss::AggregatePortThrift> aggregatePortThrift;
  client->sync_getAggregatePortTable(aggregatePortThrift);

  for (auto aggregatePort : aggregatePortThrift) {
    std::string aggPortName = *aggregatePort.name();
    for (auto memberPort : *aggregatePort.memberPorts()) {
      auto memberPortID = memberPort.memberPortID().value();
      auto it = portInfoEntries.find(memberPortID);
      if (it != portInfoEntries.end()) {
        auto vlans = it->second.vlans();
        // If L3 routing with multiple vlans, we can skip this port
        if (vlans->size() != 1) {
          continue;
        }
        this->vlanAggregatePortMap[std::to_string(vlans[0])] = aggPortName;
      }
    }
  }
}

void CmdShowRouteDetails::populateVlanPortMap(
    const std::unique_ptr<apache::thrift::Client<FbossCtrl>>& client) {
  std::map<int32_t, PortInfoThrift> portInfoEntries;
  client->sync_getAllPortInfo(portInfoEntries);

  for (const auto& portInfo : portInfoEntries) {
    if ((portInfo.second.vlans()->size() == 0) ||
        (portInfo.second.vlans()->size() > 1)) {
      continue;
    }
    auto vlan = std::to_string(portInfo.second.vlans()[0]);
    auto portName = portInfo.second.name().value();
    auto rootPort = parseRootPort(portName);
    vlanPortMap[vlan][rootPort].push_back(portName);
  }
}

CmdShowRouteDetails::RetType CmdShowRouteDetails::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedRoutes) {
  std::vector<facebook::fboss::RouteDetails> entries;
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  client->sync_getRouteTableDetails(entries);
  populateAggregatePortMap(client);
  populateVlanPortMap(client);
  // queriedRoutes can take 2 forms, ip address or network address
  // Treat the address as IP only if no mask is provided. Lookup the
  // network address for this IP and add it to a new list for output
  std::vector<std::string> finalRoutes;
  std::transform(
      queriedRoutes.begin(),
      queriedRoutes.end(),
      std::back_inserter(finalRoutes),
      [&client](std::string queryRoute) -> std::string {
        if (queryRoute.find('/') == std::string::npos) {
          facebook::fboss::RouteDetails route;
          auto addr =
              facebook::network::toAddress(folly::IPAddress(queryRoute));
          client->sync_getIpRouteDetails(route, addr, 0);
          if (route.nextHopMulti().value().size() > 0) {
            auto ipStr = utils::getAddrStr(*route.dest()->ip());
            auto ipPrefix =
                ipStr + "/" + std::to_string(*route.dest()->prefixLength());
            return ipPrefix;
          }
        }
        return queryRoute;
      });

  ObjectArgType finalQueriedRoutes(finalRoutes);
  auto model = createModel(entries, finalQueriedRoutes);
  if (const auto policies = getRunningBgpPolicies(hostInfo)) {
    if (const auto encoding = getNsfTeWeightEncoding(*policies)) {
      model.nsfTeWeightEncoding() = *encoding;
    }
  }
  return model;
}

void CmdShowRouteDetails::printOutput(const RetType& model, std::ostream& out) {
  for (const auto& entry : model.routeEntries().value()) {
    out << fmt::format(
        "\nNetwork Address: {}/{}{}\n",
        entry.ip().value(),
        folly::copy(entry.prefixLength().value()),
        folly::copy(entry.isConnected().value()) ? " (connected)" : "");

    for (const auto& clAndNxthops : entry.nextHopMulti().value()) {
      auto clientId = static_cast<ClientID>(*clAndNxthops.clientId());
      auto clientName = apache::thrift::util::enumNameSafe(clientId);
      auto prefix =
          folly::copy(clAndNxthops.isPreferred().value()) ? "> " : "  ";
      out << fmt::format(
          "{}Client: {} (Admin Distance: {})\n",
          prefix,
          clientName,
          clAndNxthops.adminDistance().value());
      if (clAndNxthops.namedNextHopGroup().has_value()) {
        out << fmt::format(
            "      Named Next Hop Group: {}\n",
            *clAndNxthops.namedNextHopGroup());
      }
      if (clAndNxthops.clientNextHopSetID().has_value()) {
        out << fmt::format(
            "      Client NextHop Set ID: {}\n",
            clAndNxthops.clientNextHopSetID().value());
      }
      out << "      Nexthops:\n";
      for (const auto& nextHop : clAndNxthops.nextHops().value()) {
        out << fmt::format(
            "        {}\n",
            show::route::utils::getNextHopInfoStr(
                nextHop, model.nsfTeWeightEncoding().to_optional()));
      }
      out << fmt::format(
          "      Counter Id: {}\n", clAndNxthops.counterID().value());
      out << fmt::format(
          "      Class Id: {}\n", clAndNxthops.classID().value());
    }

    out << fmt::format("  Action: {}\n", entry.action().value());

    auto printNextHops = [this, &out, &model](
                             const std::string& header,
                             const auto& nextHops,
                             bool isOverride,
                             const auto& nhToTopoInfo) {
      out << fmt::format("  {}\n", header);
      std::string overrideStr = (isOverride ? "(override) :" : "");
      const bool isFpf = show::route::utils::isFpfEncoding(
          model.nsfTeWeightEncoding().to_optional());
      std::map<int, int> planeIdToPathCount;
      std::map<int, int> stswIdToPathCount;
      for (const auto& nextHop : nextHops) {
        out << fmt::format(
            "  {}  {}\n",
            overrideStr,
            show::route::utils::getNextHopInfoStr(
                nextHop,
                vlanAggregatePortMap,
                vlanPortMap,
                model.nsfTeWeightEncoding().to_optional()));

        auto it = nhToTopoInfo.find(nextHop.addr().value());
        if (it != nhToTopoInfo.end()) {
          const auto& topologyInfo = it->second;
          if (isFpf) {
            if (topologyInfo.spine_id().has_value()) {
              stswIdToPathCount[topologyInfo.spine_id().value()]++;
            }
          } else if (topologyInfo.plane_id().has_value()) {
            planeIdToPathCount[topologyInfo.plane_id().value()]++;
          }
        }
      }
      if (isFpf) {
        if (stswIdToPathCount.size() > 0) {
          out << fmt::format("  Paths per stsw:\n");
          for (const auto& [stswId, pathCount] : stswIdToPathCount) {
            out << fmt::format("    Stsw {}: {}\n", stswId, pathCount);
          }
        }
      } else if (planeIdToPathCount.size() > 0) {
        out << fmt::format("  Paths per plane:\n");
        for (const auto& [planeId, pathCount] : planeIdToPathCount) {
          out << fmt::format("    Plane {}: {}\n", planeId, pathCount);
        }
      }
    };
    auto& nextHops = entry.nextHops().value();
    auto& nhToTopoInfo = entry.nhAddressToTopologyInfo().value();
    if (nextHops.size() > 0) {
      std::string header =
          (entry.overridenNextHops() ? "Original next hops:"
                                     : "Forwarding via:");
      printNextHops(header, nextHops, false /*isOverride*/, nhToTopoInfo);
    } else if (!entry.overridenNextHops().has_value()) {
      out << "  No Forwarding Info\n";
    }
    if (entry.overridenNextHops()) {
      if (entry.overridenNextHops()->size()) {
        printNextHops(
            "Forwarding via:",
            *entry.overridenNextHops(),
            true /*isOverride*/,
            nhToTopoInfo);
      } else if (!entry.overridenNextHops().has_value()) {
        out << "  No Forwarding Info\n";
      }
      out << fmt::format(
          " Num next hops lost: {}\n",
          nextHops.size() - entry.overridenNextHops()->size());
    }

    out << fmt::format(
        "  Overridden ECMP mode: {}\n", entry.overridenEcmpMode().value());
    if (entry.resolvedNextHopSetID().has_value()) {
      out << fmt::format(
          "  Resolved NextHop Set ID: {}\n",
          entry.resolvedNextHopSetID().value());
    }
    if (entry.normalizedResolvedNextHopSetID().has_value()) {
      out << fmt::format(
          "  Normalized Resolved NextHop Set ID: {}\n",
          entry.normalizedResolvedNextHopSetID().value());
    }
  }
}

CmdShowRouteDetails::RetType CmdShowRouteDetails::createModel(
    std::vector<facebook::fboss::RouteDetails>& routeEntries,
    const ObjectArgType& queriedRoutes) {
  RetType model;
  std::unordered_set<std::string> queriedSet(
      queriedRoutes.begin(), queriedRoutes.end());

  for (const auto& entry : routeEntries) {
    auto ipStr = utils::getAddrStr(*entry.dest()->ip());
    auto ipPrefix = ipStr + "/" + std::to_string(*entry.dest()->prefixLength());
    if (queriedRoutes.size() == 0 || queriedSet.count(ipPrefix)) {
      cli::RouteDetailEntry routeDetails;
      routeDetails.ip() = ipStr;
      routeDetails.prefixLength() = *entry.dest()->prefixLength();
      routeDetails.action() = *entry.action();
      routeDetails.isConnected() = *entry.isConnected();
      // Map to hold address to topologyInfo from client (NextHopsMulti
      // fields)
      std::map<std::string, NetworkTopologyInformation> nhToTopoInfo;

      auto& nextHopMulti = entry.nextHopMulti().value();
      for (const auto& clAndNxthops : nextHopMulti) {
        cli::ClientAndNextHops clAndNxthopsCli;
        clAndNxthopsCli.clientId() =
            folly::copy(clAndNxthops.clientId().value());
        if (clAndNxthops.clientNextHopSetID().has_value()) {
          clAndNxthopsCli.clientNextHopSetID() =
              clAndNxthops.clientNextHopSetID().value();
        }
        auto& nextHopAddrs = clAndNxthops.nextHopAddrs().value();
        auto& nextHops = clAndNxthops.nextHops().value();
        if (nextHopAddrs.size() > 0) {
          for (const auto& address : nextHopAddrs) {
            cli::NextHopInfo nextHopInfo;
            show::route::utils::getNextHopInfoAddr(address, nextHopInfo);
            clAndNxthopsCli.nextHops()->emplace_back(nextHopInfo);
          }
        } else if (nextHops.size() > 0) {
          for (const auto& nextHop : nextHops) {
            cli::NextHopInfo nextHopInfo;
            show::route::utils::getNextHopInfoThrift(nextHop, nextHopInfo);
            clAndNxthopsCli.nextHops()->emplace_back(nextHopInfo);
            auto topologyInfo =
                apache::thrift::get_pointer(nextHop.topologyInfo());
            if (topologyInfo) {
              nhToTopoInfo[facebook::network::toIPAddress(*nextHop.address())
                               .str()] = *topologyInfo;
            }
          }
        }
        if (clAndNxthops.namedRouteDestination().has_value() &&
            clAndNxthops.namedRouteDestination()->getType() ==
                NamedRouteDestination::Type::nextHopGroup) {
          clAndNxthopsCli.namedNextHopGroup() =
              *clAndNxthops.namedRouteDestination()->nextHopGroup_ref();
        }
        auto adminDistPtr =
            apache::thrift::get_pointer(clAndNxthops.adminDistance());
        clAndNxthopsCli.adminDistance() = adminDistPtr == nullptr
            ? "None"
            : std::to_string(static_cast<int>(*adminDistPtr));
        auto isPreferredPtr =
            apache::thrift::get_pointer(clAndNxthops.isPreferred());
        clAndNxthopsCli.isPreferred() =
            isPreferredPtr != nullptr && *isPreferredPtr;
        auto counterIDPtr =
            apache::thrift::get_pointer(clAndNxthops.counterID());
        clAndNxthopsCli.counterID() =
            counterIDPtr == nullptr ? "None" : *counterIDPtr;
        auto classIDPtr = apache::thrift::get_pointer(clAndNxthops.classID());
        clAndNxthopsCli.classID() =
            classIDPtr == nullptr ? "None" : getClassID(*classIDPtr);
        routeDetails.nextHopMulti()->emplace_back(clAndNxthopsCli);
      }

      routeDetails.nhAddressToTopologyInfo() = nhToTopoInfo;

      auto& fwdInfo = *entry.fwdInfo();
      auto& nextHops = entry.nextHops().value();
      const auto resolvedNextHops =
          entry.resolvedNextHops().value_or(std::vector<NextHopThrift>());

      auto findPreNormalizationWeight =
          [&resolvedNextHops](const NextHopThrift& nextHop) {
            auto normalizedComparable = nextHop;
            normalizedComparable.weight() = 0;
            normalizedComparable.topologyInfo().reset();
            for (auto resolvedNextHop : resolvedNextHops) {
              const auto resolvedWeight = *resolvedNextHop.weight();
              resolvedNextHop.weight() = 0;
              resolvedNextHop.topologyInfo().reset();
              if (normalizedComparable == resolvedNextHop) {
                return std::optional<int32_t>(resolvedWeight);
              }
            }
            return std::optional<int32_t>();
          };

      if (nextHops.size() > 0) {
        bool showPreNormalizationWeights = false;
        for (const auto& nextHop : nextHops) {
          cli::NextHopInfo nextHopInfo;
          show::route::utils::getNextHopInfoThrift(nextHop, nextHopInfo);
          if (auto preNormalizationWeight = findPreNormalizationWeight(nextHop);
              preNormalizationWeight.has_value()) {
            nextHopInfo.preNormalizationWeight() = *preNormalizationWeight;
            showPreNormalizationWeights |=
                (*preNormalizationWeight == 0 ? 1 : *preNormalizationWeight) !=
                (*nextHop.weight() == 0 ? 1 : *nextHop.weight());
          }
          routeDetails.nextHops()->emplace_back(nextHopInfo);
        }
        if (!showPreNormalizationWeights) {
          for (auto& nextHopInfo : *routeDetails.nextHops()) {
            nextHopInfo.preNormalizationWeight().reset();
          }
        }
      } else if (fwdInfo.size() > 0) {
        for (const auto& ifAndIp : fwdInfo) {
          cli::NextHopInfo nextHopInfo;
          nextHopInfo.interfaceID() =
              folly::copy(ifAndIp.interfaceID().value());
          show::route::utils::getNextHopInfoAddr(
              ifAndIp.ip().value(), nextHopInfo);
          routeDetails.nextHops()->emplace_back(nextHopInfo);
        }
      }

      if (entry.overridenNextHops().has_value()) {
        routeDetails.overridenNextHops() = std::vector<cli::NextHopInfo>();
        for (const auto& nextHop : *entry.overridenNextHops()) {
          cli::NextHopInfo nextHopInfo;
          show::route::utils::getNextHopInfoThrift(nextHop, nextHopInfo);
          routeDetails.overridenNextHops()->emplace_back(nextHopInfo);
        }
        routeDetails.nhopsLostDueToOverride() =
            nextHops.size() - entry.overridenNextHops()->size();
      }

      auto adminDistancePtr =
          apache::thrift::get_pointer(entry.adminDistance());
      routeDetails.adminDistance() = adminDistancePtr == nullptr
          ? "None"
          : std::to_string(static_cast<int>(*adminDistancePtr));

      auto counterIDPtr = apache::thrift::get_pointer(entry.counterID());
      routeDetails.counterID() =
          counterIDPtr == nullptr ? "None" : *counterIDPtr;

      auto classIDPtr = apache::thrift::get_pointer(entry.classID());
      routeDetails.classID() =
          classIDPtr == nullptr ? "None" : getClassID(*classIDPtr);
      if (entry.namedRouteDestination().has_value() &&
          entry.namedRouteDestination()->getType() ==
              NamedRouteDestination::Type::nextHopGroup) {
        routeDetails.namedNextHopGroup() =
            *entry.namedRouteDestination()->nextHopGroup_ref();
      }

      auto overrideEcmpModePtr =
          apache::thrift::get_pointer(entry.overridenEcmpMode());
      routeDetails.overridenEcmpMode() = overrideEcmpModePtr == nullptr
          ? "None"
          : apache::thrift::util::enumNameSafe(*overrideEcmpModePtr);
      if (entry.resolvedNextHopSetID().has_value()) {
        routeDetails.resolvedNextHopSetID() = *entry.resolvedNextHopSetID();
      }
      if (entry.normalizedResolvedNextHopSetID().has_value()) {
        routeDetails.normalizedResolvedNextHopSetID() =
            *entry.normalizedResolvedNextHopSetID();
      }
      model.routeEntries()->push_back(routeDetails);
    }
  }
  return model;
}

std::string CmdShowRouteDetails::getClassID(cfg::AclLookupClass classID) {
  int classId = static_cast<int>(classID);
  switch (classID) {
    case cfg::AclLookupClass::DST_CLASS_L3_LOCAL_1:
      return fmt::format("DST_CLASS_L3_LOCAL_1({})", classId);
    case cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2:
      return fmt::format("DST_CLASS_L3_LOCAL_2({})", classId);
    case cfg::AclLookupClass::CLASS_DROP:
      return fmt::format("CLASS_DROP({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_0({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_1({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_2({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_3({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_4({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_5:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_5({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_6:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_6({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_7:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_7({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_8:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_8({})", classId);
    case cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_9:
      return fmt::format("CLASS_QUEUE_PER_HOST_QUEUE_9({})", classId);
    case cfg::AclLookupClass::DST_CLASS_L3_DPR:
      return fmt::format("DST_CLASS_L3_DPR({})", classId);
    case cfg::AclLookupClass::DEPRECATED_CLASS_UNRESOLVED_ROUTE_TO_CPU:
      return fmt::format("CLASS_UNRESOLVED_ROUTE_TO_CPU({})", classId);
    case cfg::AclLookupClass::DEPRECATED_CLASS_CONNECTED_ROUTE_TO_INTF:
      return fmt::format("CLASS_CONNECTED_ROUTE_TO_INTF({})", classId);
    default:
      break;
  }
  throw std::runtime_error(
      "Unsupported ClassID: " + std::to_string(static_cast<int>(classID)));
}

std::string_view CmdShowRouteDetailsTraits::description() {
  return "Displays the full routing-table entry for each prefix: the advertising client and admin distance, the ECMP nexthop set with weights, counter/class IDs, and the resolved forwarding nexthops with egress interfaces. Use it to inspect how a route is programmed and forwarded.";
}

CmdShowRouteDetails::RetType CmdShowRouteDetails::sampleModel() {
  RetType model;

  cli::RouteDetailEntry entry;
  entry.ip() = "2001:db8:101c::";
  entry.prefixLength() = 47;
  entry.action() = "Nexthops";
  entry.isConnected() = false;
  entry.adminDistance() = "None";
  entry.counterID() = "None";
  entry.classID() = "None";
  entry.overridenEcmpMode() = "None";

  cli::ClientAndNextHops clientAndNH;
  clientAndNH.clientId() = 0;
  clientAndNH.adminDistance() = "20";
  clientAndNH.isPreferred() = true;
  clientAndNH.counterID() = "None";
  clientAndNH.classID() = "None";

  cli::NextHopInfo nh1, nh2, nh3;
  nh1.addr() = "2001:db8:e03f:1af8::28";
  nh1.weight() = 8;
  nh2.addr() = "2001:db8:e03f:1af9::28";
  nh2.weight() = 7;
  nh3.addr() = "2001:db8:e03f:1afa::28";
  nh3.weight() = 8;

  clientAndNH.nextHops()->push_back(nh1);
  clientAndNH.nextHops()->push_back(nh2);
  clientAndNH.nextHops()->push_back(nh3);

  entry.nextHopMulti()->push_back(clientAndNH);

  cli::NextHopInfo fwd1, fwd2, fwd3;
  fwd1.addr() = "2001:db8:e03f:1af8::28";
  fwd1.weight() = 8;
  fwd1.ifName() = "eth9/1/1";
  fwd2.addr() = "2001:db8:e03f:1af9::28";
  fwd2.weight() = 7;
  fwd2.ifName() = "eth9/3/1";
  fwd3.addr() = "2001:db8:e03f:1afa::28";
  fwd3.weight() = 8;
  fwd3.ifName() = "eth9/5/1";

  entry.nextHops()->push_back(fwd1);
  entry.nextHops()->push_back(fwd2);
  entry.nextHops()->push_back(fwd3);

  model.routeEntries()->push_back(entry);

  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowRouteDetails, CmdShowRouteDetailsTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowRouteDetails, CmdShowRouteDetailsTraits>::getValidFilters();

} // namespace facebook::fboss
