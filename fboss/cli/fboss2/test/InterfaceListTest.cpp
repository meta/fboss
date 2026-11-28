/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss::utils {

namespace fs = std::filesystem;

// Fixture that installs a ConfigSession backed by a small on-disk config so
// that InterfaceList (which resolves names against the session's agent
// config) can be unit tested.
class InterfaceListTest : public ::testing::Test {
 public:
  void SetUp() override {
    const std::string unique =
        boost::filesystem::unique_path(
            "fboss_interface_list_test_%%%%-%%%%-%%%%-%%%%")
            .string();
    homeDir_ = fs::temp_directory_path() / (unique + "_home");
    etcDir_ = fs::temp_directory_path() / (unique + "_etc");
    fs::create_directories(homeDir_);
    fs::create_directories(etcDir_ / "coop" / "cli");

    setenv("HOME", homeDir_.c_str(), 1);
    setenv("USER", "testuser", 1);

    fs::path cliConfigPath = etcDir_ / "coop" / "cli" / "agent.conf";
    {
      std::ofstream file(cliConfigPath);
      file << R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ],
    "interfaces": [
      {
        "intfID": 2001,
        "name": "uplinks_1",
        "vlanID": 0
      }
    ]
  }
})";
    }
    fs::create_symlink(cliConfigPath, etcDir_ / "coop" / "agent.conf");

    facebook::fboss::TestableConfigSession::setInstance(
        std::make_unique<facebook::fboss::TestableConfigSession>(
            (homeDir_ / ".fboss2").string(), (etcDir_ / "coop").string()));
  }

  void TearDown() override {
    facebook::fboss::TestableConfigSession::setInstance(nullptr);
    std::error_code ec;
    fs::remove_all(homeDir_, ec);
    fs::remove_all(etcDir_, ec);
  }

 private:
  fs::path homeDir_;
  fs::path etcDir_;
};

// With allowMissing=true, an unknown name is retained as an Intf with null
// port/interface pointers instead of throwing.
TEST_F(InterfaceListTest, AllowMissingRetainsUnknownName) {
  InterfaceList list({"eth_absent_but_whatever"}, /*allowMissing=*/true);

  ASSERT_EQ(list.size(), 1);
  const auto& intf = list[0];
  EXPECT_EQ(intf.getPort(), nullptr);
  EXPECT_EQ(intf.getInterface(), nullptr);
  EXPECT_EQ(intf.name(), "eth_absent_but_whatever");
}

// The default (allowMissing=false) still throws for an unknown name.
TEST_F(InterfaceListTest, DefaultThrowsForUnknownName) {
  EXPECT_THROW(
      InterfaceList({"eth_absent_but_whatever"}), std::invalid_argument);
}

// A known port is resolved regardless of allowMissing.
TEST_F(InterfaceListTest, AllowMissingResolvesKnownPort) {
  InterfaceList list({"eth1/1/1"}, /*allowMissing=*/true);

  ASSERT_EQ(list.size(), 1);
  EXPECT_NE(list[0].getPort(), nullptr);
}

// A numeric name matching an interface ID resolves to that interface.
TEST_F(InterfaceListTest, ResolvesInterfaceId) {
  InterfaceList list({"2001"});

  ASSERT_EQ(list.size(), 1);
  const auto& intf = list[0];
  EXPECT_EQ(intf.getPort(), nullptr);
  ASSERT_NE(intf.getInterface(), nullptr);
  EXPECT_EQ(*intf.getInterface()->intfID(), 2001);
}

// An interface is still resolvable by name.
TEST_F(InterfaceListTest, ResolvesInterfaceName) {
  InterfaceList list({"uplinks_1"});

  ASSERT_EQ(list.size(), 1);
  ASSERT_NE(list[0].getInterface(), nullptr);
  EXPECT_EQ(*list[0].getInterface()->intfID(), 2001);
}

// A numeric name matching no interface ID throws.
TEST_F(InterfaceListTest, ThrowsForUnknownId) {
  EXPECT_THROW(InterfaceList({"4242"}), std::invalid_argument);
}

} // namespace facebook::fboss::utils
