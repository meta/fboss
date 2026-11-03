// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <cstdlib>
#include <ctime>
#include "fboss/cli/fboss2/commands/show/reboot_history/CmdShowRebootHistory.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

namespace rcc = facebook::fboss::platform::reboot_cause_config;

namespace facebook::fboss {

namespace {

constexpr int64_t kNewestBootMs = 1792089290000; // Oct 15 2026 11:34:50 PDT
constexpr int64_t kMiddleBootMs = kNewestBootMs - 63150000; // 17h 32m 30s
constexpr int64_t kOldestBootMs = kMiddleBootMs - 111780000; // 1d 7h 3m

// Trimmed from a record reboot_cause_finder wrote on fboss327460760.snc1.
constexpr auto kNewestRecord = R"({
  "bootId": "dd4135e3-5bd3-45f8-baad-eff5d4770e68",
  "bootTimeMs": 1792089290000,
  "detectedAtMs": 1792089368000,
  "detectedAtPacific": "2026-10-15 11:36:08 PDT",
  "kernelRelease": "6.4.3-0_fbk15_2630_gf27365f948db",
  "determinedCause": {
    "providerName": "X86RebootCommand",
    "cause": {
      "description": "X86 Reboot Command",
      "occurredAtMs": 1792089184000,
      "occurredAtPacific": "2026-10-15 11:33:04 PDT",
      "rawValue": "systemd-logind[2463]: System is rebooting."
    }
  },
  "providersAttempted": []
})";

// Written before kernelRelease existed: the field is simply absent.
constexpr auto kMiddleRecord = R"({
  "bootId": "00a19eb4-465d-4ef2-84f9-96089d213090",
  "bootTimeMs": 1792026140000,
  "determinedCause": {
    "providerName": "BMCWedgePower",
    "cause": {"description": "ChassisResetFromBmc"}
  }
})";

constexpr auto kOldestRecord = R"({
  "bootId": "c58bee01-66fd-4ec5-8700-079035dee63c",
  "bootTimeMs": 1791914360000,
  "providersAttempted": []
})";

// Written by reboot_cause_finder on fboss332614880.ash7, trimmed: it names its
// enums and records a causeStatus.
constexpr auto kInconclusiveRecord = R"({
  "bootId": "3fd1ac82-98b8-4e7a-acdd-66a8d3776ac2",
  "bootTimeMs": 1792672447000,
  "causeStatus": "INCONCLUSIVE",
  "kernelRelease": "6.4.3-0_fbk15_2630_gf27365f948db",
  "providersAttempted": [
    {"name": "KernelPanic", "status": "OK", "causes": []},
    {"name": "BMCWedgePower", "status": "READ_FAILED", "causes": []}
  ]
})";

// Left by a run that was killed before it read any provider.
constexpr auto kInterruptedRecord = R"({
  "bootId": "57685230-a082-4671-8c89-17113535a17c",
  "bootTimeMs": 1792200000000,
  "causeStatus": "INTERRUPTED",
  "kernelRelease": "6.4.3-0_fbk15_2630_gf27365f948db",
  "providersAttempted": []
})";

} // namespace

class CmdShowRebootHistoryTestFixture : public CmdHandlerTestBase {
 public:
  folly::test::TemporaryDirectory tmpDir;

