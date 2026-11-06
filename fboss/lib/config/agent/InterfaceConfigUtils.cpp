/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"

namespace facebook::fboss::utility {
namespace {

constexpr int32_t kDefaultInterfaceMtu = 9000;
constexpr std::string_view kFbossLoopbackName = "fbossLoopback0";

} // namespace

cfg::Interface createVlanInterfaceConfig(
    InterfaceID interfaceID,
    VlanID vlanID) {
  cfg::Interface intf;
  intf.name() = std::to_string(static_cast<int32_t>(interfaceID));
  intf.intfID() = static_cast<int32_t>(interfaceID);
  intf.vlanID() = static_cast<int32_t>(vlanID);
  intf.type() = cfg::InterfaceType::VLAN;
  intf.routerID() = 0;
  intf.scope() = cfg::Scope::LOCAL;
  intf.mtu() = kDefaultInterfaceMtu;
  return intf;
}

void addDefaultLoopbackInterface(
    cfg::SwitchConfig& config,
    const HwAsic& asic) {
  if (asic.getSwitchType() != cfg::SwitchType::NPU) {
    return;
  }

  const auto loopbackUsesDefaultVlan =
      getDefaultVlanId(asic) == kDefaultVlanId1;
  auto loopbackVlanID = kFbossLoopbackVlanId;
  if (loopbackUsesDefaultVlan) {
    const auto hasDefaultVlan = *config.defaultVlan() == kDefaultVlanId1 &&
        std::any_of(
            config.vlans()->begin(),
            config.vlans()->end(),
            [](const auto& vlan) { return *vlan.id() == kDefaultVlanId1; });
    if (!hasDefaultVlan) {
      throw FbossError(
          "fbossLoopback0 requires default VLAN 1; call addDefaultVlan first");
    }
    loopbackVlanID = kDefaultVlanId1;
  } else {
    auto loopbackVlan = createVlanConfig(VlanID(kFbossLoopbackVlanId));
    loopbackVlan.name() = kFbossLoopbackName;
    config.vlans()->insert(config.vlans()->begin(), std::move(loopbackVlan));
  }

  auto loopbackInterface = createVlanInterfaceConfig(
      InterfaceID(kFbossLoopbackVlanId), VlanID(loopbackVlanID));
  // COOP names the loopback VLAN but leaves its interface unnamed.
  loopbackInterface.name().reset();
  loopbackInterface.isVirtual() = true;
  loopbackInterface.isStateSyncDisabled() = true;
  config.interfaces()->push_back(std::move(loopbackInterface));
}

} // namespace facebook::fboss::utility
