/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"

#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include "common/network/NetworkUtil.h"
#include "common/thrift/thrift/gen-cpp2/MonitorAsyncClient.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentConfig.h"

namespace facebook::fboss::utility {

using facebook::fboss::TestCtrl;
using RunForHwAgentFn = std::function<void(
    apache::thrift::Client<facebook::fboss::FbossHwCtrl>& client)>;

std::unique_ptr<apache::thrift::Client<TestCtrl>> getSwAgentThriftClient(
    const std::string& switchName) {
  folly::EventBase* eb = folly::EventBaseManager::get()->getEventBase();
  auto remoteSwitchIp =
      facebook::network::NetworkUtil::getHostByName(switchName);
  folly::SocketAddress agent(remoteSwitchIp, 5909);
  auto socket = folly::AsyncSocket::newSocket(eb, agent);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  return std::make_unique<apache::thrift::Client<TestCtrl>>(std::move(channel));
}

std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> getHwAgentThriftClient(
    const std::string& switchName,
    int port) {
  folly::EventBase* eb = folly::EventBaseManager::get()->getEventBase();
  auto remoteSwitchIp =
      facebook::network::NetworkUtil::getHostByName(switchName);
  folly::SocketAddress agent(remoteSwitchIp, port);
  auto socket = folly::AsyncSocket::newSocket(eb, agent);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  return std::make_unique<apache::thrift::Client<FbossHwCtrl>>(
      std::move(channel));
}

std::unique_ptr<apache::thrift::Client<facebook::fboss::QsfpService>>
getQsfpThriftClient(const std::string& switchName) {
  folly::EventBase* eb = folly::EventBaseManager::get()->getEventBase();
  auto remoteSwitchIp =
      facebook::network::NetworkUtil::getHostByName(switchName);
  folly::SocketAddress qsfp(remoteSwitchIp, 5910);
  auto socket = folly::AsyncSocket::newSocket(eb, qsfp);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  return std::make_unique<apache::thrift::Client<QsfpService>>(
      std::move(channel));
}

std::unique_ptr<apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>
getFsdbThriftClient(const std::string& switchName) {
  folly::EventBase* eb = folly::EventBaseManager::get()->getEventBase();
  auto remoteSwitchIp =
      facebook::network::NetworkUtil::getHostByName(switchName);
  folly::SocketAddress fsdb(remoteSwitchIp, 5908);
  auto socket = folly::AsyncSocket::newSocket(eb, fsdb);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  return std::make_unique<
      apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>(
      std::move(channel));
}

int64_t getAgentAliveSinceEpoch(const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  return swAgentClient->sync_aliveSince();
}

int64_t getQsfpAliveSinceEpoch(const std::string& switchName) {
  auto qsfpClient = getQsfpThriftClient(switchName);
  return qsfpClient->sync_aliveSince();
}

int64_t getFsdbAliveSinceEpoch(const std::string& switchName) {
  auto fsdbClient = getFsdbThriftClient(switchName);
  return fsdbClient->sync_aliveSince();
}

MultiSwitchRunState getMultiSwitchRunState(const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  MultiSwitchRunState runState;
  swAgentClient->sync_getMultiSwitchRunState(runState);
  return runState;
}

int getNumHwSwitches(const std::string& switchName) {
  auto runState = getMultiSwitchRunState(switchName);
  return runState.hwIndexToRunState()->size();
}

QsfpServiceRunState getQsfpServiceRunState(const std::string& switchName) {
  auto qsfpClient = getQsfpThriftClient(switchName);
  return qsfpClient->sync_getQsfpServiceRunState();
}

void runOnAllHwAgents(
    const std::string& switchName,
    const RunForHwAgentFn& fn) {
  static const std::vector kHwAgentPorts = {5931, 5932};
  auto numHwSwitches = getNumHwSwitches(switchName);
  for (int i = 0; i < numHwSwitches; i++) {
    auto hwAgentClient = getHwAgentThriftClient(switchName, kHwAgentPorts[i]);
    fn(*hwAgentClient);
  }
}

std::map<std::string, FabricEndpoint> getFabricPortToFabricEndpoint(
    const std::string& switchName) {
  std::map<std::string, FabricEndpoint> fabricPortToFabricEndpoint;
  auto hwAgentQueryFn = [&fabricPortToFabricEndpoint](
                            apache::thrift::Client<FbossHwCtrl>& client) {
    std::map<std::string, FabricEndpoint> hwAgentEntries;
    client.sync_getHwFabricConnectivity(hwAgentEntries);
    fabricPortToFabricEndpoint.merge(hwAgentEntries);
  };
  runOnAllHwAgents(switchName, hwAgentQueryFn);

  return fabricPortToFabricEndpoint;
}

std::map<std::string, std::vector<std::string>> getRemoteSwitchToReachablePorts(
    const std::string& switchName,
    const std::vector<std::string>& remoteSwitches) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::map<std::string, std::vector<std::string>> remoteSwitchToReachablePorts;
  swAgentClient->sync_getSwitchReachability(
      remoteSwitchToReachablePorts, remoteSwitches);

