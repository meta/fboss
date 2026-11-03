// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/platform/reboot_cause_finder/RebootHistory.h"

namespace facebook::fboss::platform::reboot_cause_finder {

// The records are written by a separate process on its own schedule, so the
// contract under test is that nothing found on disk -- absent, truncated,
// malformed, oversized, or simply too numerous -- makes the read throw.

namespace {
// Built by concatenation rather than fmt: the JSON braces would have to be
// doubled to satisfy fmt's compile-time format-string check, which makes the
// literal unreadable as JSON.
std::string makeRecord(
    const std::string& id,
    int64_t detectedAtMs,
    std::optional<int64_t> bootTimeMs = std::nullopt) {
  return std::string("{\n") + "  \"bootId\": \"boot-" + id + "\",\n" +
      "  \"bootTimeMs\": " +
      std::to_string(bootTimeMs.value_or(detectedAtMs - 1000)) + ",\n" +
      "  \"detectedAtMs\": " + std::to_string(detectedAtMs) + ",\n" +
      "  \"detectedAtPacific\": \"2026-10-06 12:48:56 PDT\",\n" +
      "  \"providersAttempted\": []\n}";
}
} // namespace

class RebootHistoryTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::make_unique<folly::test::TemporaryDirectory>();
    historyDir_ = dir_->path().string();
  }
  std::filesystem::path path(const std::string& name) {
    return std::filesystem::path(dir_->path().string()) / name;
  }
  void write(const std::filesystem::path& p, const std::string& contents) {
    ASSERT_TRUE(folly::writeFile(contents, p.c_str()));
  }
  RebootHistory call() {
    RebootHistory history;
    readRebootHistory(historyDir_, history);
    return history;
  }
  std::string historyDir_;
  std::unique_ptr<folly::test::TemporaryDirectory> dir_;
};

TEST_F(RebootHistoryTest, MissingDirectoryIsNotAnError) {
  historyDir_ = "/nonexistent/reboot_history";
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 0);
  EXPECT_FALSE(response.warnings.empty());
}

TEST_F(RebootHistoryTest, EmptyDirectoryReturnsNothing) {
  auto response = call();
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 0);
}

TEST_F(RebootHistoryTest, ReturnsValidRecord) {
  write(path("reboot-cause-1.json"), makeRecord("aaa", 1000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 1);
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-aaa");
  EXPECT_EQ(response.skippedCount, 0);
}

// Records name their enums. Those written before that carry numbers, and the
// retired `incomplete` field; both must still read.
TEST_F(RebootHistoryTest, ReadsNamedAndNumericEnums) {
  write(
      path("reboot-cause-named.json"),
      R"({"bootId": "named", "bootTimeMs": 2000, "causeStatus": "UNEXPLAINED",
          "providersAttempted": [{"name": "P", "status": "READ_FAILED"}]})");
  write(
      path("reboot-cause-numeric.json"),
      R"({"bootId": "numeric", "bootTimeMs": 1000, "incomplete": true,
          "providersAttempted": [{"name": "P", "status": 1}]})");
  auto response = call();
  ASSERT_EQ(response.records.size(), 2);
  EXPECT_EQ(response.skippedCount, 0);
  const auto& named = response.records.at(0);
  EXPECT_EQ(*named.bootId(), "named");
  EXPECT_EQ(
      named.causeStatus(), reboot_cause_config::RebootCauseStatus::UNEXPLAINED);
  EXPECT_EQ(
      *named.providersAttempted()->at(0).status(),
      reboot_cause_config::RebootCauseProviderStatus::READ_FAILED);
  const auto& numeric = response.records.at(1);
  EXPECT_EQ(*numeric.bootId(), "numeric");
  EXPECT_FALSE(numeric.causeStatus().has_value());
  EXPECT_EQ(
      *numeric.providersAttempted()->at(0).status(),
      reboot_cause_config::RebootCauseProviderStatus::READ_FAILED);
}