  // Boot times are shown in local time; pin it so expectations are literal.
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    if (const char* tz = std::getenv("TZ")) {
      savedTz_ = tz;
    }
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
  }

  void TearDown() override {
    if (savedTz_) {
      setenv("TZ", savedTz_->c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
    CmdHandlerTestBase::TearDown();
  }

  void write(const std::string& name, std::string_view contents) {
    ASSERT_TRUE(
        folly::writeFile(
            std::string(contents), (tmpDir.path() / name).string().c_str()));
  }

  cli::ShowRebootHistoryModel read() {
    return CmdShowRebootHistory().readHistory(tmpDir.path().string());
  }

 private:
  std::optional<std::string> savedTz_;
};

TEST_F(CmdShowRebootHistoryTestFixture, readsRecordsNewestBootFirst) {
  // Filenames deliberately disagree with boot order.
  write("reboot-cause-a-middle.json", kMiddleRecord);
  write("reboot-cause-b-oldest.json", kOldestRecord);
  write("reboot-cause-c-newest.json", kNewestRecord);

  auto model = read();

  ASSERT_EQ(model.records()->size(), 3);
  const auto& newest = model.records()[0];
  const auto& middle = model.records()[1];
  const auto& oldest = model.records()[2];

  EXPECT_EQ(*newest.bootId(), "dd4135e3-5bd3-45f8-baad-eff5d4770e68");
  EXPECT_EQ(*newest.bootTimeMs(), kNewestBootMs);
  EXPECT_EQ(*newest.kernelRelease(), "6.4.3-0_fbk15_2630_gf27365f948db");
  EXPECT_EQ(
      *newest.determinedCause()->cause()->description(), "X86 Reboot Command");
  // Written before the finder recorded causeStatus.
  EXPECT_FALSE(newest.causeStatus().has_value());
  EXPECT_EQ(*newest.determinedCause()->providerName(), "X86RebootCommand");
  EXPECT_EQ(
      *newest.determinedCause()->cause()->rawValue(),
      "systemd-logind[2463]: System is rebooting.");

  EXPECT_EQ(*middle.bootTimeMs(), kMiddleBootMs);
  EXPECT_TRUE(middle.kernelRelease()->empty());
  EXPECT_EQ(
      *middle.determinedCause()->cause()->description(), "ChassisResetFromBmc");

  EXPECT_EQ(*oldest.bootTimeMs(), kOldestBootMs);
  EXPECT_FALSE(oldest.determinedCause().has_value());
  EXPECT_FALSE(oldest.causeStatus().has_value());

  EXPECT_EQ(*model.skippedCount(), 0);
  EXPECT_TRUE(model.warnings()->empty());
}

TEST_F(CmdShowRebootHistoryTestFixture, causeStatusIsReadFromTheRecord) {
  write("reboot-cause-inconclusive.json", kInconclusiveRecord);
  write("reboot-cause-interrupted.json", kInterruptedRecord);

  auto model = read();

  ASSERT_EQ(model.records()->size(), 2);
  EXPECT_EQ(
      model.records()[0].causeStatus(), rcc::RebootCauseStatus::INCONCLUSIVE);
  EXPECT_EQ(
      model.records()[1].causeStatus(), rcc::RebootCauseStatus::INTERRUPTED);
  EXPECT_FALSE(model.records()[1].determinedCause().has_value());
}

TEST_F(CmdShowRebootHistoryTestFixture, ignoresFilesThatAreNotRecords) {
  write("reboot-cause-x.json", kNewestRecord);
  write("reboot-cause-x.json.tmp", "partial write");
  write("notes.txt", "not a record");

  auto model = read();

  EXPECT_EQ(model.records()->size(), 1);
  EXPECT_EQ(*model.skippedCount(), 0);
}

TEST_F(CmdShowRebootHistoryTestFixture, badRecordIsCountedAndOthersSurvive) {
  write("reboot-cause-good.json", kNewestRecord);
  write("reboot-cause-bad.json", "{ this is not json");
  write("reboot-cause-wrong-type.json", R"({"bootTimeMs": "yesterday"})");

  auto model = read();

  ASSERT_EQ(model.records()->size(), 1);
  EXPECT_EQ(
      *model.records()[0].determinedCause()->cause()->description(),
      "X86 Reboot Command");
  EXPECT_EQ(*model.skippedCount(), 2);
  EXPECT_FALSE(model.warnings()->empty());
}

TEST_F(CmdShowRebootHistoryTestFixture, recordWithoutBootTimeSortsLast) {
  write("reboot-cause-dated.json", kNewestRecord);
  write("reboot-cause-undated.json", R"({"bootId": "undated"})");

  auto model = read();

  ASSERT_EQ(model.records()->size(), 2);
  EXPECT_EQ(*model.records()[1].bootId(), "undated");
  EXPECT_EQ(*model.records()[1].bootTimeMs(), 0);
}

TEST_F(CmdShowRebootHistoryTestFixture, missingDirectoryIsEmptyHistory) {
  auto model = CmdShowRebootHistory().readHistory(
      (tmpDir.path() / "does-not-exist").string());
  EXPECT_TRUE(model.records()->empty());
  ASSERT_EQ(model.warnings()->size(), 1);
  EXPECT_NE(
      model.warnings()[0].find("No reboot history directory"),
      std::string::npos);
}

TEST_F(CmdShowRebootHistoryTestFixture, refusesRemoteHost) {
  HostInfo remote("fboss1.example", "", folly::IPAddress("2001:db8::1"));
  EXPECT_THROW(CmdShowRebootHistory().queryClient(remote), std::runtime_error);
}

TEST_F(CmdShowRebootHistoryTestFixture, printOutput) {
  cli::ShowRebootHistoryModel model;
  auto record = [](int64_t bootTimeMs, rcc::RebootCauseStatus status) {
    rcc::RebootCauseRecord r;
    r.bootTimeMs() = bootTimeMs;
    r.causeStatus() = status;
    return r;
  };
  auto newest = record(kNewestBootMs, rcc::RebootCauseStatus::FOUND);
  newest.kernelRelease() = "6.4.3-0_fbk15_2630_gf27365f948db";
  newest.determinedCause().emplace();
  newest.determinedCause()->cause()->description() = "X86 Reboot Command";
  model.records() = {
      newest,
      record(kMiddleBootMs, rcc::RebootCauseStatus::INCONCLUSIVE),
      record(kMiddleBootMs - 115411000, rcc::RebootCauseStatus::UNEXPLAINED),
      record(
          kMiddleBootMs - 115411000 - 86400000,
          rcc::RebootCauseStatus::INTERRUPTED),
  };
  // Written before the finder recorded causeStatus.
  rcc::RebootCauseRecord older;
  older.bootTimeMs() = kMiddleBootMs - 115411000 - 2 * 86400000;
  model.records()->push_back(older);
  // A cause with no description.
  auto blank = record(
      kMiddleBootMs - 115411000 - 3 * 86400000, rcc::RebootCauseStatus::FOUND);
  blank.determinedCause().emplace();
  model.records()->push_back(blank);
  model.skippedCount() = 1;
  model.warnings() = {"reboot-cause-bad.json: parse error"};

  std::stringstream ss;
  CmdShowRebootHistory().printOutput(model, ss);

  std::string expected =
      " Boot Time                Up For   Kernel                            Cause              \n"
      "---------------------------------------------------------------------------------------------\n"
      " 2026-10-15 11:34:50 PDT  -        6.4.3-0_fbk15_2630_gf27365f948db  X86 Reboot Command \n"
      " 2026-10-14 18:02:20 PDT  17h 32m  -                                 Inconclusive       \n"
      " 2026-10-13 09:58:49 PDT  1d 8h    -                                 Unexplained        \n"
      " 2026-10-12 09:58:49 PDT  1d 0h    -                                 Interrupted        \n"
      " 2026-10-11 09:58:49 PDT  1d 0h    -                                 -                  \n"
      " 2026-10-10 09:58:49 PDT  1d 0h    -                                 -                  \n\n"
      "1 record(s) could not be read\n"
      "Warning: reboot-cause-bad.json: parse error\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST_F(CmdShowRebootHistoryTestFixture, printOutputEmpty) {
  std::stringstream ss;
  CmdShowRebootHistory().printOutput(cli::ShowRebootHistoryModel(), ss);
  EXPECT_EQ(ss.str(), "No reboot records on this switch\n");
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowRebootHistoryTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowRebootHistoryTraits::description().empty());
  EXPECT_FALSE(CmdShowRebootHistory::sampleModel().records()->empty());
}

} // namespace facebook::fboss
