// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/reboot_cause_finder/RebootCauseFinderImpl.h"

DECLARE_int32(bmc_logfile_poll_timeout_s);
DECLARE_bool(redo_current_boot);
DECLARE_bool(clear_reboot_causes);

#include <sys/stat.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/IPAddressV6.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>

using namespace facebook::fboss::platform::reboot_cause_finder;
namespace rcc = facebook::fboss::platform::reboot_cause_config;

namespace {

constexpr int64_t kWindow = 1800;

int64_t nowSec() {
  return static_cast<int64_t>(
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

// Render an epoch instant the way syslog does: local time, no year.
std::string syslogStamp(int64_t epochSec) {
  const auto t = static_cast<std::time_t>(epochSec);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%b %e %H:%M:%S", &tm);
  return buf;
}

// Render an epoch instant the way kdump names a crash directory.
std::string crashDirName(int64_t epochSec) {
  const auto t = static_cast<std::time_t>(epochSec);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%Z", &tm);
  return buf;
}

// Sets TZ for the duration of a test. The parsers resolve local time, so a
// test asserting an absolute epoch has to say which zone it means rather
// than inheriting one from the build environment.
class ScopedTz {
 public:
  explicit ScopedTz(const char* tz) : had_(::getenv("TZ") != nullptr) {
    if (had_) {
      saved_ = ::getenv("TZ");
    }
    ::setenv("TZ", tz, 1);
    ::tzset();
  }
  ~ScopedTz() {
    if (had_) {
      ::setenv("TZ", saved_.c_str(), 1);
    } else {
      ::unsetenv("TZ");
    }
    ::tzset();
  }
  ScopedTz(const ScopedTz&) = delete;
  ScopedTz& operator=(const ScopedTz&) = delete;

 private:
  bool had_;
  std::string saved_;
};

class RebootCauseFinderImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto tmpl =
        (std::filesystem::temp_directory_path() / "rcf_test_XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    ASSERT_NE(::mkdtemp(buf.data()), nullptr);
    tmpDir_ = std::filesystem::path(buf.data());
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(tmpDir_, ec);
  }

  std::string writeSecureLog(const std::string& contents) {
    const auto path = (tmpDir_ / "secure").string();
    EXPECT_TRUE(folly::writeFile(contents, path.c_str()));
    return path;
  }

  std::string makeCrashDir(const std::vector<std::string>& entryNames) {
    const auto dir = tmpDir_ / "crash";
    std::filesystem::create_directories(dir);
    for (const auto& name : entryNames) {
      std::filesystem::create_directories(dir / name);
    }
    return dir.string();
  }

  std::string writeProcStat(const std::string& contents) {
    const auto path = (tmpDir_ / "stat").string();
    EXPECT_TRUE(folly::writeFile(contents, path.c_str()));
    return path;
  }

  std::filesystem::path tmpDir_;
};

// ---------------------------------------------------------------- boot time

TEST_F(RebootCauseFinderImplTest, BootTimeParsedFromProcStat) {
  const auto path =
      writeProcStat("cpu  1 2 3 4\nintr 0\nbtime 1781204485\nprocesses 99\n");
  EXPECT_EQ(detail::readBootTimeSec(path), 1781204485);
}

TEST_F(RebootCauseFinderImplTest, BootTimeAbsentWhenFileMissing) {
  EXPECT_FALSE(
      detail::readBootTimeSec((tmpDir_ / "does_not_exist").string())
          .has_value());
}

TEST_F(RebootCauseFinderImplTest, BootTimeAbsentWhenNoBtimeLine) {
  const auto path = writeProcStat("cpu  1 2 3 4\nintr 0\nprocesses 99\n");
  EXPECT_FALSE(detail::readBootTimeSec(path).has_value());
}

// A clock skewed into the future would make the window meaningless, so both
// log-derived readers must be disabled rather than fed a bogus anchor.
TEST_F(RebootCauseFinderImplTest, BootTimeInFutureIsRejected) {
  const auto path = writeProcStat(fmt::format("btime {}\n", nowSec() + 86400));
  EXPECT_FALSE(detail::readBootTimeSec(path).has_value());
}

// ------------------------------------------------------------- kernel panic

TEST_F(RebootCauseFinderImplTest, PanicInsideWindowIsReported) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({crashDirName(btime - 60)});

  const auto causes = *detail::readKernelPanic({dir}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].description(), "Kernel Panic");
  // The crash dir name is per-instance data, not part of the cause name.
  ASSERT_TRUE(causes[0].rawValue().has_value());
  EXPECT_EQ(*causes[0].rawValue(), crashDirName(btime - 60));
}

TEST_F(RebootCauseFinderImplTest, PanicOlderThanWindowIsIgnored) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({crashDirName(btime - kWindow - 60)});
  EXPECT_TRUE(detail::readKernelPanic({dir}, btime, kWindow).causes()->empty());
}

// A dump timestamped at or after btime belongs to the boot we are running in,
// so it cannot be the cause of that boot.
TEST_F(RebootCauseFinderImplTest, PanicAtOrAfterBootStartIsIgnored) {
  const auto btime = nowSec();
  const auto dir =
      makeCrashDir({crashDirName(btime), crashDirName(btime + 60)});
  EXPECT_TRUE(detail::readKernelPanic({dir}, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, PanicNearestBootStartWins) {
  const auto btime = nowSec();
  const auto near = btime - 30;
  const auto dir =
      makeCrashDir({crashDirName(btime - 900), crashDirName(near)});

  const auto causes = *detail::readKernelPanic({dir}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), static_cast<int64_t>(near) * 1000);
}

// kdump writes the dump during the crash kernel or in post-boot processing,
// so the directory mtime can land after btime while the panic preceded it.
// Selection must use the name, never the mtime.
TEST_F(RebootCauseFinderImplTest, PanicSelectedByNameNotMtime) {
  const auto btime = nowSec();
  const auto inWindowByName = btime - 60;
  const auto outOfWindowByName = btime + 600;
  const auto dir = makeCrashDir(
      {crashDirName(inWindowByName), crashDirName(outOfWindowByName)});

  // Invert the mtimes relative to the names. Selecting on mtime would pick the
  // out-of-window entry and drop the in-window one.
  std::filesystem::last_write_time(
      std::filesystem::path(dir) / crashDirName(inWindowByName),
      std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
  std::filesystem::last_write_time(
      std::filesystem::path(dir) / crashDirName(outOfWindowByName),
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));

  const auto causes = *detail::readKernelPanic({dir}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(
      *causes[0].occurredAtMs(), static_cast<int64_t>(inWindowByName) * 1000);
}

// A dump is always a directory. A file whose name merely starts with a
// timestamp -- a tarball bundling several dumps, say -- need not denote that
// instant at all, so it must not be read as a panic.
TEST_F(RebootCauseFinderImplTest, TimestampNamedFileIsNotAPanic) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({});
  const auto stray =
      (std::filesystem::path(dir) / (crashDirName(btime - 60) + ".tar"))
          .string();
  ASSERT_TRUE(folly::writeFile(std::string("x"), stray.c_str()));
  const auto bare =
      (std::filesystem::path(dir) / crashDirName(btime - 60)).string();
  ASSERT_TRUE(folly::writeFile(std::string("x"), bare.c_str()));

  const auto attempt = detail::readKernelPanic({dir}, btime, kWindow);
  EXPECT_TRUE(attempt.causes()->empty());
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
}

TEST_F(RebootCauseFinderImplTest, MissingCrashDirIsNotAnError) {
  EXPECT_TRUE(
      detail::readKernelPanic(
          {(tmpDir_ / "no_such_dir").string()}, nowSec(), kWindow)
          .causes()
          ->empty());
}

TEST_F(RebootCauseFinderImplTest, UnparseableCrashDirEntryIsSkipped) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({"not-a-timestamp", "README"});
  EXPECT_TRUE(detail::readKernelPanic({dir}, btime, kWindow).causes()->empty());
}

// --------------------------------------------------------- BMC wedge power

namespace {
// One entry of the BMC's persistent log, in the format rsyslog writes with
// the LogUtilFileFormat template: leading space, then a four-digit year.
std::string bmcLine(std::time_t when, const std::string& message) {
  std::tm tm{};
  localtime_r(&when, &tm);
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y %b %e %H:%M:%S", &tm);
  return fmt::format(" {} bmc user.crit bmc-image: root: {}", stamp, message);
}

std::string bmcBody(const std::vector<std::string>& lines) {
  folly::dynamic entries = folly::dynamic::array;
  for (const auto& l : lines) {
    entries.push_back(l);
  }
  folly::dynamic info = folly::dynamic::object("entries", entries);
  return folly::toJson(folly::dynamic::object("Information", info));
}
} // namespace

TEST_F(RebootCauseFinderImplTest, BmcChassisResetInsideWindowIsReported) {
  const auto btime = nowSec();
  const auto body =
      bmcBody({bmcLine(btime - 160, "Power reset the whole system ...")});

  const auto attempt = detail::parseBmcWedgePower(body, btime, kWindow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  ASSERT_EQ(attempt.causes()->size(), 1);
  EXPECT_EQ(*(*attempt.causes())[0].description(), "ChassisResetFromBmc");
}

TEST_F(RebootCauseFinderImplTest, BmcX86ResetIsDistinguishedFromChassisReset) {
  const auto btime = nowSec();
  const auto body =
      bmcBody({bmcLine(btime - 160, "Power reset x86 (userver) ...")});

  const auto causes =
      *detail::parseBmcWedgePower(body, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].description(), "X86ResetFromBmc");
}

