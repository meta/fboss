/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/lib/phy/CredoSdkVersion.h"
#include "fboss/lib/phy/ExternalPhy.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

class HwXphyFirmwareTest : public HwTest {
  std::vector<qsfp_production_features::QsfpProductionFeature>
  getProductionFeatures() const override {
    return {qsfp_production_features::QsfpProductionFeature::EXTERNAL_PHY};
  }
};

TEST_F(HwXphyFirmwareTest, CheckDefaultXphyFirmwareVersion) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::XPHY_PROGRAMMING});
  auto platformType = getHwQsfpEnsemble()->getWedgeManager()->getPlatformType();

  phy::PhyFwVersion desiredFw;
  switch (platformType) {
    case PlatformType::PLATFORM_ELBERT:
      desiredFw.version() = 1;
#if CREDO_SDK_VERSION >= CREDO_SDK_VERSION_1_2_7
      // SDK 1.2.7+ returns minorVersion as (minor << 16) | patch
      // Firmware 1.94.11 -> minorVersion = (94 << 16) | 11 = 6160395
      desiredFw.versionStr() = "1.6160395";
      desiredFw.minorVersion() = 6160395;
#else
      desiredFw.versionStr() = "1.93";
      desiredFw.minorVersion() = 93;
#endif
      break;
    case PlatformType::PLATFORM_FUJI:
#ifdef BARCHETTA2_SDK_7_4
      desiredFw.version() = 0xD013;
      desiredFw.crc() = 0x5d6e3f0e;
#else
      desiredFw.version() = 0xD008;
      desiredFw.crc() = 0x4dcf6a59;
#endif
      break;
    case PlatformType::PLATFORM_MINIPACK:
      desiredFw.version() = 0xD037;
      desiredFw.crc() = 0xbf86d423;
      break;
    case PlatformType::PLATFORM_YAMP:
      desiredFw.version() = 0x3894F5;
      desiredFw.versionStr() = "2.18.2";
      desiredFw.crc() = 0x5B4C;
      desiredFw.dateCode() = 18423;
      break;
    case PlatformType::PLATFORM_SANDIA:
      desiredFw.version() = 0;
      desiredFw.versionStr() = "0.0";
      desiredFw.minorVersion() = 0;
      break;
    case PlatformType::PLATFORM_LADAKH800BCLS:
    case PlatformType::PLATFORM_LEH800BCLS:
      // PAI fw does not support Firmware Minor version
      // Both Ladakh and Leh platforms are on PAI SDK 4.1 and expect 0xD007
      // (from AGR3_1_3)
      desiredFw.version() = 0xD007;
      desiredFw.versionStr() = "53255"; // 0xD007 = 53255 decimal
      break;
    default:
      throw FbossError("No xphys to check FW version on");
  }

  auto chips = getHwQsfpEnsemble()->getPlatformMapping()->getChips();
  if (!chips.size()) {
    return;
  }
  for (auto chip : chips) {
    if (folly::copy(chip.second.type().value()) !=
        phy::DataPlanePhyChipType::XPHY) {
      continue;
    }

    // There are some Sandia PIM's which does not have XPHY but the platform
    // mapping is generic so we need to first check if the external Phy is
    // present before attempting this test
    phy::ExternalPhy* xphy;
    try {
      xphy = getHwQsfpEnsemble()->getPhyManager()->getExternalPhy(
          GlobalXphyID(folly::copy(chip.second.physicalID().value())));
    } catch (FbossError&) {
      XLOG(ERR) << "XPHY not present in system "
                << GlobalXphyID(folly::copy(chip.second.physicalID().value()));
      continue;
    }

    const auto& actualFw = xphy->fwVersion();
    EXPECT_EQ(actualFw.version(), desiredFw.version());
    EXPECT_EQ(actualFw.versionStr(), desiredFw.versionStr());
    EXPECT_EQ(actualFw.crc(), desiredFw.crc());
    EXPECT_EQ(actualFw.minorVersion(), desiredFw.minorVersion());
    EXPECT_EQ(actualFw.dateCode(), desiredFw.dateCode());
  }
}
} // namespace facebook::fboss
