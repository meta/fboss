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
#include "fboss/agent/RouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/types.h"

#include "folly/IPAddressV4.h"
#include "folly/MacAddress.h"

/*
 * This utility is to provide utils for queue-per host tests.
 */

// Forward declarations
namespace facebook::fboss {
class HwSwitch;
class HwSwitchEnsemble;
class SwitchState;
class AgentEnsemble;
} // namespace facebook::fboss

namespace folly {
class IPAddress;
}

namespace facebook::fboss::utility {

constexpr int kQueuePerHostWeight = 1;

constexpr int kQueuePerHostQueue0 = 0;
constexpr int kQueuePerHostQueue1 = 1;
constexpr int kQueuePerHostQueue2 = 2;
constexpr int kQueuePerHostQueue3 = 3;
constexpr int kQueuePerHostQueue4 = 4;

// For verifyQueuePerHostMapping()
constexpr int kQueueId = 2;

const std::vector<int>& kQueuePerhostQueueIds();
const std::vector<cfg::AclLookupClass>& kLookupClasses();

void addQueuePerHostQueueConfig(cfg::SwitchConfig* config);
void addQueuePerHostAcls(cfg::SwitchConfig* config, bool isSai);

std::string getQueuePerHostAclTableName();
std::string getTtlAclTableName();

std::string getQueuePerHostTtlAclName();
std::string getQueuePerHostTtlCounterName();

void updateRoutesClassID(
    const std::map<
        RoutePrefix<folly::IPAddressV4>,
        std::optional<cfg::AclLookupClass>>& routePrefix2ClassID,
    RouteUpdateWrapper* updater);

void addTtlAclEntry(
    cfg::SwitchConfig* config,
    const std::string& aclTableName,
    bool aclByteCounterSupported);
void addTtlAclTable(
    cfg::SwitchConfig* config,
    int16_t priority,
    bool aclByteCounterSupported,
    bool addExtraQualifier = false);
void deleteTtlCounters(cfg::SwitchConfig* config);
void addQueuePerHostAclEntry(
    cfg::SwitchConfig* config,
    const std::string& aclTableName,
    bool isSai);
void addQueuePerHostAclTables(
    cfg::SwitchConfig* config,
    int16_t priority,
    bool addAllQualifiers,
    bool isSai);
void deleteQueuePerHostMatchers(cfg::SwitchConfig* config);

void verifyQueuePerHostMapping(
    HwSwitchEnsemble* ensemble,
    std::shared_ptr<SwitchState> swState,
    const std::vector<PortID>& portIds,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::function<std::map<PortID, HwPortStats>(const std::vector<PortID>&)>
        getHwPortStatsFn,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp);

void verifyQueuePerHostMapping(
    HwSwitch* ensemble,
    std::shared_ptr<SwitchState> swState,
    const std::vector<PortID>& portIds,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::function<std::map<PortID, HwPortStats>(const std::vector<PortID>&)>
        getHwPortStatsFn,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp);

void verifyQueuePerHostMapping(
    HwSwitchEnsemble* ensemble,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::optional<uint16_t> l4SrcPort = std::nullopt,
    std::optional<uint16_t> l4DstPort = std::nullopt,
    std::optional<uint8_t> dscp = std::nullopt);

void verifyQueuePerHostMapping(
    AgentEnsemble* ensemble,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::optional<uint16_t> l4SrcPort = std::nullopt,
    std::optional<uint16_t> l4DstPort = std::nullopt,
    std::optional<uint8_t> dscp = std::nullopt);
} // namespace facebook::fboss::utility
