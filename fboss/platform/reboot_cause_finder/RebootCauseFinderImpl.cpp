// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/reboot_cause_finder/RebootCauseFinderImpl.h"
#include "fboss/platform/reboot_cause_finder/RebootHistory.h"
#include "fboss/platform/reboot_cause_finder/if/gen-cpp2/reboot_cause_config_constants.h"

#include "fboss/lib/RestClient.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <re2/re2.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>

DEFINE_int32(
    bmc_logfile_poll_timeout_s,
    180,
    "How long to keep retrying the BMC logfile endpoint before giving up.");

DEFINE_bool(
    redo_current_boot,
    false,
    "Discard this boot's existing record and determine the cause again. For "
    "debugging: the service is a oneshot and normally refuses to run twice "
    "for one boot.");

DEFINE_bool(
    clear_reboot_causes,
    false,
    "Clear reboot causes from all providers after reading");

// Must span shutdown, POST and the bootloader. On a switch whose firmware
// attempts a network boot before falling back to local disk, the gap between
// a power cycle and btime has been observed above 25 minutes, so the default
// is deliberately generous. Erring large only risks admitting an older event:
// nearest-to-btime selection handles it when a newer one is also in range, and
// the previous boot's start bounds the window when its record exists. Erring
// small silently misses the cause.
DEFINE_int32(
    software_cause_lookback_sec,
    3600,
    "How far before boot start to look for a kernel panic, an x86 reboot "
    "command or a BMC wedge_power reset. "
    "Must cover shutdown, POST and bootloader, not the power-off duration: a "
    "long outage leaves no log line at all and is reported by the hardware "
    "providers instead, which this does not bound.");

