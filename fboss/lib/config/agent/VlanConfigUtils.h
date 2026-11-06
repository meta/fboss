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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {
class HwAsic;
namespace utility {

auto constexpr kDefaultVlanId1 = 1;
auto constexpr kDefaultVlanId4094 = 4094;
auto constexpr kFbossLoopbackVlanId = 10;

/*
 * Use vlan 2000 as the base vlan for ports in configs generated here.
 * Anything except 0 and 1 would work. 0 is reserved, and Broadcom uses 1 as
 * its default VLAN, including the CPU port as a member.
 */
auto constexpr kBaseVlanId = 2000;
auto constexpr kDownlinkBaseVlanId = 2000;
auto constexpr kUplinkBaseVlanId = 4000;

// TunManager's legacy NPU mapping converts interface IDs in the 2000 band to
// routing table IDs using `tableId = interfaceId - 2000 + 1`. Since the kernel
// table ID range available to TunManager ends at 253, 2252 is the largest
// interface/VLAN ID that is valid when 1:1 interface-to-table mapping is off.
auto constexpr kInterfaceVlanIdMin = kBaseVlanId + 1;
auto constexpr kInterfaceVlanIdMax = 2252;

// Default-profile management ports allocate downward from that safe upper
// bound while regular interface VLANs allocate upward from the lower bound.
auto constexpr kManagementVlanIdMax = kInterfaceVlanIdMax;

cfg::Vlan createVlanConfig(VlanID id);

// Return the default VLAN ID required by the ASIC's hardware pipeline.
int32_t getDefaultVlanId(const HwAsic& asic);

// Add the non-routable default VLAN required by the NPU, using the VLAN ID
// selected by its hardware pipeline. Non-NPU switch types and ASICs without a
// hardware default VLAN receive no config object.
void addDefaultVlan(cfg::SwitchConfig& config, const HwAsic& asic);

} // namespace utility
} // namespace facebook::fboss
