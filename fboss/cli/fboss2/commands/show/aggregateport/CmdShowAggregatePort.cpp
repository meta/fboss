/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowAggregatePort.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <unordered_map>
#include <unordered_set>
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fmt/format.h"
#include "folly/Conv.h"
#include "folly/IPAddress.h"
#include "folly/String.h"

namespace facebook::fboss {

using RetType = CmdShowAggregatePortTraits::RetType;

namespace {

cli::LacpEndpointEntry toLacpEndpointEntry(const LacpEndpoint& endpoint) {
  cli::LacpEndpointEntry entry;
  const auto& state = *endpoint.state();
  entry.systemPriority() = *endpoint.systemPriority();
  entry.systemID() = *endpoint.systemID();
  entry.key() = *endpoint.key();
  entry.portPriority() = *endpoint.portPriority();
  entry.port() = *endpoint.port();
  entry.activity() = *state.active() ? "Active" : "Passive";
  entry.timeout() = *state.shortTimeout() ? "Short" : "Long";
  entry.aggregatable() = *state.aggregatable();
  entry.inSync() = *state.inSync();
  entry.collecting() = *state.collecting();
  entry.distributing() = *state.distributing();
  entry.defaulted() = *state.defaulted();
  entry.expired() = *state.expired();
  return entry;
}

std::string formatLacpEndpoint(const cli::LacpEndpointEntry& endpoint) {
  std::vector<std::string> flags{
      endpoint.activity().value(),
      fmt::format("{} Timeout", endpoint.timeout().value())};
  auto addFlag = [&flags](bool set, const char* name) {
    if (set) {
      flags.emplace_back(name);
    }
  };
  addFlag(*endpoint.aggregatable(), "Aggregatable");
  addFlag(*endpoint.inSync(), "InSync");
  addFlag(*endpoint.collecting(), "Collecting");
  addFlag(*endpoint.distributing(), "Distributing");
  addFlag(*endpoint.defaulted(), "Defaulted");
  addFlag(*endpoint.expired(), "Expired");
  return fmt::format(
      "State: [{}], System: {}/{}, Key: {}, Port: {}/{}",
      folly::join(", ", flags),
      *endpoint.systemPriority(),
      *endpoint.systemID(),
      *endpoint.key(),
      *endpoint.portPriority(),
      *endpoint.port());
}

} // namespace

RetType CmdShowAggregatePort::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedPorts) {
  std::vector<facebook::fboss::AggregatePortThrift> entries;
  std::map<int32_t, facebook::fboss::PortInfoThrift> portInfo;
  std::map<int32_t, facebook::fboss::InterfaceDetail> interfaces;
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  client->sync_getAggregatePortTable(entries);
  client->sync_getAllPortInfo(portInfo);
  client->sync_getAllInterfaces(interfaces);

  // The agent throws when it runs without LACP (enable_lacp=false).
  std::optional<std::vector<facebook::fboss::LacpPartnerPair>> lacpPartnerPairs;
  try {
    std::vector<facebook::fboss::LacpPartnerPair> pairs;
    client->sync_getAllLacpPartnerPairs(pairs);
    lacpPartnerPairs = std::move(pairs);
  } catch (const thrift::FbossBaseError&) {
  }
  return createModel(
      entries, std::move(portInfo), interfaces, queriedPorts, lacpPartnerPairs);
}

void CmdShowAggregatePort::printOutput(
    const RetType& model,
    std::ostream& out) {
  for (const auto& entry : model.aggregatePortEntries().value()) {
    out << fmt::format("\nPort name: {}\n", entry.name().value());
    out << fmt::format("Description: {}\n", entry.description().value());
    if (entry.minMembersToUp().has_value()) {
      out << fmt::format(
          "Active members/Configured members/Min members: {}/{}/[{}, {}]\n",
          folly::copy(entry.activeMembers().value()),
          folly::copy(entry.configuredMembers().value()),
          folly::copy(entry.minMembers().value()),
          folly::copy(entry.minMembersToUp().value()));
    } else {
      out << fmt::format(
          "Active members/Configured members/Min members: {}/{}/{}\n",
          folly::copy(entry.activeMembers().value()),
          folly::copy(entry.configuredMembers().value()),
          folly::copy(entry.minMembers().value()));
    }
    if (*entry.lacpEnabled()) {
      out << fmt::format(
          "LACP: Enabled, System: {}/{}\n",
          *entry.systemPriority(),
          *entry.systemID());
    } else {
      out << "LACP: Disabled (static LAG)\n";
    }
    for (const auto& member : entry.members().value()) {
      out << fmt::format(
          "\t Member: {:>10}, id: {:>3}, Link: {:>5}, Fwding: {:>5}, Rate: {}{}\n",
          member.name().value(),
          folly::copy(member.id().value()),
          folly::copy(member.isLinkUp().value()) ? "Up" : "Down",
          folly::copy(member.isUp().value()) ? "True" : "False",
          member.lacpRate().value(),
          // Activity is meaningless without LACP running.
          *entry.lacpEnabled()
              ? fmt::format(", Mode: {}", member.lacpActivity().value())
              : "");
      if (member.actor().has_value()) {
        out << fmt::format(
            "\t\t Actor:   {}\n", formatLacpEndpoint(*member.actor()));
      }
      if (member.partner().has_value()) {
        out << fmt::format(
            "\t\t Partner: {}\n", formatLacpEndpoint(*member.partner()));
      }
    }
    if (!entry.rifs()->empty()) {
      out << "RIFs:\n";
      for (const auto& rif : entry.rifs().value()) {
        out << fmt::format(
            "\t OS Interface: {}, ID: {}\n",
            rif.osIfName().value(),
            rif.rifID().value());
        if (!rif.addresses()->empty()) {
          out << "\t Addresses:\n";
          for (const auto& addr : rif.addresses().value()) {
            out << fmt::format("\t\t {}\n", addr);
          }
        }
      }
    }
  }
}