namespace facebook::fboss::platform::reboot_cause_finder {

namespace {

constexpr auto kHistoryDir =
    reboot_cause_config::reboot_cause_config_constants::REBOOT_HISTORY_DIR();
constexpr auto kProcBootIdPath = "/proc/sys/kernel/random/boot_id";
constexpr auto kProcOsReleasePath = "/proc/sys/kernel/osrelease";

// These three come from the kernel, systemd and the crash dump tooling,
// and are the same on
// every platform, so they are constants rather than per-platform config.
constexpr auto kProcStatPath = "/proc/stat";
// Scanning /var/crash turns up the processed subdir itself. It is scanned in
// its own right, so it is not a dump name that failed to parse.
constexpr auto kProcessedDirName = "processed";

constexpr auto kKernelPanicProvider = "KernelPanic";
constexpr auto kX86RebootCommandProvider = "X86RebootCommand";

// systemd-logind writes this on the normal shutdown path.
constexpr auto kX86RebootCommandPattern = "System is rebooting";

// Programs which perform reboot.
constexpr std::array<folly::StringPiece, 2> kX86RebootCommandPrograms{
    "systemd-logind",
    "systemd"};

constexpr auto kBmcWedgePowerProvider = "BMCWedgePower";

// The BMC's persistent critical log, read in band over the sideband VLAN that
// systemd-networkd configures on every fboss-lite switch. The endpoint is
// POST and its ACL admits MANAGED_HOST_ANY, which the BMC grants to a peer at
// fe80::2. Its other grant path is a matching client certificate, which this
// plaintext request does not present, so the source address is the whole
// credential.
//
// The source has to be pinned: eth0.4088 carries more than one link-local,
// so the kernel's pick is arbitrary and flips on link flap.
constexpr auto kBmcAddress = "fe80::1";
constexpr auto kHostSourceAddress = "fe80::2%eth0.4088";
constexpr int kBmcPort = 8080;
constexpr auto kBmcLogfilePath = "/api/sys/logfile";
// Arguments go in the body. RestClient issues a POST, and sets
// Content-Type: application/json, only when postData is non-empty.
constexpr auto kBmcLogfileBody = R"({"lines": 200, "include_rotated": true})";
constexpr std::chrono::milliseconds kBmcTimeout{2000};
constexpr std::chrono::seconds kBmcLogfilePollInterval{10};

constexpr auto kSuddenPowerLossProvider = "SuddenPowerLoss";
// The BMC reports its own uptime here, both as the `uptime` command's output
// and, under the lowercase key, as seconds.
constexpr auto kBmcInfoPath = "/api/sys/bmc";

// The BMC shares the chassis power rail, so losing power restarts both it
// and the x86. The BMC finishes first, typically by two to three minutes on
// the platforms this runs on, so allow well beyond that. A BMC that has been up
// materially longer stayed up while only the x86 restarted, which is some other
// cause and is what every other provider is for.
constexpr int64_t kMaxBmcToX86BootSkewSec = 600;
// The BMC clock is not involved here, both figures are uptimes, so the only
// negative skew is measurement noise between the two reads.
constexpr int64_t kMinBmcToX86BootSkewSec = -60;

// wedge_power.sh emits these immediately before it acts, so they are written
// while the BMC clock is still NTP correct. `reset -s` cycles the whole
// chassis and takes the BMC down with it; a plain `reset` drops only the x86
// and the BMC stays up.
// wedge_power.sh announces an action it is about to take and ends the line
// with "...". Matching the leading text alone also matches three lines that
// are not a reset: the failure "Power reset the whole system failed", the
// script's own usage text "reset: Power reset x86 (userver) ungracefully",
// and a line truncated by log rotation mid-write. Requiring the ellipsis
// admits the longer success form, which inserts a configured start-up delay
// before it, while rejecting all three.
//
// Matching prose is fragile either way: these strings are a script's output,
// not an interface, and can change without notice. A structured source, for
// instance a BMC endpoint that reports the last reset and its cause, would
// remove the guesswork and is worth exploring instead of parsing logs.
constexpr auto kChassisResetPattern =
    R"(Power reset the whole system .*\.\.\.)";
constexpr auto kX86ResetPattern = R"(Power reset x86 \(userver\) .*\.\.\.)";

// The kernel regenerates this UUID on every boot, including kexec. It is not
// virtualized per namespace, and fboss platform services run as RootDirectory
// sandboxes rather than nspawn containers, so the value read here is the
// host's. A service inside a full container namespace would not see it,
// which is why this is read from /proc rather than cached.
// Empty when unreadable. The guard cannot be established without it, so the
// caller does nothing rather than risk a destructive clear.
std::string readBootId(const std::string& bootIdPath) {
  std::string contents;
  if (!folly::readFile(bootIdPath.c_str(), contents)) {
    return {};
  }
  return folly::trimWhitespace(contents).str();
}

// The binary runs in the boot it records, so the running kernel is that
// boot's kernel. Empty when unreadable; the record is still worth writing.
std::string readKernelRelease(const std::string& osReleasePath) {
  std::string contents;
  if (!folly::readFile(osReleasePath.c_str(), contents)) {
    return {};
  }
  return folly::trimWhitespace(contents).str();
}

// The boot id is the filename suffix of every record, so the existence of a
// matching file is itself the once-per-boot guard. Deliberately not a separate
// marker file: "the cause was recorded" and "this boot was already handled"
// must be the same fact, or the two can disagree and a boot's cause is lost.
// Matching on the suffix rather than picking the newest file also avoids
// trusting filenameStamp(), whose clock may not be NTP-synced this early.
std::string recordFilenameSuffix(const std::string& bootId) {
  return fmt::format("-{}.json", bootId);
}

// std::nullopt means the history dir could not be read, which is not the same
// as holding no record: the first says we cannot tell whether this boot was
// handled, the second says it was not. Collapsing them would let an unreadable
// dir look like a fresh boot and re-clear providers on every run.
std::optional<bool> recordExistsForBoot(
    const std::string& historyDir,
    const std::string& bootId) {
  std::error_code ec;
  std::filesystem::directory_iterator it(historyDir, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    return false;
  }
  if (ec) {
    XLOG(ERR) << fmt::format(
        "Failed to read history dir '{}': {}", historyDir, ec.message());
    return std::nullopt;
  }

  // Stepped by hand rather than with a range-for: operator++ throws on error,
  // and a mid-scan failure must land in the same "cannot tell" branch as a
  // failure to open the dir, not propagate out of determineRebootCause().
  const std::filesystem::directory_iterator end;
  const auto suffix = recordFilenameSuffix(bootId);
  while (it != end) {
    if (it->path().filename().string().ends_with(suffix)) {
      return true;
    }
    it.increment(ec);
    if (ec) {
      XLOG(ERR) << fmt::format(
          "Failed while scanning history dir '{}': {}",
          historyDir,
          ec.message());
      return std::nullopt;
    }
  }
  return false;
}

// Deletes this boot's record so the cause can be determined again. False if
// anything could not be removed, which must stop the caller: writing a
// second record for one boot would leave two answers and no way to tell
// which is current.
bool removeRecordForBoot(
    const std::string& historyDir,
    const std::string& bootId) {
  std::error_code ec;
  std::filesystem::directory_iterator it(historyDir, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    return true;
  }
  if (ec) {
    XLOG(ERR) << fmt::format(
        "Failed to read history dir '{}': {}", historyDir, ec.message());
    return false;
  }

  const std::filesystem::directory_iterator end;
  const auto suffix = recordFilenameSuffix(bootId);
  bool removedAll = true;
  while (it != end) {
    const auto path = it->path();
    if (path.filename().string().ends_with(suffix)) {
      std::error_code removeEc;
      std::filesystem::remove(path, removeEc);
      if (removeEc) {
        XLOG(ERR) << fmt::format(
            "Failed to remove '{}': {}", path.string(), removeEc.message());
        removedAll = false;
      } else {
        XLOG(INFO) << fmt::format("Removed '{}'", path.string());
      }
    }
    it.increment(ec);
    if (ec) {
      XLOG(ERR) << fmt::format(
          "Failed while scanning history dir '{}': {}",
          historyDir,
          ec.message());
      return false;
    }
  }
  return removedAll;
}

// Start of the newest boot recorded before btimeSec, in epoch seconds. A boot
// that left no record is skipped, so the result is at or before the real
// previous boot's start: it can only make the window looser than ideal, never
// tighter. std::nullopt when no earlier record is usable, which leaves the
// configured window in place.
std::optional<int64_t> previousBootStartSec(
    const std::string& historyDir,
    const std::string& bootId,
    int64_t btimeSec) {
  RebootHistory history;
  try {
    readRebootHistory(historyDir, history);
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Failed to read reboot history from '{}': {}", historyDir, ex.what());
    return std::nullopt;
  }
  for (const auto& warning : history.warnings) {
    XLOG(WARNING) << fmt::format("Reboot history: {}", warning);
  }
  // Newest boot first, so the first earlier boot is the previous one.
  for (const auto& record : history.records) {
    const int64_t startSec = *record.bootTimeMs() / 1000;
    if (*record.bootId() != bootId && startSec > 0 && startSec < btimeSec) {
      return startSec;
    }
  }
  return std::nullopt;
}

// Clearing a provider is destructive and unrecoverable, so it only runs once
// this boot's cause is safely on disk.
bool shouldClearProviders(bool recorded) {
  if (!FLAGS_clear_reboot_causes) {
    return false;
  }
  if (!recorded) {
    XLOG(ERR)
        << "Skipping provider clear because this boot's cause was not "
           "recorded; clearing now would discard it with nothing to show.";
    return false;
  }
  return true;
}

reboot_cause_config::RebootCauseProviderAttempt makeAttempt(
    const std::string& name,
    reboot_cause_config::RebootCauseProviderStatus status,
    const std::string& detail,
    std::vector<reboot_cause_config::RebootCause> causes = {}) {
  reboot_cause_config::RebootCauseProviderAttempt attempt;
  attempt.name() = name;
  attempt.status() = status;
  attempt.detail() = detail;
  attempt.causes() = std::move(causes);
  return attempt;
}

// A failed attempt records why it failed next to what it looked at. The
// status alone cannot separate an endpoint the BMC image lacks (404) from an
// ACL rejection (403) or an unreachable BMC, and those have different owners
// and different fixes. The record outlives the log the reason would otherwise
// only appear in, so it has to be carried here.
std::string detailWithReason(
    folly::StringPiece what,
    folly::StringPiece reason) {
  return fmt::format("{}: {}", what, reason);
}

std::string summariseAttempts(
    const std::vector<reboot_cause_config::RebootCauseProviderAttempt>&
        attempts) {
  std::vector<std::string> parts;
  parts.reserve(attempts.size());
  for (const auto& a : attempts) {
    parts.push_back(
        fmt::format(
            "{}={}",
            *a.name(),
            apache::thrift::util::enumNameSafe(*a.status())));
  }
  return folly::join(", ", parts);
}

int64_t toEpochMs(std::time_t t) {
  return static_cast<int64_t>(t) * 1000;
}

// Render an instant in the host's local time with the
// zone abbreviation, e.g. "2026-07-02 23:16:55 PDT".
std::string pacificString(std::time_t t) {
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
  return buf;
}

// Sortable local-time stamp used in the history filename.
std::string filenameStamp(std::time_t t) {
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", &tm);
  return buf;
}

// Parse a provider's local-time date string ("MM-DD-YYYY HH:MM:SS[.mmm]") into
// an epoch time_t; std::nullopt if it doesn't match.
std::optional<std::time_t> parseProviderDate(const std::string& s) {
  std::tm tm{};
  if (strptime(s.c_str(), "%m-%d-%Y %H:%M:%S", &tm) == nullptr) {
    return std::nullopt;
  }
  tm.tm_isdst = -1; // let mktime resolve PST vs PDT
  return std::mktime(&tm);
}

} // namespace

// Exposed for unit tests; see tests/RebootCauseFinderImplTest.cpp.
namespace detail {

const std::vector<std::string>& x86RebootCommandLogPaths() {
  static const std::vector<std::string> kPaths = {
      "/var/log/messages",
      "/var/log/secure",
  };
  return kPaths;
}

// Boot start in epoch seconds. std::nullopt when it cannot be established,
// which disables the log-derived causes: guessing a boot time would let an
// event from the wrong boot be reported as this boot's cause.
std::optional<int64_t> readBootTimeSec(const std::string& procStatPath) {
  std::string contents;
  if (!folly::readFile(procStatPath.c_str(), contents)) {
    XLOG(ERR) << fmt::format("Failed to read '{}'", procStatPath);
    return std::nullopt;
  }

  std::vector<folly::StringPiece> lines;
  folly::split('\n', contents, lines);

  int64_t btime = 0;
  bool found = false;
  for (const auto& line : lines) {
    if (folly::StringPiece(line).startsWith("btime ") &&
        folly::tryTo<int64_t>(folly::trimWhitespace(line.subpiece(6)))
            .hasValue()) {
      btime = *folly::tryTo<int64_t>(folly::trimWhitespace(line.subpiece(6)));
      found = true;
      break;
    }
  }
  if (!found) {
    XLOG(ERR) << fmt::format("No usable btime line in '{}'", procStatPath);
    return std::nullopt;
  }

  // btime is wall clock minus uptime, so a badly skewed clock can put it in
  // the future. The window would then be nonsense.
  const auto now = static_cast<int64_t>(
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
  if (btime > now) {
    XLOG(ERR) << fmt::format(
        "btime {} is in the future (now {}); skipping log-derived causes",
        btime,
        now);
    return std::nullopt;
  }
  return btime;
}

reboot_cause_config::RebootCause makeCause(
    const std::string& description,
    std::time_t occurredAt) {
  reboot_cause_config::RebootCause cause;
  cause.description() = description;
  cause.occurredAtMs() = toEpochMs(occurredAt);
  cause.occurredAtPacific() = pacificString(occurredAt);
  return cause;
}

// Only events inside [btime - window, btime) can have caused this boot.
// Anything at or after btime belongs to the boot we are running in, and
// anything older belongs to an earlier boot.
bool inBootWindow(std::time_t when, int64_t btimeSec, int64_t windowSec) {
  const auto t = static_cast<int64_t>(when);
  return t < btimeSec && t >= btimeSec - windowSec;
}

int64_t boundWindowByPreviousBoot(
    int64_t btimeSec,
    int64_t windowSec,
    std::optional<int64_t> previousBootStartSec) {
  if (!previousBootStartSec || *previousBootStartSec >= btimeSec) {
    return windowSec;
  }
  return std::min(windowSec, btimeSec - *previousBootStartSec);
}

// Crash dirs are named "%Y-%m-%dT%H:%M:%S%Z" or "%Y-%m-%d-%H:%M:%S".
// Any %Z is ignored; see below.
std::optional<std::time_t> parseCrashDirName(const std::string& name) {
  std::tm tm{};
  if (strptime(name.c_str(), "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) {
    tm = {};
    if (strptime(name.c_str(), "%Y-%m-%d-%H:%M:%S", &tm) == nullptr) {
      return std::nullopt;
    }
  }

  // The zone suffix is discarded and the time is resolved in this host's
  // zone. That is exact only while the producer's zone and the host's agree,
  // which is an assumption, not a guarantee: a producer that pins a zone
  // would misdate the dump by the offset on a host set to another one.
  // tm{} zero-inits tm_isdst to 0, which would assert "not daylight", so it
  // must be set to -1 explicitly.
  //
  // The one case this cannot resolve is the hour repeated at the DST
  // fall-back, where mktime picks one of the two candidates.
  tm.tm_isdst = -1;

  const auto when = std::mktime(&tm);
  if (when == -1) {
    return std::nullopt;
  }
  return when;
}

// the dump is written by the crash kernel or by post-boot processing, so it
// can fall after btime even though the panic preceded it.
// A dump can be in either directory, so both are searched.
const std::vector<std::string>& kernelPanicCrashDirs() {
  static const std::vector<std::string> kDirs = {
      "/var/crash",
      "/var/crash/processed",
  };
  return kDirs;
}

// Folds the nearest-to-btime match in one directory into best/bestName.
// Returns false only when the directory is present but could not be read; an
// absent directory is the normal case on a switch that has never panicked and
// must not be reported as a failure.
bool scanCrashDir(
    const std::string& crashDir,
    int64_t btimeSec,
    int64_t windowSec,
    std::optional<std::time_t>& best,
    std::string& bestName) {
  std::error_code ec;
  std::filesystem::directory_iterator it(crashDir, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    return true;
  }
  if (ec) {
    XLOG(ERR) << fmt::format(
        "Failed to open crash dir '{}': {}", crashDir, ec.message());
    return false;
  }

  const std::filesystem::directory_iterator end;
  while (it != end) {
    // directory_iterator yields files and symlinks too. A dump is always a
    // directory, and a file whose name merely starts with a timestamp (a
    // tarball of several dumps, say) need not denote that instant at all.
    // The error_code overload is the non-throwing one; it returns false if
    // the type cannot be determined, which is the answer we want anyway.
    std::error_code dirEc;
    if (it->is_directory(dirEc)) {
      const auto name = it->path().filename().string();
      const auto when =
          name == kProcessedDirName ? std::nullopt : parseCrashDirName(name);
      if (when) {
        if (inBootWindow(*when, btimeSec, windowSec) &&
            (!best || *when > *best)) {
          best = *when;
          bestName = name;
        }
      } else if (name != kProcessedDirName) {
        // A name we cannot read is a panic we cannot report, so say so rather
        // than skipping in silence.
        XLOG(ERR) << fmt::format(
            "Unrecognised crash dir name '{}' in '{}'", name, crashDir);
      }
    }

    it.increment(ec);
    if (ec) {
      XLOG(ERR) << fmt::format(
          "Failed while scanning crash dir '{}': {}", crashDir, ec.message());
      return false;
    }
  }
  return true;
}

reboot_cause_config::RebootCauseProviderAttempt readKernelPanic(
    const std::vector<std::string>& crashDirs,
    int64_t btimeSec,
    int64_t windowSec) {
  auto status = reboot_cause_config::RebootCauseProviderStatus::OK;
  std::optional<std::time_t> best;
  std::string bestName;
  for (const auto& crashDir : crashDirs) {
    // Keep scanning the remaining dirs: a cause found elsewhere is still
    // worth reporting, but the attempt is no longer a clean read.
    if (!scanCrashDir(crashDir, btimeSec, windowSec, best, bestName)) {
      status = reboot_cause_config::RebootCauseProviderStatus::READ_FAILED;
    }
  }

  std::vector<reboot_cause_config::RebootCause> causes;
  if (best) {
    // The description is a stable name so consumers can group on it; the
    // crash dir varies per instance and belongs in rawValue.
    auto cause = makeCause("Kernel Panic", *best);
    cause.rawValue() = bestName;
    causes.push_back(std::move(cause));
  }
  return makeAttempt(
      kKernelPanicProvider,
      status,
      folly::join(", ", crashDirs),
      std::move(causes));
}

// Syslog carries no year. Try the boot's year, then the one before: a reboot
// logged 23:55 on Dec 31 is read by a finder that booted on Jan 1.
std::optional<std::time_t> parseSyslogTimestamp(
    const std::string& line,
    int64_t btimeSec) {
  // Most lines in a live log were written after boot. For those the prior
  // year is always <= btime, so without an age bound they would resolve to
  // an instant a year old rather than being rejected. Live logs rotate
  // daily, so nothing legitimately this old is in one.
  constexpr int64_t kMaxAgeSec = 180 * 24 * 60 * 60;

  std::tm tm{};
  if (strptime(line.c_str(), "%b %d %H:%M:%S", &tm) == nullptr) {
    return std::nullopt;
  }

  std::tm btimeTm{};
  const auto btimeT = static_cast<std::time_t>(btimeSec);
  localtime_r(&btimeT, &btimeTm);

  for (int year : {btimeTm.tm_year, btimeTm.tm_year - 1}) {
    std::tm candidate = tm;
    candidate.tm_year = year;
    candidate.tm_isdst = -1;
    const auto when = std::mktime(&candidate);
    if (when == -1) {
      continue;
    }
    const auto delta = btimeSec - static_cast<int64_t>(when);
    if (delta >= 0 && delta <= kMaxAgeSec) {
      return when;
    }
  }
  return std::nullopt;
}

// Nearest to boot start wins, across every cause in the given attempts.
// Returns the provider alongside the cause, because a cause on its own does
// not say which provider found it.
reboot_cause_config::RebootCauseStatus rebootCauseStatus(
    bool causeDetermined,
    const std::vector<reboot_cause_config::RebootCauseProviderAttempt>&
        attempts) {
  if (causeDetermined) {
    return reboot_cause_config::RebootCauseStatus::FOUND;
  }
  const bool allRead =
      std::all_of(attempts.begin(), attempts.end(), [](const auto& attempt) {
        return *attempt.status() ==
            reboot_cause_config::RebootCauseProviderStatus::OK;
      });
  return allRead ? reboot_cause_config::RebootCauseStatus::UNEXPLAINED
                 : reboot_cause_config::RebootCauseStatus::INCONCLUSIVE;
}

std::optional<reboot_cause_config::DeterminedCause> selectNearestToBoot(
    const std::vector<reboot_cause_config::RebootCauseProviderAttempt>&
        attempts) {
  std::optional<reboot_cause_config::DeterminedCause> best;
  for (const auto& attempt : attempts) {
    for (const auto& cause : *attempt.causes()) {
      if (!best || *cause.occurredAtMs() > *best->cause()->occurredAtMs()) {
        reboot_cause_config::DeterminedCause determined;
        determined.providerName() = *attempt.name();
        determined.cause() = cause;
        best = determined;
      }
    }
  }
  return best;
}

// A syslog line is "<mon> <day> <hh:mm:ss> <host> <program>[<pid>]: <msg>".
// Everything from <msg> on is untrusted: sshd records each remote command
// verbatim into these same files, so a line quoting the phrase is
// indistinguishable from the announcement unless the program field is
// checked. Grepping for the phrase is exactly what an operator investigating
// a reboot does, and that grep lands in the window it would then poison.
bool isX86RebootCommandLine(folly::StringPiece line) {
  size_t i = 0;
  const auto skipSpaces = [&] {
    while (i < line.size() && line[i] == ' ') {
      ++i;
    }
  };
  // Four fields precede the program: month, day, time, host. The day is
  // space padded to two columns ("Sep  4"), so runs of spaces collapse.
  for (int field = 0; field < 4; ++field) {
    skipSpaces();
    if (i >= line.size()) {
      return false;
    }
    while (i < line.size() && line[i] != ' ') {
      ++i;
    }
  }
  skipSpaces();

  const size_t programStart = i;
  while (i < line.size() && line[i] != ':' && line[i] != ' ') {
    ++i;
  }
  if (i >= line.size() || line[i] != ':') {
    return false;
  }
  auto program = line.subpiece(programStart, i - programStart);
  ++i;

  if (const auto bracket = program.find('[');
      bracket != folly::StringPiece::npos) {
    program = program.subpiece(0, bracket);
  }
  if (std::find(
          kX86RebootCommandPrograms.begin(),
          kX86RebootCommandPrograms.end(),
          program) == kX86RebootCommandPrograms.end()) {
    return false;
  }

  skipSpaces();
  return line.subpiece(i).startsWith(kX86RebootCommandPattern);
}

reboot_cause_config::RebootCauseProviderAttempt readX86RebootCommand(
    const std::vector<std::string>& logPaths,
    int64_t btimeSec,
    int64_t windowSec) {
  auto status = reboot_cause_config::RebootCauseProviderStatus::OK;
  std::vector<reboot_cause_config::RebootCause> causes;
  std::optional<std::time_t> best;
  std::string bestLine;

  for (const auto& logPath : logPaths) {
    // An absent path is normal: which file systemd-logind lands in depends on
    // the image's rsyslog routing. A path that exists but will not read is a
    // failure, and folly::readFile reports both the same way, so presence is
    // checked separately.
    std::error_code ec;
    const bool present = std::filesystem::exists(logPath, ec);
    if (ec) {
      XLOG(ERR) << fmt::format(
          "Failed to stat '{}': {}", logPath, ec.message());
      status = reboot_cause_config::RebootCauseProviderStatus::READ_FAILED;
      continue;
    }
    if (!present) {
      continue;
    }

    std::string contents;
    if (!folly::readFile(logPath.c_str(), contents)) {
      XLOG(ERR) << fmt::format("Failed to read '{}'", logPath);
      status = reboot_cause_config::RebootCauseProviderStatus::READ_FAILED;
      continue;
    }

    std::vector<folly::StringPiece> lines;
    folly::split('\n', contents, lines);

    for (const auto& line : lines) {
      if (!isX86RebootCommandLine(line)) {
        continue;
      }
      const auto when = parseSyslogTimestamp(line.str(), btimeSec);
      if (when && inBootWindow(*when, btimeSec, windowSec) &&
          (!best || *when > *best)) {
        best = when;
        bestLine = line.str();
      }
    }
  }

  if (best) {
    auto cause = makeCause("X86 Reboot Command", *best);
    // The matched line. Its program field is what separates a real
    // announcement from an operator's grep echoed back by sshd, so keeping it
    // lets a reader audit that call instead of trusting it. It also carries
    // the second to look at in /var/log/secure to find out who asked.
    cause.rawValue() = bestLine;
    causes.push_back(std::move(cause));
  }
  return makeAttempt(
      kX86RebootCommandProvider,
      status,
      folly::join(", ", logPaths),
      std::move(causes));
}

// Timestamps in the BMC log carry a year, unlike syslog on the x86, so no
// year has to be inferred. mktime interprets them in this host's zone; the
// BMC and the x86 it sits in are assumed to be set to the same one. A
// mismatch shifts every event by the offset and out of the boot window.
//
// Lines written before the BMC finishes NTP sync carry the image's
// build-default date instead of the real time, which is why the log is not
// monotonic. Those dates are months away from any boot window, so the window
// check below discards them without needing to recognise them.
std::optional<std::time_t> parseBmcLogTimestamp(const std::string& line) {
  std::tm tm{};
  if (strptime(line.c_str(), " %Y %b %d %H:%M:%S", &tm) == nullptr) {
    return std::nullopt;
  }
  tm.tm_isdst = -1;
  const auto when = std::mktime(&tm);
  if (when == -1) {
    return std::nullopt;
  }
  return when;
}

reboot_cause_config::RebootCauseProviderAttempt parseBmcWedgePower(
    const std::string& body,
    int64_t btimeSec,
    int64_t windowSec) {
  auto status = reboot_cause_config::RebootCauseProviderStatus::OK;
  std::vector<reboot_cause_config::RebootCause> causes;

  std::optional<std::time_t> best;
  std::string bestDescription;
  std::string bestLine;
  try {
    const auto json = folly::parseJson(body);
    for (const auto& entry : json["Information"]["entries"]) {
      const auto line = entry.asString();
      static const RE2 chassisReset(kChassisResetPattern);
      static const RE2 x86Reset(kX86ResetPattern);
      const char* description = nullptr;
      if (RE2::PartialMatch(line, chassisReset)) {
        description = "ChassisResetFromBmc";
      } else if (RE2::PartialMatch(line, x86Reset)) {
        description = "X86ResetFromBmc";
      } else {
        continue;
      }
      const auto when = parseBmcLogTimestamp(line);
      if (when && inBootWindow(*when, btimeSec, windowSec) &&
          (!best || *when > *best)) {
        best = when;
        bestDescription = description;
        bestLine = line;
      }
    }
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Failed to parse BMC logfile response: {}", ex.what());
    return makeAttempt(
        kBmcWedgePowerProvider,
        reboot_cause_config::RebootCauseProviderStatus::PARSE_FAILED,
        detailWithReason(kBmcLogfilePath, ex.what()),
        {});
  }

  if (best) {
    auto cause = makeCause(bestDescription, *best);
    // The line the timestamp was derived from. The BMC log is not ordered by
    // time, so keeping the source lets a reader audit the parse rather than
    // trust it.
    cause.rawValue() = bestLine;
    causes.push_back(std::move(cause));
  }
  return makeAttempt(
      kBmcWedgePowerProvider, status, kBmcLogfilePath, std::move(causes));
}

folly::StringPiece bmcHostSourceAddress() {
  return kHostSourceAddress;
}

bool bmcFailureIsRetryable(std::optional<long> responseCode) {
  // No status at all means the request never reached a response: the BMC is
  // still booting, or the sideband address is not assignable yet. Both clear
  // on their own.
  if (!responseCode) {
    return true;
  }
  // 403 is the ACL that a BMC reboot wipes; it comes back when the BMC is
  // reconfigured. 404 is an image without the endpoint, which no amount of
  // waiting changes. Anything else is not known to be transient, and
  // retrying it would just delay the record.
  return *responseCode == 403;
}

namespace {

struct BmcLogfileRead {
  std::string body;
  std::string failure;
  std::optional<long> responseCode;
  bool retryable{false};
};

BmcLogfileRead readBmcLogfileOnce() {
  BmcLogfileRead read;
  // Declared outside the try so the status survives into the catch.
  RestClient client(folly::IPAddress(kBmcAddress), kBmcPort);
  try {
    client.setSourceAddress(folly::IPAddressV6(kHostSourceAddress));
    client.setTimeout(kBmcTimeout);
    read.body = client.requestWithOutput(kBmcLogfilePath, kBmcLogfileBody);
  } catch (const std::exception& ex) {
    read.failure = ex.what();
  }
  // Settled on one path for both outcomes, so a read that returns without
  // throwing cannot skip the decision.
  read.responseCode = client.lastResponseCode();
  read.retryable = bmcFailureIsRetryable(read.responseCode);
  return read;
}

} // namespace

reboot_cause_config::RebootCauseProviderAttempt readBmcWedgePower(
    int64_t btimeSec,
    int64_t windowSec) {
  // One attempt up front, then one per interval until the timeout is spent.
  // Each attempt is itself bounded by kBmcTimeout, so the elapsed time runs
  // slightly past the nominal timeout; the flag sizes the wait, it is not a
  // hard deadline.
  const int maxAttempts = 1 +
      std::max(0, FLAGS_bmc_logfile_poll_timeout_s) /
          static_cast<int>(kBmcLogfilePollInterval.count());

  BmcLogfileRead read;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    read = readBmcLogfileOnce();
    XLOG(INFO) << fmt::format(
        "BMC logfile attempt {} of {}: status={} retryable={} {}",
        attempt,
        maxAttempts,
        read.responseCode ? std::to_string(*read.responseCode) : "none",
        read.retryable,
        read.body.empty() ? read.failure : "ok");
    if (!read.body.empty() || !read.retryable || attempt == maxAttempts) {
      break;
    }
    std::this_thread::sleep_for(kBmcLogfilePollInterval);
  }

  if (read.body.empty()) {
    // The BMC is unreachable, the source address could not be bound, the
    // image has no logfile endpoint, or its ACL does not admit us.
    // All are a failed read, not an absence of causes.
    return makeAttempt(
        kBmcWedgePowerProvider,
        reboot_cause_config::RebootCauseProviderStatus::READ_FAILED,
        detailWithReason(
            kBmcLogfilePath,
            read.failure.empty() ? folly::StringPiece("empty response")
                                 : folly::StringPiece(read.failure)),
        {});
  }
  return parseBmcWedgePower(read.body, btimeSec, windowSec);
}

// Sudden power loss leaves no log line anywhere by definition: nothing had a
// chance to write one. It is inferred instead from the BMC and the x86 having
// started together. Deliberately not windowed against btime: the event this
// describes IS this boot, not something that happened before it.
reboot_cause_config::RebootCauseProviderAttempt parseSuddenPowerLoss(
    const std::string& body,
    int64_t btimeSec,
    int64_t nowSec) {
  double bmcUptimeSec = 0;
  std::string resetReason;
  try {
    const auto json = folly::parseJson(body);
    const auto& info = json["Information"];
    // The lowercase key is seconds; "Uptime" is the uptime(1) output.
    bmcUptimeSec = folly::to<double>(info["uptime"].asString());
    if (const auto* reason = info.get_ptr("Reset Reason")) {
      resetReason = reason->asString();
    }
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Failed to parse BMC info response: {}", ex.what());
    return makeAttempt(
        kSuddenPowerLossProvider,
        reboot_cause_config::RebootCauseProviderStatus::PARSE_FAILED,
        detailWithReason(kBmcInfoPath, ex.what()),
        {});
  }

  const int64_t bmcUptime = static_cast<int64_t>(bmcUptimeSec);
  const int64_t x86Uptime = nowSec - btimeSec;
  const int64_t skew = bmcUptime - x86Uptime;

  std::vector<reboot_cause_config::RebootCause> causes;
  if (skew >= kMinBmcToX86BootSkewSec && skew <= kMaxBmcToX86BootSkewSec) {
    // The BMC started first, so its start is the closest estimate of when
    // power was lost.
    auto cause = makeCause(
        "SuddenPowerLoss", static_cast<std::time_t>(nowSec - bmcUptime));
    cause.rawValue() = fmt::format(
        "BMC uptime {}s, x86 uptime {}s, skew {}s, BMC reset reason '{}'",
        bmcUptime,
        x86Uptime,
        skew,
        resetReason);
    causes.push_back(std::move(cause));
  }
  return makeAttempt(
      kSuddenPowerLossProvider,
      reboot_cause_config::RebootCauseProviderStatus::OK,
      kBmcInfoPath,
      std::move(causes));
}

reboot_cause_config::RebootCauseProviderAttempt readSuddenPowerLoss(
    int64_t btimeSec,
    int64_t nowSec) {
  std::string body;
  std::string failure;
  try {
    RestClient client(folly::IPAddress(kBmcAddress), kBmcPort);
    client.setSourceAddress(folly::IPAddressV6(kHostSourceAddress));
    client.setTimeout(kBmcTimeout);
    body = client.requestWithOutput(kBmcInfoPath);
  } catch (const std::exception& ex) {
    failure = ex.what();
    XLOG(ERR) << fmt::format("Failed to reach the BMC info API: {}", failure);
    body.clear();
  }
  if (body.empty()) {
    return makeAttempt(
        kSuddenPowerLossProvider,
        reboot_cause_config::RebootCauseProviderStatus::READ_FAILED,
        detailWithReason(
            kBmcInfoPath, failure.empty() ? "empty response" : failure),
        {});
  }
  return parseSuddenPowerLoss(body, btimeSec, nowSec);
}

reboot_cause_config::RebootCauseProviderAttempt readProvider(
    const reboot_cause_config::RebootCauseProviderConfig& providerConfig) {
  auto status = reboot_cause_config::RebootCauseProviderStatus::OK;
  std::vector<reboot_cause_config::RebootCause> causes;

  std::string contents;
  if (!folly::readFile(providerConfig.sysfsReadPath()->c_str(), contents)) {
    const auto reason = folly::errnoStr(errno);
    XLOG(ERR) << fmt::format(
        "Failed to read reboot causes from provider '{}' at path '{}': {}",
        *providerConfig.name(),
        *providerConfig.sysfsReadPath(),
        reason);
    status = reboot_cause_config::RebootCauseProviderStatus::READ_FAILED;
    return makeAttempt(
        *providerConfig.name(),
        status,
        detailWithReason(*providerConfig.sysfsReadPath(), reason),
        std::move(causes));
  }

  try {
    auto json = folly::parseJson(contents);
    for (const auto& causeJson : json["causes"]) {
      reboot_cause_config::RebootCause cause;
      cause.description() = causeJson["description"].asString();

      auto dateStr = causeJson["date"].asString();
      bool dateParsed = false;
      if (auto t = parseProviderDate(dateStr)) {
        cause.occurredAtMs() = toEpochMs(*t);
        cause.occurredAtPacific() = pacificString(*t);
        dateParsed = true;
      } else {
        // Leave the rendered field unset rather than filling it with text
        // that is not a timestamp.
        cause.occurredAtMs() = 0;
        XLOG(ERR) << fmt::format(
            "Provider '{}' wrote an unparsable date '{}'",
            *providerConfig.name(),
            dateStr);
      }

      if (causeJson.count("rawValue")) {
        cause.rawValue() = causeJson["rawValue"].asString();
      } else if (!dateParsed) {
        // Nothing better to show, so keep what the provider actually wrote.
        cause.rawValue() = dateStr;
      }
      causes.push_back(std::move(cause));
    }
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Failed to parse reboot causes from provider '{}': {}",
        *providerConfig.name(),
        ex.what());
    // Causes already in the vector are whole -- a cause is pushed only once
    // every field is set -- so the throw truncates the list rather than
    // corrupting a member of it. They are kept, and PARSE_FAILED records
    // that more may have followed.
    status = reboot_cause_config::RebootCauseProviderStatus::PARSE_FAILED;
  }

  return makeAttempt(
      *providerConfig.name(),
      status,
      *providerConfig.sysfsReadPath(),
      std::move(causes));
}

} // namespace detail

namespace {
std::string runOutcomeName(RunOutcome outcome) {
  switch (outcome) {
    case RunOutcome::NOT_RUN:
      return "NOT_RUN";
    case RunOutcome::ALREADY_HANDLED:
      return "ALREADY_HANDLED";
    case RunOutcome::RECORDED:
      return "RECORDED";
    case RunOutcome::RECORD_FAILED:
      return "RECORD_FAILED";
  }
  return "UNKNOWN";
}
} // namespace

std::unordered_map<std::string, std::string> runEventTags(
    const RunResult& result,
    int exitStatus) {
  std::unordered_map<std::string, std::string> tags{
      {"outcome", runOutcomeName(result.outcome)},
      {"boot_id", result.bootId},
      {"exit_status", folly::to<std::string>(exitStatus)},
  };
  if (!result.record.has_value()) {
    return tags;
  }
  const auto& record = *result.record;
  if (record.causeStatus().has_value()) {
    tags["cause_status"] =
        apache::thrift::util::enumNameSafe(*record.causeStatus());
  }
  if (record.determinedCause().has_value()) {
    const auto& determined = *record.determinedCause();
    tags["cause"] = *determined.cause()->description();
    tags["provider"] = *determined.providerName();
  }
  // Providers that could not be read or parsed, so a run that found nothing
  // because it could not look is visible at scale.
  std::vector<std::string> failed;
  for (const auto& attempt : *record.providersAttempted()) {
    if (*attempt.status() ==
            reboot_cause_config::RebootCauseProviderStatus::READ_FAILED ||
        *attempt.status() ==
            reboot_cause_config::RebootCauseProviderStatus::PARSE_FAILED) {
      failed.push_back(*attempt.name());
    }
  }
  tags["failed_providers"] = folly::join(",", failed);
  return tags;
}

RebootCauseFinderImpl::Paths RebootCauseFinderImpl::defaultPaths() {
  return Paths{
      kHistoryDir,
      kProcStatPath,
      kProcBootIdPath,
      kProcOsReleasePath,
      detail::kernelPanicCrashDirs(),
      detail::x86RebootCommandLogPaths()};
}

RebootCauseFinderImpl::RebootCauseFinderImpl(
    const reboot_cause_config::RebootCauseConfig& config,
    Paths paths)
    : config_(config), paths_(std::move(paths)) {}

void RebootCauseFinderImpl::clearProvider(
    const reboot_cause_config::RebootCauseProviderConfig& providerConfig) {
  if (!folly::writeFile(
          std::string("1"), providerConfig.sysfsClearPath()->c_str())) {
    XLOG(ERR) << fmt::format(
        "Failed to clear reboot causes at '{}'",
        *providerConfig.sysfsClearPath());
  }
}

RunResult RebootCauseFinderImpl::determineRebootCause() {
  RunResult result;
  // Anything that re-runs this binary within a boot (a manual invocation, a
  // package upgrade, a restart of whatever unit owns it) would otherwise
  // record a reboot that never happened and, worse, clear the providers that
  // still hold the real boot's causes. Only the first run of a boot is
  // meaningful.
  const auto bootId = readBootId(paths_.bootId);
  result.bootId = bootId;
  if (bootId.empty()) {
    XLOG(ERR) << fmt::format(
        "Failed to read boot id from '{}'. Doing nothing: without it a fresh "
        "boot is indistinguishable from a re-run of this binary within the "
        "same boot, and clearing the providers would risk discarding a real "
        "boot's causes.",
        paths_.bootId);
    return result;
  }
  // Logged on every run, including the ones the guard below stops, so each run
  // in the log file can be tied to the boot it ran in.
  XLOG(INFO) << fmt::format("Running in boot {}", bootId);
  const auto alreadyHandled = recordExistsForBoot(paths_.historyDir, bootId);
  if (!alreadyHandled.has_value()) {
    XLOG(ERR) << "Doing nothing: cannot tell whether this boot was already "
                 "handled, so clearing the providers would risk discarding a "
                 "real boot's causes.";
    return result;
  }
  if (*alreadyHandled) {
    if (!FLAGS_redo_current_boot) {
      XLOG(INFO) << fmt::format(
          "Reboot causes were already determined during boot {}. Nothing to "
          "do.",
          bootId);
      result.outcome = RunOutcome::ALREADY_HANDLED;
      return result;
    }
    if (!removeRecordForBoot(paths_.historyDir, bootId)) {
      XLOG(ERR) << "Doing nothing: asked to redo this boot, but the existing "
                   "record could not be removed and a second record for one "
                   "boot would leave two answers with no way to tell which "
                   "is current.";
      return result;
    }
    XLOG(WARNING) << fmt::format(
        "Discarded the record for boot {} and re-reading every provider, "
        "because --redo_current_boot was set.",
        bootId);
    if (FLAGS_clear_reboot_causes) {
      XLOG(WARNING)
          << "--clear_reboot_causes is also set. If an earlier run this boot "
             "already cleared the hardware providers, they have nothing left "
             "to report and this run will find less than that one did.";
    }
  }

  const auto btime = detail::readBootTimeSec(paths_.procStat);
  const int64_t bootTimeMs =
      btime ? toEpochMs(static_cast<std::time_t>(*btime)) : 0;
  const auto kernelRelease = readKernelRelease(paths_.osRelease);

  // Computed once, so the placeholder and the finished record are the same
  // file even when the stamp falls back to the current time.
  const auto stampTime = bootTimeMs > 0
      ? static_cast<std::time_t>(bootTimeMs / 1000)
      : std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  const auto filePath = fmt::format(
      "{}/reboot-cause-{}{}",
      paths_.historyDir,
      filenameStamp(stampTime),
      recordFilenameSuffix(bootId));

  // Written before any provider is read, so the boot is on record even if
  // this run is killed at its runtime cap or crashes. Not fatal: the finished
  // record below is still written, and it alone decides whether to clear.
  {
    reboot_cause_config::RebootCauseRecord placeholder;
    const auto start = std::chrono::system_clock::now();
    placeholder.detectedAtMs() =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            start.time_since_epoch())
            .count();
    placeholder.detectedAtPacific() =
        pacificString(std::chrono::system_clock::to_time_t(start));
    placeholder.bootId() = bootId;
    placeholder.bootTimeMs() = bootTimeMs;
    placeholder.kernelRelease() = kernelRelease;
    placeholder.causeStatus() =
        reboot_cause_config::RebootCauseStatus::INTERRUPTED;
    if (persistResult(placeholder, filePath)) {
      XLOG(INFO) << fmt::format(
          "Wrote a placeholder record for boot {} to '{}'; it stays "
          "INTERRUPTED if this run does not finish",
          bootId,
          filePath);
    }
  }

