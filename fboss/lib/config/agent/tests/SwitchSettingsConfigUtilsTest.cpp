/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/lib/config/agent/SwitchSettingsConfigUtils.h"

namespace facebook::fboss::utility {
namespace {

struct DualNpuTestCase {
  PlatformType platformType;
  std::optional<PlatformCPUType> platformCPUType;
  std::array<std::string, 2> connectionHandles;
};

struct DefaultConnectionHandleTestCase {
  cfg::AsicType asicType;
  std::optional<std::string_view> connectionHandle;
};

} // namespace

TEST(SwitchSettingsConfigUtilsTest, GeneratesSingleNpuSwitchSettings) {
  const auto switchSettings =
      genNpuSwitchSettingsConfig(cfg::AsicType::ASIC_TYPE_TOMAHAWK5);

  EXPECT_EQ(*switchSettings.switchType(), cfg::SwitchType::NPU);
  EXPECT_TRUE(*switchSettings.needL2EntryForNeighbor());
  const auto& switchInfos = *switchSettings.switchIdToSwitchInfo();
  ASSERT_EQ(switchInfos.size(), 1);
  const auto& switchInfo = switchInfos.at(0);
  EXPECT_EQ(*switchInfo.switchType(), cfg::SwitchType::NPU);
  EXPECT_EQ(*switchInfo.asicType(), cfg::AsicType::ASIC_TYPE_TOMAHAWK5);
  EXPECT_EQ(*switchInfo.switchIndex(), 0);
  EXPECT_EQ(
      *switchInfo.portIdRange()->minimum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN());
  EXPECT_EQ(
      *switchInfo.portIdRange()->maximum(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX());
  EXPECT_FALSE(switchInfo.connectionHandle().has_value());
}

TEST(SwitchSettingsConfigUtilsTest, GeneratesSingleNpuTomahawk6SwitchSettings) {
  const auto switchSettings = genNpuSwitchSettingsConfig(
      cfg::AsicType::ASIC_TYPE_TOMAHAWK6, PlatformType::PLATFORM_ICECUBE800BC);

  const auto& switchInfos = *switchSettings.switchIdToSwitchInfo();
  ASSERT_EQ(switchInfos.size(), 1);
  const auto& switchInfo = switchInfos.at(0);
  EXPECT_EQ(*switchInfo.asicType(), cfg::AsicType::ASIC_TYPE_TOMAHAWK6);
  EXPECT_FALSE(switchInfo.connectionHandle().has_value());
}

TEST(SwitchSettingsConfigUtilsTest, ReturnsDefaultConnectionHandles) {
  const std::vector<DefaultConnectionHandleTestCase> testCases{
      {cfg::AsicType::ASIC_TYPE_RAMON, "0c:00"},
      {cfg::AsicType::ASIC_TYPE_RAMON3, "15:00"},
      {cfg::AsicType::ASIC_TYPE_JERICHO2, "68:00"},
      {cfg::AsicType::ASIC_TYPE_JERICHO3, "15:00"},
      {cfg::AsicType::ASIC_TYPE_JERICHO4, "15:00"},
      {cfg::AsicType::ASIC_TYPE_EBRO, "/dev/uio0"},
      {cfg::AsicType::ASIC_TYPE_P200, "/dev/uio0"},
      {cfg::AsicType::ASIC_TYPE_YUBA, "/dev/uio0"},
      {cfg::AsicType::ASIC_TYPE_G202X, "/dev/uio0"},
      {cfg::AsicType::ASIC_TYPE_TOMAHAWK5, std::nullopt},
      {cfg::AsicType::ASIC_TYPE_TOMAHAWK6, std::nullopt},
  };

  for (const auto& testCase : testCases) {
    EXPECT_EQ(
        getDefaultConnectionHandle(testCase.asicType),
        testCase.connectionHandle);
  }
}

TEST(SwitchSettingsConfigUtilsTest, AppliesDefaultConnectionHandle) {
  const auto switchSettings =
      genNpuSwitchSettingsConfig(cfg::AsicType::ASIC_TYPE_G202X);

  EXPECT_EQ(
      *switchSettings.switchIdToSwitchInfo()->at(0).connectionHandle(),
      "/dev/uio0");
}

TEST(SwitchSettingsConfigUtilsTest, GeneratesLadakhAndLehSwitchSettings) {
  const std::vector<DualNpuTestCase> testCases{
      {
          PlatformType::PLATFORM_LADAKH800BCLS,
          std::nullopt,
          {"0000:15:00=0", "0000:18:00=0"},
      },
      {
          PlatformType::PLATFORM_LEH800BCLS,
          PlatformCPUType::INTEL,
          {"0000:15:00=0", "0000:18:00=0"},
      },
      {
          PlatformType::PLATFORM_LADAKH800BCLS,
          PlatformCPUType::AMD,
          {"0000:03:00=0", "0000:04:00=0"},
      },
  };

  for (const auto& testCase : testCases) {
    const auto switchSettings = genNpuSwitchSettingsConfig(
        cfg::AsicType::ASIC_TYPE_TOMAHAWK6,
        testCase.platformType,
        testCase.platformCPUType);

    const auto& switchInfos = *switchSettings.switchIdToSwitchInfo();
    ASSERT_EQ(switchInfos.size(), 2);
    for (int16_t switchIndex = 0; switchIndex < 2; ++switchIndex) {
      const auto& switchInfo = switchInfos.at(switchIndex);
      EXPECT_EQ(*switchInfo.switchType(), cfg::SwitchType::NPU);
      EXPECT_EQ(*switchInfo.asicType(), cfg::AsicType::ASIC_TYPE_TOMAHAWK6);
      EXPECT_EQ(*switchInfo.switchIndex(), switchIndex);
      EXPECT_EQ(
          *switchInfo.portIdRange()->minimum(), switchIndex == 0 ? 0 : 2048);
      EXPECT_EQ(
          *switchInfo.portIdRange()->maximum(), switchIndex == 0 ? 2047 : 4095);
      EXPECT_EQ(
          *switchInfo.connectionHandle(),
          testCase.connectionHandles[switchIndex]);
    }
  }
}

} // namespace facebook::fboss::utility
