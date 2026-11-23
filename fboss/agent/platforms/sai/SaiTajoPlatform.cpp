/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiTajoPlatform.h"
#include "fboss/agent/hw/sai/api/UdfApi.h"
#include "fboss/agent/hw/switch_asics/TajoAsic.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

namespace facebook::fboss {
SaiTajoPlatform::SaiTajoPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : SaiPlatform(
          std::move(productInfo),
          std::move(platformMapping),
          localMac) {}

std::optional<SaiSwitchTraits::Attributes::AclFieldList>
SaiTajoPlatform::getAclFieldList() const {
  return std::nullopt;
}

const std::set<sai_api_t>& SaiTajoPlatform::getSupportedApiList() const {
  return getDefaultSwitchAsicSupportedApis();
}

std::string SaiTajoPlatform::getHwConfig() {
  auto chipConfigType = config()->thrift.platform()->chip()->getType();
  if (chipConfigType != facebook::fboss::cfg::ChipConfig::Type::asicConfig) {
    return *config()->thrift.platform()->chip().value().asic()->config();
  }

  auto& asicConfig =
      config()->thrift.platform()->chip().value().asicConfig().value();
  return asicConfig.common().value().jsonConfig().value();
}

std::vector<PortID> SaiTajoPlatform::getAllPortsInGroup(PortID portID) const {
  std::vector<PortID> allPortsinGroup;
  if (const auto& platformPorts = getPlatformPorts(); !platformPorts.empty()) {
    const auto& portList =
        utility::getPlatformPortsByControllingPort(platformPorts, portID);
    for (const auto& port : portList) {
      allPortsinGroup.emplace_back(*port.mapping()->id());
    }
  }
  return allPortsinGroup;
}

} // namespace facebook::fboss