  XLOG(INFO) << "Reading reboot causes from all providers";

  // Read providers in priority order (lower value = higher priority).
  auto providerConfigs = *config_.rebootCauseProviderConfigs();
  std::sort(
      providerConfigs.begin(),
      providerConfigs.end(),
      [](const auto& a, const auto& b) {
        return *a.priority() < *b.priority();
      });

  // A panic names the failure and an x86 reboot command names the actor; the
  // CPLD
  // reports only the mechanism (some generic warm-reset bit) for both. So
  // these outrank every hardware provider and are collected first. They are
  // not config providers: their paths and patterns come from systemd and
  // kdump and are the same on every platform.
  std::vector<reboot_cause_config::RebootCauseProviderAttempt> attempts;
  std::optional<reboot_cause_config::DeterminedCause> determined;

  if (btime) {
    const int64_t window = detail::boundWindowByPreviousBoot(
        *btime,
        FLAGS_software_cause_lookback_sec,
        previousBootStartSec(paths_.historyDir, bootId, *btime));
    if (window < FLAGS_software_cause_lookback_sec) {
      XLOG(INFO) << fmt::format(
          "Searching the {}s since the previous boot started, not the "
          "configured {}s",
          window,
          FLAGS_software_cause_lookback_sec);
    }

    std::vector<reboot_cause_config::RebootCauseProviderAttempt> software;
    software.push_back(
        detail::readKernelPanic(paths_.crashDirs, *btime, window));
    software.push_back(
        detail::readX86RebootCommand(paths_.logPaths, *btime, window));
    software.push_back(detail::readBmcWedgePower(*btime, window));

    // Nearest to boot start wins, not whichever reader ran first. A panic and
    // a later operator reboot can both fall inside one window; the reboot is
    // then the cause and the panic belongs to the boot before it.
    determined = detail::selectNearestToBoot(software);
    attempts.insert(attempts.end(), software.begin(), software.end());
  } else {
    // Without boot start there is no window, so neither implicit provider can
    // be evaluated. Say so rather than letting their absence read as "looked
    // and found nothing".
    for (const auto* name :
         {kKernelPanicProvider,
          kX86RebootCommandProvider,
          kBmcWedgePowerProvider}) {
      attempts.push_back(makeAttempt(
          name,
          reboot_cause_config::RebootCauseProviderStatus::SKIPPED,
          "boot time unavailable"));
    }
  }

