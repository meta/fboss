// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/SocketAddress.h>
#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/if/gen-cpp2/fboss_types.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeUtils.h"
#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "nettools/ebb/platform/if/gen-cpp2/TeSrv6Agent_clients.h"
#include "nettools/ebb/platform/if/gen-cpp2/TeSrv6Agent_constants.h"

DECLARE_bool(enable_lacp);
DECLARE_bool(enable_route_counters_for_named_nhg);

namespace facebook::fboss {
namespace te = facebook::nettools::ebb::platform::fboss::thrift::srv6;
namespace {

constexpr size_t kExpectedLinkCount{8};
constexpr int kLinkUpRetries{60};
constexpr auto kLinkUpRetryInterval{std::chrono::seconds(5)};
constexpr int32_t kL3LoopInitialLeaseSeconds{60};
constexpr int32_t kL3LoopExtensionSeconds{300};
constexpr int64_t kL3LoopSustainSeconds{60};
constexpr auto kL3LoopSampleInterval{std::chrono::seconds(5)};
constexpr uint8_t kAnchorPrefixLength{128};
constexpr uint8_t kServicePrefixLength{64};
const std::string kSrv6TunnelId{"srv6Tunnel0"};
const std::string kNexthopGroupName{"lspgrp_tst-lab-test-class"};
const folly::IPAddressV6 kAnchorAddress{"fdad:face:b00c::1"};
const folly::IPAddressV6 kLinkSubnet{"fdad:face::"};
const folly::IPAddressV6 kServiceSubnet{"2800::"};

struct TestLink {
  std::string localPort;
  std::string remotePort;
  std::string remoteSystem;
  int32_t vlanId;
};

IpPrefix makePrefix(const folly::IPAddress& address, uint8_t prefixLength) {
  IpPrefix prefix;
  prefix.ip() = network::toBinaryAddress(address);
  prefix.prefixLength() = prefixLength;
  return prefix;
}

NextHopThrift makeNextHop(const folly::IPAddress& address) {
  NextHopThrift nextHop;
  nextHop.address() = network::toBinaryAddress(address);
  nextHop.weight() = ECMP_WEIGHT;
  return nextHop;
}

std::string toBinary(const folly::IPAddressV6& address) {
  const auto bytes = address.toByteArray();
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

te::IpPrefix makeTePrefix(
    const folly::IPAddressV6& address,
    uint8_t prefixLength) {
  te::IpPrefix prefix;
  prefix.prefix() = toBinary(address);
  prefix.length() = prefixLength;
  return prefix;
}

te::NexthopGroup makeNexthopGroup(
    const std::vector<folly::IPAddressV6>& nextHopAddresses,
    const folly::IPAddressV6& decapSid) {
  te::NexthopGroup nexthopGroup;
  nexthopGroup.nhgName() = kNexthopGroupName;
  nexthopGroup.bsid() = 0;
  nexthopGroup.primary()->reserve(nextHopAddresses.size());
  for (const auto& nextHopAddress : nextHopAddresses) {
    te::NexthopEntry entry;
    entry.container() = toBinary(decapSid);
    entry.nhIp() = toBinary(nextHopAddress);
    nexthopGroup.primary()->push_back(std::move(entry));
  }
  return nexthopGroup;
}

std::unique_ptr<apache::thrift::Client<te::TeSrv6AgentService>>
createTeSrv6AgentServiceClient(const std::string& hostname) {
  constexpr auto kPort = static_cast<uint16_t>(
      te::TeSrv6Agent_constants::DEFAULT_TE_SRV6_AGENT_PORT());
  return utils::tryCreateEncryptedClient<te::TeSrv6AgentService>(
      utils::ConnectionOptions(
          folly::SocketAddress{hostname, kPort, /*allowNameLookup=*/true}));
}

UnicastRoute makeRoute(
    const folly::IPAddress& address,
    uint8_t prefixLength,
    AdminDistance adminDistance,
    std::vector<NextHopThrift> nextHops) {
  UnicastRoute route;
  route.dest() = makePrefix(address, prefixLength);
  route.adminDistance() = adminDistance;
  route.nextHops() = std::move(nextHops);
  return route;
}

int64_t getCounter(
    const std::map<std::string, int64_t>& counters,
    const std::string& name) {
  const auto it = counters.find(name);
  return it == counters.end() ? 0 : it->second;
}

int64_t sumPortCounters(
    const std::map<std::string, int64_t>& counters,
    const std::vector<TestLink>& links,
    const std::string TestLink::* portName,
    const std::string& counterSuffix) {
  int64_t total{0};
  for (const auto& link : links) {
    total += getCounter(counters, (link.*portName) + counterSuffix);
  }
  return total;
}

int32_t getPortIdByName(
    const std::string& hostname,
    const std::string& portName) {
  const auto portIdToPortInfo = utility::getPortIdToPortInfo(hostname);
  const auto port = std::find_if(
      portIdToPortInfo.begin(),
      portIdToPortInfo.end(),
      [&portName](const auto& entry) {
        return *entry.second.name() == portName;
      });
  if (port == portIdToPortInfo.end()) {
    throw FbossError("No port ID found for port ", portName, " on ", hostname);
  }
  return port->first;
}

HwPortStats readPortStats(
    apache::thrift::Client<TestCtrl>& client,
    const std::string& portName) {
  std::map<std::string, HwPortStats> stats;
  client.sync_getHwPortStats(stats);
  return stats.at(portName);
}

bool intervalIsAtLineRate(
    const HwPortStats& before,
    const HwPortStats& after,
    int64_t speedMbps,
    const std::string& hostname) {
  const auto seconds = *after.timestamp_() - *before.timestamp_();
  if (seconds <= 0 || *before.inBytes_() < 0 || *before.outBytes_() < 0 ||
      *after.inBytes_() < *before.inBytes_() ||
      *after.outBytes_() < *before.outBytes_()) {
    return false; // Stale/uninitialized statistics or a counter reset.
  }
  const auto inMbps =
      (*after.inBytes_() - *before.inBytes_()) * 8.0 / seconds / 1000000;
  const auto outMbps =
      (*after.outBytes_() - *before.outBytes_()) * 8.0 / seconds / 1000000;
  XLOG(DBG2) << "L3 sustain sample on " << hostname << "/" << *after.portName_()
             << ": seconds=" << seconds << ", inMbps=" << inMbps
             << ", outMbps=" << outMbps;
  return inMbps >= speedMbps * 0.95 && outMbps >= speedMbps * 0.95;
}

void setupLoopEndpoint(
    apache::thrift::Client<TestCtrl>& client,
    const std::string& portName,
    const folly::IPAddressV6& qualificationAddress,
    const folly::IPAddressV6& peerNextHop,
    const std::string& peerMac) {
  client.sync_setupL3TrafficLoops(
      std::vector<network::thrift::BinaryAddress>{
          network::toBinaryAddress(qualificationAddress)},
      std::vector<std::string>{portName},
      std::vector<network::thrift::BinaryAddress>{
          network::toBinaryAddress(peerNextHop)},
      std::vector<std::string>{peerMac},
      std::vector<int32_t>{kL3LoopInitialLeaseSeconds});
}

void getReadyLoopStatus(
    apache::thrift::Client<TestCtrl>& client,
    const std::string& portName,
    L3TrafficLoopStatus& status) {
  std::map<std::string, L3TrafficLoopStatus> statuses;
  client.sync_getL3TrafficLoopStatus(
      statuses, std::vector<std::string>{portName});
  const auto it = statuses.find(portName);
  ASSERT_NE(it, statuses.end()) << "Missing loop status for " << portName;
  status = it->second;
  ASSERT_EQ(*status.state(), L3TrafficLoopState::READY) << portName;
  ASSERT_TRUE(status.expirationTimeMs().has_value()) << portName;
}

} // namespace

class AgentMultiNodeTeSrv6Test : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& /*ensemble*/) const override {
    return *AgentConfig::fromFile(FLAGS_config)->thrift.sw();
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SRV6_ENCAP,
        ProductionFeature::SRV6_DECAP,
    };
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_disable_neighbor_updates = false;
    FLAGS_enable_lacp = true;
    FLAGS_enable_lldp = true;
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_enable_route_counters_for_named_nhg = true;
    FLAGS_publish_stats_to_fsdb = true;
    FLAGS_resolve_nexthops_from_id = true;
    FLAGS_tun_intf = true;
  }

  bool isTeSrv6Config() const {
    const auto config = getSw()->getConfig();
    if (!config.srv6Tunnels().has_value()) {
      return false;
    }
    const auto& tunnels = *config.srv6Tunnels();
    return std::any_of(tunnels.begin(), tunnels.end(), [](const auto& tunnel) {
      return *tunnel.tunnelType() == TunnelType::SRV6_ENCAP;
    });
  }

  std::vector<TestLink> getTestLinks() const {
    std::vector<TestLink> links;
    const auto config = getSw()->getConfig();
    for (const auto& port : *config.ports()) {
      if (*port.state() != cfg::PortState::ENABLED ||
          *port.portType() != cfg::PortType::INTERFACE_PORT) {
        continue;
      }
      const auto& lldpValues = *port.expectedLLDPValues();
      const auto remotePort = lldpValues.find(cfg::LLDPTag::PORT);
      const auto remoteSystem = lldpValues.find(cfg::LLDPTag::SYSTEM_NAME);
      if (remotePort == lldpValues.end() || remoteSystem == lldpValues.end()) {
        continue;
      }
      links.push_back(
          TestLink{
              .localPort = *port.name(),
              .remotePort = remotePort->second,
              .remoteSystem = remoteSystem->second,
              .vlanId = *port.ingressVlan(),
          });
    }
    return links;
  }

  std::string getRemoteHostname(const std::vector<TestLink>& links) const {
    std::set<std::string> remoteHosts;
    for (const auto& link : links) {
      remoteHosts.insert(link.remoteSystem);
    }
    if (remoteHosts.size() != 1) {
      throw FbossError("Expected exactly one remote TE SRv6 switch");
    }
    return *remoteHosts.begin();
  }

  std::vector<folly::IPAddressV6> getRemoteLinkAddresses(
      const std::string& remoteHostname,
      const std::vector<TestLink>& links) const {
    std::map<int32_t, folly::IPAddressV6> vlanToAddress;
    for (const auto& [_, interface] :
         utility::getIntfIdToIntf(remoteHostname)) {
      for (const auto& prefix : *interface.address()) {
        const auto address = network::toIPAddress(*prefix.ip());
        if (address.isV6() && *prefix.prefixLength() == 64 &&
            address.inSubnet(kLinkSubnet, 32)) {
          vlanToAddress.emplace(*interface.vlanId(), address.asV6());
        }
      }
    }

    std::vector<folly::IPAddressV6> addresses;
    addresses.reserve(links.size());
    for (const auto& link : links) {
      const auto it = vlanToAddress.find(link.vlanId);
      if (it == vlanToAddress.end()) {
        throw FbossError("No remote IPv6 address for VLAN ", link.vlanId);
      }
      addresses.push_back(it->second);
    }
    return addresses;
  }

  std::string getInterfaceMac(
      const std::string& hostname,
      const folly::IPAddressV6& address) const {
    for (const auto& [_, interface] : utility::getIntfIdToIntf(hostname)) {
      for (const auto& prefix : *interface.address()) {
        if (network::toIPAddress(*prefix.ip()) == address) {
          return *interface.mac();
        }
      }
    }
    throw FbossError(
        "No interface MAC found for ", hostname, " address ", address);
  }

  folly::IPAddressV6 getRemoteServiceAddress(
      const std::string& remoteHostname) const {
    for (const auto& [_, interface] :
         utility::getIntfIdToIntf(remoteHostname)) {
      for (const auto& prefix : *interface.address()) {
        const auto address = network::toIPAddress(*prefix.ip());
        if (address.isV6() && *prefix.prefixLength() == 128 &&
            address.inSubnet(kServiceSubnet, 16)) {
          return address.asV6();
        }
      }
    }
    throw FbossError("No remote TE service address found");
  }

  folly::IPAddressV6 getRemoteDecapSid(
      apache::thrift::Client<TestCtrl>& remoteClient) const {
    std::vector<MySidEntry> entries;
    remoteClient.sync_getMySidEntries(entries);
    const auto entry =
        std::find_if(entries.begin(), entries.end(), [](const auto& candidate) {
          return *candidate.type() == MySidType::DECAPSULATE_AND_LOOKUP;
        });
    if (entry == entries.end()) {
      throw FbossError("Remote switch has no End.DT46 MySID");
    }
    return network::toIPAddress(*entry->mySid()->prefixAddress()).asV6();
  }

  bool linksAreUp(
      const std::string& localHostname,
      const std::string& remoteHostname,
      const std::vector<TestLink>& links,
      std::string& failureReason) const {
    failureReason.clear();
    std::set<std::string> localPorts;
    try {
      localPorts = getUpInterfacePorts(localHostname);
    } catch (const std::exception& error) {
      failureReason = "Failed to query interface ports on " + localHostname +
          ": " + error.what();
      throw;
    }

    std::set<std::string> remotePorts;
    try {
      remotePorts = getUpInterfacePorts(remoteHostname);
    } catch (const std::exception& error) {
      failureReason = "Failed to query interface ports on " + remoteHostname +
          ": " + error.what();
      throw;
    }

    for (const auto& link : links) {
      if (!localPorts.contains(link.localPort)) {
        failureReason = localHostname + ":" + link.localPort + " is down";
        return false;
      }
      if (!remotePorts.contains(link.remotePort)) {
        failureReason = remoteHostname + ":" + link.remotePort + " is down";
        return false;
      }
    }

    return aggregatePortsAreForwarding(localHostname, failureReason) &&
        aggregatePortsAreForwarding(remoteHostname, failureReason);
  }

  bool aggregatePortsAreForwarding(
      const std::string& hostname,
      std::string& failureReason) const {
    std::vector<AggregatePortThrift> aggregatePorts;
    try {
      auto client = utility::getSwAgentThriftClient(hostname);
      client->sync_getAggregatePortTable(aggregatePorts);
    } catch (const std::exception& error) {
      failureReason = "Failed to query aggregate ports on " + hostname + ": " +
          error.what();
      throw;
    }

    if (aggregatePorts.size() != kExpectedLinkCount) {
      failureReason = hostname + ": expected " +
          std::to_string(kExpectedLinkCount) + " aggregate ports, found " +
          std::to_string(aggregatePorts.size());
      return false;
    }
    for (const auto& port : aggregatePorts) {
      if (!*port.isUp()) {
        failureReason = hostname + ": an aggregate port is down";
        return false;
      }
      for (const auto& member : *port.memberPorts()) {
        if (!*member.isForwarding()) {
          failureReason = hostname + ": an aggregate member is not forwarding";
          return false;
        }
      }
    }
    return true;
  }

  std::set<std::string> getUpInterfacePorts(const std::string& hostname) const {
    std::set<std::string> ports;
    for (const auto& [_, port] : utility::getPortIdToPortInfo(hostname)) {
      if (*port.portType() == cfg::PortType::INTERFACE_PORT &&
          *port.operState() == PortOperState::UP) {
        ports.insert(*port.name());
      }
    }
    return ports;
  }

  bool serviceRouteUsesSid(
      apache::thrift::Client<TestCtrl>& client,
      const folly::IPAddressV6& serviceAddress,
      const folly::IPAddressV6& decapSid) const {
    std::vector<RouteDetails> routes;
    client.sync_getRouteTableDetails(routes);
    const auto route =
        std::find_if(routes.begin(), routes.end(), [&](const auto& candidate) {
          return network::toIPAddress(*candidate.dest()->ip()) ==
              serviceAddress.mask(kServicePrefixLength) &&
              *candidate.dest()->prefixLength() == kServicePrefixLength;
        });
    if (route == routes.end()) {
      return false;
    }
    return std::any_of(
        route->nextHops()->begin(),
        route->nextHops()->end(),
        [&](const auto& nextHop) {
          return nextHop.tunnelType() == TunnelType::SRV6_ENCAP &&
              nextHop.tunnelId() == kSrv6TunnelId &&
              nextHop.srv6SegmentList()->size() == 1 &&
              network::toIPAddress(nextHop.srv6SegmentList()->front()) ==
              decapSid;
        });
  }

  std::optional<te::NexthopGroupCounter> getEncapNexthopGroupCounter(
      apache::thrift::Client<te::TeSrv6AgentService>& client) const {
    te::GetNexthopGroupCountersRequest request;
    te::GetNexthopGroupCountersResponse response;
    client.sync_getNexthopGroupCounters(response, request);
    const auto counter = std::find_if(
        response.counters()->begin(),
        response.counters()->end(),
        [](const auto& entry) {
          return *entry.nhgName() == kNexthopGroupName;
        });
    if (counter == response.counters()->end()) {
      return std::nullopt;
    }
    return *counter;
  }
};

