// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/IPAddressV6.h>
#include <string>
#include <unordered_map>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

// Build the full SID IPv6 address from locator address + function ID.
// locatorAddr: the locator block address (e.g., from "3001:db8::/32")
// locatorPrefixLen: the locator block length in bits (e.g., 32)
// functionId: hex string (e.g., "ffff", "1", "7fff")
// Returns the combined address (e.g., "3001:db8:ffff::")
folly::IPAddressV6 buildSidAddress(
    const folly::IPAddressV6& locatorAddr,
    uint8_t locatorPrefixLen,
    const std::string& functionId);

// Build a map from port name to InterfaceID for all physical and aggregate
// ports in the switch config.
std::unordered_map<std::string, InterfaceID> buildPortNameToInterfaceIdMap(
    const cfg::SwitchConfig& config);

// Convert MySidConfig (switch_config.thrift) to (MySid state, unresolved
// next-hop set) pairs using a precomputed port-name → InterfaceID map (see
// buildPortNameToInterfaceIdMap), one per (locator, function ID). The next-hop
// set is empty for DECAPSULATE_AND_LOOKUP and ADJACENCY_MICRO_SID entries;
// populated for NODE_MICRO_SID.
std::vector<MySidWithNextHops> convertMySidConfig(
    const cfg::MySidConfig& config,
    const std::unordered_map<std::string, InterfaceID>& portNameToInterfaceId);

} // namespace facebook::fboss
