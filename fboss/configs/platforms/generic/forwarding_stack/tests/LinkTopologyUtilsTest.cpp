/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.h"

namespace facebook::fboss::configgen {
namespace {

namespace fs = std::filesystem;

void writeTestFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  if (!folly::writeFile(contents, path.c_str())) {
    throw std::runtime_error("Unable to write test file " + path.string());
  }
}

TEST(LinkTopologyUtilsTest, ParsesPairedSelfLoopAndUnmodeledPeerEntries) {
  constexpr std::string_view kCsv =
      "portA,portB,profile,hasTransceiver\n"
      "eth1/1/1,eth1/2/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,true\n"
      "eth1/65/1,eth1/65/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,false\n"
      "eth1/3/1,,PROFILE_100G_4_NRZ_RS528_OPTICAL,true\n";
  const LinkTopology expected{
      {.portA = "eth1/1/1",
       .portB = "eth1/2/1",
       .profile = cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
       .hasTransceiver = true},
      {.portA = "eth1/65/1",
       .portB = "eth1/65/1",
       .profile = cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
       .hasTransceiver = false},
      {.portA = "eth1/3/1",
       .portB = std::nullopt,
       .profile = cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
       .hasTransceiver = true},
  };

  EXPECT_EQ(parseLinkTopologyCsv(kCsv, "topology.csv"), expected);
}

TEST(LinkTopologyUtilsTest, RejectsInvalidRows) {
  const std::vector<std::string_view> invalidCsvs{
      "portA,portB,profile\neth1/1/1,eth1/2/1,PROFILE_DEFAULT\n",
      "portA,portB,profile,hasTransceiver\n",
      "portA,portB,profile,hasTransceiver\n,eth1/2/1,PROFILE_DEFAULT,true\n",
      "portA,portB,profile,hasTransceiver\neth1/1/1,eth1/2/1,NOT_A_PROFILE,true\n",
      "portA,portB,profile,hasTransceiver\neth1/1/1,eth1/2/1,PROFILE_DEFAULT,true\n",
      "portA,portB,profile,hasTransceiver\neth1/1/1,eth1/2/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,yes\n",
      "portA,portB,profile,hasTransceiver\neth1/1/1,eth1/2/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,true\neth1/2/1,eth1/3/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,true\n",
  };

  for (const auto csv : invalidCsvs) {
    EXPECT_THROW(parseLinkTopologyCsv(csv, "topology.csv"), FbossError) << csv;
  }
}

TEST(LinkTopologyUtilsTest, LoadsVariantFromPlatformDirectory) {
  constexpr std::string_view kCsv =
      "portA,portB,profile,hasTransceiver\n"
      "eth1/1/1,eth1/2/1,PROFILE_100G_4_NRZ_RS528_OPTICAL,true\n";
  const LinkTopology expected{
      {.portA = "eth1/1/1",
       .portB = "eth1/2/1",
       .profile = cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
       .hasTransceiver = true},
  };
  folly::test::TemporaryDirectory temporaryDirectory;
  const fs::path platformDirectory{temporaryDirectory.path().string()};
  writeTestFile(
      platformDirectory / "forwarding_stack" / "link_topology" /
          "link_test_default.csv",
      kCsv);

  EXPECT_EQ(
      loadLinkTopologyVariant(platformDirectory, "link_test_default"),
      expected);
}

TEST(LinkTopologyUtilsTest, RejectsInvalidOrMissingVariant) {
  folly::test::TemporaryDirectory temporaryDirectory;
  const fs::path platformDirectory{temporaryDirectory.path().string()};

  EXPECT_THROW(
      loadLinkTopologyVariant(platformDirectory, "../link_test_default"),
      FbossError);
  EXPECT_THROW(
      loadLinkTopologyVariant(platformDirectory, "missing"), FbossError);
}

} // namespace
} // namespace facebook::fboss::configgen