class AgentMultiNodeL3TrafficLoopTest : public AgentMultiNodeTeSrv6Test {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {};
  }

  bool qsfpProgrammingComplete(
      const std::string& hostname,
      const std::vector<TestLink>& links,
      const std::string TestLink::* portName,
      int64_t& firstReadySnapshotTime,
      std::string& failureReason) const {
    auto agentClient = utility::getSwAgentThriftClient(hostname);
    ConfigAppliedInfo configInfo;
    agentClient->sync_getConfigAppliedInfo(configInfo);
    auto qsfpClient = utility::getQsfpThriftClient(hostname);
    std::map<std::string, std::vector<int32_t>> portTransceivers;
    qsfpClient->sync_getPortTransceiverIDs(portTransceivers);
    std::set<int32_t> transceiverIds;
    for (const auto& link : links) {
      const auto& name = link.*portName;
      const auto it = portTransceivers.find(name);
      if (it == portTransceivers.end() || it->second.empty()) {
        firstReadySnapshotTime = 0;
        failureReason = hostname;
        failureReason.append(": no transceiver for ").append(name);
        return false;
      }
      transceiverIds.insert(it->second.begin(), it->second.end());
    }

    std::map<int32_t, TransceiverInfo> infos;
    qsfpClient->sync_getTransceiverInfo(
        infos,
        std::vector<int32_t>(transceiverIds.begin(), transceiverIds.end()));
    int64_t oldestSnapshotTime = std::numeric_limits<int64_t>::max();
    for (const auto id : transceiverIds) {
      const auto it = infos.find(id);
      if (it == infos.end()) {
        firstReadySnapshotTime = 0;
        failureReason = hostname + ": missing QSFP information";
        return false;
      }
      const auto& state = *it->second.tcvrState();
      auto machineState = state.stateMachineState();
      const bool programmed = machineState.has_value() &&
          (*machineState ==
               TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED ||
           *machineState == TransceiverStateMachineState::ACTIVE);
      // timeCollected is in seconds; the agent's config timestamp is in ms.
      if (!*state.present() || *state.fwUpgradeInProgress() || !programmed ||
          *state.timeCollected() * 1000 <= *configInfo.lastAppliedInMs()) {
        firstReadySnapshotTime = 0;
        failureReason = hostname;
        failureReason.append(": QSFP transceiver ")
            .append(std::to_string(id))
            .append(" has not finished programming the current config");
        return false;
      }
      oldestSnapshotTime = std::min(oldestSnapshotTime, *state.timeCollected());
    }

    // Observe a second fresh ready snapshot so a cached state from before
    // QSFP picks up the agent's config cannot immediately pass the check.
    if (firstReadySnapshotTime * 1000 <= *configInfo.lastAppliedInMs()) {
      firstReadySnapshotTime = oldestSnapshotTime;
    } else if (oldestSnapshotTime > firstReadySnapshotTime) {
      XLOG(DBG2) << "QSFP programming complete on " << hostname
                 << ": configAppliedMs=" << *configInfo.lastAppliedInMs()
                 << ", snapshotSeconds=" << oldestSnapshotTime;
      return true;
    }
    failureReason = hostname + ": waiting for a fresh ready QSFP snapshot";
    return false;
  }
};