  for (const auto& providerConfig : providerConfigs) {
    XLOG(INFO) << fmt::format("Reading provider '{}'", *providerConfig.name());
    auto attempt = detail::readProvider(providerConfig);
    // Providers are sorted by priority, so the first one to report anything
    // is the highest-priority hardware answer. It only applies when neither
    // implicit provider found a cause.
    if (!determined.has_value() && !attempt.causes()->empty()) {
      reboot_cause_config::DeterminedCause d;
      d.providerName() = *providerConfig.name();
      d.cause() = attempt.causes()->front();
      determined = d;
    }
    attempts.push_back(std::move(attempt));
  }

  const auto now = std::chrono::system_clock::now();

  // Last resort. Every provider above reports a positive signal; this one
  // infers a cause from there being none, so it runs only once they have all
  // come up empty. It also has to stay out of selectNearestToBoot, which
  // breaks ties by picking the cause closest to boot: this one dates its
  // cause to when the BMC started, which is always shortly before boot, so
  // it would win that tie-break against a genuine cause by
  // arithmetic rather than evidence.
  //
  // It is meaningful only if the BMC log was actually read. Uptime
  // correlation shows the chassis restarted, not why: `wedge_power.sh reset
  // -s` power-cycles the BMC too, so a chassis reset and a power loss look
  // identical here. What separates them is the wedge_power marker, so if
  // BMCWedgePower could not read the log we cannot tell "no operator reset"
  // from "could not look", and must not guess.
  const bool bmcLogWasRead =
      std::any_of(attempts.begin(), attempts.end(), [](const auto& attempt) {
        return *attempt.name() == kBmcWedgePowerProvider &&
            *attempt.status() ==
            reboot_cause_config::RebootCauseProviderStatus::OK;
      });
  if (btime && bmcLogWasRead) {
    auto attempt = detail::readSuddenPowerLoss(
        *btime,
        static_cast<int64_t>(std::chrono::system_clock::to_time_t(now)));
    if (!determined.has_value() && !attempt.causes()->empty()) {
      reboot_cause_config::DeterminedCause d;
      d.providerName() = kSuddenPowerLossProvider;
      d.cause() = attempt.causes()->front();
      determined = d;
    }
    attempts.push_back(std::move(attempt));
  } else {
    attempts.push_back(makeAttempt(
        kSuddenPowerLossProvider,
        reboot_cause_config::RebootCauseProviderStatus::SKIPPED,
        btime ? "BMC log unread, cannot rule out an operator reset"
              : "boot time unavailable"));
  }

