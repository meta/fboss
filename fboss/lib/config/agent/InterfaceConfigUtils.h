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

cfg::Interface createVlanInterfaceConfig(
    InterfaceID interfaceID,
    VlanID vlanID);

// Add the portless fbossLoopback0 VLAN interface and its backing VLAN for an
// NPU. Classic VLAN-RIF NPUs use VLAN/interface 10. NPUs whose pipeline
// requires a VLAN RIF over the adapter-owned default VLAN use interface 10 on
// the default VLAN previously added by addDefaultVlan(). Non-NPU switch types
// receive no loopback interface.
void addDefaultLoopbackInterface(cfg::SwitchConfig& config, const HwAsic& asic);

} // namespace utility
} // namespace facebook::fboss
