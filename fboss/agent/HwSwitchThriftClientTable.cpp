/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/HwSwitchThriftClientTable.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/ReconnectingRequestChannel.h>
#include <thrift/lib/cpp2/async/RetryingRequestChannel.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>

#include "fboss/agent/FbossError.h"

#include <memory>

DEFINE_int32(hwswitch_query_timeout, 120, "Timeout for hw switch thrift query");

namespace facebook::fboss {
/*
 * Creates a reconnecting thrift client to query HwAgent. This does not
 * connect to server yet. The connection happens when the first thrift api
 * call is made
 */
std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> createFbossHwClient(
    int16_t port,
    std::shared_ptr<folly::ScopedEventBaseThread> evbThread) {
  auto reconnectingChannel =
      apache::thrift::PooledRequestChannel::newSyncChannel(
          evbThread, [port, evbThread](folly::EventBase& evb) {
            return apache::thrift::RetryingRequestChannel::newChannel(
                evb,
                2, /*retries before error*/
                apache::thrift::ReconnectingRequestChannel::newChannel(
                    *evbThread->getEventBase(), [port](folly::EventBase& evb) {
                      auto socket = folly::AsyncSocket::UniquePtr(
                          new folly::AsyncSocket(&evb));
                      socket->connect(
                          nullptr, folly::SocketAddress("::1", port));
                      auto channel =
                          apache::thrift::RocketClientChannel::newChannel(
                              std::move(socket));
                      channel->setTimeout(FLAGS_hwswitch_query_timeout * 1000);
                      return channel;
                    }));
          });
  return std::make_unique<apache::thrift::Client<FbossHwCtrl>>(
      std::move(reconnectingChannel));
}

HwSwitchThriftClientTable::HwSwitchThriftClientTable(
    int16_t basePort,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  for (const auto& [switchId, switchInfo] : switchIdToSwitchInfo) {
    auto evbThread = std::make_shared<folly::ScopedEventBaseThread>(
        fmt::format("HwSwitchCtrlClient-{}", *switchInfo.switchIndex()));
    auto port = basePort + *switchInfo.switchIndex();
    clientInfos_.emplace(
        SwitchID(switchId),
        std::make_pair(createFbossHwClient(port, evbThread), evbThread));
  }
}

apache::thrift::Client<FbossHwCtrl>* HwSwitchThriftClientTable::getClient(
    SwitchID switchId) {
  if (!clientInfos_.contains(switchId)) {
    throw FbossError("No client found for switch ", switchId);
  }
  return clientInfos_.at(switchId).first.get();
}

std::optional<std::map<::std::int64_t, FabricEndpoint>>
HwSwitchThriftClientTable::getFabricReachability(SwitchID switchId) {
  std::map<::std::int64_t, FabricEndpoint> reachability;
  auto client = getClient(switchId);
  try {
    client->sync_getHwFabricReachability(reachability);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get fabric reachability for switch : " << switchId
              << " error: " << ex.what();
    return std::nullopt;
  }
  return reachability;
}

std::map<::std::string, FabricEndpoint>
HwSwitchThriftClientTable::getFabricConnectivity(SwitchID switchId) {
  std::map<::std::string, FabricEndpoint> connectivity;
  auto client = getClient(switchId);
  try {
    client->sync_getHwFabricConnectivity(connectivity);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get fabric connectivity for switch : " << switchId
              << " error: " << ex.what();
    throw FbossError(
        "Failed to get fabric connectivity for switch ",
        switchId,
        " error: ",
        ex.what());
  }
  return connectivity;
}

void HwSwitchThriftClientTable::clearHwPortStats(
    SwitchID switchId,
    std::vector<int32_t>& ports) {
  auto client = getClient(switchId);
  try {
    client->sync_clearHwPortStats(ports);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to clear ports for switch : " << switchId
              << " error: " << ex.what();
  }
}

void HwSwitchThriftClientTable::clearAllHwPortStats(SwitchID switchId) {
  auto client = getClient(switchId);
  try {
    client->sync_clearAllHwPortStats();
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to clear ports for switch : " << switchId
              << " error: " << ex.what();
  }
}

void HwSwitchThriftClientTable::getHwL2Table(
    SwitchID switchId,
    std::vector<L2EntryThrift>& l2Table) {
  auto client = getClient(switchId);
  try {
    client->sync_getHwL2Table(l2Table);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get l2 table for switch : " << switchId
              << " error: " << ex.what();
  }
}

std::string HwSwitchThriftClientTable::diagCmd(
    SwitchID switchId,
    const std::string& cmd,
    const ClientInformation& clientInfo) {
  auto client = getClient(switchId);
  fbstring out;
  try {
    client->sync_diagCmd(
        out,
        cmd,
        clientInfo,
        0 /* serverTimeoutMsecs */,
        false /* bypassFilter */);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed run diagCmd for switch : " << switchId
              << " error: " << ex.what();
  }
  return out.toStdString();
}

std::vector<FirmwareInfo> HwSwitchThriftClientTable::getAllFirmwareInfo(
    SwitchID switchId) {
  std::vector<FirmwareInfo> firmwareInfoList;

  auto client = getClient(switchId);
  try {
    client->sync_getAllHwFirmwareInfo(firmwareInfoList);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get firmware info for switch : " << switchId
              << " error: " << ex.what();
    return std::vector<FirmwareInfo>();
  }
  return firmwareInfoList;
}

std::string HwSwitchThriftClientTable::getHwDebugDump(SwitchID switchId) {
  auto client = getClient(switchId);
  std::string out{};
  try {
    client->sync_getHwDebugDump(out);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get hardware debug dump : " << switchId
              << " error: " << ex.what();
  }
  return out;
}

void HwSwitchThriftClientTable::setSdkRegDumpEnabled(
    SwitchID switchId,
    bool enabled) {
  // Let exceptions (e.g. unsupported on this device) propagate so callers can
  // surface them to the user.
  auto client = getClient(switchId);
  client->sync_setSdkRegDumpEnabled(enabled);
}

std::vector<phy::PrbsLaneStats> HwSwitchThriftClientTable::getPortAsicPrbsStats(
    const SwitchID& switchId,
    const PortID& portId) {
  std::vector<phy::PrbsLaneStats> prbsStats;
  auto client = getClient(switchId);
  try {
    client->sync_getPortAsicPrbsStats(prbsStats, portId);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get port asic prbs stats for switch : " << switchId
              << " error: " << ex.what();
  }
  return prbsStats;
}
void HwSwitchThriftClientTable::clearPortAsicPrbsStats(
    const SwitchID& switchId,
    const PortID& portId) {
  auto client = getClient(switchId);
  try {
    client->sync_clearPortAsicPrbsStats(portId);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to clear port asic prbs stats for switch : "
              << switchId << " error: " << ex.what();
  }
}
std::vector<prbs::PrbsPolynomial>
HwSwitchThriftClientTable::getPortPrbsPolynomials(
    const SwitchID& switchId,
    const PortID& portId) {
  std::vector<prbs::PrbsPolynomial> prbsPolynomials;
  auto client = getClient(switchId);
  try {
    client->sync_getPortPrbsPolynomials(prbsPolynomials, portId);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get port prbs polynomials for switch : " << switchId
              << " error: " << ex.what();
  }
  return prbsPolynomials;
}
prbs::InterfacePrbsState HwSwitchThriftClientTable::getPortPrbsState(
    const SwitchID& switchId,
    const PortID& portId) {
  prbs::InterfacePrbsState prbsState;
  auto client = getClient(switchId);
  try {
    client->sync_getPortPrbsState(prbsState, portId);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get port prbs state for switch : " << switchId
              << " error: " << ex.what();
  }

  return prbsState;
}

state::SwitchState HwSwitchThriftClientTable::getProgrammedState(
    const SwitchID& switchId) {
  state::SwitchState hwSwitchState;
  auto client = getClient(switchId);
  try {
    client->sync_getProgrammedState(hwSwitchState);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get programmed switch state for switch : "
              << switchId << " error: " << ex.what();
    throw;
  }
  return hwSwitchState;
}

SwitchRunState HwSwitchThriftClientTable::getHwSwitchRunState(
    const SwitchID& switchId) {
  SwitchRunState runState;
  auto client = getClient(switchId);
  try {
    runState = client->sync_getHwSwitchRunState();
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to get hw switch run state for switch : " << switchId
              << " error: " << ex.what();
    throw;
  }
  return runState;
}

BootType HwSwitchThriftClientTable::getHwSwitchBootType(
    const SwitchID& switchId) {
  auto client = getClient(switchId);
  try {
    return client->sync_getBootType();
  } catch (const std::exception& ex) {
    // The caller retries and logs the final failure once.
    XLOG(DBG2) << "Failed to get hw switch boot type for switch : " << switchId
               << " error: " << ex.what();
    throw;
  }
}

} // namespace facebook::fboss
