/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/CmdShowInterface.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/Table.h"

#include <re2/re2.h>

namespace {

std::string getRif(const std::string& name, int32_t port) {
  if (port != 0 &&
      (name.rfind("eth", 0) == 0 || name.rfind("evt", 0) == 0 ||
       name.rfind("hyp", 0) == 0)) {
    return "fboss" + std::to_string(port);
  }
  return "";
}

} // anonymous namespace

namespace facebook::fboss {

CmdShowInterface::RetType CmdShowInterface::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);

  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::map<int32_t, facebook::fboss::InterfaceDetail> intfDetails;

  client->sync_getAllPortInfo(portEntries);
  client->sync_getAllInterfaces(intfDetails);

  // Get DSF nodes data
  std::map<int64_t, cfg::DsfNode> dsfNodes;
  try {
    std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;
    client->sync_getSwitchIdToSwitchInfo(switchIdToSwitchInfo);
    isVoq =
        (cfg::SwitchType::VOQ ==
         facebook::fboss::utils::getSwitchType(switchIdToSwitchInfo));
    if (isVoq) {
      client->sync_getDsfNodes(dsfNodes);
    }
  }
  // Thrift exception is expected to be thrown for switches which do not yet
  // have the API getSwitchIdToSwitchInfo implemented.
  catch (const std::exception& e) {
    XLOG(DBG2) << e.what();
  }

  return createModel(hostInfo, portEntries, intfDetails, dsfNodes, queriedIfs);
}

CmdShowInterface::RetType CmdShowInterface::createModel(
    const HostInfo& hostInfo,
    const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
    std::map<int32_t, facebook::fboss::InterfaceDetail> intfDetails,
    const std::map<int64_t, cfg::DsfNode>& dsfNodes,
    const ObjectArgType& queriedIfs) {
  RetType model;
  std::unordered_set<std::string> queriedSet(
      queriedIfs.begin(), queriedIfs.end());
  std::unordered_map<int32_t, int32_t> vlanToMtu;
  std::unordered_map<int32_t, std::vector<cli::IpPrefix>> vlanToPrefixes;

  populateVlanToMtu(vlanToMtu, intfDetails);
  populateVlanToPrefixes(vlanToPrefixes, intfDetails);

  std::optional<int32_t> localSysPortOffset, globalSysPortOffset;

  // Convert localhost to correct hostname
  std::string hostname = hostInfo.getName();
  auto hostnameOpt = utils::getMyHostname(hostname);
  if (hostnameOpt.has_value()) {
    hostname = hostnameOpt.value();
  } else {
    std::cerr << "Could not retrieve hostname." << std::endl;
  }

  for (const auto& [id, node] : dsfNodes) {
    if (hostname == utils::removeFbDomains(*node.name())) {
      if (node.localSystemPortOffset().has_value()) {
        localSysPortOffset = *node.localSystemPortOffset();
      }
      if (node.globalSystemPortOffset().has_value()) {
        globalSysPortOffset = *node.globalSystemPortOffset();
      }
      break;
    }
  }

  for (const auto& [portId, portInfo] : portEntries) {
    if (queriedIfs.size() == 0 || queriedSet.count(*portInfo.name())) {
      cli::Interface ifModel;
      const auto operState = *portInfo.operState();
      ifModel.name() = *portInfo.name();
      ifModel.description() = *portInfo.description();
      ifModel.status() =
          (operState == facebook::fboss::PortOperState::UP) ? "up" : "down";
      ifModel.speed() = std::to_string(*portInfo.speedMbps() / 1000) + "G";
      ifModel.prefixes() = {};
      ifModel.portType() = *portInfo.portType();
      ifModel.scope() = *portInfo.scope();

      // We assume that there is a one-to-one association between
      // port, interface, and VLAN, or that an ingress VLAN is configured.
      if (portInfo.vlans()->size() == 1) {
        const auto& vlan = portInfo.vlans()->at(0);
        ifModel.vlan() = vlan;
        if (vlanToMtu.contains(vlan)) {
          ifModel.mtu() = vlanToMtu[vlan];
        }
        if (vlanToPrefixes.contains(vlan)) {
          ifModel.prefixes() = vlanToPrefixes[vlan];
        }
      } else if (portInfo.vlans()->empty()) {
        if (auto ingressVlan = portInfo.ingressVlan()) {
          ifModel.vlan() = *ingressVlan;
        }
        if (vlanToMtu.contains(*portInfo.portId())) {
          ifModel.mtu() = vlanToMtu[*portInfo.portId()];
        }
        if (vlanToPrefixes.contains(*portInfo.portId())) {
          ifModel.prefixes() = vlanToPrefixes[*portInfo.portId()];
        }
      }

      if (localSysPortOffset.has_value() && globalSysPortOffset.has_value()) {
        // TODO factor in portId range for this switchId. Needed for
        // multi-asic systems
        int systemPortId =
            (portInfo.scope() == cfg::Scope::GLOBAL ? *globalSysPortOffset
                                                    : *localSysPortOffset) +
            portId;
        ifModel.systemPortId() = systemPortId;

        // Extract IP addresses for DSF switches
        auto intf = intfDetails.find(systemPortId);
        if (intf != intfDetails.end()) {
          for (auto ipPrefix : *intf->second.address()) {
            cli::IpPrefix prefix;
            prefix.ip() = folly::IPAddress::fromBinary(
                              folly::ByteRange(
                                  reinterpret_cast<const unsigned char*>(
                                      ipPrefix.ip()->addr()->data()),
                                  ipPrefix.ip()->addr()->size()))
                              .str();
            prefix.prefixLength() = *ipPrefix.prefixLength();
            ifModel.prefixes()->push_back(std::move(prefix));
          }
        }
      }

      model.interfaces()->push_back(std::move(ifModel));
    }
  }

  sortByInterfaceName(model);

  return model;
}

