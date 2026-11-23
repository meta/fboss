// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss {
class AgentEnsemble;
class SwSwitch;
class SwitchState;
struct MySidWithNextHops;
} // namespace facebook::fboss

namespace facebook::fboss::utility {

// Max ECMP width used by RUN_HW_LOAD_BALANCER_TEST macros
inline constexpr int kSrv6MaxEcmpWidth = 8;

inline constexpr auto kSrv6LegacyLocatorPrefix = "fdad:ffff::/32";
inline constexpr auto kSrv6PlaneLocatorPrefix = "fdad:20d::/32";
inline constexpr int16_t kSrv6DecapFunctionId = 0x7fff;

cfg::Srv6Tunnel makeSrv6TunnelConfig(
    const std::string& name,
    InterfaceID interfaceId);

cfg::Srv6Tunnel makeSrv6DecapTunnelConfig(const std::string& name);

cfg::SwitchConfig srv6EcmpInitialConfig(const AgentEnsemble& ensemble);

// Common ECN verification for SRv6 encap/decap/midpoint tests.
// Disables TX to build congestion, calls sendFloodPackets to inject
// ECN-capable traffic, re-enables TX, then verifies outEcnCounter
// incremented and a trapped packet matches isEcnMarkedPacket.
void verifySrv6EcnMarking(
    AgentEnsemble* ensemble,
    PortID egressPort,
    const std::function<void()>& sendFloodPackets,
    const std::function<
        bool(const folly::IPAddressV6& dstAddr, uint8_t ecnBits)>&
        isEcnMarkedPacket);

// Look up port name from switch config by port ID.
std::string portNameForConfig(const cfg::SwitchConfig& cfg, PortID portId);

// Create an adjacency-based MySidConfig. The portName can be a physical port
// name (from portNameForConfig) or an aggregate name like "AGG-1".
cfg::MySidConfig makeAdjacencyMySidConfig(
    const std::string& portName,
    const std::string& locatorPrefix,
    int functionId);

cfg::MySidConfig makeDecapMySidConfig(
    const std::vector<std::string>& locatorPrefixes);

// Poll until a MySid entry with the given prefix reaches the expected
// resolved/unresolved state. Must be called from within a test context
// (uses WITH_RETRIES / EXPECT_EVENTUALLY macros).
void waitForMySidResolveOrUnresolve(
    const std::function<std::shared_ptr<SwitchState>()>& getState,
    const folly::IPAddressV6& sidPrefix,
    uint8_t prefixLen,
    bool resolved);

// Add a DECAPSULATE_AND_LOOKUP MySid entry via the RIB.
void addDecapMySidEntry(
    SwSwitch* sw,
    const folly::IPAddressV6& addr,
    uint8_t prefixLen);

// Add a BINDING_MICRO_SID MySid entry via ThriftHandler.
void addBindingSidEntry(
    SwSwitch* sw,
    const folly::IPAddressV6& mySidAddr,
    uint8_t prefixLen,
    const std::vector<NextHopThrift>& nhops);

// Build a NextHopThrift for SRv6 encap with a single SID.
NextHopThrift makeSrv6NextHopThrift(
    const folly::IPAddressV6& nhopAddr,
    const folly::IPAddressV6& sid,
    const std::string& tunnelId = "srv6Tunnel0");

// Build numEntries ADJACENCY_MICRO_SID MySID entries, each a /48 at
// 3001:db8:<i + sidOffset>:: with one resolved adjacency next hop cycling
// through the first numNhops resolved ecmp next hops. Shared by the SRv6 MySID
// scale and full-scale warmboot benchmarks. sidOffset lets callers keep the SID
// range clear of other SIDs (e.g. SRv6 tunnel SIDs) in the same switch.
std::vector<MySidWithNextHops> makeAdjacencyMySidEntries(
    const EcmpSetupAnyNPorts6& ecmpHelper,
    int numNhops,
    int numEntries,
    int sidOffset);

// Program the given MySID entries in a single batched rib->update (a per-entry
// update reprocesses the whole RIB, which is O(entries x routes) at scale).
void programMySidEntries(SwSwitch* sw, std::vector<MySidWithNextHops> entries);

// Delete numEntries MySID entries previously added at 3001:db8:<i +
// sidOffset>::.
void deleteScaleMySidEntries(SwSwitch* sw, int numEntries, int sidOffset);

} // namespace facebook::fboss::utility
