// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/SdkDumpPath.h"

#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

// Empty input is rejected.
TEST(SdkDumpPathTest, RejectsEmpty) {
  EXPECT_THROW(sanitizeSdkDumpPath(""), FbossError);
}

// Absolute paths are rejected outright so a caller cannot direct the SDK write
// outside the service-owned dump directory.
TEST(SdkDumpPathTest, RejectsAbsolute) {
  EXPECT_THROW(sanitizeSdkDumpPath("/etc/cron.d/x"), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath("/etc/ld.so.preload"), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath("/root/.ssh/authorized_keys"), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath("/"), FbossError);
}

// A basename that reduces to "." or ".." is rejected.
TEST(SdkDumpPathTest, RejectsDotBasename) {
  EXPECT_THROW(sanitizeSdkDumpPath("."), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath(".."), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath("foo/."), FbossError);
  EXPECT_THROW(sanitizeSdkDumpPath("foo/.."), FbossError);
}

// A trailing slash leaves an empty basename, which is rejected.
TEST(SdkDumpPathTest, RejectsTrailingSlash) {
  EXPECT_THROW(sanitizeSdkDumpPath("foo/"), FbossError);
}

// A plain valid filename is confined to kSdkDumpDir.
TEST(SdkDumpPathTest, ConfinesValidName) {
  EXPECT_EQ(
      sanitizeSdkDumpPath("dump.txt"), std::string(kSdkDumpDir) + "dump.txt");
}

// A sub-path is reduced to its basename and confined to kSdkDumpDir.
TEST(SdkDumpPathTest, ConfinesSubPathToBasename) {
  EXPECT_EQ(
      sanitizeSdkDumpPath("a/b/c/dump.txt"),
      std::string(kSdkDumpDir) + "dump.txt");
}

// A relative path containing ".." components cannot escape kSdkDumpDir because
// only the basename is used.
TEST(SdkDumpPathTest, ParentTraversalStaysConfined) {
  EXPECT_EQ(
      sanitizeSdkDumpPath("../../etc/passwd"),
      std::string(kSdkDumpDir) + "passwd");
  EXPECT_EQ(
      sanitizeSdkDumpPath("a/../../b/dump.txt"),
      std::string(kSdkDumpDir) + "dump.txt");
}

// Legitimate names that merely contain ".." as a substring are not falsely
// rejected.
TEST(SdkDumpPathTest, AllowsDotDotSubstring) {
  EXPECT_EQ(sanitizeSdkDumpPath("..bar"), std::string(kSdkDumpDir) + "..bar");
  EXPECT_EQ(
      sanitizeSdkDumpPath("foo/..bar"), std::string(kSdkDumpDir) + "..bar");
}

} // namespace facebook::fboss
