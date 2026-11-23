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
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"

#include <gtest/gtest.h>
#include <memory>

#include "fboss/agent/AgentFeatures.h"

using namespace facebook::fboss;

class Jericho3AsicTest : public ::testing::Test {
 public:
  void SetUp() override {
    cfg::Range64 sysPortRange;
    sysPortRange.minimum() = 100;
    sysPortRange.maximum() = 144;
    cfg::SwitchInfo switchInfo;
    switchInfo.systemPortRanges()->systemPortRanges()->push_back(sysPortRange);
    switchInfo.switchType() = cfg::SwitchType::VOQ;
    switchInfo.switchMac() = "02:00:00:00:0F:0B";
    switchInfo.switchIndex() = 0;
    asic_ = std::make_unique<Jericho3Asic>(0, switchInfo);
  }
  std::unique_ptr<Jericho3Asic> asic_;
};

TEST_F(Jericho3AsicTest, cgmErrorsRequireSaiSdk12_2) {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::VOQ;
  switchInfo.switchMac() = "02:00:00:00:0F:0B";
  switchInfo.switchIndex() = 0;
  const std::vector<std::pair<std::string, bool>> cases{
      {"11.7.0.0_dnx_odp", false},
      {"12.0.0.0_dnx_odp", false},
      {"12.2.0.0_dnx_odp", true},
      {"14.2.0.0_dnx_odp", true},
      {"16.0_ea_dnx_odp", true},
      {"sai", false}};
  for (const auto& [version, expected] : cases) {
    cfg::SdkVersion sdkVersion;
    sdkVersion.saiSdk() = version;
    Jericho3Asic asic(0, switchInfo, sdkVersion);
    EXPECT_EQ(
        asic.isSupported(
            HwAsic::Feature::VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS),
        expected)
        << version;
  }
  EXPECT_FALSE(asic_->isSupported(
      HwAsic::Feature::VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS));
}

// The group cap must never let the member table be oversubscribed:
// groups * ecmp_width <= getMaxEcmpMembers(). Hyperport EDSW runs 2K wide
// without 3q2q mode set, so a stage-keyed cap hands it 64 groups against a
// member table that holds 16 of them.
class Jericho3AsicMaxEcmpGroupsTest : public Jericho3AsicTest {
 public:
  void SetUp() override {
    Jericho3AsicTest::SetUp();
    savedEcmpWidth_ = FLAGS_ecmp_width;
    savedDualStageEdsw_ = FLAGS_dual_stage_edsw_3q_2q;
    savedDualStageRdsw_ = FLAGS_dual_stage_rdsw_3q_2q;
  }
  void TearDown() override {
    FLAGS_ecmp_width = savedEcmpWidth_;
    FLAGS_dual_stage_edsw_3q_2q = savedDualStageEdsw_;
    FLAGS_dual_stage_rdsw_3q_2q = savedDualStageRdsw_;
  }

 private:
  uint32_t savedEcmpWidth_{};
  bool savedDualStageEdsw_{};
  bool savedDualStageRdsw_{};
};

TEST_F(Jericho3AsicMaxEcmpGroupsTest, singleStageWidth) {
  FLAGS_dual_stage_edsw_3q_2q = false;
  FLAGS_dual_stage_rdsw_3q_2q = false;
  FLAGS_ecmp_width = 512;
  EXPECT_EQ(asic_->getMaxEcmpGroups(), 64);
}

TEST_F(Jericho3AsicMaxEcmpGroupsTest, dualStageWidth) {
  FLAGS_dual_stage_rdsw_3q_2q = true;
  FLAGS_ecmp_width = 2048;
  EXPECT_EQ(asic_->getMaxEcmpGroups(), 16);
}