// The BMC's ACL grants MANAGED_HOST_ANY to this address and no other, and
// RestClient::setSourceAddress throws unless it is a zoned link-local. On a
// switch either failure is recorded as READ_FAILED, which is
// indistinguishable from the BMC being unreachable.
//
// Checked textually rather than by parsing the whole string: folly resolves
// the zone through getaddrinfo, so IPAddressV6("fe80::2%eth0.4088") throws
// anywhere that interface is absent, this test host included.
namespace {
// Shape of the real /api/sys/bmc response, trimmed to the fields read.
std::string bmcInfoBody(
    const std::string& uptimeSec,
    const std::string& reason) {
  return fmt::format(
      R"({{"Information": {{"Description": "BMC",
          "Reset Reason": "{}", "uptime": "{}"}},
          "Actions": [], "Resources": []}})",
      reason,
      uptimeSec);
}
constexpr int64_t kNow = 1790723381;
} // namespace

// The BMC shares the chassis power rail, so losing power brings both up
// together, the BMC first.
TEST_F(RebootCauseFinderImplTest, PowerLossWhenBmcAndX86StartedTogether) {
  const int64_t btime = kNow - 400; // x86 up 400s
  auto attempt = detail::parseSuddenPowerLoss(
      bmcInfoBody("550.25", "Power ON Reset"), btime, kNow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  ASSERT_EQ(attempt.causes()->size(), 1);
  EXPECT_EQ(*attempt.causes()->front().description(), "SuddenPowerLoss");
  // The BMC started first, so its start estimates when power was lost.
  EXPECT_EQ(*attempt.causes()->front().occurredAtMs(), (kNow - 550) * 1000);
  const auto& raw = *attempt.causes()->front().rawValue();
  EXPECT_NE(raw.find("skew 150s"), std::string::npos) << raw;
  EXPECT_NE(raw.find("Power ON Reset"), std::string::npos) << raw;
}

// A BMC that stayed up while only the x86 restarted is not a power loss.
TEST_F(RebootCauseFinderImplTest, NoPowerLossWhenBmcStayedUp) {
  const int64_t btime = kNow - 400;
  auto attempt = detail::parseSuddenPowerLoss(
      bmcInfoBody("86400.0", "Power ON Reset"), btime, kNow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  EXPECT_TRUE(attempt.causes()->empty());
}

// The x86 trails the BMC. A BMC younger than the x86 by more than read noise
// means the BMC restarted on its own, which is a different event.
TEST_F(RebootCauseFinderImplTest, NoPowerLossWhenBmcIsYoungerThanX86) {
  const int64_t btime = kNow - 4000;
  auto attempt = detail::parseSuddenPowerLoss(
      bmcInfoBody("120.0", "Power ON Reset"), btime, kNow);
  EXPECT_TRUE(attempt.causes()->empty());
}

// The observed skew band is roughly 130-185s, so it has to be admitted.
TEST_F(RebootCauseFinderImplTest, PowerLossAcceptsTheMeasuredBootSkewBand) {
  for (const int64_t skew : {133, 183}) {
    const int64_t btime = kNow - 300;
    auto attempt = detail::parseSuddenPowerLoss(
        bmcInfoBody(folly::to<std::string>(300 + skew, ".0"), "Power ON Reset"),
        btime,
        kNow);
    EXPECT_EQ(attempt.causes()->size(), 1) << "skew " << skew;
  }
}

TEST_F(RebootCauseFinderImplTest, PowerLossReportsParseFailedOnGarbage) {
  auto attempt = detail::parseSuddenPowerLoss("not json", kNow - 100, kNow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
  EXPECT_TRUE(attempt.causes()->empty());
}

TEST_F(
    RebootCauseFinderImplTest,
    PowerLossReportsParseFailedWhenUptimeMissing) {
  auto attempt = detail::parseSuddenPowerLoss(
      R"({"Information": {"Reset Reason": "Power ON Reset"}})",
      kNow - 100,
      kNow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
}

// Every other provider keeps the source its answer came from. This one is
// where a reader goes next to find out who asked for the reboot, so the line
// and its timestamp have to survive.
TEST_F(RebootCauseFinderImplTest, X86RebootCommandKeepsTheSourceLine) {
  const auto btime = 1790572718;
  const auto line = fmt::format(
      "{} sw systemd-logind[1]: System is rebooting.", syslogStamp(btime - 30));
  const auto path = writeSecureLog(line + "\n");
  auto attempt = detail::readX86RebootCommand({path}, btime, 3600);
  ASSERT_EQ(attempt.causes()->size(), 1);
  ASSERT_TRUE(attempt.causes()->front().rawValue().has_value());
  EXPECT_EQ(*attempt.causes()->front().rawValue(), line);
}

// wedge_power.sh ends an action announcement with "...". Three lines carry
// the leading text without being a reset: the failure the celestica
// powercycle patch logs, the script's own usage text, and a line truncated
// by log rotation. All three were reported as successful resets.
TEST_F(RebootCauseFinderImplTest, BmcNonResetLinesAreNotCauses) {
  const auto btime = nowSec();
  for (const auto* message :
       {"Power reset the whole system failed",
        "  reset: Power reset x86 (userver) ungracefully",
        "Power reset the whole system"}) {
    const auto body = bmcBody({bmcLine(btime - 160, message)});
    const auto attempt = detail::parseBmcWedgePower(body, btime, kWindow);
    EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
    EXPECT_TRUE(attempt.causes()->empty()) << message;
  }
}

// The success line takes a longer form when a start-up delay is configured,
// so the ellipsis cannot be matched as a fixed suffix.
TEST_F(RebootCauseFinderImplTest, BmcChassisResetWithStartupDelayIsReported) {
  const auto btime = nowSec();
  const auto body = bmcBody({bmcLine(
      btime - 160,
      "Power reset the whole system with start-up time delay 30 seconds ...")});
  const auto attempt = detail::parseBmcWedgePower(body, btime, kWindow);
  ASSERT_EQ(attempt.causes()->size(), 1);
  EXPECT_EQ(*(*attempt.causes())[0].description(), "ChassisResetFromBmc");
}

TEST_F(RebootCauseFinderImplTest, BmcSourceAddressIsZonedLinkLocal) {
  auto source = detail::bmcHostSourceAddress();
  auto zone = source.find('%');
  ASSERT_NE(zone, std::string::npos) << "source must carry its zone";
  EXPECT_TRUE(folly::IPAddressV6(source.subpiece(0, zone).str()).isLinkLocal());
  EXPECT_FALSE(source.subpiece(zone + 1).empty());
}

TEST_F(RebootCauseFinderImplTest, BmcRawValueKeepsTheSourceLine) {
  const auto btime = nowSec();
  const auto line = bmcLine(btime - 160, "Power reset the whole system ...");
  const auto causes =
      *detail::parseBmcWedgePower(bmcBody({line}), btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  // The parse is auditable only if the line it came from survives.
  EXPECT_EQ(*causes[0].rawValue(), line);
}

TEST_F(RebootCauseFinderImplTest, BmcNearestToBootStartWins) {
  const auto btime = nowSec();
  const auto body = bmcBody(
      {bmcLine(btime - 900, "Power reset x86 (userver) ..."),
       bmcLine(btime - 160, "Power reset the whole system ...")});

  const auto causes =
      *detail::parseBmcWedgePower(body, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].description(), "ChassisResetFromBmc");
}

TEST_F(RebootCauseFinderImplTest, BmcEventOlderThanWindowIsIgnored) {
  const auto btime = nowSec();
  const auto body = bmcBody(
      {bmcLine(btime - kWindow - 60, "Power reset the whole system ...")});

  EXPECT_TRUE(
      detail::parseBmcWedgePower(body, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, BmcEventAfterBootStartIsIgnored) {
  const auto btime = nowSec();
  const auto body =
      bmcBody({bmcLine(btime + 60, "Power reset the whole system ...")});

  EXPECT_TRUE(
      detail::parseBmcWedgePower(body, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, BmcPreNtpDefaultDateIsIgnored) {
  const auto btime = nowSec();
  // A line written before the BMC syncs NTP carries the image build-default
  // date. It is months from any real boot, so the window rejects it.
  const auto body =
      bmcBody({" 2026 Mar 13 08:35:46 bmc user.crit bmc-image: root: "
               "Power reset the whole system ..."});

  EXPECT_TRUE(
      detail::parseBmcWedgePower(body, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, BmcUnrelatedLinesAreNotCauses) {
  const auto btime = nowSec();
  const auto body = bmcBody(
      {bmcLine(btime - 160, "Power on uServer (from power-on.sh).."),
       bmcLine(btime - 150, "Successfully power off micro-server")});

  const auto attempt = detail::parseBmcWedgePower(body, btime, kWindow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  EXPECT_TRUE(attempt.causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, BmcMalformedResponseIsParseFailed) {
  const auto btime = nowSec();
  const auto attempt = detail::parseBmcWedgePower("not json", btime, kWindow);
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
  EXPECT_TRUE(attempt.causes()->empty());
}

// ------------------------------------------------------ x86 reboot command

TEST_F(RebootCauseFinderImplTest, X86RebootCommandInsideWindowIsReported) {
  const auto btime = nowSec();
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime - 60)));

  const auto causes =
      *detail::readX86RebootCommand({path}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].description(), "X86 Reboot Command");
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandOlderThanWindowIsIgnored) {
  const auto btime = nowSec();
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime - kWindow - 60)));
  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

// The line is written before the machine goes down, so a line at or after
// btime is a reboot being requested now -- the cause of the *next* boot.
TEST_F(RebootCauseFinderImplTest, X86RebootCommandAfterBootStartIsIgnored) {
  const auto btime = nowSec() - 300;
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime + 60)));
  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandExactlyAtBootStartIsIgnored) {
  const auto btime = nowSec() - 300;
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime)));
  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandNearestBootStartWins) {
  const auto btime = nowSec();
  const auto near = btime - 30;
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw systemd-logind[1]: System is rebooting.\n"
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime - 900),
          syslogStamp(near)));

  const auto causes =
      *detail::readX86RebootCommand({path}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), static_cast<int64_t>(near) * 1000);
}

