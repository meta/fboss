/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/tests/utils/CreateTestPlatform.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/tests/utils/BcmTestDarwinPlatform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestElbertPlatform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestFujiPlatform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestMinipackPlatform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestWedge100Platform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestWedge400Platform.h"
#include "fboss/agent/platforms/tests/utils/BcmTestYampPlatform.h"

namespace facebook::fboss {

PlatformType getPlatformType() {
  try {
    facebook::fboss::PlatformProductInfo productInfo(FLAGS_fruid_filepath);
    productInfo.initialize();
    return productInfo.getType();
  } catch (const facebook::fboss::FbossError&) {
    return PlatformType::PLATFORM_FAKE_WEDGE;
  }
}

std::unique_ptr<Platform> createTestPlatform() {
  auto mode = getPlatformType();
  std::unique_ptr<PlatformProductInfo> productInfo;
  if (mode != PlatformType::PLATFORM_FAKE_WEDGE) {
    productInfo = std::make_unique<PlatformProductInfo>(FLAGS_fruid_filepath);
    productInfo->initialize();
  }
  if (mode == PlatformType::PLATFORM_WEDGE100) {
    return std::make_unique<BcmTestWedge100Platform>(std::move(productInfo));
  } else if (mode == PlatformType::PLATFORM_MINIPACK) {
    return std::make_unique<BcmTestMinipackPlatform>(std::move(productInfo));
  } else if (mode == PlatformType::PLATFORM_YAMP) {
    return std::make_unique<BcmTestYampPlatform>(std::move(productInfo));
  } else if (mode == PlatformType::PLATFORM_WEDGE400) {
    return std::make_unique<BcmTestWedge400Platform>(std::move(productInfo));
  } else if (
      mode == PlatformType::PLATFORM_DARWIN ||
      mode == PlatformType::PLATFORM_DARWIN48V) {
    return std::make_unique<BcmTestDarwinPlatform>(std::move(productInfo));
  } else if (mode == PlatformType::PLATFORM_FUJI) {
    return std::make_unique<BcmTestFujiPlatform>(std::move(productInfo));
  } else if (mode == PlatformType::PLATFORM_ELBERT) {
    return std::make_unique<BcmTestElbertPlatform>(std::move(productInfo));
  } else {
    throw std::runtime_error("invalid mode ");
  }
}

} // namespace facebook::fboss
