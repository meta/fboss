// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/util/test/QsfpUtilTest.h"
#include <folly/FileUtil.h>
#include <folly/Singleton.h>
#include <folly/logging/xlog.h>
#include <folly/testing/TestUtil.h>
#include <gflags/gflags.h>
#include <fstream>
#include "common/init/Init.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"
#include "fboss/util/wedge_qsfp_util.h"

using namespace facebook::fboss;

int main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);

  facebook::initFacebook(&argc, &argv);

  return RUN_ALL_TESTS();
}

namespace facebook {
namespace fboss {

void QsfpUtilTest::SetUp() {
  // Reset any global state being tracked in singletons
  // Each test then sets up its own state as needed.
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();
}

void QsfpUtilTest::TearDown() {}

TEST_F(QsfpUtilTest, VerifyCheckSum) {
  CdbCommandBlock cdb;
  std::vector<uint8_t> lplMem;

  uint8_t* pSum = (uint8_t*)&cdb + 5;

  // For command 0x0100 the 1's command block checksum will be 0xfe
  cdb.createCdbCmdGeneric(0x0100, lplMem);
  ASSERT_EQ(*pSum, 0xfe);

  // For command 0x0043 the 1's command block checksum will be 0xbc
  cdb.createCdbCmdGeneric(0x0043, lplMem);
  ASSERT_EQ(*pSum, 0xbc);

  // For command 0x0042 the 1's command block checksum will be 0xbd
  cdb.createCdbCmdGeneric(0x0042, lplMem);
  ASSERT_EQ(*pSum, 0xbd);

  // For command 0x0000 with LPL memory bytes 100, 200 the 1's command block
  // the checksum will be 0xd1
  lplMem.push_back(100);
  lplMem.push_back(200);
  cdb.createCdbCmdGeneric(0, lplMem);
  ASSERT_EQ(*pSum, 0xd1);
}

TEST_F(QsfpUtilTest, portRegexTest1) {
  // Input value
  std::string portListStr("1,2,3-9,12-17,29");
  // Expected result
  std::vector<unsigned int> resultPortList{
      1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 29};
  // Call the function
  std::vector<unsigned int> portListVec = portRangeStrToPortList(portListStr);
  // Verify the result
  ASSERT_EQ(portListVec, resultPortList);
}

TEST_F(QsfpUtilTest, portRegexTest2) {
  // Input value
  std::string portListStr("1");
  // Expected result
  std::vector<unsigned int> resultPortList{1};
  // Call the function
  std::vector<unsigned int> portListVec = portRangeStrToPortList(portListStr);
  // Verify the result
  ASSERT_EQ(portListVec, resultPortList);
}

TEST_F(QsfpUtilTest, portRegexTest3) {
  // Input value
  std::string portListStr("99,100-101,127-128");
  // Expected result
  std::vector<unsigned int> resultPortList{99, 100, 101, 127, 128};
  // Call the function
  std::vector<unsigned int> portListVec = portRangeStrToPortList(portListStr);
  // Verify the result
  ASSERT_EQ(portListVec, resultPortList);
}

TEST_F(QsfpUtilTest, portBucketize1) {
  // Input value
  std::vector<unsigned int> modlist{
      1, 3, 5, 8, 12, 15, 21, 33, 38, 46, 55, 59, 60, 83, 88, 122, 124, 128};
  // Expected result
  std::vector<std::vector<unsigned int>> resultBucket{
      {1, 3, 5, 8},
      {12, 15},
      {21},
      {33, 38},
      {46},
      {55},
      {59, 60},
      {83, 88},
      {122, 124, 128},
  };
  std::vector<std::vector<unsigned int>> bucket;

  // Call the function
  bucketize(modlist, 8, bucket);

  // Verify the result
  ASSERT_EQ(bucket.size(), resultBucket.size());

  for (int i = 0; i < bucket.size(); i++) {
    XLOG(INFO) << "Iteration " << i;
    ASSERT_EQ(bucket[i], resultBucket[i]);
  }
}

TEST_F(QsfpUtilTest, CheckProcessName) {
  // Get command line of this test process
  int currPid = getpid();
  std::string commandPath =
      folly::to<std::string>("/proc/", currPid, "/cmdline");
  std::ifstream commandFile(commandPath.c_str());
  std::string commandLine;
  getline(commandFile, commandLine);
  ASSERT_NE(commandLine.empty(), true);

  // Command line could  be like /usr/local/bin/buck test @mode/opt....
  // Extract the command name 'buck' from that
  size_t position = commandLine.find('\0');
  if (position != std::string::npos) {
    commandLine = commandLine.substr(0, position);
  }
  position = commandLine.find('/');
  if (position != std::string::npos) {
    commandLine = commandLine.substr(position + 1);
  }

  // Check if the getPidForProcess function returns correct PID for this test
  // process name
  auto resultPidList = getPidForProcess(commandLine);
  bool foundProcess = false;
  for (auto resultPid : resultPidList) {
    if (resultPid == currPid) {
      foundProcess = true;
    }
  }
  ASSERT_EQ(foundProcess, true);
}

namespace {
void useQsfpConfig(const std::string& path) {
  gflags::SetCommandLineOption("qsfp_config", path.c_str());
  gflags::SetCommandLineOption("qsfp_config_current", path.c_str());
}
} // namespace

TEST_F(QsfpUtilTest, PlatformDescriptorPathFromQsfpConfig) {
  gflags::FlagSaver saver;
  folly::test::TemporaryFile config;
  folly::writeFile(
      std::string(
          R"({"defaultCommandLineArgs": {"platform_descriptor_config_path": "/qsfp/descriptors/"}})"),
      config.path().c_str());
  useQsfpConfig(config.path().string());
  FLAGS_platform_descriptor_config_path = "";

  initPlatformDescriptorPathFromQsfpConfig();

  EXPECT_EQ(FLAGS_platform_descriptor_config_path, "/qsfp/descriptors/");
}

TEST_F(QsfpUtilTest, PlatformDescriptorPathFlagWinsOverQsfpConfig) {
  gflags::FlagSaver saver;
  folly::test::TemporaryFile config;
  folly::writeFile(
      std::string(
          R"({"defaultCommandLineArgs": {"platform_descriptor_config_path": "/qsfp/descriptors/"}})"),
      config.path().c_str());
  useQsfpConfig(config.path().string());
  FLAGS_platform_descriptor_config_path = "/explicit/";

  initPlatformDescriptorPathFromQsfpConfig();

  EXPECT_EQ(FLAGS_platform_descriptor_config_path, "/explicit/");
}

TEST_F(QsfpUtilTest, PlatformDescriptorPathWithoutQsfpConfig) {
  gflags::FlagSaver saver;
  folly::test::TemporaryDirectory dir;
  useQsfpConfig((dir.path() / "missing.conf").string());
  FLAGS_platform_descriptor_config_path = "";

  initPlatformDescriptorPathFromQsfpConfig();

  EXPECT_EQ(FLAGS_platform_descriptor_config_path, "");
}

} // namespace fboss
} // namespace facebook