TEST_F(RebootCauseFinderImplTest, NonMatchingLinesAreIgnored) {
  const auto btime = nowSec();
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw sshd: Accepted publickey for admin\n"
          "{} sw sudo: admin : TTY=pts/0 ; COMMAND=/bin/ls\n",
          syslogStamp(btime - 60),
          syslogStamp(btime - 50)));
  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

TEST_F(RebootCauseFinderImplTest, MissingSecureLogIsNotAnError) {
  EXPECT_TRUE(
      detail::readX86RebootCommand(
          {(tmpDir_ / "no_such_file").string()}, nowSec(), kWindow)
          .causes()
          ->empty());
}

// Syslog carries no year. Anchoring on btime rather than on the current time
// is what keeps a December line correct when it is read in January.
TEST_F(
    RebootCauseFinderImplTest,
    DecemberLineReadInJanuaryResolvesToPriorYear) {
  // btime: 2027-01-01 00:10:00 local. Event: Dec 31 23:55:00, 15 min earlier.
  std::tm bootTm{};
  bootTm.tm_year = 127; // 2027
  bootTm.tm_mon = 0;
  bootTm.tm_mday = 1;
  bootTm.tm_hour = 0;
  bootTm.tm_min = 10;
  bootTm.tm_isdst = -1;
  const auto btime = static_cast<int64_t>(std::mktime(&bootTm));
  ASSERT_NE(btime, -1);

  const auto path = writeSecureLog(
      "Dec 31 23:55:00 sw systemd-logind[1]: System is rebooting.\n");

  const auto causes =
      *detail::readX86RebootCommand({path}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  // Must land 15 minutes before boot, not ~a year after it.
  EXPECT_EQ(*causes[0].occurredAtMs(), (btime - 900) * 1000);
}

// --------------------------------------------- golden inputs from the real
// producers. These are literal strings, not round-tripped through the same
// strftime the implementation parses with, so a shared wrong assumption about
// the format cannot cancel out. Tests that assert an absolute epoch set the
// zone themselves with ScopedTz, since the zone is part of the assertion.

TEST_F(RebootCauseFinderImplTest, GoldenCrashDirNameWithDaylightZone) {
  // 2026-09-11 16:46:31 PDT == 1789170391, cross-checked against a record
  // written by a real dump. The epoch is absolute, so the zone is part of
  // the assertion.
  const ScopedTz tz("America/Los_Angeles");
  const auto when = detail::parseCrashDirName("2026-09-11T16:46:31PDT");
  ASSERT_TRUE(when.has_value());
  EXPECT_EQ(static_cast<int64_t>(*when), 1789170391);
}

TEST_F(RebootCauseFinderImplTest, GoldenCrashDirNameWithStandardZone) {
  // A winter, standard-time producer string. The expectation below builds the
  // same instant by hand with tm_isdst = 0, which is only true in a zone whose
  // January is standard time.
  const ScopedTz tz("America/Los_Angeles");
  const auto when = detail::parseCrashDirName("2026-01-15T01:15:29PST");
  ASSERT_TRUE(when.has_value());
  std::tm tm{};
  tm.tm_year = 126;
  tm.tm_mon = 0;
  tm.tm_mday = 15;
  tm.tm_hour = 1;
  tm.tm_min = 15;
  tm.tm_sec = 29;
  tm.tm_isdst = 0;
  EXPECT_EQ(*when, std::mktime(&tm));
}

// The name is produced by localtime() on this host and read back by
// mktime() on the same host, so the round trip is exact. The zone text is
// not interpreted: two names differing only in that text denote the same
// wall clock and resolve identically.
TEST_F(RebootCauseFinderImplTest, ZoneTextIsNotInterpreted) {
  const auto pdt = detail::parseCrashDirName("2026-11-01T01:30:00PDT");
  const auto pst = detail::parseCrashDirName("2026-11-01T01:30:00PST");
  ASSERT_TRUE(pdt.has_value());
  ASSERT_TRUE(pst.has_value());
  EXPECT_EQ(*pdt, *pst);
}

// Round trip against the producer: render an instant the way kdump does,
// parse it back, and require the original instant.
TEST_F(RebootCauseFinderImplTest, RoundTripsAgainstStrftime) {
  for (const int64_t t : {1789170391L, 1800000000L, nowSec()}) {
    const auto tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%Z", &tm);
    const auto parsed = detail::parseCrashDirName(buf);
    ASSERT_TRUE(parsed.has_value()) << buf;
    EXPECT_EQ(static_cast<int64_t>(*parsed), t) << buf;
  }
}

// The round trip must hold in any zone, not just the pinned one. Europe is
// the case that matters: CEST and BST are *summer* times whose abbreviation
// ends in "ST", so any attempt to infer daylight from the suffix misdates
// every summer instant there by an hour. Pinning TZ hides that, so this test
// changes it deliberately.
TEST_F(RebootCauseFinderImplTest, RoundTripsInZonesWhereSuffixMisleads) {
  for (const char* tz : {"Europe/Berlin", "Europe/London", "Asia/Tokyo"}) {
    const ScopedTz scoped(tz);
    const auto tt = static_cast<std::time_t>(1789170391); // a summer instant
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%Z", &tm);
    const auto parsed = detail::parseCrashDirName(buf);
    EXPECT_TRUE(parsed.has_value()) << tz << " " << buf;
    if (parsed) {
      EXPECT_EQ(static_cast<int64_t>(*parsed), 1789170391) << tz << " " << buf;
    }
  }
}

// The hyphen-separated form, with no zone.
TEST_F(RebootCauseFinderImplTest, GoldenCrashDirNameKdumpHyphenForm) {
  EXPECT_TRUE(detail::parseCrashDirName("2026-09-11-16:46:31").has_value());
}

TEST_F(RebootCauseFinderImplTest, CrashDirNameGarbageRejected) {
  EXPECT_FALSE(detail::parseCrashDirName("README").has_value());
  EXPECT_FALSE(detail::parseCrashDirName("not-a-timestamp").has_value());
  EXPECT_FALSE(detail::parseCrashDirName("").has_value());
}

// The %Z text is accepted and ignored, in every form a producer emits. The
// remainder is deliberately not validated, because producers emit zone
// abbreviations of two to five characters as well as numeric offsets and
// rejecting an unfamiliar one would lose a real dump, so this
// pins what parses, not what is rejected.
TEST_F(RebootCauseFinderImplTest, CrashDirNameZoneSuffixesAccepted) {
  for (const auto* name : {
           "2026-09-24T16:32:00", // no zone at all
           "2026-09-24T16:32:00PDT",
           "2026-09-24T16:32:00PST",
           "2026-09-24T16:32:00UTC", // lost to a DT/ST-only rule
           "2026-09-24T16:32:00GMT",
           "2026-09-24T16:32:00CEST", // four letters
           "2026-09-24T16:32:00AEDT",
           "2026-09-24T16:32:00+06", // zones with no abbreviation
           "2026-09-24T16:32:00+0530",
       }) {
    EXPECT_TRUE(detail::parseCrashDirName(name).has_value()) << name;
  }
}

// Real systemd-logind line, space-padded single-digit day.
TEST_F(RebootCauseFinderImplTest, GoldenSyslogLineSpacePaddedDay) {
  std::tm tm{};
  tm.tm_year = 126;
  tm.tm_mon = 8;
  tm.tm_mday = 3;
  tm.tm_hour = 1;
  tm.tm_min = 2;
  tm.tm_sec = 13;
  tm.tm_isdst = -1;
  const auto eventT = std::mktime(&tm);
  ASSERT_NE(eventT, -1);
  const auto btime = static_cast<int64_t>(eventT) + 120;

  const auto path = writeSecureLog(
      "Sep  3 01:02:03 sw systemd-logind[1234]: The system will reboot now!\n"
      "Sep  3 01:02:13 sw systemd-logind[1234]: System is rebooting.\n");

  const auto causes =
      *detail::readX86RebootCommand({path}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), static_cast<int64_t>(eventT) * 1000);
}

