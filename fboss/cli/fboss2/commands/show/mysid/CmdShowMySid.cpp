// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/mysid/CmdShowMySid.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/show/route/utils.h"

#include <unordered_map>

namespace facebook::fboss {

namespace {

// Helper struct for indexing ARP/NDP entries by IP and interface ID
struct IpIntfKey {
  std::string ip;
  int32_t intfId;

  bool operator<(const IpIntfKey& other) const {
    if (ip != other.ip) {
      return ip < other.ip;
    }
    return intfId < other.intfId;
  }
};

// Helper function to build IP+Interface to PortDescriptor map from ARP/NDP
// tables
std::map<IpIntfKey, cfg::PortDescriptor> buildIpIntfToPortDescMap(
    const std::vector<ArpEntryThrift>& arpTable,
    const std::vector<NdpEntryThrift>& ndpTable) {
  std::map<IpIntfKey, cfg::PortDescriptor> ipIntfToPortDesc;

  // Lambda to process entries from both ARP and NDP tables
  auto processEntries = [&](const auto& table) {
    for (const auto& entry : table) {
      auto ip = network::toIPAddress(entry.ip().value()).str();
      int32_t intfId = entry.interfaceID().value();
      ipIntfToPortDesc[{ip, intfId}] = entry.portDescriptor().value();
    }
  };

  processEntries(arpTable);
  processEntries(ndpTable);

  return ipIntfToPortDesc;
}

// Helper function to build port ID to port name map
std::unordered_map<int32_t, std::string> buildPortIdToNameMap(
    const std::map<int32_t, PortInfoThrift>& portInfo) {
  std::unordered_map<int32_t, std::string> portIdToName;
  for (const auto& [portId, pInfo] : portInfo) {
    portIdToName[portId] = pInfo.name().value();
  }
  return portIdToName;
}

// Helper function to build aggregate port ID to name map
std::unordered_map<int32_t, std::string> buildAggPortIdToNameMap(
    const std::vector<AggregatePortThrift>& aggregatePorts) {
  std::unordered_map<int32_t, std::string> aggPortIdToName;
  for (const auto& aggPort : aggregatePorts) {
    aggPortIdToName[aggPort.key().value()] = aggPort.name().value();
  }
  return aggPortIdToName;
}

// A link local address is ambiguous without the interface it was learned on,
// so scope it the way the kernel tooling does.
std::string getNextHopIpStr(const network::thrift::BinaryAddress& addr) {
  auto ip = network::toIPAddress(addr);
  auto ifNamePtr = apache::thrift::get_pointer(addr.ifName());
  if (ifNamePtr != nullptr && ip.isV6() && ip.asV6().isLinkLocal()) {
    return fmt::format("{}@{}", ip.str(), *ifNamePtr);
  }
  return ip.str();
}

// Helper function to get port name from port descriptor
std::string getPortNameFromDescriptor(
    const cfg::PortDescriptor& portDesc,
    const std::unordered_map<int32_t, std::string>& portIdToName,
    const std::unordered_map<int32_t, std::string>& aggPortIdToName,
    const std::string& ifName = "") {
  int32_t portId = portDesc.portId().value();
  auto portType = portDesc.portType().value();
  std::string portName;

  switch (portType) {
    case cfg::PortDescriptorType::Aggregate: {
      auto aggPortIt = aggPortIdToName.find(portId);
      if (aggPortIt != aggPortIdToName.end()) {
        portName = aggPortIt->second;
      } else {
        portName = fmt::format("{}", portId);
      }
      break;
    }
    case cfg::PortDescriptorType::Physical: {
      auto portNameIt = portIdToName.find(portId);
      if (portNameIt != portIdToName.end()) {
        portName = portNameIt->second;
      } else {
        portName = fmt::format("{}", portId);
      }
      break;
    }
    case cfg::PortDescriptorType::SystemPort:
      throw std::runtime_error(
          "SystemPort type not supported for MySid egress resolution");
  }

  // Append interface name in parentheses if available
  if (!ifName.empty()) {
    portName += fmt::format(", {}", ifName);
  }

  return portName;
}

// Reuses the route utils' renderer so an SRv6 nexthop's SID list reads the
// same here as under `show namednexthopgroups`.
std::string getSrv6SidListStr(const NextHopThrift& nh) {
  cli::NextHopInfo nextHopInfo;
  show::route::utils::getNextHopInfoThrift(nh, nextHopInfo);
  return show::route::utils::getSrv6SidListStr(nextHopInfo);
}

// Render one resolved nexthop (primary or backup), including egress port
// information when the nexthop resolves via a known ARP/NDP entry.
std::string formatResolvedNextHopStr(
    const NextHopThrift& nh,
    const std::map<IpIntfKey, cfg::PortDescriptor>& ipIntfToPortDesc,
    const std::unordered_map<int32_t, std::string>& portIdToName,
    const std::unordered_map<int32_t, std::string>& aggPortIdToName) {
  auto nhIp = network::toIPAddress(nh.address().value()).str();

  // Try to get interface name from the address if available
  bool found = false;
  cfg::PortDescriptor portDesc;
  std::string ifNameStr;
  auto ifNamePtr = apache::thrift::get_pointer(nh.address().value().ifName());
  if (ifNamePtr != nullptr) {
    ifNameStr = *ifNamePtr;
    // Address has interface name, extract interface ID
    // Format is typically "fboss<id>" or just "<id>"
    std::string ifName = *ifNamePtr;
    // Remove "fboss" prefix if present
    if (ifName.find("fboss") == 0) {
      ifName = ifName.substr(5);
    }
    try {
      int32_t intfId = folly::to<int32_t>(ifName);
      auto key = IpIntfKey{nhIp, intfId};
      auto it = ipIntfToPortDesc.find(key);
      if (it != ipIntfToPortDesc.end()) {
        portDesc = it->second;
        found = true;
      }
    } catch (const std::exception&) {
      // Failed to parse interface ID
    }
  }

  auto sidListStr = getSrv6SidListStr(nh);
  if (found) {
    // Found in ARP/NDP table, get port name using helper function
    std::string portName = getPortNameFromDescriptor(
        portDesc, portIdToName, aggPortIdToName, ifNameStr);
    return fmt::format("{} via {}{}", nhIp, portName, sidListStr);
  }
  // No egress port to show, so scope a link local address to its
  // interface instead.
  return fmt::format("{}{}", getNextHopIpStr(nh.address().value()), sidListStr);
}

} // namespace

CmdShowMySid::RetType CmdShowMySid::queryClient(const HostInfo& hostInfo) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::vector<MySidEntry> entries;
  client->sync_getMySidEntries(entries);

