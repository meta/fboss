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
#include "fboss/agent/platforms/common/yangra/YangraPlatformMapping.h"
#include "fboss/agent/platforms/common/yangra2/Yangra2PlatformMapping.h"
#include "fboss/agent/platforms/sai/GenericSaiYangraPlatform.h"
#include "fboss/agent/platforms/sai/SaiChenabPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiMinipack3NPlatform.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"

#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
namespace {

bool useGenericSaiYangraPlatform(PlatformType type) {
  switch (type) {
    case PlatformType::PLATFORM_MINIPACK3N:
      return false;
    default:
      return true;
  }
}

std::unique_ptr<PlatformMapping> createGenericSaiYangraPlatformMapping(
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
    case PlatformType::PLATFORM_YANGRA:
      return std::make_unique<YangraPlatformMapping>();
    case PlatformType::PLATFORM_YANGRA2:
      return std::make_unique<Yangra2PlatformMapping>();
    default:
      throw FbossError(
          "Generic Yangra SAI platform is missing platform mapping for platform type ",
          apache::thrift::util::enumNameSafe(type));
  }
}

} // namespace

std::unique_ptr<SaiPlatform> createGenericSaiYangraPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac,
    const std::string& platformMappingStr) {
  const auto platformType = productInfo->getType();
  return std::make_unique<GenericSaiYangraPlatform>(
      std::move(productInfo),
      createGenericSaiYangraPlatformMapping(platformType, platformMappingStr),
      localMac);
}

std::unique_ptr<SaiPlatform> chooseSaiPlatformImpl(
    std::unique_ptr<PlatformProductInfo>& productInfo,
    folly::MacAddress localMac,
    const std::string& platformMappingStr) {
  const auto type = productInfo->getType();
  if (useGenericSaiYangraPlatform(type)) {
    return createGenericSaiYangraPlatform(
        std::move(productInfo), localMac, platformMappingStr);
  }
  if (type == PlatformType::PLATFORM_MINIPACK3N) {
    return std::make_unique<SaiMinipack3NPlatform>(
        std::move(productInfo), localMac, platformMappingStr);
  }
  return nullptr;
}

std::unique_ptr<SaiPlatformPort> createSaiPlatformPort(
    const PortID& portId,
    SaiPlatform* platform) {
  const auto platformMode = platform->getType();
  if (useGenericSaiYangraPlatform(platformMode) ||
      platformMode == PlatformType::PLATFORM_MINIPACK3N) {
    return std::make_unique<SaiChenabPlatformPort>(portId, platform);
  }
  return nullptr;
}

} // namespace facebook::fboss
