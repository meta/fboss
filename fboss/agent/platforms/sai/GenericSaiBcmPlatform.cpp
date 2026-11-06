/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/GenericSaiBcmPlatform.h"

#include "fboss/agent/hw/switch_asics/HwAsic.h"

namespace facebook::fboss {

GenericSaiBcmPlatform::GenericSaiBcmPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : SaiBcmPlatform(
          std::move(productInfo),
          std::move(platformMapping),
          localMac) {}

GenericSaiBcmPlatform::~GenericSaiBcmPlatform() = default;

void GenericSaiBcmPlatform::setupAsic(
    std::optional<int64_t> switchId,
    const cfg::SwitchInfo& switchInfo,
    std::optional<HwAsic::FabricNodeRole> fabricNodeRole) {
  std::optional<cfg::SdkVersion> sdkVersion;
  auto agentConfig = config();
  if (agentConfig->thrift.sw()->sdkVersion().has_value()) {
    sdkVersion = agentConfig->thrift.sw()->sdkVersion().value();
  }
  asic_ = HwAsic::makeAsic(
      switchId.value_or(0), switchInfo, sdkVersion, fabricNodeRole);
}

HwAsic* GenericSaiBcmPlatform::getAsic() const {
  return asic_.get();
}

std::vector<FlexPortMode> GenericSaiBcmPlatform::getSupportedFlexPortModes()
    const {
  return {};
}

bool GenericSaiBcmPlatform::supportInterfaceType() const {
  return true;
}

} // namespace facebook::fboss
