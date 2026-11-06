package "meta.com/fboss/platform/platform_checks"

namespace cpp2 facebook.fboss.platform.platform_checks

/*
 * Identifies the type of check
 */
enum CheckType {
  PCI_DEVICE_CHECK = 1,
  MAC_ADDRESS_CHECK = 2,
  RECENT_MANUAL_REBOOT_CHECK = 3,
  RECENT_KERNEL_PANIC_CHECK = 4,
  WATCHDOG_DID_NOT_STOP_CHECK = 5,
  KERNEL_VERSION_CHECK = 6,
  I801_SMBUS_TIMEOUT_CHECK = 7,
  X86_FW_UTIL_CHECK = 8,
  X86_BMC_LINK_CHECK = 9,
  IPMI_MC_INFO_CHECK = 10,
  X86_BMC_REST_API_CHECK = 11,
  BMC_FW_UTIL_CHECK = 12,
  BMC_X86_LINK_CHECK = 13,
  BMC_REST_API_CHECK = 14,
  BMC_MAC_ADDRESS_CHECK = 15,
  BMC_EEPROM_CHECK = 16,
  X86_MAC_CONSISTENCY_CHECK = 17,
  BMC_SOL_CONSOLE_CHECK = 18,
  BMC_SECONDARY_FLASH_BOOT_CHECK = 19,
  WEDGE_POWER_CHECK = 20,
  PLATFORM_HW_TEST_CHECK = 21,
  BMC_CIT_TEST_CHECK = 22,
  BMC_REACHABILITY_CHECK = 23,
}

/*
 * OK - check passed
 * PROBLEM - check failed in an expected way. Remediation may be available.
 * ERROR - unable to complete the check properly
 * SKIPPED - check was not run, e.g. a required host is unavailable. The
 *           reason is in errorMessage.
 */
enum CheckStatus {
  OK = 1,
  PROBLEM = 2,
  ERROR = 3,
  SKIPPED = 4,
}

/*
 * Identifies the remediation required
 *
 * MANUAL_REMEDIATION - further information is reported in the CheckResult
 */
enum RemediationType {
  NONE = 1,
  REBOOT_REQUIRED = 2,
  REPROVISION_REQUIRED = 3,
  RMA_REQUIRED = 4,
  MANUAL_REMEDIATION = 5,
}

struct CheckResult {
  1: CheckType checkType;
  2: CheckStatus status;
  3: optional RemediationType remediation;
  4: optional string remediationMessage;
  5: optional string errorMessage;
  6: optional string checkName;
  // Verbose evidence for debugging, e.g. commands run and their output.
  7: optional string details;
  // Wall time the check took; unset for skipped checks.
  8: optional i64 durationMs;
}

struct CheckInfo {
  1: CheckType checkType;
  2: set<string> supportedPlatforms;
  3: string description;
}

struct CheckReport {
  1: list<CheckResult> results;
  2: optional string platformName;
  // Host the checks ran against, e.g. "localhost" or a switch hostname.
  3: optional string target;
}
