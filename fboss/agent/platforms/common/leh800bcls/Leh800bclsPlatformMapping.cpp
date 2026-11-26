/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsPlatformMapping.h"
#include <cctype>
#include <string>

#include <folly/logging/xlog.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsDualDrainPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsOsfpTrayPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsProdPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsTestPlatformMapping.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace facebook::fboss {
namespace {
// Only EVT1 boards (FBPN 20-100438) keep the old rack mapping. Every other
// board gets the dual drain platform mapping which EVT2 drainless boards also
// share.
constexpr auto kEvt1PartNumber = "20100438";

std::string readPartNumber() {
  PlatformProductInfo productInfo(FLAGS_fruid_filepath);
  try {
    productInfo.initialize();
  } catch (const std::exception& ex) {
    // Any failure leaves the part number empty, which selects dual drain.
    XLOG(INFO) << "Couldn't fully initialize product info: " << ex.what();
  }
  ProductInfo info;
  productInfo.getInfo(info);
  return *info.partNumber();
}

// Also accept the dashed form ("20-100438") used in SeRF and BOMs, in case an
// EEPROM was programmed that way.
bool isEvt1PartNumber(std::string partNumber) {
  std::erase_if(
      partNumber, [](unsigned char c) { return c == '-' || std::isspace(c); });
  return partNumber == kEvt1PartNumber;
}

static const std::string getPlatformMappingStr() {
  if (FLAGS_osfp_tray) {
    XLOG(INFO) << "Using OSFP Tray Platform Mapping";
    return kJsonOsfpTrayPlatformMappingStr;
  }
  if (FLAGS_test_fixture) {
    XLOG(INFO) << "Using Test Fixture Platform Mapping";
    return kJsonTestPlatformMappingStr;
  }
  const auto partNumber = readPartNumber();
  XLOG(INFO) << "Product part number: '" << partNumber << "'";
  if (isEvt1PartNumber(partNumber)) {
    XLOG(INFO) << "Using EVT1 Drainless Platform Mapping";
    return kJsonProdPlatformMappingStr;
  }
  XLOG(INFO) << "Using Dual Drain Platform Mapping";
  return kJsonDualDrainPlatformMappingStr;
}
} // namespace

Leh800bclsPlatformMapping::Leh800bclsPlatformMapping()
    : PlatformMapping(getPlatformMappingStr()) {}

Leh800bclsPlatformMapping::Leh800bclsPlatformMapping(
    const std::string& platformMappingStr)
    : PlatformMapping(platformMappingStr) {}

} // namespace facebook::fboss
