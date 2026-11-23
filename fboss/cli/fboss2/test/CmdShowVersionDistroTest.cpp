/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/version/CmdShowVersionDistro.h"

#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

TEST(CmdShowVersionDistroTest, readsRecordAndPrintsSummary) {
  folly::test::TemporaryFile file;
  ASSERT_TRUE(
      folly::writeFile(
          std::string(
              R"({"schema":1,"image":{"manifest":"image.json","source_revision":"rev-a"},"versions":{"bsp":"4.4.2-1","kernel":"6.11.1-1.fboss.el9","native_sdk":"6.5.34","sai":"14.2.0.0_odp"},"components":{"fboss-forwarding-stack[agent]":{"artifact":{"name":"agent.tar.zst","sha256":"sha256:abc"},"provenance":{"sdk":{"version":"sdk-a"},"modules":[],"empty":{}}}}})"),
          file.path().c_str()));
  auto result = CmdShowVersionDistro::readVersionFile(file.path().string());
  EXPECT_EQ(result.at("source_revision"), "rev-a");
  EXPECT_EQ(result.at("kernel"), "6.11.1-1.fboss.el9");
  testing::internal::CaptureStdout();
  CmdShowVersionDistro().printOutput(result);
  const auto output = testing::internal::GetCapturedStdout();
  EXPECT_EQ(
      output,
      "bsp             4.4.2-1\n"
      "kernel          6.11.1-1.fboss.el9\n"
      "native_sdk      6.5.34\n"
      "sai             14.2.0.0_odp\n"
      "manifest        image.json\n"
      "source_revision rev-a\n");

  testing::internal::CaptureStdout();
  CmdShowVersionDistroDetail().printOutput(result);
  const auto detail = testing::internal::GetCapturedStdout();
  std::string contents;
  ASSERT_TRUE(folly::readFile(file.path().c_str(), contents));
  EXPECT_EQ(folly::parseJson(detail), folly::parseJson(contents));
  EXPECT_NE(detail.find("\n  \"components\""), std::string::npos);
  EXPECT_TRUE(CmdShowVersionDistroDetail().getValidFilters().empty());
}

TEST(CmdShowVersionDistroTest, summaryOmitsUnrecordedVersions) {
  folly::test::TemporaryFile file;
  ASSERT_TRUE(
      folly::writeFile(
          std::string(R"({"schema":1,"image":{"manifest":"image.json"}})"),
          file.path().c_str()));
  const auto result =
      CmdShowVersionDistro::readVersionFile(file.path().string());
  testing::internal::CaptureStdout();
  CmdShowVersionDistro().printOutput(result);
  EXPECT_EQ(
      testing::internal::GetCapturedStdout(), "manifest        image.json\n");
}

TEST(
    CmdShowVersionDistroTest,
    summaryPrintsServiceVersionsAndDetailKeepsRecords) {
  folly::test::TemporaryFile file;
  const std::string contents = R"({"schema":1,"components":{
    "fboss-forwarding-stack[agent]":{
      "artifact":{"name":"agent.tar","sha256":"sha256:abc"},
      "provenance":{"build_info":{"declaration_schema":1,"binaries":{
        "fboss_sw_agent":{"schema":1,"build_target":"fboss_sw_agent","build_version":"release-42","source_revision":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"agent-sha"},
        "fboss_hw_agent-sai_impl":{"schema":1,"build_target":"fboss_hw_agent-sai_impl","build_version":"release-43","source_revision":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","source_dirty":true,"sha256":"hw-sha"}
      }}}},
    "fboss-forwarding-stack[fsdb]":{
      "artifact":{"name":"fsdb.tar","sha256":"sha256:def"},
      "provenance":{"build_info":{"declaration_schema":1,"binaries":{
        "fsdb":{"schema":1,"build_target":"fsdb","sha256":"fsdb-sha"}
      }}}},
    "fboss-platform-stack":{
      "artifact":{"name":"platform.tar","sha256":"sha256:ghi"},
      "provenance":{"build_info":{"declaration_schema":1,"binaries":{
        "fan_service":{"schema":1,"build_target":"fan_service","build_version":"dev","source_revision":"cccccccccccccccccccccccccccccccccccccccc","sha256":"fan-sha"}
      }}}}
  }})";
  ASSERT_TRUE(folly::writeFile(contents, file.path().c_str()));
  const auto result =
      CmdShowVersionDistro::readVersionFile(file.path().string());
  testing::internal::CaptureStdout();
  CmdShowVersionDistro().printOutput(result);
  const auto output = testing::internal::GetCapturedStdout();
  EXPECT_EQ(
      output,
      "\nService versions:\n"
      "  fan_service                     dev\n"
      "  fboss_hw_agent-sai_impl         release-43\n"
      "  fboss_sw_agent                  release-42\n"
      "  fsdb                            Unavailable\n");
  testing::internal::CaptureStdout();
  CmdShowVersionDistroDetail().printOutput(result);
  EXPECT_EQ(
      folly::parseJson(testing::internal::GetCapturedStdout()),
      folly::parseJson(contents));
}

TEST(CmdShowVersionDistroTest, missingFileIsInformational) {
  folly::test::TemporaryDirectory dir;
  auto result = CmdShowVersionDistro::readVersionFile(
      (dir.path() / "missing.json").string());
  EXPECT_NE(
      result.at("Distro version").find("older images"), std::string::npos);
  testing::internal::CaptureStdout();
  CmdShowVersionDistro().printOutput(result);
  const auto summary = testing::internal::GetCapturedStdout();
  testing::internal::CaptureStdout();
  CmdShowVersionDistroDetail().printOutput(result);
  EXPECT_EQ(testing::internal::GetCapturedStdout(), summary);
}

TEST(CmdShowVersionDistroTest, malformedRecordIsAnError) {
  folly::test::TemporaryFile file;
  ASSERT_TRUE(folly::writeFile(std::string("not json"), file.path().c_str()));
  EXPECT_ANY_THROW(CmdShowVersionDistro::readVersionFile(file.path().string()));
  ASSERT_TRUE(
      folly::writeFile(std::string(R"({"schema":2})"), file.path().c_str()));
  EXPECT_THROW(
      CmdShowVersionDistro::readVersionFile(file.path().string()),
      std::runtime_error);
}

TEST(CmdShowVersionDistroTest, malformedSummaryFieldsAreErrors) {
  folly::test::TemporaryFile file;
  for (const auto* contents : {
           R"({"schema":1,"versions":{"kernel":42}})",
           R"({"schema":1,"image":{"manifest":["image.json"]}})",
           R"({"schema":1,"versions":[]})",
           R"({"schema":1,"components":[]})",
       }) {
    ASSERT_TRUE(folly::writeFile(std::string(contents), file.path().c_str()));
    try {
      CmdShowVersionDistro::readVersionFile(file.path().string());
      FAIL() << "Expected invalid record error for " << contents;
    } catch (const std::runtime_error& error) {
      EXPECT_NE(
          std::string(error.what()).find("Invalid distro version record field"),
          std::string::npos);
      EXPECT_NE(
          std::string(error.what()).find(file.path().string()),
          std::string::npos);
    }
  }
}

} // namespace facebook::fboss
