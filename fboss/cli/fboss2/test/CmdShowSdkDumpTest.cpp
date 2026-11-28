// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/show/sdk/dump/CmdShowSdkDump.h"

#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

TEST(CmdShowSdkDumpTest, UsesRelativeRequestInConfiguredDumpDirectory) {
  folly::test::TemporaryDirectory dumpDir;
  CmdShowQsfpSdkDump command(dumpDir.path());

  const auto requestFileName = command.prepareSdkDumpFile();

  ASSERT_NE(command.tempSdkFile, nullptr);
  EXPECT_EQ(command.tempSdkFile->path().parent_path(), dumpDir.path());
  EXPECT_EQ(requestFileName, command.tempSdkFile->path().filename().string());
  EXPECT_FALSE(folly::fs::path(requestFileName).is_absolute());
  EXPECT_EQ(requestFileName.find('/'), std::string::npos);
}

} // namespace facebook::fboss