TEST_F(AgentMultiNodeTeSrv6Test, VerifySetupAndBasicForwarding) {
  ASSERT_TRUE(isTeSrv6Config()) << "Requires the TE SRv6 multi-node config";

  const auto links = getTestLinks();
  ASSERT_EQ(links.size(), kExpectedLinkCount);
  const auto localHostname = *getSw()->getConfig().hostname();
  const auto remoteHostname = getRemoteHostname(links);

  std::string linkReadinessFailure;
  const auto linksUp = checkWithRetryErrorReturn(
      [&]() {
        return linksAreUp(
            localHostname, remoteHostname, links, linkReadinessFailure);
      },
      kLinkUpRetries,
      kLinkUpRetryInterval,
      true);
  ASSERT_TRUE(linksUp) << "Link readiness check failed: "
                       << linkReadinessFailure;

  auto localClient = utility::getSwAgentThriftClient(localHostname);
  auto remoteClient = utility::getSwAgentThriftClient(remoteHostname);
  const auto decapSid = getRemoteDecapSid(*remoteClient);
  const auto serviceAddress = getRemoteServiceAddress(remoteHostname);
  const auto remoteLinkAddresses =
      getRemoteLinkAddresses(remoteHostname, links);

  std::vector<NextHopThrift> openrNextHops;
  openrNextHops.reserve(remoteLinkAddresses.size());
  for (const auto& address : remoteLinkAddresses) {
    openrNextHops.push_back(makeNextHop(address));
  }

  const auto openrClient = static_cast<int16_t>(ClientID::OPENR);
  const auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto teSrv6Client = createTeSrv6AgentServiceClient(localHostname);
  const auto anchorPrefix = makePrefix(kAnchorAddress, kAnchorPrefixLength);
  const auto teAnchorPrefix = makeTePrefix(kAnchorAddress, kAnchorPrefixLength);

  localClient->sync_addUnicastRoutes(
      openrClient,
      {makeRoute(
          kAnchorAddress,
          kAnchorPrefixLength,
          AdminDistance::OPENR,
          std::move(openrNextHops))});
  localClient->sync_addUnicastRoutes(
      bgpClient,
      {makeRoute(
          serviceAddress.mask(kServicePrefixLength),
          kServicePrefixLength,
          AdminDistance::EBGP,
          {makeNextHop(kAnchorAddress)})});

  te::ProgramNexthopGroupsRequest nexthopGroupRequest;
  nexthopGroupRequest.nexthopGroups() = {
      makeNexthopGroup(remoteLinkAddresses, decapSid)};
  te::ProgramNexthopGroupsResponse nexthopGroupResponse;
  teSrv6Client->sync_programNexthopGroups(
      nexthopGroupResponse, nexthopGroupRequest);

  te::IpRoute teRoute;
  teRoute.prefix() = teAnchorPrefix;
  teRoute.nhgName() = kNexthopGroupName;
  te::ProgramIpRoutesRequest routeRequest;
  routeRequest.routes() = {std::move(teRoute)};
  te::ProgramIpRoutesResponse routeResponse;
  teSrv6Client->sync_programIpRoutes(routeResponse, routeRequest);

  ASSERT_TRUE(checkWithRetryErrorReturn(
      [&]() {
        te::GetNexthopGroupsRequest request;
        request.programmedOnly() = true;
        te::GetNexthopGroupsResponse response;
        teSrv6Client->sync_getNexthopGroups(response, request);
        return std::any_of(
            response.nexthopGroups()->begin(),
            response.nexthopGroups()->end(),
            [&](const auto& group) {
              return *group.nhgName() == kNexthopGroupName &&
                  group.primary()->size() == remoteLinkAddresses.size();
            });
      },
      30,
      std::chrono::seconds(1),
      true))
      << "SRv6 NHG was not programmed to hardware";

  ASSERT_TRUE(checkWithRetryErrorReturn(
      [&]() {
        return serviceRouteUsesSid(*localClient, serviceAddress, decapSid);
      },
      30,
      std::chrono::seconds(1),
      true));

  const auto localCountersBefore =
      utility::getCounterNameToCount(localHostname);
  const auto remoteCountersBefore =
      utility::getCounterNameToCount(remoteHostname);
  std::map<std::string, int64_t> remoteSwCountersBefore;
  remoteClient->sync_getCounters(remoteSwCountersBefore);
  const auto localOutBytesBefore = sumPortCounters(
      localCountersBefore, links, &TestLink::localPort, ".out_bytes.sum");
  const auto remoteInBytesBefore = sumPortCounters(
      remoteCountersBefore, links, &TestLink::remotePort, ".in_bytes.sum");
  std::optional<te::NexthopGroupCounter> localEncapCounterBefore;
  WITH_RETRIES({
    localEncapCounterBefore = getEncapNexthopGroupCounter(*teSrv6Client);
    ASSERT_EVENTUALLY_TRUE(localEncapCounterBefore.has_value());
  });
  const auto localEncapBytesBefore = *localEncapCounterBefore->bytes();
  const auto localEncapPacketsBefore = *localEncapCounterBefore->packets();
  const auto remoteHostRxBefore =
      getCounter(remoteSwCountersBefore, "host.rx.sum");

  const auto vlanId = getVlanIDForTx().value();
  const auto mac = getMacForFirstInterfaceWithPorts(getProgrammedState());
  WITH_RETRIES({
    for (uint16_t sourcePort = 10000; sourcePort < 10100; ++sourcePort) {
      sendPacketSwitchedAsync(
          utility::makeUDPTxPacket(
              getSw(),
              vlanId,
              mac,
              mac,
              folly::IPAddressV6{"1001::1"},
              serviceAddress,
              sourcePort,
              20000));
    }

    const auto localCounters = utility::getCounterNameToCount(localHostname);
    const auto remoteCounters = utility::getCounterNameToCount(remoteHostname);
    const auto localEncapCounter = getEncapNexthopGroupCounter(*teSrv6Client);
    std::map<std::string, int64_t> remoteSwCounters;
    remoteClient->sync_getCounters(remoteSwCounters);
    EXPECT_EVENTUALLY_GT(
        sumPortCounters(
            localCounters, links, &TestLink::localPort, ".out_bytes.sum"),
        localOutBytesBefore);
    EXPECT_EVENTUALLY_GT(
        sumPortCounters(
            remoteCounters, links, &TestLink::remotePort, ".in_bytes.sum"),
        remoteInBytesBefore);
    ASSERT_EVENTUALLY_TRUE(localEncapCounter.has_value());
    if (localEncapCounter.has_value()) {
      EXPECT_EVENTUALLY_GT(*localEncapCounter->bytes(), localEncapBytesBefore);
      EXPECT_EVENTUALLY_GT(
          *localEncapCounter->packets(), localEncapPacketsBefore);
    }
    EXPECT_EVENTUALLY_GT(
        getCounter(remoteSwCounters, "host.rx.sum"), remoteHostRxBefore);
  });
}