TEST_F(RebootHistoryTest, OrdersNewestFirstByBootTime) {
  // Written so filename order disagrees with boot order.
  write(path("reboot-cause-a.json"), makeRecord("oldest", 1000));
  write(path("reboot-cause-b.json"), makeRecord("newest", 3000));
  write(path("reboot-cause-c.json"), makeRecord("middle", 2000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 3);
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-newest");
  EXPECT_EQ(*response.records.at(1).bootId(), "boot-middle");
  EXPECT_EQ(*response.records.at(2).bootId(), "boot-oldest");
}

// A boot whose cause was determined long after it (a manual or redo run) stays
// in boot order rather than jumping to the top.
TEST_F(RebootHistoryTest, LateDeterminationKeepsBootOrder) {
  write(path("reboot-cause-a.json"), makeRecord("older", 9000, 1000));
  write(path("reboot-cause-b.json"), makeRecord("newer", 3000, 2000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 2);
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-newer");
  EXPECT_EQ(*response.records.at(1).bootId(), "boot-older");
}

// bootTimeMs is not optional, so a record written without it deserializes to 0:
// it is returned, last, rather than failing the call.
TEST_F(RebootHistoryTest, RecordWithoutBootTimeSortsLast) {
  write(
      path("reboot-cause-a.json"),
      R"({"bootId": "boot-nobtime", "detectedAtMs": 9000})");
  write(path("reboot-cause-b.json"), makeRecord("dated", 3000, 2000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 2);
  EXPECT_EQ(response.skippedCount, 0);
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-dated");
  EXPECT_EQ(*response.records.at(1).bootId(), "boot-nobtime");
  EXPECT_EQ(*response.records.at(1).bootTimeMs(), 0);
}

TEST_F(RebootHistoryTest, MalformedRecordIsSkippedAndOthersSurvive) {
  write(path("reboot-cause-good.json"), makeRecord("good", 2000));
  write(path("reboot-cause-bad.json"), "{ this is not json");
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  ASSERT_EQ(response.records.size(), 1);
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-good");
  EXPECT_EQ(response.skippedCount, 1);
  EXPECT_FALSE(response.warnings.empty());
}

TEST_F(RebootHistoryTest, TruncatedRecordIsSkipped) {
  auto full = makeRecord("truncated", 1000);
  write(path("reboot-cause-trunc.json"), full.substr(0, full.size() / 2));
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 1);
}

TEST_F(RebootHistoryTest, EmptyFileIsSkipped) {
  write(path("reboot-cause-empty.json"), "");
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 1);
}

TEST_F(RebootHistoryTest, BinaryGarbageIsSkipped) {
  write(path("reboot-cause-bin.json"), std::string("\x00\x01\x02\xff", 4));
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 1);
}

TEST_F(RebootHistoryTest, OversizedRecordIsSkipped) {
  write(path("reboot-cause-big.json"), std::string((1 << 20) + 1, 'x'));
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  EXPECT_TRUE(response.records.empty());
  EXPECT_EQ(response.skippedCount, 1);
}

TEST_F(RebootHistoryTest, SubdirectoriesAreIgnored) {
  std::filesystem::create_directory(path("reboot-cause-dir.json"));
  write(path("reboot-cause-ok.json"), makeRecord("ok", 1000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 1);
  EXPECT_EQ(response.skippedCount, 0);
}

TEST_F(RebootHistoryTest, UnrelatedFilesAreIgnoredNotCounted) {
  // A stray file is not a record that failed; it was never a record.
  write(path("README"), "notes");
  write(path("reboot-cause-1.json.tmp"), "partial write");
  write(path("reboot-cause-ok.json"), makeRecord("ok", 1000));
  auto response = call();
  ASSERT_EQ(response.records.size(), 1);
  EXPECT_EQ(response.skippedCount, 0);
}

TEST_F(RebootHistoryTest, UnreadableDirectoryReportsAWarning) {
  // A directory that cannot be opened must not look like an empty one: the
  // iterator is reset to end on error, so this only holds if the error code
  // is checked after construction rather than inside the loop.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root ignores the directory permissions this relies on";
  }
  ASSERT_EQ(::chmod(dir_->path().string().c_str(), 0), 0);
  RebootHistory response;
  EXPECT_NO_THROW({ response = call(); });
  ASSERT_EQ(::chmod(dir_->path().string().c_str(), 0755), 0);
  EXPECT_TRUE(response.records.empty());
  EXPECT_FALSE(response.warnings.empty());
}

TEST_F(RebootHistoryTest, CapsRecordCountAndReportsTheRest) {
  for (int i = 0; i < 70; ++i) {
    char name[64];
    snprintf(name, sizeof(name), "reboot-cause-%03d.json", i);
    char id[16];
    snprintf(id, sizeof(id), "%03d", i);
    write(path(name), makeRecord(id, 1000 + i));
  }
  auto response = call();
  EXPECT_EQ(response.records.size(), 64);
  EXPECT_EQ(response.skippedCount, 6);
  EXPECT_FALSE(response.warnings.empty());
  EXPECT_EQ(*response.records.at(0).bootId(), "boot-069");
}

} // namespace facebook::fboss::platform::reboot_cause_finder