  return remoteSwitchToReachablePorts;
}

std::map<int32_t, PortInfoThrift> getPortIdToPortInfo(
    const std::string& switchName) {
  std::map<int32_t, PortInfoThrift> portIdToPortInfo;
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_getAllPortInfo(portIdToPortInfo);
  return portIdToPortInfo;
}

std::map<int64_t, facebook::fboss::SystemPortThrift>
getSystemPortdIdToSystemPort(const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::map<int64_t, facebook::fboss::SystemPortThrift> systemPortIdToSystemPort;
  swAgentClient->sync_getSystemPorts(systemPortIdToSystemPort);
  return systemPortIdToSystemPort;
}

std::map<int32_t, facebook::fboss::InterfaceDetail> getIntfIdToIntf(
    const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::map<int32_t, facebook::fboss::InterfaceDetail> intfIdToIntf;
  swAgentClient->sync_getAllInterfaces(intfIdToIntf);
  return intfIdToIntf;
}

std::vector<facebook::fboss::NdpEntryThrift> getNdpEntries(
    const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::vector<facebook::fboss::NdpEntryThrift> ndpEntries;
  swAgentClient->sync_getNdpTable(ndpEntries);
  return ndpEntries;
}

std::vector<facebook::fboss::DsfSessionThrift> getDsfSessions(
    const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::vector<facebook::fboss::DsfSessionThrift> sessions;
  swAgentClient->sync_getDsfSessions(sessions);
  return sessions;
}

std::map<int64_t, cfg::DsfNode> getSwitchIdToDsfNode(
    const std::string& switchName) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  std::map<int64_t, cfg::DsfNode> switchIdToDsfNode;
  swAgentClient->sync_getDsfNodes(switchIdToDsfNode);
  return switchIdToDsfNode;
}

facebook::fboss::fsdb::SubscriberIdToOperSubscriberInfos
getSubscriberIdToOperSusbscriberInfos(const std::string& switchName) {
  auto fsdbClient = getFsdbThriftClient(switchName);
  facebook::fboss::fsdb::SubscriberIdToOperSubscriberInfos subInfos;
  fsdbClient->sync_getAllOperSubscriberInfos(subInfos);
  return subInfos;
}

void triggerGracefulAgentRestart(
    const std::string& switchName,
    int32_t delayInSeconds) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_gracefullyRestartService(
        "fboss_sw_agent_test", delayInSeconds);
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void triggerUngracefulAgentRestart(const std::string& switchName) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_ungracefullyRestartService("fboss_sw_agent_test");
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void triggerGracefulAgentRestartWithDelay(
    const std::string& switchName,
    int32_t delayInSeconds) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    const std::string serviceName =
        FLAGS_multi_switch ? "fboss_sw_agent_test" : "wedge_agent-test";
    swAgentClient->sync_gracefullyRestartServiceWithDelay(
        serviceName, delayInSeconds);
  } catch (const apache::thrift::transport::TTransportException& ex) {
    // Expected as agent exits mid-RPC.
    XLOG(DBG2) << "triggerGracefulAgentRestartWithDelay on " << switchName
               << " transport exception (expected): " << ex.what();
  }
}

void triggerGracefulQsfpRestart(const std::string& switchName) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_gracefullyRestartService("qsfp_service_for_testing", 0);
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void triggerUngracefulQsfpRestart(const std::string& switchName) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_ungracefullyRestartService("qsfp_service_for_testing");
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void triggerGracefulFsdbRestart(const std::string& switchName) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_gracefullyRestartService("fsdb_service_for_testing", 0);
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void triggerUngracefulFsdbRestart(const std::string& switchName) {
  try {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_ungracefullyRestartService("fsdb_service_for_testing");
  } catch (...) {
    // Thrift request may throw error as the Agent exits.
    // Ignore it, as we only wanted to trigger exit.
  }
}