// Hyperport EDSW: 2K wide, but not 3q2q. Must get the 2K-wide cap.
TEST_F(Jericho3AsicMaxEcmpGroupsTest, wideEcmpOutsideDualStage) {
  FLAGS_dual_stage_edsw_3q_2q = false;
  FLAGS_dual_stage_rdsw_3q_2q = false;
  FLAGS_ecmp_width = 2048;
  EXPECT_EQ(asic_->getMaxEcmpGroups(), 16);
}

// The invariant the cap exists to enforce, checked independently of any
// single expected value.
TEST_F(Jericho3AsicMaxEcmpGroupsTest, capNeverOversubscribesMemberTable) {
  ASSERT_TRUE(asic_->getMaxEcmpMembers().has_value());
  const auto maxMembers = *asic_->getMaxEcmpMembers();
  for (auto width : {64u, 128u, 256u, 512u, 1024u, 2048u}) {
    FLAGS_ecmp_width = width;
    auto groups = asic_->getMaxEcmpGroups();
    ASSERT_TRUE(groups.has_value());
    EXPECT_LE(*groups * width, maxMembers)
        << "width " << width << " x " << *groups << " groups oversubscribes "
        << maxMembers << " members";
  }
}

// Narrow widths can use more groups than the legacy 64-group limit while
// remaining bounded by both the member table and the 4K group table.
TEST_F(Jericho3AsicMaxEcmpGroupsTest, narrowWidthUsesMemberTableCapacity) {
  FLAGS_ecmp_width = 64;
  EXPECT_EQ(asic_->getMaxEcmpGroups(), 512);
}

TEST_F(Jericho3AsicTest, checkPortGroups) {
  const std::vector<std::pair<int, int>> kExpectedPortGroups{
      {1024, 1063}, {1064, 1103}, {1104, 1143}, {1144, 1183}};
  auto portGroups = asic_->getPortGroups();
  EXPECT_EQ(portGroups, kExpectedPortGroups);
}

TEST_F(Jericho3AsicTest, noCableLens) {
  EXPECT_FALSE(asic_->computePortGroupSkew({}).has_value());
}

TEST_F(Jericho3AsicTest, singlePortGroupCables) {
  std::map<PortID, uint32_t> singlePortGroup{
      {PortID(1024), 100},
      {PortID(1034), 200},
  };
  EXPECT_EQ(asic_->computePortGroupSkew(singlePortGroup), 0);
}

TEST_F(Jericho3AsicTest, fourPortGroupCables) {
  {
    // Skew b/w PG1 and PG2
    std::map<PortID, uint32_t> portGroups{
        // PG1
        {PortID(1024), 100},
        {PortID(1034), 200},
        // PG2
        {PortID(1065), 100},
        {PortID(1075), 50},
        // PG3
        {PortID(1104), 100},
        {PortID(1114), 150},
        // PG4
        {PortID(1144), 100},
        {PortID(1154), 100},

    };
    EXPECT_EQ(asic_->computePortGroupSkew(portGroups), 100);
  }
  {
    // Skew b/w PG1 and PG3
    std::map<PortID, uint32_t> portGroups{
        // PG1
        {PortID(1024), 100},
        {PortID(1034), 200},
        // PG2
        {PortID(1065), 100},
        {PortID(1075), 150},
        // PG3
        {PortID(1104), 100},
        {PortID(1114), 50},
        // PG4
        {PortID(1144), 100},
        {PortID(1154), 100},

    };
    EXPECT_EQ(asic_->computePortGroupSkew(portGroups), 100);
  }
  {
    // Skew b/w PG1 and PG4
    std::map<PortID, uint32_t> portGroups{
        // PG1
        {PortID(1024), 100},
        {PortID(1034), 200},
        // PG2
        {PortID(1065), 100},
        {PortID(1075), 150},
        // PG3
        {PortID(1104), 100},
        {PortID(1114), 150},
        // PG4
        {PortID(1144), 100},
        {PortID(1154), 50},

    };
    EXPECT_EQ(asic_->computePortGroupSkew(portGroups), 100);
  }
}