  reboot_cause_config::RebootCauseRecord record;
  record.detectedAtMs() = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();
  record.detectedAtPacific() =
      pacificString(std::chrono::system_clock::to_time_t(now));
  // Left unset when nothing was found. A placeholder cause would have to
  // carry a placeholder timestamp, and that timestamp necessarily postdates
  // the boot it claims to explain.
  if (determined.has_value()) {
    record.determinedCause() = *determined;
  }
  record.bootId() = bootId;
  record.bootTimeMs() = bootTimeMs;
  record.kernelRelease() = kernelRelease;
  record.providersAttempted() = attempts;
  record.causeStatus() =
      detail::rebootCauseStatus(record.determinedCause().has_value(), attempts);

  if (record.determinedCause().has_value()) {
    XLOG(INFO) << fmt::format(
        "Determined reboot cause: '{}' [Provider: {}]",
        *record.determinedCause()->cause()->description(),
        *record.determinedCause()->providerName());
  } else {
    XLOG(INFO) << fmt::format(
        "No reboot cause determined. Providers attempted: {}",
        summariseAttempts(attempts));
  }

  // Writing the record is what arms the guard, so it has to succeed before
  // anything destructive happens. If it failed, the providers stay latched and
  // the next run retries this boot from scratch.
  const bool recorded = persistResult(record, filePath);
  if (recorded) {
    XLOG(INFO) << fmt::format(
        "Wrote the reboot cause record for boot {} to '{}'", bootId, filePath);
  }

