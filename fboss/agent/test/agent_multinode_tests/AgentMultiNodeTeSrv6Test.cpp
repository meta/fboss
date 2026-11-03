// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/SocketAddress.h>
#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
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

} // namespace facebook::fboss
