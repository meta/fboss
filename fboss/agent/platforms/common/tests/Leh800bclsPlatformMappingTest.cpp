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

#include <filesystem>
#include <string>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsDualDrainPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsOsfpTrayPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsProdPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsTestPlatformMapping.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace fs = std::filesystem;

using namespace facebook::fboss;

namespace {

// Minimal fruid.json in the format Leh writes. "Production State" is stored as
// text, which PlatformProductInfo can't parse as an integer.
std::string fruid(const std::string& partNumber) {
  return R"({"Information": {"Version": "6", "Product Name": "LEH800BCLS",
    "Product Part Number": ")" +
      partNumber +
      R"(", "Production State": "EVT", "Production Sub-State": "2",
    "Switch ASIC MAC Base": "02:00:00:00:00:01",
    "Switch ASIC MAC Address Size": "1"}})";
}

class Leh800bclsPlatformMappingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_osfp_tray = false;
    FLAGS_test_fixture = false;
    fruidPath_ = (fs::path(tmpDir_.path().string()) / "fruid.json").string();
    FLAGS_fruid_filepath = fruidPath_;
  }

  void writeFruid(const std::string& contents) {
    ASSERT_TRUE(folly::writeFile(contents, fruidPath_.c_str()));
  }

  void expectMapping(const std::string& expectedMappingStr) {
    Leh800bclsPlatformMapping selected;
    Leh800bclsPlatformMapping expected(expectedMappingStr);
    EXPECT_EQ(selected.toThrift(), expected.toThrift());
  }

  gflags::FlagSaver flagSaver_;
  folly::test::TemporaryDirectory tmpDir_;
  std::string fruidPath_;
};

} // namespace

TEST_F(Leh800bclsPlatformMappingTest, Evt1PicksProdMapping) {
  writeFruid(fruid("20100438"));
  expectMapping(kJsonProdPlatformMappingStr);
}

TEST_F(
    Leh800bclsPlatformMappingTest,
    Evt1WithDashedPartNumberPicksProdMapping) {
  writeFruid(fruid("20-100438"));
  expectMapping(kJsonProdPlatformMappingStr);
}

TEST_F(Leh800bclsPlatformMappingTest, DualDrainPicksDualDrainMapping) {
  writeFruid(fruid("20100527"));
  expectMapping(kJsonDualDrainPlatformMappingStr);
}

TEST_F(Leh800bclsPlatformMappingTest, DrainlessPicksDualDrainMapping) {
  for (const auto* partNumber : {"20100513", "20100516", "20100526"}) {
    SCOPED_TRACE(partNumber);
    writeFruid(fruid(partNumber));
    expectMapping(kJsonDualDrainPlatformMappingStr);
  }
}

TEST_F(Leh800bclsPlatformMappingTest, MissingPartNumberPicksDualDrainMapping) {
  writeFruid(fruid(""));
  expectMapping(kJsonDualDrainPlatformMappingStr);
}

TEST_F(Leh800bclsPlatformMappingTest, MissingFruidPicksDualDrainMapping) {
  expectMapping(kJsonDualDrainPlatformMappingStr);
}

TEST_F(Leh800bclsPlatformMappingTest, MalformedFruidPicksDualDrainMapping) {
  writeFruid("not json");
  expectMapping(kJsonDualDrainPlatformMappingStr);
}

TEST_F(Leh800bclsPlatformMappingTest, TestFixtureFlagWins) {
  FLAGS_test_fixture = true;
  for (const auto* partNumber : {"20100438", "20100527"}) {
    writeFruid(fruid(partNumber));
    expectMapping(kJsonTestPlatformMappingStr);
  }
}

TEST_F(Leh800bclsPlatformMappingTest, OsfpTrayFlagWins) {
  FLAGS_osfp_tray = true;
  FLAGS_test_fixture = true;
  for (const auto* partNumber : {"20100438", "20100527"}) {
    writeFruid(fruid(partNumber));
    expectMapping(kJsonOsfpTrayPlatformMappingStr);
  }
}
