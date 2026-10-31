// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/platform/config_lib/ConfigLib.h"

using namespace ::testing;
using namespace facebook::fboss::platform;

namespace {
const std::string kDarwin = "darwin";
const std::string kMontblanc = "montblanc";
const std::string kMeru800bia = "meru800bia";
const std::string kMeru800bfa = "meru800bfa";
const std::string kMorgan800cc = "morgan800cc";
const std::string kJanga800bic = "janga800bic";
const std::string kTahan800bc = "tahan800bc";
const std::string kBlackwolf800banw = "blackwolf800banw";
const std::string kIcecube = "icecube";
const std::string kM4061nhp = "m4061nhp";
const std::string kM4062nhp = "m4062nhp";
const std::string kM4340nhp = "m4340nhp";
const std::string kSample = "sample";
const std::string kWedge800cact = "wedge800cact";
const std::string kWedge800cnhp = "wedge800cnhp";
const std::string kNonExistentPlatform = "nonExistentPlatform";
} // namespace

TEST(ConfigLibTest, Basic) {
  // SensorService Configs
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kDarwin));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kMeru800bia));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kMorgan800cc));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kJanga800bic));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kTahan800bc));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kBlackwolf800banw));
  EXPECT_NO_THROW(ConfigLib().getSensorServiceConfig(kM4061nhp));
  EXPECT_THROW(
      ConfigLib().getSensorServiceConfig(kNonExistentPlatform),
      std::runtime_error);

  // FanService Configs
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kDarwin));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kMeru800bia));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kMorgan800cc));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kJanga800bic));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kTahan800bc));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kBlackwolf800banw));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kSample));
  EXPECT_NO_THROW(ConfigLib().getFanServiceConfig(kM4061nhp));
  EXPECT_THROW(
      ConfigLib().getFanServiceConfig(kNonExistentPlatform),
      std::runtime_error);

  // PlatformManager Configs
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kMeru800bia));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kMontblanc));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kM4061nhp));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kM4062nhp));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kM4340nhp));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kMorgan800cc));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kJanga800bic));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kTahan800bc));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kBlackwolf800banw));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kIcecube));
  EXPECT_NO_THROW(ConfigLib().getPlatformManagerConfig(kSample));
  EXPECT_THROW(
      ConfigLib().getPlatformManagerConfig(kNonExistentPlatform),
      std::runtime_error);

  // fwutil Configs
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kDarwin));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kMeru800bia));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kJanga800bic));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kTahan800bc));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kM4061nhp));
  EXPECT_NO_THROW(ConfigLib().getFwUtilConfig(kM4340nhp));
  EXPECT_THROW(
      ConfigLib().getFwUtilConfig(kNonExistentPlatform), std::runtime_error);

  // LedManager Configs
  EXPECT_NO_THROW(ConfigLib().getLedManagerConfig(kDarwin));
  EXPECT_NO_THROW(ConfigLib().getLedManagerConfig(kMorgan800cc));
  EXPECT_NO_THROW(ConfigLib().getLedManagerConfig(kM4061nhp));
  EXPECT_THROW(
      ConfigLib().getLedManagerConfig(kNonExistentPlatform),
      std::runtime_error);

  // BspTests Configs
  EXPECT_NO_THROW(ConfigLib().getBspTestConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getBspTestConfig(kBlackwolf800banw));
  EXPECT_NO_THROW(ConfigLib().getBspTestConfig(kM4061nhp));
  EXPECT_NO_THROW(ConfigLib().getBspTestConfig(kM4062nhp));
  EXPECT_NO_THROW(ConfigLib().getBspTestConfig(kM4340nhp));
  EXPECT_THROW(
      ConfigLib().getBspTestConfig(kNonExistentPlatform), std::out_of_range);

  // Showtech Configs
  EXPECT_NO_THROW(ConfigLib().getShowtechConfig(kMorgan800cc));
  EXPECT_NO_THROW(ConfigLib().getShowtechConfig(kSample));
  EXPECT_NO_THROW(ConfigLib().getShowtechConfig(kM4061nhp));
  EXPECT_THROW(
      ConfigLib().getShowtechConfig(kNonExistentPlatform), std::out_of_range);

  // RebootCauseFinder Configs
  EXPECT_NO_THROW(ConfigLib().getRebootCauseFinderConfig(kMeru800bfa));
  EXPECT_NO_THROW(ConfigLib().getRebootCauseFinderConfig(kMeru800bia));
  EXPECT_THROW(
      ConfigLib().getRebootCauseFinderConfig(kNonExistentPlatform),
      std::runtime_error);
}

TEST(ConfigLibTest, Wedge800cnhpReusesWedge800cactConfig) {
  // WEDGE800CNHP is distinct hardware that reuses WEDGE800CACT's config.
  EXPECT_EQ(
      ConfigLib().getPlatformManagerConfig(kWedge800cnhp),
      ConfigLib().getPlatformManagerConfig(kWedge800cact));
  EXPECT_EQ(
      ConfigLib().getSensorServiceConfig(kWedge800cnhp),
      ConfigLib().getSensorServiceConfig(kWedge800cact));
}

TEST(ConfigLibTest, Wedge800bnhpReusesWedge800bactConfig) {
  EXPECT_EQ(
      ConfigLib().getPlatformManagerConfig("wedge800bnhp"),
      ConfigLib().getPlatformManagerConfig("wedge800bact"));
  EXPECT_EQ(
      ConfigLib().getSensorServiceConfig("wedge800bnhp"),
      ConfigLib().getSensorServiceConfig("wedge800bact"));
}

TEST(ConfigLibTest, CanonicalConfigPlatformNameResolvesAlias) {
  // An aliased platform resolves to the platform whose config it reuses.
  EXPECT_EQ(
      ConfigLib::canonicalConfigPlatformName("WEDGE800CNHP"), "WEDGE800CACT");
  EXPECT_EQ(
      ConfigLib::canonicalConfigPlatformName("wedge800cnhp"), "WEDGE800CACT");
  // A non-aliased platform is returned unchanged, normalized to uppercase.
  EXPECT_EQ(
      ConfigLib::canonicalConfigPlatformName("WEDGE800CACT"), "WEDGE800CACT");
  EXPECT_EQ(ConfigLib::canonicalConfigPlatformName("montblanc"), "MONTBLANC");
}

TEST(ConfigLibTest, VerifyPlatformNameMatches) {
  // Exact match passes.
  EXPECT_NO_THROW(
      ConfigLib::verifyPlatformNameMatches("MONTBLANC", "MONTBLANC"));
  // The config name's case is normalized before comparing.
  EXPECT_NO_THROW(
      ConfigLib::verifyPlatformNameMatches("montblanc", "MONTBLANC"));
  // An aliased inferred name matches the target platform's config.
  EXPECT_NO_THROW(
      ConfigLib::verifyPlatformNameMatches("WEDGE800CACT", "WEDGE800CNHP"));
  // A genuine mismatch throws.
  EXPECT_THROW(
      ConfigLib::verifyPlatformNameMatches("MONTBLANC", "DARWIN"),
      std::runtime_error);
}
