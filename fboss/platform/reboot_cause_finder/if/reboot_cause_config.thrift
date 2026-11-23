namespace cpp2 facebook.fboss.platform.reboot_cause_config
namespace go neteng.fboss.platform.reboot_cause_config
namespace py neteng.fboss.platform.reboot_cause_config
namespace py3 neteng.fboss.platform.reboot_cause_config
namespace py.asyncio neteng.fboss.platform.asyncio.reboot_cause_config

package "facebook.com/fboss/platform/reboot_cause_config"

// `RebootCauseProviderConfig` defines a hardware provider of a reboot cause.
//
// `name`: Unique name of the provider (e.g. MERU800BIA_SMB_CPLD).
//
// `priority`: Importance of the provider (lower value = higher priority).
//
// `sysfsReadPath`: Sysfs path to read reboot causes from.
//
// `sysfsClearPath`: Sysfs path to write to clear reboot causes.
struct RebootCauseProviderConfig {
  1: string name;
  2: i16 priority;
  3: string sysfsReadPath;
  4: string sysfsClearPath;
}

// `RebootCauseConfig` is the per-platform reboot_cause_finder configuration.
//
// `rebootCauseProviderConfigs`: Providers of reboot causes for the platform.
struct RebootCauseConfig {
  1: list<RebootCauseProviderConfig> rebootCauseProviderConfigs;
}

// `RebootCause` models one decoded cause read from one provider.
//
// `description`: Human-readable decoded cause from the provider.
//
// `occurredAtSec`: When the cause occurred, seconds since the epoch; 0 when
// unknown.
//
// `occurredAtMs`: Deprecated, `occurredAtSec` * 1000. Still written for readers
// not yet on `occurredAtSec`.
//
// `occurredAtPacific`: Same instant in Pacific time for humans, e.g.
// "2026-07-02 23:16:55 PDT". Display only; `occurredAtSec` is authoritative.
//
// `rawValue`: Raw undecoded register value, if the provider exposes it.
struct RebootCause {
  1: string description;
  2: i64 occurredAtMs;
  3: string occurredAtPacific;
  4: optional string rawValue;
  5: i64 occurredAtSec;
}

// `DeterminedCause` pairs the winning cause with the provider that reported
// it. Provenance is structural everywhere else -- a cause sits inside its
// provider's attempt -- so this is the one place it has to be stated.
struct DeterminedCause {
  1: string providerName;
  2: RebootCause cause;
}

// How a provider fared on this boot. Recorded for every provider, including
// the two implicit ones, so that "no cause found" can be told apart from
// "the sources we rely on were unreadable".
//
// `OK`: attempted and completed. It may or may not have reported a cause.
// `READ_FAILED`: the source could not be read at all.
// `PARSE_FAILED`: the source was read but its content was not understood.
// `SKIPPED`: not attempted, e.g. boot time was unavailable.
enum RebootCauseProviderStatus {
  OK = 0,
  READ_FAILED = 1,
  PARSE_FAILED = 2,
  SKIPPED = 3,
}

// `RebootCauseProviderAttempt` records one provider's outcome.
//
// `name`: Provider name, matching `DeterminedCause.providerName` when this
// provider's cause is the one selected.
//
// `status`: How the attempt went.
//
// `detail`: Path consulted, or the reason for a failure. For humans.
struct RebootCauseProviderAttempt {
  1: string name;
  2: RebootCauseProviderStatus status;
  3: string detail;
  // Everything this provider reported. Empty when it read cleanly and had
  // nothing, which `status` tells apart from a failed read.
  4: list<RebootCause> causes;
}

// What a boot's record says about its cause, decided once by the finder so
// every reader (fboss2, the dashboard, the run event) says the same thing.
//
// `FOUND`: a provider reported the cause, in `determinedCause`.
// `UNEXPLAINED`: every provider was read and none reported a cause.
// `INCONCLUSIVE`: no cause, and at least one provider could not be read,
// parsed or attempted, so the cause may be in a source we could not see.
// `INTERRUPTED`: the run was killed or crashed before it finished this boot.
enum RebootCauseStatus {
  FOUND = 0,
  UNEXPLAINED = 1,
  INCONCLUSIVE = 2,
  INTERRUPTED = 3,
}

// Where reboot_cause_finder persists its per-boot records. Shared so readers
// (e.g. sensor_service's getRebootHistory) cannot drift from the writer.
const string REBOOT_HISTORY_DIR = "/var/facebook/fboss/reboot_history";

// `RebootCauseRecord` is the per-boot record persisted under
// /var/facebook/fboss/reboot_history/.
//
// `detectedAtSec`: When the finder ran, seconds since the epoch.
//
// `detectedAtMs`: Deprecated, when the finder ran in epoch milliseconds. Still
// written for readers not yet on `detectedAtSec`.
//
// `detectedAtPacific`: Detection time in Pacific for humans, e.g.
// "2026-07-02 23:22:12 PDT".
//
// `determinedCause`: The cause determined to be responsible for the reboot.
// Unset when no provider reported one. Deliberately not a placeholder: a
// synthetic "Unknown" cause has to carry a synthetic timestamp, and that
// timestamp is necessarily after the boot it claims to explain.
//
// `bootTimeSec`: Boot start from /proc/stat btime, seconds since the epoch. 0
// when it could not be read, in which case the implicit providers were
// skipped.
//
// `bootTimeMs`: Deprecated, `bootTimeSec` * 1000. Still written for readers not
// yet on `bootTimeSec`.
//
// `providersAttempted`: Every provider consulted this boot and how it fared,
// including the implicit KernelPanic and ManualReboot providers.
//
// `bootId`: Kernel boot id (/proc/sys/kernel/random/boot_id) this record
// belongs to. Doubles as the once-per-boot guard: a run whose boot id matches
// the previous run's exits without touching the providers. Empty when the boot
// id could not be read.
//
// `causeStatus`: `INTERRUPTED` on the record written as soon as a run knows
// its boot, before any provider is read; the finished record replaces it with
// the run's status. Unset on records written before this field existed.
struct RebootCauseRecord {
  1: i64 detectedAtMs;
  2: string detectedAtPacific;
  3: optional DeterminedCause determinedCause;
  // 4 was allCauses; it is now providersAttempted[*].causes. Do not reuse.
  5: string bootId;
  6: i64 bootTimeMs;
  7: list<RebootCauseProviderAttempt> providersAttempted;
  // Release of the kernel this boot ran (`uname -r`). Empty if unreadable.
  8: string kernelRelease;
  // 9 was `incomplete`; it is now `causeStatus == INTERRUPTED`. Do not reuse.
  10: optional RebootCauseStatus causeStatus;
  11: i64 detectedAtSec;
  12: i64 bootTimeSec;
}
