// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/platforms/common/PlatformMapping.h"

#include <filesystem>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

using namespace facebook::fboss;

namespace {

constexpr auto kOverrideMapJson = R"({
  "entries": [
    {
      "placeholder": {"name": "Vendor_1", "partNumber": "PartNum_1"},
      "patterns": [
        {"vendorName": "(?i)ACME", "partNumber": "(?i)LPO-\\d+"},
        {"vendorName": "(?i)OTHER", "partNumber": "(?i)X-LPO"}
      ]
    }
  ]
})";

bool match(
    const std::string& overrideVendor,
    const std::string& overridePart,
    const std::string& vendor,
    const std::string& part) {
  PlatformPortProfileConfigMatcher matcher(cfg::PortProfileID::PROFILE_DEFAULT);
  return matcher.isTransceiverVendorOverrideMatch(
      overrideVendor, overridePart, vendor, part);
}

} // namespace

// The map is loaded once per process, so a single fixture-wide file serves
// every case and the flag is set before the first match call.
class PlatformMappingVendorOverrideTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    tmpDir_ = std::make_unique<folly::test::TemporaryDirectory>();
    auto path = fs::path(tmpDir_->path().string()) / "vendor_overrides.json";
    ASSERT_TRUE(folly::writeFile(std::string(kOverrideMapJson), path.c_str()));
    FLAGS_transceiver_vendor_override_map_path = path.string();
  }

  static void TearDownTestSuite() {
    tmpDir_.reset();
  }

  static std::unique_ptr<folly::test::TemporaryDirectory> tmpDir_;
};

std::unique_ptr<folly::test::TemporaryDirectory>
    PlatformMappingVendorOverrideTest::tmpDir_;

TEST_F(PlatformMappingVendorOverrideTest, matchesListedVendorAndPart) {
  EXPECT_TRUE(match("Vendor_1", "PartNum_1", "ACME", "LPO-168"));
  EXPECT_TRUE(match("Vendor_1", "PartNum_1", "Other", "x-lpo"));
}

TEST_F(PlatformMappingVendorOverrideTest, rejectsUnlistedPart) {
  EXPECT_FALSE(match("Vendor_1", "PartNum_1", "ACME", "FRO-168"));
  EXPECT_FALSE(match("Vendor_1", "PartNum_1", "ACME", "LPO-168-extra"));
  EXPECT_FALSE(match("Vendor_1", "PartNum_1", "ACME", "xLPO-168"));
  EXPECT_FALSE(match("Vendor_1", "PartNum_1", "ACMEx", "LPO-168"));
}

TEST_F(PlatformMappingVendorOverrideTest, rejectsUnknownPlaceholder) {
  EXPECT_FALSE(match("Vendor_2", "PartNum_2", "ACME", "LPO-168"));
  EXPECT_FALSE(match("ACME", "LPO-168", "ACME", "LPO-168"));
}
