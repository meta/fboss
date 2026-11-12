/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/agent/if/gen-cpp2/TestCtrlAsyncClient.h"
#include "fboss/fsdb/if/gen-cpp2/FsdbService.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_clients.h"

#include <folly/MacAddress.h>

namespace facebook::fboss::utility {

std::unique_ptr<apache::thrift::Client<facebook::fboss::TestCtrl>>
getSwAgentThriftClient(const std::string& switchName);
std::unique_ptr<apache::thrift::Client<FbossHwCtrl>> getHwAgentThriftClient(
    const std::string& switchName,
    int port);
std::unique_ptr<apache::thrift::Client<facebook::fboss::QsfpService>>
getQsfpThriftClient(const std::string& switchName);
std::unique_ptr<apache::thrift::Client<facebook::fboss::fsdb::FsdbService>>
getFsdbThriftClient(const std::string& switchName);

int64_t getAgentAliveSinceEpoch(const std::string& switchName);
int64_t getQsfpAliveSinceEpoch(const std::string& switchName);
int64_t getFsdbAliveSinceEpoch(const std::string& switchName);

MultiSwitchRunState getMultiSwitchRunState(const std::string& switchName);
int getNumHwSwitches(const std::string& switchName);
QsfpServiceRunState getQsfpServiceRunState(const std::string& switchName);

std::map<std::string, FabricEndpoint> getFabricPortToFabricEndpoint(
    const std::string& switchName);
std::map<std::string, std::vector<std::string>> getRemoteSwitchToReachablePorts(
    const std::string& switchName,
    const std::vector<std::string>& remoteSwitches);

std::map<int32_t, PortInfoThrift> getPortIdToPortInfo(
    const std::string& switchName);
std::map<int64_t, facebook::fboss::SystemPortThrift>
getSystemPortdIdToSystemPort(const std::string& switchName);

std::map<int32_t, facebook::fboss::InterfaceDetail> getIntfIdToIntf(
    const std::string& switchName);
std::vector<facebook::fboss::NdpEntryThrift> getNdpEntries(
    const std::string& switchName);

std::vector<facebook::fboss::DsfSessionThrift> getDsfSessions(
    const std::string& switchName);

std::map<int64_t, cfg::DsfNode> getSwitchIdToDsfNode(
    const std::string& switchName);

facebook::fboss::fsdb::SubscriberIdToOperSubscriberInfos
getSubscriberIdToOperSusbscriberInfos(const std::string& switchName);

void triggerGracefulAgentRestart(
    const std::string& switchName,
    int32_t delayInSeconds = 0);
void triggerUngracefulAgentRestart(const std::string& switchName);
void triggerGracefulAgentRestartWithDelay(
    const std::string& switchName,
    int32_t delayInSeconds);

void triggerGracefulQsfpRestart(const std::string& switchName);
void triggerUngracefulQsfpRestart(const std::string& switchName);

void triggerGracefulFsdbRestart(const std::string& switchName);
void triggerUngracefulFsdbRestart(const std::string& switchName);

void adminDisablePort(const std::string& switchName, int32_t portID);
void adminEnablePort(const std::string& switchName, int32_t portID);

void drainPort(const std::string& switchName, int32_t portID);
void undrainPort(const std::string& switchName, int32_t portID);

void enableConditionalEntropyRehash(
    const std::string& switchName,
    int32_t portID);
void disableConditionalEntropyRehash(
    const std::string& switchName,
    int32_t portID);

void setSelfHealingLagEnable(const std::string& switchName, int32_t portID);
void setSelfHealingLagDisable(const std::string& switchName, int32_t portID);

void addNeighbor(
    const std::string& switchName,
    const int32_t& interfaceID,
    const folly::IPAddress& neighborIP,
    const folly::MacAddress& macAddress,
    int32_t portID);

void removeNeighbor(
    const std::string& switchName,
    const int32_t& interfaceID,
    const folly::IPAddress& neighborIP);

void addRoute(
    const std::string& switchName,
    const folly::IPAddress& destPrefix,
    const int16_t prefixLength,
    const std::vector<folly::IPAddress>& nexthops);

std::vector<RouteDetails> getAllRoutes(const std::string& switchName);

std::map<std::string, int64_t> getCounterNameToCount(
    const std::string& switchName);

fsdb::OperState getFsdbOperStats(
    const std::string& switchName,
    const std::vector<std::string>& fsdbPath);

} // namespace facebook::fboss::utility