// Which file systemd-logind's line lands in depends on the image's syslog
// routing. Searching only one path is a false negative on images that route
// it to the other.
TEST_F(RebootCauseFinderImplTest, X86RebootCommandFoundInSecondLogPath) {
  const auto btime = nowSec();
  const auto messages = (tmpDir_ / "messages").string();
  ASSERT_TRUE(
      folly::writeFile(
          fmt::format(
              "{} sw systemd-logind[1]: System is rebooting.\n",
              syslogStamp(btime - 60)),
          messages.c_str()));
  const auto secure = writeSecureLog("Sep  3 01:02:03 sw sudo: nothing here\n");

  const auto causes =
      *detail::readX86RebootCommand({secure, messages}, btime, kWindow)
           .causes();
  ASSERT_EQ(causes.size(), 1);
}

// A line after btime must fail outright, not resolve to the prior year. The
// prior-year candidate is always <= btime, so without a plausibility bound it
// silently returns a timestamp roughly a year old.
TEST_F(RebootCauseFinderImplTest, PostBootLineDoesNotResolveToPriorYear) {
  std::tm tm{};
  tm.tm_year = 126;
  tm.tm_mon = 5;
  tm.tm_mday = 15;
  tm.tm_hour = 12;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  const auto btime = static_cast<int64_t>(std::mktime(&tm));

  // Line one hour AFTER btime.
  const auto path = writeSecureLog(
      "Jun 15 13:00:00 sw systemd-logind[1]: System is rebooting.\n");
  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

// ------------------------------------------- absent source vs unreadable one

// The whole point of RebootCauseProviderStatus is to tell "read cleanly and
// found nothing" apart from "could not read the source". An absent source is
// the former: most switches have never panicked, and no image uses every log
// path. Both readers must leave status OK for it.
TEST_F(RebootCauseFinderImplTest, AbsentSourcesAreNotAReadFailure) {
  const auto panic =
      detail::readKernelPanic({(tmpDir_ / "nope").string()}, nowSec(), kWindow);
  EXPECT_TRUE(panic.causes()->empty());
  EXPECT_EQ(*panic.status(), rcc::RebootCauseProviderStatus::OK);

  const auto attempt = detail::readX86RebootCommand(
      {(tmpDir_ / "nope").string()}, nowSec(), kWindow);
  EXPECT_TRUE(attempt.causes()->empty());
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
}

// A crash "dir" that is really a regular file exists but cannot be iterated.
// Using ENOTDIR rather than chmod keeps the test honest when it runs as root,
// where a 0000 mode is still readable.
TEST_F(RebootCauseFinderImplTest, UnreadableCrashDirIsReadFailure) {
  const auto notADir = (tmpDir_ / "crash_is_a_file").string();
  ASSERT_TRUE(folly::writeFile(std::string("x"), notADir.c_str()));

  const auto attempt = detail::readKernelPanic({notADir}, nowSec(), kWindow);
  EXPECT_TRUE(attempt.causes()->empty());
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
}

// Likewise a log "file" that is really a directory: present, but EISDIR.
TEST_F(RebootCauseFinderImplTest, UnreadableLogPathIsReadFailure) {
  const auto notAFile = (tmpDir_ / "secure_is_a_dir").string();
  std::filesystem::create_directories(notAFile);

  const auto attempt =
      detail::readX86RebootCommand({notAFile}, nowSec(), kWindow);
  EXPECT_TRUE(attempt.causes()->empty());
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
}

// A path that cannot even be stat'ed is distinct from one that stats fine but
// will not open. A symlink loop gives ELOOP from exists() itself, and unlike
// chmod it behaves the same when the test runs as root.
TEST_F(RebootCauseFinderImplTest, UnstatableLogPathIsReadFailure) {
  const auto a = tmpDir_ / "loop_a";
  const auto b = tmpDir_ / "loop_b";
  std::error_code ec;
  std::filesystem::create_symlink(b, a, ec);
  ASSERT_FALSE(ec);
  std::filesystem::create_symlink(a, b, ec);
  ASSERT_FALSE(ec);

  const auto attempt =
      detail::readX86RebootCommand({a.string()}, nowSec(), kWindow);
  EXPECT_TRUE(attempt.causes()->empty());
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
}

// One bad source must not discard a cause found in a good one: the attempt is
// both READ_FAILED and carries the cause.
TEST_F(RebootCauseFinderImplTest, FailedSourceStillReportsCauseFromGoodOne) {
  const auto btime = nowSec();
  const auto good = makeCrashDir({crashDirName(btime - 60)});
  const auto bad = (tmpDir_ / "crash_is_a_file").string();
  ASSERT_TRUE(folly::writeFile(std::string("x"), bad.c_str()));

  const auto attempt = detail::readKernelPanic({bad, good}, btime, kWindow);
  ASSERT_EQ(attempt.causes()->size(), 1);
  EXPECT_EQ(*attempt.causes()->front().rawValue(), crashDirName(btime - 60));
  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
}

// ------------------------------------------------- where the dump lives

// A dump can sit in either directory, so searching only one loses it.
TEST_F(RebootCauseFinderImplTest, PanicFoundInUnprocessedCrashDir) {
  const auto btime = nowSec();
  const auto raw = (tmpDir_ / "crash").string();
  std::filesystem::create_directories(
      std::filesystem::path(raw) / crashDirName(btime - 60));
  const auto processed = (tmpDir_ / "crash" / "processed").string();
  std::filesystem::create_directories(processed);

  const auto causes =
      *detail::readKernelPanic({raw, processed}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].rawValue(), crashDirName(btime - 60));
}

// A dump in the processed directory is still found. Scanning the parent
// also turns up the processed dir itself; kProcessedDirName keeps that from
// logging a spurious parse error, which is log noise only and so is not
// asserted here.
TEST_F(RebootCauseFinderImplTest, PanicFoundInProcessedCrashDir) {
  const auto btime = nowSec();
  const auto raw = (tmpDir_ / "crash").string();
  const auto processed = (tmpDir_ / "crash" / "processed").string();
  std::filesystem::create_directories(
      std::filesystem::path(processed) / crashDirName(btime - 60));

  const auto causes =
      *detail::readKernelPanic({raw, processed}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].rawValue(), crashDirName(btime - 60));
}

// A dump caught mid-move exists in both places; it is one panic, not two,
// and the reported instant must not depend on scan order.
TEST_F(RebootCauseFinderImplTest, SameDumpInBothDirsReportedOnce) {
  const auto btime = nowSec();
  const auto raw = (tmpDir_ / "crash").string();
  const auto processed = (tmpDir_ / "crash" / "processed").string();
  std::filesystem::create_directories(
      std::filesystem::path(raw) / crashDirName(btime - 60));
  std::filesystem::create_directories(
      std::filesystem::path(processed) / crashDirName(btime - 60));

  const auto causes =
      *detail::readKernelPanic({raw, processed}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), (btime - 60) * 1000);
}

// Nearest-to-btime must win across directories, not just within one.
TEST_F(RebootCauseFinderImplTest, NearestPanicWinsAcrossCrashDirs) {
  const auto btime = nowSec();
  const auto raw = (tmpDir_ / "crash").string();
  const auto processed = (tmpDir_ / "crash" / "processed").string();
  std::filesystem::create_directories(
      std::filesystem::path(raw) / crashDirName(btime - 30));
  std::filesystem::create_directories(
      std::filesystem::path(processed) / crashDirName(btime - 900));

  const auto causes =
      *detail::readKernelPanic({raw, processed}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), (btime - 30) * 1000);
}

TEST_F(RebootCauseFinderImplTest, CrashDirsCoverBothLocations) {
  const std::vector<std::string> expected{"/var/crash", "/var/crash/processed"};
  EXPECT_EQ(detail::kernelPanicCrashDirs(), expected);
}

// ------------------------------------------------- who wrote the syslog line

// sshd records every remote command verbatim into the same files this reader
// searches, so anyone grepping for the phrase plants a line containing it.
// This is not hypothetical: a bulk sweep counting reboot lines put the
// phrase into /var/log/secure on every host it ran against. Both fixtures sit
// inside the window, so only the program check rejects them.
TEST_F(RebootCauseFinderImplTest, SshdEchoOfThePhraseIsNotAReboot) {
  const auto btime = nowSec();
  const auto stamp = syslogStamp(btime - 60);
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw sshd[99]: Exec Request for user admin with command "
          "grep -c \"System is rebooting\" /var/log/messages\n"
          "{} sw sshd[99]: command: grep -c \"System is rebooting\" "
          "/var/log/messages\n",
          stamp,
          stamp));

  EXPECT_TRUE(
      detail::readX86RebootCommand({path}, btime, kWindow).causes()->empty());
}