RetType CmdShowAggregatePort::createModel(
    const std::vector<facebook::fboss::AggregatePortThrift>&
        aggregatePortEntries,
    std::map<int32_t, facebook::fboss::PortInfoThrift> portInfo,
    const std::map<int32_t, facebook::fboss::InterfaceDetail>& interfaces,
    const ObjectArgType& queriedPorts,
    const std::optional<std::vector<facebook::fboss::LacpPartnerPair>>&
        lacpPartnerPairs) {
  RetType model;
  std::unordered_set<std::string> queriedSet(
      queriedPorts.begin(), queriedPorts.end());

  // The actor's port number is the local PortID.
  std::unordered_map<int32_t, const LacpPartnerPair*> portToPartnerPair;
  if (lacpPartnerPairs.has_value()) {
    for (const auto& pair : *lacpPartnerPairs) {
      portToPartnerPair[*pair.localEndpoint()->port()] = &pair;
    }
  }

  for (const auto& entry : aggregatePortEntries) {
    const auto& portName = *entry.name();
    if (queriedPorts.empty() || queriedSet.count(portName)) {
      cli::AggregatePortEntry aggPortDetails;
      aggPortDetails.name() = portName;
      aggPortDetails.description() = *entry.description();
      aggPortDetails.minMembers() = *entry.minimumLinkCount();
      aggPortDetails.configuredMembers() = entry.memberPorts()->size();
      aggPortDetails.lacpEnabled() = lacpPartnerPairs.has_value();
      aggPortDetails.systemPriority() = *entry.systemPriority();
      aggPortDetails.systemID() = *entry.systemID();
      if (auto minLinkCountToUp = entry.minimumLinkCountToUp()) {
        aggPortDetails.minMembersToUp() = *minLinkCountToUp;
      }
      int32_t activeMemberCount = 0;
      for (const auto& subport : *entry.memberPorts()) {
        cli::AggregateMemberPortEntry memberDetails;
        memberDetails.id() = *subport.memberPortID();
        memberDetails.name() = *portInfo[*subport.memberPortID()].name();
        memberDetails.isUp() = *subport.isForwarding();
        memberDetails.isLinkUp() =
            *portInfo[*subport.memberPortID()].operState() == PortOperState::UP;
        memberDetails.lacpRate() =
            *subport.rate() == LacpPortRateThrift::FAST ? "Fast" : "Slow";
        memberDetails.lacpActivity() =
            *subport.activity() == LacpPortActivityThrift::ACTIVE ? "Active"
                                                                  : "Passive";
        auto pairIt = portToPartnerPair.find(*subport.memberPortID());
        if (pairIt != portToPartnerPair.end()) {
          memberDetails.actor() =
              toLacpEndpointEntry(*pairIt->second->localEndpoint());
          memberDetails.partner() =
              toLacpEndpointEntry(*pairIt->second->remoteEndpoint());
        }
        if (*subport.isForwarding()) {
          activeMemberCount++;
        }
        aggPortDetails.members()->push_back(memberDetails);
      }
      aggPortDetails.activeMembers() = activeMemberCount;

      // Find RIFs associated with this aggregate port by checking the first
      // member port
      if (!entry.memberPorts()->empty()) {
        auto firstMemberPortId = *entry.memberPorts()->front().memberPortID();
        auto firstMemberPortName = portInfo[firstMemberPortId].name();
        for (const auto& [rifId, intf] : interfaces) {
          if (!intf.portNames()->empty()) {
            for (const auto& intfPortName : *intf.portNames()) {
              if (intfPortName == firstMemberPortName) {
                cli::AggregatePortRifEntry rifEntry;
                rifEntry.rifID() = *intf.interfaceId();
                rifEntry.osIfName() =
                    fmt::format("fboss{}", *intf.interfaceId());
                for (const auto& addr : *intf.address()) {
                  auto ip = folly::IPAddress::fromBinary(
                      folly::ByteRange(
                          reinterpret_cast<const unsigned char*>(
                              addr.ip()->addr()->data()),
                          addr.ip()->addr()->size()));
                  rifEntry.addresses()->push_back(
                      folly::to<std::string>(
                          ip.str(), "/", *addr.prefixLength()));
                }
                aggPortDetails.rifs()->push_back(rifEntry);
                break;
              }
            }
          }
        }
      }

      model.aggregatePortEntries()->push_back(aggPortDetails);
    }
  }
  return model;
}