  // Fetch ARP and NDP tables to resolve egress ports for next hops
  std::vector<ArpEntryThrift> arpTable;
  std::vector<NdpEntryThrift> ndpTable;
  client->sync_getArpTable(arpTable);
  client->sync_getNdpTable(ndpTable);

  // Fetch port info and aggregate ports to resolve port names
  std::map<int32_t, PortInfoThrift> portInfo;
  std::vector<AggregatePortThrift> aggregatePorts;
  client->sync_getAllPortInfo(portInfo);
  client->sync_getAggregatePortTable(aggregatePorts);

  return createModel(entries, arpTable, ndpTable, portInfo, aggregatePorts);
}

CmdShowMySid::RetType CmdShowMySid::createModel(
    const std::vector<MySidEntry>& entries,
    const std::vector<ArpEntryThrift>& arpTable,
    const std::vector<NdpEntryThrift>& ndpTable,
    const std::map<int32_t, PortInfoThrift>& portInfo,
    const std::vector<AggregatePortThrift>& aggregatePorts) {
  RetType model;

  // Build maps using helper functions
  auto ipIntfToPortDesc = buildIpIntfToPortDescMap(arpTable, ndpTable);
  auto portIdToName = buildPortIdToNameMap(portInfo);
  auto aggPortIdToName = buildAggPortIdToNameMap(aggregatePorts);

  for (const auto& entry : entries) {
    cli::MySidEntryModel entryModel;

    // Format prefix as addr/len
    const auto& mySid = entry.mySid().value();
    auto prefixAddr = network::toIPAddress(mySid.prefixAddress().value()).str();
    entryModel.prefix() =
        prefixAddr + "/" + std::to_string(mySid.prefixLength().value());

    entryModel.type() =
        apache::thrift::util::enumNameSafe(entry.type().value());

    // Only show nexthop for NODE and BINDING type mysids
    switch (entry.type().value()) {
      case MySidType::NODE_MICRO_SID:
      case MySidType::BINDING_MICRO_SID:
        for (const auto& nh : entry.nextHops().value()) {
          entryModel.nextHops()->push_back(
              fmt::format(
                  "{}{}",
                  getNextHopIpStr(nh.address().value()),
                  getSrv6SidListStr(nh)));
        }
        break;
      case MySidType::ADJACENCY_MICRO_SID:
      case MySidType::DECAPSULATE_AND_LOOKUP:
        // Do not show nexthop for these types
        break;
    }
    // Continue showing resolvedNextHops for all if present
    // Include egress port information if available
    for (const auto& nh : entry.resolvedNextHops().value()) {
      entryModel.resolvedNextHops()->push_back(formatResolvedNextHopStr(
          nh, ipIntfToPortDesc, portIdToName, aggPortIdToName));
    }
    // Show backup (FRR standby) resolved nexthops separately from primaries
    for (const auto& nh : entry.backupResolvedNextHops().value()) {
      entryModel.backupResolvedNextHops()->push_back(formatResolvedNextHopStr(
          nh, ipIntfToPortDesc, portIdToName, aggPortIdToName));
    }

    model.mySidEntries()->push_back(std::move(entryModel));
  }
  return model;
}