// The same file can hold both. The announcement must still be found.
TEST_F(RebootCauseFinderImplTest, AnnouncementFoundAlongsideSshdEcho) {
  const auto btime = nowSec();
  const auto path = writeSecureLog(
      fmt::format(
          "{} sw sshd[99]: Exec Request for user root with command "
          "grep -c \"System is rebooting\" /var/log/messages\n"
          "{} sw systemd-logind[1]: System is rebooting.\n",
          syslogStamp(btime - 120),
          syslogStamp(btime - 60)));

  const auto causes =
      *detail::readX86RebootCommand({path}, btime, kWindow).causes();
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), (btime - 60) * 1000);
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandLineAcceptsTrustedEmitters) {
  EXPECT_TRUE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw systemd-logind[1234]: System is rebooting."));
  EXPECT_TRUE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw systemd-logind: System is rebooting."));
  EXPECT_TRUE(
      detail::isX86RebootCommandLine(
          "Sep 13 01:02:13 sw systemd[1]: System is rebooting."));
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandLineRejectsOtherEmitters) {
  EXPECT_FALSE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw sshd[99]: System is rebooting."));
  EXPECT_FALSE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw sudo[99]: System is rebooting."));
  // A program whose name merely ends in the trusted one.
  EXPECT_FALSE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw not-systemd[1]: System is rebooting."));
}

// The phrase has to begin the message, so a line quoting it mid-sentence
// under a trusted program name still does not count.
TEST_F(
    RebootCauseFinderImplTest,
    X86RebootCommandLineRequiresPhraseAtMessageStart) {
  EXPECT_FALSE(
      detail::isX86RebootCommandLine(
          "Sep  3 01:02:13 sw systemd[1]: checking whether System is rebooting."));
}

TEST_F(RebootCauseFinderImplTest, X86RebootCommandLineRejectsMalformedLines) {
  EXPECT_FALSE(detail::isX86RebootCommandLine(""));
  EXPECT_FALSE(detail::isX86RebootCommandLine("System is rebooting."));
  EXPECT_FALSE(detail::isX86RebootCommandLine("Sep  3 01:02:13 sw systemd[1]"));
}

// ----------------------------------------------------------- exact boundaries

TEST_F(RebootCauseFinderImplTest, PanicExactlyAtWindowLowerBoundIsIncluded) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({crashDirName(btime - kWindow)});
  EXPECT_EQ(detail::readKernelPanic({dir}, btime, kWindow).causes()->size(), 1);
}

TEST_F(RebootCauseFinderImplTest, PanicOneSecondBeforeWindowIsExcluded) {
  const auto btime = nowSec();
  const auto dir = makeCrashDir({crashDirName(btime - kWindow - 1)});
  EXPECT_TRUE(detail::readKernelPanic({dir}, btime, kWindow).causes()->empty());
}

// ------------------------------------------- window bounded by previous boot

constexpr int64_t kBtime = 1'000'000;

TEST_F(RebootCauseFinderImplTest, WindowIsUnchangedWithoutAPreviousBoot) {
  EXPECT_EQ(
      detail::boundWindowByPreviousBoot(kBtime, kWindow, std::nullopt),
      kWindow);
}

TEST_F(RebootCauseFinderImplTest, WindowStopsAtThePreviousBootsStart) {
  EXPECT_EQ(
      detail::boundWindowByPreviousBoot(kBtime, kWindow, kBtime - 600), 600);
}

TEST_F(RebootCauseFinderImplTest, PreviousBootBeforeTheWindowLeavesItAlone) {
  EXPECT_EQ(
      detail::boundWindowByPreviousBoot(kBtime, kWindow, kBtime - kWindow - 1),
      kWindow);
}

// A clock stepped back between boots can date the previous boot after this
// one, which would make the window negative.
TEST_F(RebootCauseFinderImplTest, PreviousBootDatedAfterThisOneIsIgnored) {
  EXPECT_EQ(
      detail::boundWindowByPreviousBoot(kBtime, kWindow, kBtime + 60), kWindow);
}

TEST_F(RebootCauseFinderImplTest, BootTimeExactlyNowIsAccepted) {
  const auto now = nowSec();
  const auto path = writeProcStat(fmt::format("btime {}\n", now));
  EXPECT_TRUE(detail::readBootTimeSec(path).has_value());
}

TEST_F(RebootCauseFinderImplTest, BootTimeOneSecondInFutureIsRejected) {
  const auto path = writeProcStat(fmt::format("btime {}\n", nowSec() + 60));
  EXPECT_FALSE(detail::readBootTimeSec(path).has_value());
}

TEST_F(RebootCauseFinderImplTest, MalformedBtimeLineIsRejected) {
  EXPECT_FALSE(
      detail::readBootTimeSec(writeProcStat("btime notanumber\n")).has_value());
  EXPECT_FALSE(detail::readBootTimeSec(writeProcStat("btime\n")).has_value());
  EXPECT_FALSE(
      detail::readBootTimeSec(writeProcStat("xbtime 12345\n")).has_value());
}

// ------------------------------------------------ precedence and year bound

facebook::fboss::platform::reboot_cause_config::RebootCause causeAt(
    const std::string& description,
    int64_t epochSec) {
  facebook::fboss::platform::reboot_cause_config::RebootCause c;
  c.description() = description;
  c.occurredAtMs() = epochSec * 1000;
  c.occurredAtPacific() = "";
  return c;
}

facebook::fboss::platform::reboot_cause_config::RebootCauseProviderAttempt
attemptWith(
    const std::string& name,
    std::vector<facebook::fboss::platform::reboot_cause_config::RebootCause>
        causes) {
  facebook::fboss::platform::reboot_cause_config::RebootCauseProviderAttempt a;
  a.name() = name;
  a.status() = facebook::fboss::platform::reboot_cause_config::
      RebootCauseProviderStatus::OK;
  a.detail() = "";
  a.causes() = std::move(causes);
  return a;
}

// A panic and a later operator reboot can both land inside one window. The
// reboot is then the cause; the panic belongs to the boot before it. Ordering
// must come from the timestamps, not from which reader ran first.
TEST_F(RebootCauseFinderImplTest, NearestToBootWinsRegardlessOfListOrder) {
  const int64_t btime = 1789170391;
  const auto panic = causeAt("Kernel Panic", btime - 900);
  const auto rebootCmd = causeAt("X86 Reboot Command", btime - 30);

  auto panicFirst = detail::selectNearestToBoot(
      {attemptWith("KernelPanic", {panic}),
       attemptWith("X86RebootCommand", {rebootCmd})});
  ASSERT_TRUE(panicFirst.has_value());
  EXPECT_EQ(*panicFirst->providerName(), "X86RebootCommand");

  auto rebootCmdFirst = detail::selectNearestToBoot(
      {attemptWith("X86RebootCommand", {rebootCmd}),
       attemptWith("KernelPanic", {panic})});
  ASSERT_TRUE(rebootCmdFirst.has_value());
  EXPECT_EQ(*rebootCmdFirst->providerName(), "X86RebootCommand");
}

TEST_F(RebootCauseFinderImplTest, NearestToBootPicksPanicWhenItIsNearer) {
  const int64_t btime = 1789170391;
  auto best = detail::selectNearestToBoot(
      {attemptWith(
           "X86RebootCommand", {causeAt("X86 Reboot Command", btime - 900)}),
       attemptWith("KernelPanic", {causeAt("Kernel Panic", btime - 30)})});
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(*best->providerName(), "KernelPanic");
}

// Reading only /var/log/secure missed a real graceful reboot on minipack3n,
// where systemd-logind logs to /var/log/messages. Lock both paths in.
TEST_F(RebootCauseFinderImplTest, X86RebootCommandSearchesMessagesAndSecure) {
  const auto& paths = detail::x86RebootCommandLogPaths();
  EXPECT_NE(
      std::find(paths.begin(), paths.end(), "/var/log/messages"), paths.end());
  EXPECT_NE(
      std::find(paths.begin(), paths.end(), "/var/log/secure"), paths.end());
}

TEST_F(RebootCauseFinderImplTest, NearestToBootEmptyListYieldsNothing) {
  EXPECT_FALSE(detail::selectNearestToBoot({}).has_value());
}

// The prior-year candidate is always <= btime, so without a plausibility
// bound a post-boot line resolves to a timestamp about a year old instead of
// failing. Asserted directly on the parser, since the window check downstream
// would mask it.
TEST_F(RebootCauseFinderImplTest, SyslogPostBootLineYieldsNullopt) {
  std::tm tm{};
  tm.tm_year = 126;
  tm.tm_mon = 5;
  tm.tm_mday = 15;
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  const auto btime = static_cast<int64_t>(std::mktime(&tm));

  EXPECT_FALSE(
      detail::parseSyslogTimestamp("Jun 15 13:00:00 sw x: y", btime)
          .has_value());
}

TEST_F(RebootCauseFinderImplTest, SyslogDecemberLineReadInJanuaryResolves) {
  std::tm tm{};
  tm.tm_year = 127;
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_min = 10;
  tm.tm_isdst = -1;
  const auto btime = static_cast<int64_t>(std::mktime(&tm));

  const auto when =
      detail::parseSyslogTimestamp("Dec 31 23:55:00 sw x: y", btime);
  ASSERT_TRUE(when.has_value());
  EXPECT_EQ(static_cast<int64_t>(*when), btime - 900);
}

// ------------------------------------------------ determineRebootCause

