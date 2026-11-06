/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/EbroAsic.h"
#include "fboss/agent/hw/switch_asics/FakeAsic.h"
#include "fboss/agent/hw/switch_asics/MockAsic.h"
#include "fboss/agent/hw/switch_asics/P200Asic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk6Asic.h"
#include "fboss/agent/hw/switch_asics/YubaAsic.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {
cfg::SwitchInfo npuSwitchInfo() {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::NPU;
  switchInfo.switchMac() = "02:00:00:00:0F:0B";
  switchInfo.switchIndex() = 0;
  return switchInfo;
}

cfg::SdkVersion saiSdk(const std::string& version) {
  cfg::SdkVersion sdkVersion;
  sdkVersion.saiSdk() = version;
  return sdkVersion;
}

Tomahawk6Asic asicWithSaiSdk(const std::string& version) {
  return Tomahawk6Asic{0, npuSwitchInfo(), saiSdk(version)};
}
} // namespace

// Netcastle substitutes the real SDK string into the agent config per test
// config, so these are the forms the comparison actually sees. The early
// access / GA distinction comes from the version named by the caller, not
// from a separate argument.
TEST(SaiSdkVersionTest, earlyAccessSortsBelowItsOwnLineGa) {
  EXPECT_FALSE(asicWithSaiSdk("15.4_ea_odp").saiSdkAtLeast("15.4.0.0_odp"));
  EXPECT_TRUE(asicWithSaiSdk("15.4.0.0_odp").saiSdkAtLeast("15.4.0.0_odp"));
  EXPECT_TRUE(asicWithSaiSdk("16.0_ea_odp").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_TRUE(asicWithSaiSdk("16.0.0.0_odp").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_FALSE(asicWithSaiSdk("16.0_ea_odp").saiSdkAtLeast("16.0.0.0_odp"));
}

TEST(SaiSdkVersionTest, comparesAcrossLines) {
  EXPECT_TRUE(asicWithSaiSdk("16.0_ea_odp").saiSdkAtLeast("15.4.0.0_odp"));
  EXPECT_FALSE(asicWithSaiSdk("15.4.0.0_odp").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_FALSE(asicWithSaiSdk("14.2.0.0_odp").saiSdkAtLeast("15.4.0.0_odp"));
  EXPECT_TRUE(asicWithSaiSdk("17.1.0.0_odp").saiSdkAtLeast("16.0_ea_odp"));
}

// Only major, minor and the EA/GA rank are compared. Trailing fields are not
// ordered across vendors - tajo's 5210/5211 are variant codes rather than
// successive versions - so a caller cannot express a patch level minimum.
TEST(SaiSdkVersionTest, patchAndBuildFieldsAreIgnored) {
  EXPECT_TRUE(asicWithSaiSdk("15.4.0.0_odp").saiSdkAtLeast("15.4.9.9_odp"));
  EXPECT_TRUE(asicWithSaiSdk("15.4.9.9_odp").saiSdkAtLeast("15.4.0.0_odp"));
  EXPECT_TRUE(asicWithSaiSdk("16.0_ea_dnx_odp").saiSdkAtLeast("16.0_ea_odp"));
}

// The shared getAsicSdkVersion() assigns components by dot count and so reads
// "15.4_ea_odp" as 15.0.4. Both sides must fail closed on anything they
// cannot parse rather than inherit that misreading.
TEST(SaiSdkVersionTest, unparseableVersionsFailClosed) {
  EXPECT_FALSE(asicWithSaiSdk("sai").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_FALSE(asicWithSaiSdk("").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_FALSE(asicWithSaiSdk("_ea_odp").saiSdkAtLeast("16.0_ea_odp"));
  EXPECT_FALSE(asicWithSaiSdk("16.0_ea_odp").saiSdkAtLeast("bogus"));
  EXPECT_FALSE(Tomahawk6Asic(0, npuSwitchInfo()).saiSdkAtLeast("16.0_ea_odp"));
}

// 15.4 is GA and is not being patched for the qualifier, so unlike the TTL
// trap the bar is 16.0 and the early access drop at that boundary qualifies.
TEST(SaiSdkVersionTest, aclMplsLabel0TtlFollowsEaBoundary) {
  EXPECT_FALSE(asicWithSaiSdk("14.2.0.0_odp")
                   .isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
  EXPECT_FALSE(asicWithSaiSdk("15.4_ea_odp")
                   .isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
  EXPECT_FALSE(asicWithSaiSdk("15.4.0.0_odp")
                   .isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
  EXPECT_TRUE(asicWithSaiSdk("16.0_ea_odp")
                  .isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
  EXPECT_TRUE(asicWithSaiSdk("16.0.0.0_odp")
                  .isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
  EXPECT_FALSE(asicWithSaiSdk("sai").isSupported(
      HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
}

// Neither ASIC implements the qualifier. Both sit next to
// SAI_MPLS_TTL_1_TRAP, which they do support, so a case appended there would
// silently inherit a true verdict.
TEST(SaiSdkVersionTest, aclMplsLabel0TtlOffOnAsicsThatNeighbourTheTtlTrap) {
  EbroAsic ebro{0, npuSwitchInfo(), saiSdk("26.7.5211")};
  EXPECT_TRUE(ebro.isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_FALSE(ebro.isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));

  P200Asic p200{0, npuSwitchInfo(), saiSdk("26.7.5211")};
  EXPECT_TRUE(p200.isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_FALSE(p200.isSupported(HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL));
}

// The placeholder netcastle overwrites. If substitution ever regresses, the
// feature must stay off rather than be advertised against an SDK that lacks it.
TEST(SaiSdkVersionTest, mplsTtl1TrapFollowsGaBoundary) {
  EXPECT_FALSE(asicWithSaiSdk("14.2.0.0_odp")
                   .isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_FALSE(asicWithSaiSdk("15.4_ea_odp")
                   .isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_TRUE(asicWithSaiSdk("15.4.0.0_odp")
                  .isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_TRUE(asicWithSaiSdk("16.0_ea_odp")
                  .isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
  EXPECT_FALSE(
      asicWithSaiSdk("sai").isSupported(HwAsic::Feature::SAI_MPLS_TTL_1_TRAP));
}

TEST(SaiSdkVersionTest, yubaUsesPortTtlDecrementDisableOnMorganSdk) {
  YubaAsic yuba{0, npuSwitchInfo(), saiSdk("26.2.4210")};
  EXPECT_FALSE(
      yuba.isSupported(HwAsic::Feature::SAI_TTL0_PACKET_FORWARD_ENABLE));
  EXPECT_TRUE(yuba.isSupported(HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE));
  EXPECT_TRUE(yuba.isSupported(HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE));
}

TEST(HwAsicFeatureTest, portLatchStatusIsLimitedToYuba) {
  EbroAsic ebro{0, npuSwitchInfo()};
  EXPECT_FALSE(ebro.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));

  YubaAsic yuba{0, npuSwitchInfo()};
  EXPECT_TRUE(yuba.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));

  P200Asic p200{0, npuSwitchInfo()};
  EXPECT_FALSE(p200.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));

  FakeAsic fake{0, npuSwitchInfo()};
  EXPECT_FALSE(fake.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));

  MockAsic mock{0, npuSwitchInfo()};
  EXPECT_FALSE(mock.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));

  Tomahawk6Asic tomahawk6{0, npuSwitchInfo()};
  EXPECT_FALSE(tomahawk6.isSupported(HwAsic::Feature::PORT_LATCH_STATUS));
}
