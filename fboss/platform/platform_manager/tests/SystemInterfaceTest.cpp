// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <filesystem>

#include <fmt/args.h>
#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_manager/PkgManager.h"

using namespace ::testing;

namespace facebook::fboss::platform::platform_manager {
class MockPlatformUtils : public PlatformUtils {
 public:
  MOCK_METHOD(
      (std::pair<int, std::string>),
      execCommand,
      (const std::string&),
      (const));
};

TEST(SystemInterfaceTest, lsmod) {
  auto lsmod =
      "Module                  Size  Used by\n"
      "mp3_smbcpld            12288  0\n"
      "mp3_scmcpld            28672  0\n"
      "fboss_iob_xcvr         12288  0\n"
      "fboss_iob_led          12288  0\n"
      "led_class              20480  3 fboss_iob_led,mp3_scmcpld,mp3_fancpld\n"
      "autofs4                49152  2\n";
  auto expectedKmods = std::array<std::string, 6>{
      "mp3_smbcpld",
      "mp3_scmcpld",
      "fboss_iob_xcvr",
      "fboss_iob_led",
      "led_class",
      "autofs4"};
  auto platformUtils = std::make_shared<MockPlatformUtils>();
  EXPECT_CALL(*platformUtils, execCommand(_))
      .WillOnce(Return(std::pair{0, lsmod}));
  package_manager::SystemInterface interface(platformUtils);
  auto kmods = interface.lsmod();
  EXPECT_EQ(kmods.size(), expectedKmods.size());
  for (const auto& expectedKmod : expectedKmods) {
    EXPECT_TRUE(kmods.contains(expectedKmod));
  }
}

TEST(SystemInterfaceTest, GetBoundDriverVersionReadsModuleVersion) {
  folly::test::TemporaryDirectory tmpDir;
  const auto devicePath =
      std::filesystem::path(tmpDir.path().string()) / "scd.xcvr_ctrl.1";
  std::filesystem::create_directories(devicePath / "driver" / "module");
  ASSERT_TRUE(
      folly::writeFile(
          std::string("0.7.26\n"),
          (devicePath / "driver" / "module" / "version").c_str()));
  package_manager::SystemInterface interface;
  EXPECT_EQ(interface.getBoundDriverVersion(devicePath.string()), "0.7.26");
}

TEST(SystemInterfaceTest, GetBoundDriverVersionNoBoundDriver) {
  folly::test::TemporaryDirectory tmpDir;
  package_manager::SystemInterface interface;
  EXPECT_EQ(
      interface.getBoundDriverVersion(tmpDir.path().string()), std::nullopt);
}

TEST(SystemInterfaceTest, GetBoundDriverVersionEmptyVersionFile) {
  folly::test::TemporaryDirectory tmpDir;
  const auto devicePath =
      std::filesystem::path(tmpDir.path().string()) / "scd.xcvr_ctrl.1";
  std::filesystem::create_directories(devicePath / "driver" / "module");
  ASSERT_TRUE(
      folly::writeFile(
          std::string(" \n"),
          (devicePath / "driver" / "module" / "version").c_str()));
  package_manager::SystemInterface interface;
  EXPECT_EQ(interface.getBoundDriverVersion(devicePath.string()), std::nullopt);
}

TEST(BspVersionTest, FromStringParsesTriple) {
  auto v = package_manager::BspVersion::fromString("0.7.23");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ((*v), (package_manager::BspVersion{0, 7, 23}));
}

TEST(BspVersionTest, FromStringIgnoresTrailingParts) {
  // Extra dotted segments beyond the triple are tolerated.
  auto v = package_manager::BspVersion::fromString("1.2.3.4");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ((*v), (package_manager::BspVersion{1, 2, 3}));
}

TEST(BspVersionTest, FromStringRejectsNonTriple) {
  EXPECT_FALSE(package_manager::BspVersion::fromString("1.2").has_value());
  EXPECT_FALSE(package_manager::BspVersion::fromString("").has_value());
}

TEST(BspVersionTest, FromStringRejectsNonNumeric) {
  EXPECT_FALSE(package_manager::BspVersion::fromString("1.x.3").has_value());
}

TEST(BspVersionTest, Ordering) {
  using package_manager::BspVersion;
  EXPECT_LT((BspVersion{0, 7, 22}), (BspVersion{0, 7, 23}));
  EXPECT_LT((BspVersion{0, 6, 99}), (BspVersion{0, 7, 0}));
  EXPECT_GE((BspVersion{1, 0, 0}), (BspVersion{0, 7, 23}));
  EXPECT_EQ((BspVersion{0, 7, 23}), (BspVersion{0, 7, 23}));
}

}; // namespace facebook::fboss::platform::platform_manager