// Drives the whole of determineRebootCause() against a temp tree, which is
// where the "never fabricate a cause" rule actually lives. Asserting on the
// persisted record rather than on hand-built thrift structs means these fail
// if the arbitration changes, not only if the serializer does.
class DetermineRebootCauseTest : public RebootCauseFinderImplTest {
 public:
  static constexpr auto kBootId = "11111111-2222-3333-4444-555555555555";

  // By name, not position: the implicit providers are prepended, so any new
  // one would silently shift a positional index onto the wrong attempt.
  static folly::dynamic attemptNamed(
      const folly::dynamic& rec,
      const std::string& name) {
    for (const auto& a : rec["providersAttempted"]) {
      if (a["name"].asString() == name) {
        return a;
      }
    }
    ADD_FAILURE() << "no attempt named " << name;
    return folly::dynamic::object();
  }

 protected:
  void SetUp() override {
    RebootCauseFinderImplTest::SetUp();
    // No BMC here, so every logfile read fails with no response, which is
    // retryable. Without this the whole poll window elapses per test.
    FLAGS_bmc_logfile_poll_timeout_s = 0;
    historyDir_ = (tmpDir_ / "history").string();
    procStat_ = (tmpDir_ / "stat").string();
    bootIdPath_ = (tmpDir_ / "boot_id").string();
    osReleasePath_ = (tmpDir_ / "osrelease").string();
    crashDir_ = (tmpDir_ / "crash").string();
    logPath_ = (tmpDir_ / "messages").string();
    std::filesystem::create_directories(crashDir_);
    btime_ = nowSec();
    ASSERT_TRUE(
        folly::writeFile(
            fmt::format("cpu 1 2 3\nbtime {}\n", btime_), procStat_.c_str()));
    ASSERT_TRUE(
        folly::writeFile(fmt::format("{}\n", kBootId), bootIdPath_.c_str()));
    ASSERT_TRUE(
        folly::writeFile(
            std::string("6.4.3-0_fbk15_test\n"), osReleasePath_.c_str()));
  }

  RebootCauseFinderImpl::Paths paths() const {
    return RebootCauseFinderImpl::Paths{
        historyDir_,
        procStat_,
        bootIdPath_,
        osReleasePath_,
        {crashDir_},
        {logPath_}};
  }

  // Path of the single record determineRebootCause() persisted.
  std::string singleRecordPath() const {
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(historyDir_)) {
      files.push_back(e.path().string());
    }
    EXPECT_EQ(files.size(), 1);
    return files.empty() ? std::string{} : files.front();
  }

  // The single record determineRebootCause() persisted, as parsed JSON.
  folly::dynamic readRecord() const {
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(historyDir_)) {
      files.push_back(e.path().string());
    }
    EXPECT_EQ(files.size(), 1);
    std::string contents;
    EXPECT_TRUE(folly::readFile(files.front().c_str(), contents));
    return folly::parseJson(contents);
  }

  rcc::RebootCauseConfig configWithProvider(const std::string& readPath) {
    rcc::RebootCauseProviderConfig pc;
    pc.name() = "TEST_CPLD";
    pc.priority() = 1;
    pc.sysfsReadPath() = readPath;
    pc.sysfsClearPath() = readPath + ".clear";
    rcc::RebootCauseConfig c;
    c.rebootCauseProviderConfigs() = {pc};
    return c;
  }

  // The record an earlier run left for the boot that started at bootStartSec.
  void writeEarlierBootRecord(int64_t bootStartSec) {
    rcc::RebootCauseRecord record;
    record.bootId() = fmt::format("earlier-boot-{}", bootStartSec);
    record.bootTimeMs() = bootStartSec * 1000;
    std::filesystem::create_directories(historyDir_);
    ASSERT_TRUE(
        folly::writeFile(
            folly::toJson(
                facebook::thrift::to_dynamic(
                    record, facebook::thrift::dynamic_format::PORTABLE)),
            fmt::format(
                "{}/reboot-cause-{}-{}.json",
                historyDir_,
                bootStartSec,
                *record.bootId())
                .c_str()));
  }

  void writeRebootLine(int64_t atSec) {
    ASSERT_TRUE(
        folly::writeFile(
            fmt::format(
                "{} sw systemd-logind[1]: System is rebooting.\n",
                syslogStamp(atSec)),
            logPath_.c_str()));
  }

  // This boot's record, when the history dir also holds earlier ones.
  folly::dynamic readThisBootsRecord() const {
    for (const auto& e : std::filesystem::directory_iterator(historyDir_)) {
      if (e.path().string().ends_with(fmt::format("-{}.json", kBootId))) {
        std::string contents;
        EXPECT_TRUE(folly::readFile(e.path().c_str(), contents));
        return folly::parseJson(contents);
      }
    }
    ADD_FAILURE() << "no record for this boot";
    return folly::dynamic::object();
  }

  int64_t btime_{};
  std::string historyDir_, procStat_, bootIdPath_, osReleasePath_, crashDir_,
      logPath_;
};

// The headline rule: nothing found means nothing claimed.
TEST_F(DetermineRebootCauseTest, NoProviderReportsAnythingSoNoCauseIsClaimed) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readRecord();
  EXPECT_EQ(rec.count("determinedCause"), 0);
  EXPECT_EQ(rec["bootTimeMs"].asInt(), btime_ * 1000);
  // KernelPanic and X86RebootCommand read absent sources cleanly.
  // BMCWedgePower cannot reach a BMC from a test host, so it reports
  // READ_FAILED rather than a false "nothing found".
  for (const auto& a : rec["providersAttempted"]) {
    EXPECT_EQ(a["causes"].size(), 0);
  }
  EXPECT_EQ(attemptNamed(rec, "KernelPanic")["status"].asString(), "OK");
  EXPECT_EQ(attemptNamed(rec, "X86RebootCommand")["status"].asString(), "OK");
}

TEST_F(DetermineRebootCauseTest, PanicInWindowBecomesTheDeterminedCause) {
  std::filesystem::create_directories(
      std::filesystem::path(crashDir_) / crashDirName(btime_ - 60));

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readRecord();
  ASSERT_EQ(rec.count("determinedCause"), 1);
  EXPECT_EQ(rec["determinedCause"]["providerName"].asString(), "KernelPanic");
  EXPECT_EQ(
      rec["determinedCause"]["cause"]["description"].asString(),
      "Kernel Panic");
}

// A provider that cannot be read is recorded as READ_FAILED and claims
// nothing, rather than being indistinguishable from one that found nothing.
TEST_F(DetermineRebootCauseTest, UnreadableProviderIsRecordedNotClaimed) {
  RebootCauseFinderImpl(
      configWithProvider((tmpDir_ / "absent").string()), paths())
      .determineRebootCause();

  const auto rec = readRecord();
  EXPECT_EQ(rec.count("determinedCause"), 0);
  const auto hw = attemptNamed(rec, "TEST_CPLD");
  EXPECT_EQ(hw["status"].asString(), "READ_FAILED");
}

// A provider file that parses partway must not have its half-read cause
// promoted: the attempt is PARSE_FAILED, so it reports nothing at all.
TEST_F(DetermineRebootCauseTest, PartiallyParsedProviderKeepsWholeCauses) {
  const auto path = (tmpDir_ / "causes").string();
  ASSERT_TRUE(
      folly::writeFile(
          std::string(
              R"({"causes":[{"description":"Power loss","date":)"
              R"("09-24-2026 16:32:00"},{"no_description":1}]})"),
          path.c_str()));

  RebootCauseFinderImpl(configWithProvider(path), paths())
      .determineRebootCause();

  const auto rec = readRecord();
  const auto hw = attemptNamed(rec, "TEST_CPLD");
  EXPECT_EQ(hw["status"].asString(), "PARSE_FAILED");
  ASSERT_EQ(hw["causes"].size(), 1) << "the whole cause must be kept";
  EXPECT_EQ(hw["causes"][0]["description"].asString(), "Power loss");
  ASSERT_EQ(rec.count("determinedCause"), 1) << "a whole cause must promote";
  EXPECT_EQ(
      rec["determinedCause"]["cause"]["description"].asString(), "Power loss");
}

// The guard is the record itself: a second run in the same boot is a no-op.
TEST_F(DetermineRebootCauseTest, SecondRunInTheSameBootDoesNothing) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto first = readRecord()["detectedAtMs"].asInt();

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  EXPECT_EQ(readRecord()["detectedAtMs"].asInt(), first);
}

// A clean reboot, then within the hour an unclean reset that leaves no log
// line of its own (a chassis reset, a power loss). With the BMC log
// unreadable, the clean reboot's line from before the previous boot must not
// be claimed for this one.
TEST_F(
    DetermineRebootCauseTest,
    RebootLineFromBeforeThePreviousBootIsNotClaimed) {
  writeEarlierBootRecord(btime_ - 600);
  writeRebootLine(btime_ - 700);

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readThisBootsRecord();
  EXPECT_EQ(rec.count("determinedCause"), 0);
  EXPECT_EQ(attemptNamed(rec, "X86RebootCommand")["causes"].size(), 0);
}

TEST_F(DetermineRebootCauseTest, RebootLineAfterThePreviousBootIsClaimed) {
  writeEarlierBootRecord(btime_ - 600);
  writeRebootLine(btime_ - 60);

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readThisBootsRecord();
  ASSERT_EQ(rec.count("determinedCause"), 1);
  EXPECT_EQ(
      rec["determinedCause"]["providerName"].asString(), "X86RebootCommand");
}