void CmdShowInterface::populateVlanToMtu(
    std::unordered_map<int32_t, int32_t>& vlanToMtu,
    const std::map<int32_t, facebook::fboss::InterfaceDetail>& intfDetails) {
  for (const auto& [intfId, intfDetail] : intfDetails) {
    // NO_VLAN = -1 in switch config
    if (*intfDetail.vlanId() <= 0) {
      vlanToMtu[*intfDetail.portId()] = *intfDetail.mtu();
    } else {
      vlanToMtu[*intfDetail.vlanId()] = *intfDetail.mtu();
    }
  }
}

void CmdShowInterface::populateVlanToPrefixes(
    std::unordered_map<int32_t, std::vector<cli::IpPrefix>>& vlanToPrefixes,
    const std::map<int32_t, facebook::fboss::InterfaceDetail>& intfDetails) {
  for (const auto& [intfId, intfDetail] : intfDetails) {
    if (intfDetail.remoteIntfType() == RemoteInterfaceType::DYNAMIC_ENTRY) {
      continue; // Only search for local interfaces
    }

    const auto& vlan = (*intfDetail.vlanId() <= 0) ? *intfDetail.portId()
                                                   : *intfDetail.vlanId();
    for (const auto& ifAddr : *intfDetail.address()) {
      cli::IpPrefix prefix;
      prefix.ip() = folly::IPAddress::fromBinary(
                        folly::ByteRange(
                            reinterpret_cast<const unsigned char*>(
                                ifAddr.ip()->addr()->data()),
                            ifAddr.ip()->addr()->size()))
                        .str();
      prefix.prefixLength() = *ifAddr.prefixLength();
      vlanToPrefixes[vlan].emplace_back(std::move(prefix));
    }
  }
}

void CmdShowInterface::sortByInterfaceName(RetType& model) {
  std::unordered_map<std::string, PortPosition> nameToPortPosition;
  std::vector<std::string> portNames;
  for (const auto& intf : *model.interfaces()) {
    portNames.emplace_back(*intf.name());
  }
  populateNameToPortPositionMap(nameToPortPosition, portNames);

  std::sort(
      model.interfaces()->begin(),
      model.interfaces()->end(),
      [&nameToPortPosition](cli::Interface& a, cli::Interface b) {
        if (nameToPortPosition.contains(*a.name()) &&
            nameToPortPosition.contains(*b.name())) {
          const auto& aPos = nameToPortPosition[*a.name()];
          const auto& bPos = nameToPortPosition[*b.name()];
          if (aPos.linecard != bPos.linecard) {
            return aPos.linecard < bPos.linecard;
          }
          if (aPos.mod != bPos.mod) {
            return aPos.mod < bPos.mod;
          }
          return aPos.port < bPos.port;
        }
        return *a.name() < *b.name();
      });
}

void CmdShowInterface::populateNameToPortPositionMap(
    std::unordered_map<std::string, PortPosition>& nameToPortPosition,
    const std::vector<std::string>& names) {
  re2::RE2 ethPortMatcher("eth([0-9]+)/([0-9]+)/([0-9]+)");
  for (const auto& name : names) {
    std::string tLinecard, tMod, tPort;
    if (re2::RE2::FullMatch(name, ethPortMatcher, &tLinecard, &tMod, &tPort)) {
      nameToPortPosition[name] = PortPosition{
          folly::to<int32_t>(tLinecard),
          folly::to<int32_t>(tMod),
          folly::to<int32_t>(tPort)};
    }
  }
}