std::string_view CmdShowAggregatePortTraits::description() {
  return "Displays each link-aggregation group (port-channel): its description, active/configured/min member counts, whether LACP is enabled and its system priority/ID, per-member link and forwarding state and, when LACP is enabled, LACP mode (active/passive), per-member actor/partner LACP negotiation state, and the routed interfaces (RIFs) with their addresses. Use it to verify LAG membership, LACP negotiation, and health.";
}

CmdShowAggregatePort::RetType CmdShowAggregatePort::sampleModel() {
  RetType model;

  // Port-Channel5133
  cli::AggregatePortEntry entry1;
  entry1.name() = "Port-Channel5133";
  entry1.description() = "aggregate to peer001";
  entry1.activeMembers() = 2;
  entry1.configuredMembers() = 2;
  entry1.minMembers() = 2;
  entry1.lacpEnabled() = true;
  entry1.systemPriority() = 65535;
  entry1.systemID() = "02:00:00:00:00:01";

  cli::AggregateMemberPortEntry member1;
  member1.name() = "eth5/1/1";
  member1.id() = 72;
  member1.isLinkUp() = true;
  member1.isUp() = true;
  member1.lacpRate() = "Slow";
  member1.lacpActivity() = "Active";
  cli::LacpEndpointEntry actor1;
  actor1.systemPriority() = 65535;
  actor1.systemID() = "02:00:00:00:00:01";
  actor1.key() = 5133;
  actor1.portPriority() = 32768;
  actor1.port() = 72;
  actor1.activity() = "Active";
  actor1.timeout() = "Long";
  actor1.aggregatable() = true;
  actor1.inSync() = true;
  actor1.collecting() = true;
  actor1.distributing() = true;
  member1.actor() = actor1;
  cli::LacpEndpointEntry partner1 = actor1;
  partner1.systemID() = "02:00:00:00:00:02";
  partner1.key() = 1;
  partner1.port() = 11;
  partner1.activity() = "Passive";
  member1.partner() = partner1;
  entry1.members()->push_back(member1);

  cli::AggregateMemberPortEntry member2;
  member2.name() = "eth5/3/1";
  member2.id() = 91;
  member2.isLinkUp() = true;
  member2.isUp() = true;
  member2.lacpRate() = "Slow";
  member2.lacpActivity() = "Active";
  entry1.members()->push_back(member2);

  cli::AggregatePortRifEntry rif1;
  rif1.osIfName() = "fboss2049";
  rif1.rifID() = 2049;
  rif1.addresses()->push_back("2001:db8:c::1/127");
  rif1.addresses()->push_back("fe80::200:11ff:fe22:33aa/64");
  entry1.rifs()->push_back(rif1);

  model.aggregatePortEntries()->push_back(entry1);

  // Port-Channel5143
  cli::AggregatePortEntry entry2;
  entry2.name() = "Port-Channel5143";
  entry2.description() = "aggregate to peer002";
  entry2.activeMembers() = 2;
  entry2.configuredMembers() = 2;
  entry2.minMembers() = 2;
  entry2.lacpEnabled() = true;
  entry2.systemPriority() = 65535;
  entry2.systemID() = "02:00:00:00:00:01";

  cli::AggregateMemberPortEntry member3;
  member3.name() = "eth5/5/1";
  member3.id() = 106;
  member3.isLinkUp() = true;
  member3.isUp() = true;
  member3.lacpRate() = "Slow";
  member3.lacpActivity() = "Active";
  entry2.members()->push_back(member3);

  cli::AggregateMemberPortEntry member4;
  member4.name() = "eth5/7/1";
  member4.id() = 123;
  member4.isLinkUp() = true;
  member4.isUp() = true;
  member4.lacpRate() = "Slow";
  member4.lacpActivity() = "Active";
  entry2.members()->push_back(member4);

  cli::AggregatePortRifEntry rif2;
  rif2.osIfName() = "fboss2053";
  rif2.rifID() = 2053;
  rif2.addresses()->push_back("2001:db8:c:2::1/127");
  rif2.addresses()->push_back("fe80::200:11ff:fe22:33aa/64");
  entry2.rifs()->push_back(rif2);

  model.aggregatePortEntries()->push_back(entry2);

  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowAggregatePort, CmdShowAggregatePortTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowAggregatePort, CmdShowAggregatePortTraits>::getValidFilters();

} // namespace facebook::fboss
