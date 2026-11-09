/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiPlatformInitImpl.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/platforms/common/morgan800cc/Morgan800ccPlatformMapping.h"
#include "fboss/agent/platforms/sai/GenericSaiTajoPlatform.h"
#include "fboss/agent/platforms/sai/SaiTajoPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiWedge400CPlatform.h"
#include "fboss/agent/platforms/sai/SaiWedge400CPlatformPort.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"

#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
namespace {

bool useGenericSaiTajoPlatform(PlatformType type) {
  switch (type) {
    case PlatformType::PLATFORM_WEDGE400C:
    case PlatformType::PLATFORM_WEDGE400C_VOQ:
    case PlatformType::PLATFORM_WEDGE400C_FABRIC:
    case PlatformType::PLATFORM_WEDGE400C_GRANDTETON:
      return false;
    default:
      return true;
  }
}

std::unique_ptr<PlatformMapping> createGenericSaiTajoPlatformMapping(
    PlatformType type,
    const std::string& platformMappingStr) {
  if (!platformMappingStr.empty()) {
    return std::make_unique<PlatformMapping>(platformMappingStr);
  }
  if (!FLAGS_platform_descriptor_config_path.empty()) {
    throw FbossError(
        "Generic SAI platform descriptor is missing platform mapping");
  }

  switch (type) {
    case PlatformType::PLATFORM_MORGAN800CC:
      return std::make_unique<Morgan800ccPlatformMapping>();
    default:
      throw FbossError(
          "Generic Tajo SAI platform is missing platform mapping for platform type ",
          apache::thrift::util::enumNameSafe(type));
  }
}

} // namespace

std::unique_ptr<SaiPlatform> createGenericSaiTajoPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac,
    const std::string& platformMappingStr) {
  const auto platformType = productInfo->getType();
  return std::make_unique<GenericSaiTajoPlatform>(
      std::move(productInfo),
      createGenericSaiTajoPlatformMapping(platformType, platformMappingStr),
      localMac);
}

std::unique_ptr<SaiPlatform> chooseSaiPlatformImpl(
    std::unique_ptr<PlatformProductInfo>& productInfo,
    folly::MacAddress localMac,
    const std::string& platformMappingStr) {
  const auto type = productInfo->getType();
  if (type == PlatformType::PLATFORM_WEDGE400C) {
    return std::make_unique<SaiWedge400CPlatform>(
        std::move(productInfo), localMac, platformMappingStr);
  }
  if (useGenericSaiTajoPlatform(type)) {
    return createGenericSaiTajoPlatform(
        std::move(productInfo), localMac, platformMappingStr);
  }
  return nullptr;
}

std::unique_ptr<SaiPlatformPort> createSaiPlatformPort(
    const PortID& portId,
    SaiPlatform* platform) {
  const auto platformMode = platform->getType();
  if (platformMode == PlatformType::PLATFORM_WEDGE400C ||
      platformMode == PlatformType::PLATFORM_WEDGE400C_VOQ ||
      platformMode == PlatformType::PLATFORM_WEDGE400C_FABRIC) {
    return std::make_unique<SaiWedge400CPlatformPort>(portId, platform);
  }
  if (useGenericSaiTajoPlatform(platformMode)) {
    return std::make_unique<SaiTajoPlatformPort>(portId, platform);
  }
  return nullptr;
}

} // namespace facebook::fboss
