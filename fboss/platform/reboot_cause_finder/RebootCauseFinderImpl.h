// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/Range.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "fboss/platform/reboot_cause_finder/if/gen-cpp2/reboot_cause_config_types.h"

namespace facebook::fboss::platform::reboot_cause_finder {

// Causes derived from files the kernel, systemd and kdump write, rather than
// from a per-platform hardware provider. Paths are parameters so tests can
// point them at a temp dir; production call sites pass the real constants.
namespace detail {

// Reads and decodes one hardware provider's sysfs file. Uses no member state,
// so it is a free function and can be tested directly. The returned attempt's
// status distinguishes a clean read that found nothing from a source that
// could not be read or could not be parsed.
reboot_cause_config::RebootCauseProviderAttempt readProvider(
    const reboot_cause_config::RebootCauseProviderConfig& config);

// Boot start in epoch seconds. std::nullopt when it cannot be established or
// is in the future, which disables both readers below.
std::optional<int64_t> readBootTimeSec(const std::string& procStatPath);

// The window to search before btimeSec, cut so it starts no earlier than the
// previous boot did. Nothing that happened before the previous boot started
// can have caused this one, yet a fixed window can still select it: an unclean
// reset leaves no line of its own, so an older clean reboot's line is the only
// one in range. std::nullopt, or a start at or after btimeSec, leaves
// windowSec unchanged.
int64_t boundWindowByPreviousBoot(
    int64_t btimeSec,
    int64_t windowSec,
    std::optional<int64_t> previousBootStartSec);

// Causes inside [btimeSec - windowSec, btimeSec). At most one each; the match
// nearest btimeSec wins. Empty when the source is absent.
// The returned attempt's status is READ_FAILED when a source is present but
// unreadable, and OK when it is simply absent -- most switches have never
// panicked and no image uses every log path.
reboot_cause_config::RebootCauseProviderAttempt readKernelPanic(
    const std::vector<std::string>& crashDirs,
    int64_t btimeSec,
    int64_t windowSec);

// Directories searched for a crash dump; a dump can be in either.
const std::vector<std::string>& kernelPanicCrashDirs();

reboot_cause_config::RebootCauseProviderAttempt readX86RebootCommand(
    const std::vector<std::string>& logPaths,
    int64_t btimeSec,
    int64_t windowSec);

// Reads the BMC's persistent critical log over the in-band sideband VLAN.
reboot_cause_config::RebootCauseProviderAttempt readBmcWedgePower(
    int64_t btimeSec,
    int64_t windowSec);

// The source address the BMC's ACL grants. Exposed so a test can assert it
// still parses and is a zoned link-local; RestClient::setSourceAddress throws
// otherwise, and on a switch that throw is indistinguishable from an outage.
folly::StringPiece bmcHostSourceAddress();

// Whether a failed BMC logfile read can succeed on a later attempt.
// std::nullopt means the request produced no response at all. Exposed so the
// retry decision can be tested without an HTTP server.
bool bmcFailureIsRetryable(std::optional<long> responseCode);

// Exposed separately from readBmcWedgePower so the parsing can be tested
// without an HTTP server.
reboot_cause_config::RebootCauseProviderAttempt parseBmcWedgePower(
    const std::string& body,
    int64_t btimeSec,
    int64_t windowSec);

// Infers a loss of chassis power from the BMC and the x86 having started
// together. Says nothing about where the loss came from: an upstream feed,
// a PSU, or a rail all look the same from here.
// Last resort only: it asserts a cause from the absence of evidence, so it
// must not outrank a provider that found a positive signal.
reboot_cause_config::RebootCauseProviderAttempt readSuddenPowerLoss(
    int64_t btimeSec,
    int64_t nowSec);

// Exposed separately from readSuddenPowerLoss so the inference can be tested
// without an HTTP server.
reboot_cause_config::RebootCauseProviderAttempt
parseSuddenPowerLoss(const std::string& body, int64_t btimeSec, int64_t nowSec);

// std::nullopt when the name is not a timestamp this code understands.
std::optional<std::time_t> parseCrashDirName(const std::string& name);

// Resolves a syslog stamp's missing year against btime. std::nullopt when no
// plausible year puts the line at or before btime.
std::optional<std::time_t> parseSyslogTimestamp(
    const std::string& line,
    int64_t btimeSec);

// Log files searched for the systemd-logind reboot line, in no
// particular order.
const std::vector<std::string>& x86RebootCommandLogPaths();

// Whether a syslog line is systemd-logind announcing a reboot. False for a
// line that merely contains the phrase, such as sshd's verbatim record of a
// remote command that grepped for it.
bool isX86RebootCommandLine(folly::StringPiece line);

// The cause nearest to boot start across every attempt, paired with the
// provider that reported it. std::nullopt when no attempt reported anything.
std::optional<reboot_cause_config::DeterminedCause> selectNearestToBoot(
    const std::vector<reboot_cause_config::RebootCauseProviderAttempt>&
        attempts);

// The record's cause status for a finished run. A cause wins even if other
// providers failed; without one, any provider not read cleanly makes the
// absence inconclusive.
reboot_cause_config::RebootCauseStatus rebootCauseStatus(
    bool causeDetermined,
    const std::vector<reboot_cause_config::RebootCauseProviderAttempt>&
        attempts);

} // namespace detail

// How a run ended, for the one structured event Main logs per run.
enum class RunOutcome {
  // Did nothing: the boot id or the history dir could not be read, or a redo
  // could not remove the existing record.
  NOT_RUN,
  // This boot already has a record and no redo was asked for.
  ALREADY_HANDLED,
  // The finished record is on disk.
  RECORDED,
  // The providers were read but the finished record could not be written.
  RECORD_FAILED,
};

struct RunResult {
  RunOutcome outcome{RunOutcome::NOT_RUN};
  // Empty when it could not be read.
  std::string bootId;
  // The finished record, for RECORDED and RECORD_FAILED.
  std::optional<reboot_cause_config::RebootCauseRecord> record;
};

// Tags for the per-run structured event. Built here rather than in Main so the
// event's contents can be tested without the logger, which reads the platform
// name from the host.
std::unordered_map<std::string, std::string> runEventTags(
    const RunResult& result,
    int exitStatus);

class RebootCauseFinderImpl {
 public:
  // Everything the finder reads or writes outside the config. Defaults are
  // the real locations; tests point them at a temp dir so the whole of
  // determineRebootCause() can be exercised without touching the host.
  struct Paths {
    std::string historyDir;
    std::string procStat;
    std::string bootId;
    std::string osRelease;
    std::vector<std::string> crashDirs;
    std::vector<std::string> logPaths;
  };
  static Paths defaultPaths();

  explicit RebootCauseFinderImpl(
      const reboot_cause_config::RebootCauseConfig& config,
      Paths paths = defaultPaths());

  // Read all providers, determine the reboot cause, persist the record, and
  // optionally clear providers (when --clear_reboot_causes is set).
  // Invoked once when the one-shot binary runs.
  RunResult determineRebootCause();

 private:
  const reboot_cause_config::RebootCauseConfig config_;
  const Paths paths_;

  void clearProvider(
      const reboot_cause_config::RebootCauseProviderConfig& config);

  // Persist the record as a pretty-printed JSON file at filePath, replacing
  // what is there atomically. The filename carries the boot id, so a
  // successful write is also what arms the once-per-boot guard. Returns false
  // if the record did not reach disk.
  [[nodiscard]] bool persistResult(
      const reboot_cause_config::RebootCauseRecord& record,
      const std::string& filePath);
};

} // namespace facebook::fboss::platform::reboot_cause_finder