void adminDisablePort(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setPortState(portID, false /* disable port */);
}

void adminEnablePort(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setPortState(portID, true /* enable port */);
}

void drainPort(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setPortDrainState(portID, true /* drain port */);
}

void undrainPort(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setPortDrainState(portID, false /* undrain port */);
}

void enableConditionalEntropyRehash(
    const std::string& switchName,
    int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setConditionalEntropyRehash(
      portID, true /* enable conditional entropy rehash */);
}

void disableConditionalEntropyRehash(
    const std::string& switchName,
    int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setConditionalEntropyRehash(
      portID, false /* disable conditional entropy rehash */);
}

void setSelfHealingLagEnable(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setSelfHealingLagState(portID, true /* enable SHEL */);
}

void setSelfHealingLagDisable(const std::string& switchName, int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_setSelfHealingLagState(portID, false /* disable SHEL */);
}

void addNeighbor(
    const std::string& switchName,
    const int32_t& interfaceID,
    const folly::IPAddress& neighborIP,
    const folly::MacAddress& macAddress,
    int32_t portID) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_addNeighbor(
      interfaceID,
      facebook::network::toBinaryAddress(neighborIP),
      macAddress.toString(),
      portID);
}

void removeNeighbor(
    const std::string& switchName,
    const int32_t& interfaceID,
    const folly::IPAddress& neighborIP) {
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_flushNeighborEntry(
      facebook::network::toBinaryAddress(neighborIP), interfaceID);
}

void addRoute(
    const std::string& switchName,
    const folly::IPAddress& destPrefix,
    const int16_t prefixLength,
    const std::vector<folly::IPAddress>& nexthops) {
  UnicastRoute route;
  route.dest()->ip() = facebook::network::toBinaryAddress(destPrefix);
  route.dest()->prefixLength() = prefixLength;
  for (const auto& nexthop : nexthops) {
    route.nextHopAddrs()->push_back(
        facebook::network::toBinaryAddress(nexthop));
  }

  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_addUnicastRoutes(
      static_cast<int16_t>(ClientID::STATIC_ROUTE), {std::move(route)});
}

std::vector<RouteDetails> getAllRoutes(const std::string& switchName) {
  std::vector<RouteDetails> routes;
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_getRouteTableDetails(routes);
  return routes;
}

std::map<std::string, int64_t> getCounterNameToCount(
    const std::string& switchName) {
  std::map<std::string, int64_t> counterNameToCount;

  auto multiSwitchRunState = getMultiSwitchRunState(switchName);
  // For split binary (multiSwitchEnabled), the counters are available in the
  // HwAgent. For monolithic, the counters are available in the SwAgent.
  if (*multiSwitchRunState.multiSwitchEnabled()) {
    auto hwAgentQueryFn =
        [&counterNameToCount](apache::thrift::Client<FbossHwCtrl>& client) {
          std::map<std::string, int64_t> hwAgentCounters;
          apache::thrift::Client<facebook::thrift::Monitor> monitoringClient{
              client.getChannelShared()};
          monitoringClient.sync_getCounters(hwAgentCounters);
          counterNameToCount.merge(hwAgentCounters);
        };
    runOnAllHwAgents(switchName, hwAgentQueryFn);
  } else {
    auto swAgentClient = getSwAgentThriftClient(switchName);
    swAgentClient->sync_getCounters(counterNameToCount);
  }

  return counterNameToCount;
}

fsdb::OperState getFsdbOperStats(
    const std::string& switchName,
    const std::vector<std::string>& fsdbPath) {
  fsdb::OperGetRequest req;
  fsdb::OperPath path;
  path.raw() = fsdbPath;
  req.path() = path;
  req.protocol() = fsdb::OperProtocol::SIMPLE_JSON;

  fsdb::OperState operStats;
  auto fsdbClient = getFsdbThriftClient(switchName);
  fsdbClient->sync_getOperStats(operStats, req);

  return operStats;
}

} // namespace facebook::fboss::utility