  result.outcome = recorded ? RunOutcome::RECORDED : RunOutcome::RECORD_FAILED;
  result.record = record;

  if (shouldClearProviders(recorded)) {
    for (const auto& providerConfig : providerConfigs) {
      XLOG(INFO) << fmt::format(
          "Clearing provider '{}'", *providerConfig.name());
      clearProvider(providerConfig);
    }
  }
  return result;
}

bool RebootCauseFinderImpl::persistResult(
    const reboot_cause_config::RebootCauseRecord& record,
    const std::string& filePath) {
  std::error_code ec;
  std::filesystem::create_directories(paths_.historyDir, ec);
  if (ec) {
    XLOG(ERR) << fmt::format(
        "Failed to create history dir '{}': {}",
        paths_.historyDir,
        ec.message());
    return false;
  }

  // Enums are written by name, so the file reads without the thrift IDL.
  const auto json = facebook::thrift::to_dynamic(
      record, facebook::thrift::dynamic_format::PORTABLE);
  // Atomic, so a reader or a run killed mid-write never sees half a record,
  // and synced, so a power loss right after does not lose the boot.
  folly::WriteFileAtomicOptions options;
  options.setSyncType(folly::SyncType::WITH_SYNC);
  if (const int err = folly::writeFileAtomicNoThrow(
          filePath, folly::toPrettyJson(json), options)) {
    XLOG(ERR) << fmt::format(
        "Failed to write history file '{}': {}",
        filePath,
        folly::errnoStr(err));
    return false;
  }
  return true;
}

} // namespace facebook::fboss::platform::reboot_cause_finder