void CmdShowInterface::printOutput(const RetType& model, std::ostream& out) {
  Table outTable{true};

  if (isVoq) {
    outTable.setHeader({
        "Interface",
        "RIF",
        "Status",
        "Speed",
        "VLAN",
        "MTU",
        "Addresses",
        "Description",
    });
  } else {
    outTable.setHeader({
        "Interface",
        "Status",
        "Speed",
        "VLAN",
        "MTU",
        "Addresses",
        "Description",
    });
  }

  bool foundInbandPort = false;

  for (const auto& interface : *model.interfaces()) {
    std::string name = *interface.name();
    std::vector<std::string> prefixes;

    if (interface.portType() != cfg::PortType::FABRIC_PORT) {
      for (const auto& prefix : *interface.prefixes()) {
        prefixes.push_back(
            fmt::format("{}/{}", *prefix.ip(), *prefix.prefixLength()));
      }
    }

    // Tag first global recycle port as the inband port
    auto description = *interface.description();
    if (!foundInbandPort &&
        interface.portType() == cfg::PortType::RECYCLE_PORT &&
        interface.scope() == cfg::Scope::GLOBAL) {
      if (!description.empty() && !description.ends_with("\n")) {
        description += "\n";
      }
      description += "Inband port";
      foundInbandPort = true;
    }

    std::vector<Table::RowData> row;
    if (isVoq) {
      outTable.addRow(
          {name,
           getRif(name, *interface.systemPortId()),
           colorStatusCell(*interface.status()),
           *interface.speed(),
           (interface.vlan() ? std::to_string(*interface.vlan()) : ""),
           (interface.mtu() ? std::to_string(*interface.mtu()) : ""),
           (prefixes.size() > 0 ? folly::join("\n", prefixes) : ""),
           description});
    } else {
      outTable.addRow(
          {name,
           colorStatusCell(*interface.status()),
           *interface.speed(),
           (interface.vlan() ? std::to_string(*interface.vlan()) : ""),
           (interface.mtu() ? std::to_string(*interface.mtu()) : ""),
           (prefixes.size() > 0 ? folly::join("\n", prefixes) : ""),
           description});
    }
  }
  out << outTable << std::endl;
}

Table::StyledCell CmdShowInterface::colorStatusCell(const std::string& status) {
  Table::Style cellStyle = Table::Style::NONE;
  if (status == "up") {
    cellStyle = Table::Style::GOOD;
  }
  return Table::StyledCell(status, cellStyle);
}

std::string_view CmdShowInterfaceTraits::description() {
  return "Displays each interface's operational status, speed, VLAN, MTU, configured IP addresses, and description. Use it for a quick overview of a port's configuration and addressing.";
}

CmdShowInterface::RetType CmdShowInterface::sampleModel() {
  RetType model;

  cli::Interface if1;
  if1.name() = "eth1/1/1";
  if1.status() = "up";
  if1.speed() = "400G";
  if1.vlan() = 2001;
  if1.mtu() = 9000;
  cli::IpPrefix prefix1_1;
  prefix1_1.ip() = "2001:db8:a::3f";
  prefix1_1.prefixLength() = 127;
  cli::IpPrefix prefix1_2;
  prefix1_2.ip() = "fe80::200:11ff:fe22:3301";
  prefix1_2.prefixLength() = 64;
  if1.prefixes() = {prefix1_1, prefix1_2};
  if1.description() = "neighbor peer001 on eth1/1/1";

  cli::Interface if2;
  if2.name() = "eth1/2/1";
  if2.status() = "up";
  if2.speed() = "200G";
  if2.vlan() = 2003;
  if2.mtu() = 9000;
  cli::IpPrefix prefix2_1;
  prefix2_1.ip() = "2001:db8:b::1";
  prefix2_1.prefixLength() = 127;
  cli::IpPrefix prefix2_2;
  prefix2_2.ip() = "fe80::200:11ff:fe22:3302";
  prefix2_2.prefixLength() = 64;
  if2.prefixes() = {prefix2_1, prefix2_2};
  if2.description() = "neighbor peer002 on eth1/2/1";

  model.interfaces() = {if1, if2};
  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowInterface, CmdShowInterfaceTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowInterface, CmdShowInterfaceTraits>::getValidFilters();

} // namespace facebook::fboss
