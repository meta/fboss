/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/VlanConfigUtils.h"

#include <string>

#include "fboss/agent/hw/switch_asics/HwAsic.h"

namespace facebook::fboss::utility {
namespace {

cfg::Vlan createDefaultVlanConfig(VlanID id) {
  auto vlan = createVlanConfig(id);
  vlan.name() = "default";
  vlan.routable() = false;
  return vlan;
}

} // namespace

cfg::Vlan createVlanConfig(VlanID id) {
  cfg::Vlan vlan;
  vlan.id() = static_cast<int32_t>(id);
  vlan.name() = "vlan" + std::to_string(static_cast<int32_t>(id));
  vlan.routable() = true;
  vlan.recordStats() = true;
  return vlan;
}

int32_t getDefaultVlanId(const HwAsic& asic) {
  return asic.getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB ||
          asic.getAsicType() == cfg::AsicType::ASIC_TYPE_P200
      ? kDefaultVlanId1
      : kDefaultVlanId4094;
}

void addDefaultVlan(cfg::SwitchConfig& config, const HwAsic& asic) {
  if (asic.getSwitchType() != cfg::SwitchType::NPU ||
      !asic.isSupported(HwAsic::Feature::DEFAULT_VLAN)) {
    return;
  }

  const auto defaultVlanID = getDefaultVlanId(asic);
  config.vlans()->insert(
      config.vlans()->begin(), createDefaultVlanConfig(VlanID(defaultVlanID)));
  config.defaultVlan() = defaultVlanID;
}

} // namespace facebook::fboss::utility
