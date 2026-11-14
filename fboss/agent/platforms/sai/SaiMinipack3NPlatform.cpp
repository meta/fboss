/*
 *  Copyright (c) 2023-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiMinipack3NPlatform.h"
#include "fboss/agent/platforms/common/minipack3n/Minipack3NPlatformMapping.h"

namespace facebook::fboss {

SaiMinipack3NPlatform::SaiMinipack3NPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac,
    const std::string& platformMappingStr)
    : GenericSaiYangraPlatform(
          std::move(productInfo),
          platformMappingStr.empty()
              ? std::make_unique<Minipack3NPlatformMapping>()
              : std::make_unique<Minipack3NPlatformMapping>(platformMappingStr),
          localMac) {}

const std::unordered_map<std::string, std::string>
SaiMinipack3NPlatform::getSaiProfileVendorExtensionValues() const {
  auto kv_map = GenericSaiYangraPlatform::getSaiProfileVendorExtensionValues();
  auto itr = kv_map.find("SAI_KEY_AUTO_POPULATE_PORT_DB");
  if (itr != kv_map.end()) {
    // Disable auto-population of the port database. When enabled, the SDK
    // discovers ports by querying the ASIC and populates its port DB
    // automatically. On Minipack3N, port definitions come from the seed
    // XML (minipack3n.xml) in the agent config, so auto-discovery is not
    // needed.
    kv_map.erase(itr);
  }

  // Override module mode from INDEPENDENT (1) to STANDALONE (2).
  // Minipack3N's board design has qsfp_service programming transceivers
  // directly over I2C without any ASIC firmware involvement. In
  // INDEPENDENT mode (base Yangra), the firmware still acts as a relay
  // for transceiver commands. STANDALONE removes the firmware from the
  // path entirely, which is required for Minipack3N's direct-attach
  // transceiver management architecture.
  // 0 = DEPENDENT: firmware owns transceiver programming
  // 1 = INDEPENDENT: external programs, firmware relays
  // 2 = STANDALONE: external programs, no firmware involvement
  kv_map.insert_or_assign("SAI_INDEPENDENT_MODULE_MODE", "2");

  // Set initial SerDes precoding for all ports at creation time, avoiding
  // the need to configure precoding per-port via port_serdes objects.
  // 0 = precoding unset (SDK default), 1 = ON, 2 = OFF.
  // Minipack3N sets to OFF based on board signal integrity requirements.
  kv_map.insert(std::make_pair("SAI_KEY_PORT_CREATE_INITIAL_PRECODING", "2"));
  return kv_map;
}

SaiMinipack3NPlatform::~SaiMinipack3NPlatform() = default;

std::string SaiMinipack3NPlatform::getHwConfig() {
  auto hwConfig = config()
                      ->thrift.platform()
                      ->chip()
                      ->asicConfig()
                      ->common()
                      ->config()
                      .value();
  auto constexpr kMinipack3nXml = "minipack3n.xml";
  if (!hwConfig.contains(kMinipack3nXml)) {
    throw FbossError("xml config not found in hw config");
  }
  return hwConfig[kMinipack3nXml];
}
} // namespace facebook::fboss