TEST_F(AgentMultiNodeL3TrafficLoopTest, VerifyThriftApiLifecycle) {
  const auto links = getTestLinks();
  ASSERT_FALSE(links.empty());

  const auto localHostname = *getSw()->getConfig().hostname();
  const auto remoteHostname = getRemoteHostname(links);
  const auto& link = links.front();
  std::string linkReadinessFailure;
  int64_t localReadySnapshotTime{0};
  int64_t remoteReadySnapshotTime{0};
  ASSERT_TRUE(checkWithRetryErrorReturn(
      [&]() {
        const bool localQsfpReady = qsfpProgrammingComplete(
            localHostname,
            links,
            &TestLink::localPort,
            localReadySnapshotTime,
            linkReadinessFailure);
        const bool remoteQsfpReady = qsfpProgrammingComplete(
            remoteHostname,
            links,
            &TestLink::remotePort,
            remoteReadySnapshotTime,
            linkReadinessFailure);
        return localQsfpReady && remoteQsfpReady &&
            linksAreUp(
                   localHostname, remoteHostname, links, linkReadinessFailure);
      },
      kLinkUpRetries,
      kLinkUpRetryInterval,
      true))
      << "Link readiness check failed: " << linkReadinessFailure;
  const std::vector<TestLink> loopLink{link};
  const auto localAddresses = getRemoteLinkAddresses(localHostname, loopLink);
  const auto remoteAddresses = getRemoteLinkAddresses(remoteHostname, loopLink);
  ASSERT_EQ(localAddresses.size(), 1);
  ASSERT_EQ(remoteAddresses.size(), 1);

  auto localClient = utility::getSwAgentThriftClient(localHostname);
  auto remoteClient = utility::getSwAgentThriftClient(remoteHostname);

  std::map<std::string, L3TrafficLoopStatus> localStatuses;
  std::map<std::string, L3TrafficLoopStatus> remoteStatuses;
  localClient->sync_getL3TrafficLoopStatus(localStatuses, {});
  remoteClient->sync_getL3TrafficLoopStatus(remoteStatuses, {});
  ASSERT_EQ(localStatuses.count(link.localPort), 0);
  ASSERT_EQ(remoteStatuses.count(link.remotePort), 0);

  const folly::IPAddressV6 qualificationAddress{"100::ffff"};
  const auto localPeerMac =
      getInterfaceMac(remoteHostname, remoteAddresses.front());
  const auto remotePeerMac =
      getInterfaceMac(localHostname, localAddresses.front());

  bool localConfigured{false};
  bool remoteConfigured{false};
  SCOPE_EXIT {
    if (remoteConfigured) {
      try {
        remoteClient->sync_teardownL3TrafficLoops(
            std::vector<std::string>{link.remotePort});
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Failed to tear down remote L3 traffic loop: "
                  << ex.what();
      }
    }
    if (localConfigured) {
      try {
        localClient->sync_teardownL3TrafficLoops(
            std::vector<std::string>{link.localPort});
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Failed to tear down local L3 traffic loop: " << ex.what();
      }
    }
  };

  localConfigured = true;
  setupLoopEndpoint(
      *localClient,
      link.localPort,
      qualificationAddress,
      remoteAddresses.front(),
      localPeerMac);
  remoteConfigured = true;
  setupLoopEndpoint(
      *remoteClient,
      link.remotePort,
      qualificationAddress,
      localAddresses.front(),
      remotePeerMac);

  L3TrafficLoopStatus localStatus;
  L3TrafficLoopStatus remoteStatus;
  ASSERT_NO_FATAL_FAILURE(
      getReadyLoopStatus(*localClient, link.localPort, localStatus));
  ASSERT_NO_FATAL_FAILURE(
      getReadyLoopStatus(*remoteClient, link.remotePort, remoteStatus));
  const auto localExpiration = *localStatus.expirationTimeMs();
  const auto remoteExpiration = *remoteStatus.expirationTimeMs();

  EXPECT_THROW(
      localClient->sync_extendL3TrafficLoops(
          std::vector<std::string>{link.localPort, "not-configured"},
          kL3LoopExtensionSeconds),
      thrift::FbossBaseError);
  ASSERT_NO_FATAL_FAILURE(
      getReadyLoopStatus(*localClient, link.localPort, localStatus));
  EXPECT_EQ(*localStatus.expirationTimeMs(), localExpiration);

  localClient->sync_extendL3TrafficLoops(
      std::vector<std::string>{link.localPort}, kL3LoopExtensionSeconds);
  remoteClient->sync_extendL3TrafficLoops(
      std::vector<std::string>{link.remotePort}, kL3LoopExtensionSeconds);
  ASSERT_NO_FATAL_FAILURE(
      getReadyLoopStatus(*localClient, link.localPort, localStatus));
  ASSERT_NO_FATAL_FAILURE(
      getReadyLoopStatus(*remoteClient, link.remotePort, remoteStatus));
  ASSERT_GT(*localStatus.expirationTimeMs(), localExpiration);
  ASSERT_GT(*remoteStatus.expirationTimeMs(), remoteExpiration);

  // A positive Hop Limit stays unchanged when TTL decrement is disabled.
  auto txPacket = utility::makeUDPTxPacket(
      getSw(),
      std::nullopt,
      folly::MacAddress(remotePeerMac),
      folly::MacAddress(localPeerMac),
      localAddresses.front(),
      qualificationAddress,
      8000,
      8001,
      0, // Traffic class.
      255, // IPv6 Hop Limit.
      std::vector<uint8_t>(1200, 0xff));
  auto* packetBuffer = txPacket->buf();
  packetBuffer->coalesce();
  const auto l3PayloadOffset = static_cast<size_t>(EthHdr::UNTAGGED_PKT_SIZE);
  ASSERT_GT(packetBuffer->length(), l3PayloadOffset);
  const folly::fbstring l3Payload(
      reinterpret_cast<const char*>(packetBuffer->data() + l3PayloadOffset),
      packetBuffer->length() - l3PayloadOffset);
  // Seed 1,000 packets once at 10 ms intervals, then check the continuing loop.
  localClient->sync_txPktL3(l3Payload, 1000, 10);

  const auto localPortId = getPortIdByName(localHostname, link.localPort);
  const auto remotePortId = getPortIdByName(remoteHostname, link.remotePort);
  auto localLineRate =
      std::async(std::launch::async, [localHostname, localPortId] {
        return utility::verifyLineRate(localHostname, localPortId);
      });
  const bool remoteLineRate =
      utility::verifyLineRate(remoteHostname, remotePortId);
  ASSERT_TRUE(localLineRate.get())
      << "L3 traffic did not reach line rate on " << localHostname;
  ASSERT_TRUE(remoteLineRate)
      << "L3 traffic did not reach line rate on " << remoteHostname;

  // Require simultaneous fresh RX and TX deltas at >=95% line rate for one
  // minute.
  const auto localSpeed =
      *utility::getPortIdToPortInfo(localHostname).at(localPortId).speedMbps();
  const auto remoteSpeed = *utility::getPortIdToPortInfo(remoteHostname)
                                .at(remotePortId)
                                .speedMbps();
  auto localBefore = readPortStats(*localClient, link.localPort);
  auto remoteBefore = readPortStats(*remoteClient, link.remotePort);
  const auto localStart = *localBefore.timestamp_();
  const auto remoteStart = *remoteBefore.timestamp_();
  int64_t sustainedSeconds{0};
  while (sustainedSeconds < kL3LoopSustainSeconds) {
    // Sample spacing is part of the measured one-minute stability check.
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(kL3LoopSampleInterval);
    auto localAfter = readPortStats(*localClient, link.localPort);
    auto remoteAfter = readPortStats(*remoteClient, link.remotePort);
    ASSERT_TRUE(intervalIsAtLineRate(
        localBefore, localAfter, localSpeed, localHostname));
    ASSERT_TRUE(intervalIsAtLineRate(
        remoteBefore, remoteAfter, remoteSpeed, remoteHostname));
    sustainedSeconds = std::min(
        *localAfter.timestamp_() - localStart,
        *remoteAfter.timestamp_() - remoteStart);
    XLOG(DBG2) << "L3 traffic sustained at line rate for " << sustainedSeconds
               << "/" << kL3LoopSustainSeconds << " seconds";
    localBefore = std::move(localAfter);
    remoteBefore = std::move(remoteAfter);
  }

  localClient->sync_teardownL3TrafficLoops(
      std::vector<std::string>{link.localPort});
  localConfigured = false;
  remoteClient->sync_teardownL3TrafficLoops(
      std::vector<std::string>{link.remotePort});
  remoteConfigured = false;
  localClient->sync_getL3TrafficLoopStatus(localStatuses, {});
  remoteClient->sync_getL3TrafficLoopStatus(remoteStatuses, {});
  ASSERT_NE(localStatuses.count(link.localPort), 0);
  ASSERT_NE(remoteStatuses.count(link.remotePort), 0);
  EXPECT_EQ(
      *localStatuses.at(link.localPort).state(),
      L3TrafficLoopState::NOT_CONFIGURED);
  EXPECT_EQ(
      *remoteStatuses.at(link.remotePort).state(),
      L3TrafficLoopState::NOT_CONFIGURED);
}

} // namespace facebook::fboss