// With no record of the previous boot there is nothing to bound by, so the
// configured window applies.
TEST_F(DetermineRebootCauseTest, WithoutAnEarlierRecordTheFullWindowApplies) {
  writeRebootLine(btime_ - 700);

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readRecord();
  ASSERT_EQ(rec.count("determinedCause"), 1);
  EXPECT_EQ(
      rec["determinedCause"]["providerName"].asString(), "X86RebootCommand");
}

TEST_F(DetermineRebootCauseTest, UnparseableEarlierRecordDoesNotLoseTheBound) {
  writeEarlierBootRecord(btime_ - 600);
  ASSERT_TRUE(
      folly::writeFile(
          std::string("{truncated"),
          (std::filesystem::path(historyDir_) / "reboot-cause-x-y.json")
              .c_str()));
  writeRebootLine(btime_ - 700);

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  EXPECT_EQ(readThisBootsRecord().count("determinedCause"), 0);
}

// --redo_current_boot lifts that guard, for debugging a boot in place.
TEST_F(DetermineRebootCauseTest, RedoCurrentBootReplacesTheRecord) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  // Mark the record rather than comparing detectedAtMs: that has millisecond
  // resolution and both runs can land inside the same millisecond, which
  // makes the timestamp a coin flip rather than a signal. The guard matches
  // on the filename, so the contents can be anything.
  ASSERT_TRUE(
      folly::writeFile(std::string("SENTINEL"), singleRecordPath().c_str()));

  FLAGS_redo_current_boot = true;
  SCOPE_EXIT {
    FLAGS_redo_current_boot = false;
  };
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  // singleRecordPath() asserts there is exactly one file, so this pins both
  // halves: the marked record is gone and no second one was left beside it.
  std::string contents;
  ASSERT_TRUE(folly::readFile(singleRecordPath().c_str(), contents));
  EXPECT_THAT(contents, testing::Not(testing::HasSubstr("SENTINEL")));
  EXPECT_THAT(contents, testing::HasSubstr("detectedAtMs"));
}

// The filename stamp is the boot time, not the time of the run, so a record
// determined long after its boot still sorts with that boot.
TEST_F(DetermineRebootCauseTest, RecordIsNamedByBootTime) {
  const std::time_t btime = btime_ - 86400;
  ASSERT_TRUE(
      folly::writeFile(
          fmt::format("cpu 1 2 3\nbtime {}\n", btime), procStat_.c_str()));

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  std::tm tm{};
  localtime_r(&btime, &tm);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H-%M-%S", &tm);
  EXPECT_EQ(
      std::filesystem::path(singleRecordPath()).filename().string(),
      fmt::format("reboot-cause-{}-{}.json", stamp, kBootId));
}

TEST_F(DetermineRebootCauseTest, RecordCarriesTheKernelRelease) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  EXPECT_EQ(readRecord()["kernelRelease"].asString(), "6.4.3-0_fbk15_test");
}

// The kernel release is informational, so losing it must not cost the record.
TEST_F(DetermineRebootCauseTest, UnreadableKernelReleaseStillWritesRecord) {
  std::filesystem::remove(osReleasePath_);
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  EXPECT_EQ(readRecord()["kernelRelease"].asString(), "");
}

// A run writes an INTERRUPTED record before reading any provider and replaces
// it when done, so a finished run leaves exactly one record, with its cause
// status. No BMC is reachable from a test host, so a run without a cause is
// INCONCLUSIVE.
TEST_F(DetermineRebootCauseTest, FinishedRunReplacesTheInterruptedRecord) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto rec = readRecord();
  EXPECT_EQ(rec["causeStatus"].asString(), "INCONCLUSIVE");
  EXPECT_GT(rec["providersAttempted"].size(), 0);
  // Enums are written by name so the file reads without the thrift IDL.
  EXPECT_TRUE(rec["providersAttempted"][0]["status"].isString());
}

// Without btime the filename stamp falls back to the current time, which moves
// between the two writes. Both must still land in one file.
TEST_F(DetermineRebootCauseTest, WithoutBootTimeBothWritesShareOneFile) {
  std::filesystem::remove(procStat_);
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto rec = readRecord();
  EXPECT_NE(rec["causeStatus"].asString(), "INTERRUPTED");
  EXPECT_EQ(rec["bootTimeMs"].asInt(), 0);
}

// The per-run event carries the record's cause status and, when no cause was
// found, which providers could not even be read.
TEST_F(DetermineRebootCauseTest, RunEventNamesTheProvidersThatFailed) {
  const auto result =
      RebootCauseFinderImpl(
          configWithProvider((tmpDir_ / "absent").string()), paths())
          .determineRebootCause();
  EXPECT_EQ(result.outcome, RunOutcome::RECORDED);

  const auto tags = runEventTags(result, 0);
  EXPECT_EQ(tags.at("outcome"), "RECORDED");
  EXPECT_EQ(tags.at("boot_id"), kBootId);
  EXPECT_EQ(tags.at("exit_status"), "0");
  EXPECT_EQ(tags.at("cause_status"), "INCONCLUSIVE");
  EXPECT_EQ(tags.count("cause"), 0);
  // No BMC is reachable from a test host, so BMCWedgePower fails too.
  EXPECT_THAT(tags.at("failed_providers"), testing::HasSubstr("TEST_CPLD"));
  EXPECT_THAT(tags.at("failed_providers"), testing::HasSubstr("BMCWedgePower"));
}

TEST_F(DetermineRebootCauseTest, RunEventCarriesTheDeterminedCause) {
  writeRebootLine(btime_ - 60);
  const auto result = RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
                          .determineRebootCause();

  const auto tags = runEventTags(result, 0);
  EXPECT_EQ(tags.at("cause_status"), "FOUND");
  EXPECT_EQ(tags.at("provider"), "X86RebootCommand");
  EXPECT_FALSE(tags.at("cause").empty());
}

TEST_F(DetermineRebootCauseTest, RunEventForASecondRunSaysAlreadyHandled) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto result = RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
                          .determineRebootCause();
  EXPECT_EQ(result.outcome, RunOutcome::ALREADY_HANDLED);

  const auto tags = runEventTags(result, 0);
  EXPECT_EQ(tags.at("outcome"), "ALREADY_HANDLED");
  EXPECT_EQ(tags.at("boot_id"), kBootId);
  EXPECT_EQ(tags.count("cause_status"), 0);
}

// What a run killed at its runtime cap leaves behind. The boot is on record,
// and a later run in the same boot must not overwrite it: the providers may
// already have been read, so a second answer would be a worse one.
TEST_F(DetermineRebootCauseTest, InterruptedRecordFromAKilledRunIsKept) {
  rcc::RebootCauseRecord killed;
  killed.bootId() = kBootId;
  killed.bootTimeMs() = btime_ * 1000;
  killed.causeStatus() = rcc::RebootCauseStatus::INTERRUPTED;
  std::filesystem::create_directories(historyDir_);
  ASSERT_TRUE(
      folly::writeFile(
          folly::toJson(
              facebook::thrift::to_dynamic(
                  killed, facebook::thrift::dynamic_format::PORTABLE)),
          fmt::format(
              "{}/reboot-cause-{}-{}.json", historyDir_, btime_, kBootId)
              .c_str()));

  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  const auto rec = readRecord();
  EXPECT_EQ(rec["causeStatus"].asString(), "INTERRUPTED");
  EXPECT_EQ(rec["providersAttempted"].size(), 0);
}

// A cause wins over failed providers. Without one, the status separates
// "every source read and none had a cause" from "a source could not be seen".
TEST(RebootCauseStatusTest, SeparatesUnexplainedFromInconclusive) {
  using rcc::RebootCauseProviderStatus;
  using rcc::RebootCauseStatus;
  auto attempt = [](RebootCauseProviderStatus status) {
    rcc::RebootCauseProviderAttempt a;
    a.name() = "P";
    a.status() = status;
    return a;
  };
  const std::vector<rcc::RebootCauseProviderAttempt> allOk{
      attempt(RebootCauseProviderStatus::OK),
      attempt(RebootCauseProviderStatus::OK)};
  const std::vector<rcc::RebootCauseProviderAttempt> oneFailed{
      attempt(RebootCauseProviderStatus::OK),
      attempt(RebootCauseProviderStatus::READ_FAILED)};
  const std::vector<rcc::RebootCauseProviderAttempt> oneSkipped{
      attempt(RebootCauseProviderStatus::OK),
      attempt(RebootCauseProviderStatus::SKIPPED)};

  EXPECT_EQ(
      detail::rebootCauseStatus(true, oneFailed), RebootCauseStatus::FOUND);
  EXPECT_EQ(
      detail::rebootCauseStatus(false, allOk), RebootCauseStatus::UNEXPLAINED);
  EXPECT_EQ(
      detail::rebootCauseStatus(false, oneFailed),
      RebootCauseStatus::INCONCLUSIVE);
  EXPECT_EQ(
      detail::rebootCauseStatus(false, oneSkipped),
      RebootCauseStatus::INCONCLUSIVE);
}