void CmdShowMySid::printOutput(const RetType& model, std::ostream& out) {
  for (const auto& entry : model.mySidEntries().value()) {
    out << fmt::format(
        "MySid: {}  Type: {}\n", entry.prefix().value(), entry.type().value());
    if (!entry.nextHops()->empty()) {
      out << "\tDestination:\n";
      for (const auto& nh : entry.nextHops().value()) {
        out << fmt::format("\t\t{}\n", nh);
      }
    }
    if (!entry.resolvedNextHops()->empty()) {
      out << "\tResolved via:\n";
      for (const auto& nh : entry.resolvedNextHops().value()) {
        out << fmt::format("\t\t{}\n", nh);
      }
    }
    if (!entry.backupResolvedNextHops()->empty()) {
      out << "\tBackup resolved via:\n";
      for (const auto& nh : entry.backupResolvedNextHops().value()) {
        out << fmt::format("\t\t{}\n", nh);
      }
    }
  }
}

std::string_view CmdShowMySidTraits::description() {
  return "Displays the SRv6 MySID table: each locally-instantiated SID prefix, its behavior/type, and (when resolved) the nexthop link-local and egress interface. Use it to inspect SRv6 segment endpoints.";
}

CmdShowMySid::RetType CmdShowMySid::sampleModel() {
  RetType model;

  // First entry: fdad:ffff:0001::/48 ADJACENCY_MICRO_SID (no resolved nexthop)
  cli::MySidEntryModel entry1;
  entry1.prefix() = "fdad:ffff:0001::/48";
  entry1.type() = "ADJACENCY_MICRO_SID";
  entry1.nextHops() = std::vector<std::string>();
  entry1.resolvedNextHops() = std::vector<std::string>();
  entry1.backupResolvedNextHops() = std::vector<std::string>();
  model.mySidEntries()->push_back(entry1);

  // Second entry: fdad:ffff:0002::/48 ADJACENCY_MICRO_SID (with resolved
  // nexthop)
  cli::MySidEntryModel entry2;
  entry2.prefix() = "fdad:ffff:0002::/48";
  entry2.type() = "ADJACENCY_MICRO_SID";
  entry2.nextHops() = std::vector<std::string>();
  entry2.resolvedNextHops() = std::vector<std::string>{
      "fe80::200:11ff:fe22:3301 via Port-Channel914, fboss2006"};
  entry2.backupResolvedNextHops() = std::vector<std::string>();
  model.mySidEntries()->push_back(entry2);

  // Binding sid: an unresolved nexthop and a resolved one, both carrying the
  // SRv6 SID list they encap with.
  cli::MySidEntryModel entryBinding;
  entryBinding.prefix() = "fdad:ffff:0003::/48";
  entryBinding.type() = "BINDING_MICRO_SID";
  entryBinding.nextHops() = std::vector<std::string>{
      "fe80::200:11ff:fe22:3303@fboss2007 SRv6 SID List [fdad:ffff:7fff::]"};
  entryBinding.resolvedNextHops() = std::vector<std::string>{
      "fe80::200:11ff:fe22:3303 via Port-Channel915, fboss2007 SRv6 SID List [fdad:ffff:7fff::]"};
  entryBinding.backupResolvedNextHops() = std::vector<std::string>{
      "fe80::200:11ff:fe22:3304 via Port-Channel916, fboss2008 SRv6 SID List [fdad:ffff:7fff::]"};
  model.mySidEntries()->push_back(entryBinding);

  // Third entry: fdad:ffff:7fff::/48 DECAPSULATE_AND_LOOKUP (no resolved
  // nexthop)
  cli::MySidEntryModel entry3;
  entry3.prefix() = "fdad:ffff:7fff::/48";
  entry3.type() = "DECAPSULATE_AND_LOOKUP";
  entry3.nextHops() = std::vector<std::string>();
  entry3.resolvedNextHops() = std::vector<std::string>();
  entry3.backupResolvedNextHops() = std::vector<std::string>();
  model.mySidEntries()->push_back(entry3);

  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowMySid, CmdShowMySidTraits>::run();

} // namespace facebook::fboss