// A redo that cannot clear the way for itself must stop. Writing a second
// record for one boot would leave two answers and no way to tell which is
// current, which is worse than refusing to redo.
TEST_F(DetermineRebootCauseTest, RedoStopsWhenTheRecordCannotBeRemoved) {
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root ignores the directory permissions this relies on";
  }
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto first = readRecord()["detectedAtMs"].asInt();

  // Removing a file needs write permission on its directory, not the file.
  std::filesystem::permissions(
      historyDir_,
      std::filesystem::perms::owner_write,
      std::filesystem::perm_options::remove);
  SCOPE_EXIT {
    std::filesystem::permissions(
        historyDir_,
        std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
  };

  FLAGS_redo_current_boot = true;
  SCOPE_EXIT {
    FLAGS_redo_current_boot = false;
  };
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  // readRecord() asserts on there being exactly one file, so this pins both
  // halves: the original survived and no second record was written.
  std::filesystem::permissions(
      historyDir_,
      std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
  EXPECT_EQ(readRecord()["detectedAtMs"].asInt(), first);
}

// Clearing the providers is destructive, so a redo that follows a run which
// already cleared them will quietly find less. It is allowed, but it says so.
TEST_F(DetermineRebootCauseTest, RedoWarnsWhenProviderClearingIsAlsoOn) {
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();

  FLAGS_redo_current_boot = true;
  FLAGS_clear_reboot_causes = true;
  SCOPE_EXIT {
    FLAGS_redo_current_boot = false;
    FLAGS_clear_reboot_causes = false;
  };

  testing::internal::CaptureStderr();
  RebootCauseFinderImpl(rcc::RebootCauseConfig{}, paths())
      .determineRebootCause();
  const auto logs = testing::internal::GetCapturedStderr();

  EXPECT_THAT(logs, testing::HasSubstr("--clear_reboot_causes is also set"));
}

// ------------------------------------------------------- hardware providers

// readProvider is the only code path that runs today, and its status is what
// the record now reports. Absent, unparseable and good are three distinct
// outcomes and must not collapse into one another.
namespace {
rcc::RebootCauseProviderConfig providerAt(const std::string& path) {
  rcc::RebootCauseProviderConfig c;
  c.name() = "TEST_CPLD";
  c.priority() = 1;
  c.sysfsReadPath() = path;
  c.sysfsClearPath() = path + ".clear";
  return c;
}
} // namespace

// A failed attempt has to say why it failed, not just what it looked at. The
// status is the same whether the BMC image lacks the endpoint, its ACL
// refused us, or the sysfs node is missing, and those have different owners.
// The record outlives the log, so the reason has to survive in `detail`.

TEST_F(RebootCauseFinderImplTest, ProviderReadFailureDetailCarriesErrno) {
  const auto path = (tmpDir_ / "definitely-absent").string();
  const auto attempt = detail::readProvider(providerAt(path));

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
  // The path alone was all the record used to carry; the errno is the part
  // that separates "not exported by this BSP" from "exists but unreadable".
  EXPECT_THAT(*attempt.detail(), ::testing::HasSubstr(path));
  EXPECT_THAT(*attempt.detail(), ::testing::HasSubstr(folly::errnoStr(ENOENT)));
}

TEST_F(RebootCauseFinderImplTest, ProviderReadFailureDetailSeparatesEacces) {
  const auto path = (tmpDir_ / "unreadable").string();
  ASSERT_TRUE(folly::writeFile(std::string("x"), path.c_str()));
  ASSERT_EQ(::chmod(path.c_str(), 0), 0);
  SCOPE_EXIT {
    ::chmod(path.c_str(), 0600);
  };
  if (::geteuid() == 0) {
    GTEST_SKIP() << "running as root, mode 0 is still readable";
  }

  const auto attempt = detail::readProvider(providerAt(path));

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
  // Distinct from the absent case above: same status, different reason.
  EXPECT_THAT(*attempt.detail(), ::testing::HasSubstr(folly::errnoStr(EACCES)));
  EXPECT_THAT(
      *attempt.detail(),
      ::testing::Not(::testing::HasSubstr(folly::errnoStr(ENOENT))));
}

TEST_F(RebootCauseFinderImplTest, BmcWedgePowerParseFailureDetailCarriesWhy) {
  const auto attempt = detail::parseBmcWedgePower("not json at all", 0, 3600);

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
  // Must be the path *plus* the parser's complaint. Asserting only that the
  // path appears, or only that the string is longer than some prefix, would
  // also hold for the bare path and so would not test anything.
  EXPECT_THAT(*attempt.detail(), ::testing::StartsWith("/api/sys/logfile: "));
  EXPECT_GT(attempt.detail()->size(), std::string("/api/sys/logfile: ").size());
}

TEST_F(RebootCauseFinderImplTest, SuddenPowerLossParseFailureDetailCarriesWhy) {
  const auto attempt = detail::parseSuddenPowerLoss("{\"bogus\": 1}", 0, 100);

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
  EXPECT_THAT(*attempt.detail(), ::testing::StartsWith("/api/sys/bmc: "));
  EXPECT_GT(attempt.detail()->size(), std::string("/api/sys/bmc: ").size());
}

TEST_F(RebootCauseFinderImplTest, ProviderGoodReadIsOkAndDecodes) {
  const auto path = (tmpDir_ / "causes").string();
  ASSERT_TRUE(
      folly::writeFile(
          std::string(
              R"({"causes":[{"description":"Power loss",)"
              R"("date":"09-24-2026 16:32:00","rawValue":"0x40"}]})"),
          path.c_str()));

  const auto attempt = detail::readProvider(providerAt(path));
  const auto& causes = *attempt.causes();

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].description(), "Power loss");
  ASSERT_TRUE(causes[0].rawValue().has_value());
  EXPECT_EQ(*causes[0].rawValue(), "0x40");
  EXPECT_NE(*causes[0].occurredAtMs(), 0);
}

// A provider that reads cleanly but reports nothing is OK with no causes --
// the case that must not look like a failure.
TEST_F(RebootCauseFinderImplTest, ProviderEmptyCauseListIsOk) {
  const auto path = (tmpDir_ / "causes").string();
  ASSERT_TRUE(folly::writeFile(std::string(R"({"causes":[]})"), path.c_str()));

  const auto attempt = detail::readProvider(providerAt(path));
  const auto& causes = *attempt.causes();

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  EXPECT_TRUE(causes.empty());
}

TEST_F(RebootCauseFinderImplTest, ProviderMalformedJsonIsParseFailed) {
  const auto path = (tmpDir_ / "causes").string();
  ASSERT_TRUE(folly::writeFile(std::string("not json at all"), path.c_str()));

  const auto attempt = detail::readProvider(providerAt(path));
  const auto& causes = *attempt.causes();

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::PARSE_FAILED);
  EXPECT_TRUE(causes.empty());
}

TEST_F(RebootCauseFinderImplTest, ProviderMissingFileIsReadFailed) {
  const auto attempt =
      detail::readProvider(providerAt((tmpDir_ / "nope").string()));
  const auto& causes = *attempt.causes();

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::READ_FAILED);
  EXPECT_TRUE(causes.empty());
}

// An unparseable date must not discard the cause: the description is still
// the useful part, and the raw string is preserved for a human.
TEST_F(RebootCauseFinderImplTest, ProviderUnparseableDateKeepsTheCause) {
  const auto path = (tmpDir_ / "causes").string();
  ASSERT_TRUE(
      folly::writeFile(
          std::string(
              R"({"causes":[{"description":"Power loss","date":"soon"}]})"),
          path.c_str()));

  const auto attempt = detail::readProvider(providerAt(path));
  const auto& causes = *attempt.causes();

  EXPECT_EQ(*attempt.status(), rcc::RebootCauseProviderStatus::OK);
  ASSERT_EQ(causes.size(), 1);
  EXPECT_EQ(*causes[0].occurredAtMs(), 0);
  // The unparsed text is not a timestamp, so the rendered field is left
  // empty and the text is kept verbatim where unparsed source belongs.
  EXPECT_TRUE(causes[0].occurredAtPacific()->empty());
  ASSERT_TRUE(causes[0].rawValue().has_value());
  EXPECT_EQ(*causes[0].rawValue(), "soon");
}

} // namespace

TEST(BmcFailureIsRetryableTest, NoResponseIsRetryable) {
  // The BMC is still booting, or the sideband address is not up yet.
  EXPECT_TRUE(detail::bmcFailureIsRetryable(std::nullopt));
}

TEST(BmcFailureIsRetryableTest, ForbiddenIsRetryable) {
  // The ACL a reboot wiped comes back when the BMC is reconfigured.
  EXPECT_TRUE(detail::bmcFailureIsRetryable(403));
}

TEST(BmcFailureIsRetryableTest, NotFoundIsNotRetryable) {
  // An image without the endpoint will not grow one while we wait.
  EXPECT_FALSE(detail::bmcFailureIsRetryable(404));
}

TEST(BmcFailureIsRetryableTest, OtherStatusesAreNotRetryable) {
  EXPECT_FALSE(detail::bmcFailureIsRetryable(200));
  EXPECT_FALSE(detail::bmcFailureIsRetryable(401));
  EXPECT_FALSE(detail::bmcFailureIsRetryable(500));
}
