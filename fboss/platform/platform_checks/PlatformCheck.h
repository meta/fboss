#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "fboss/platform/platform_checks/Host.h"
#include "fboss/platform/platform_checks/LocalHost.h"
#include "fboss/platform/platform_checks/gen-cpp2/check_types_types.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * The machine a check inspects and the platform whose configs describe it.
 */
struct CheckTarget {
  // Null if the machine is unavailable (e.g. no BMC); the check is skipped.
  std::shared_ptr<const Host> host{std::make_shared<LocalHost>()};
  // Unset means the platform of the machine fixmyfboss runs on.
  std::optional<std::string> platformName;
  // Why `host` is null, reported as the skip reason.
  std::string unavailableReason;
};

/**
 * Abstract base class for all platform checks. Checks must do all I/O through
 * host(), so that they work both on-device and against a remote switch.
 */
class PlatformCheck {
 public:
  explicit PlatformCheck(CheckTarget target = {})
      : target_(std::move(target)) {}

  virtual ~PlatformCheck() = default;

  virtual CheckResult run() = 0;

  virtual CheckType getType() const = 0;

  /**
   * human-readable description of what this check validates
   */
  virtual std::string getDescription() const = 0;

  virtual std::string getName() const = 0;

  virtual std::set<std::string> getSupportedPlatforms() const {
    return {}; // Empty set = all platforms supported
  }

  // Disruptive checks reboot or power cycle parts of the switch.
  virtual bool isDisruptive() const {
    return false;
  }

  // Why the check cannot run in this environment, if it cannot.
  virtual std::optional<std::string> getSkipReason() const {
    if (!target_.host) {
      return target_.unavailableReason;
    }
    return std::nullopt;
  }

 protected:
  const Host& host() const {
    return *target_.host;
  }

  const std::optional<std::string>& platformName() const {
    return target_.platformName;
  }

  /**
   * Get platform configuration. Mockable for unit tests.
   */
  virtual platform_manager::PlatformConfig getPlatformConfig() const;

  CheckResult makeError(const std::string& errorMessage) const {
    CheckResult result;
    result.checkType() = getType();
    result.checkName() = getName();
    result.status() = CheckStatus::ERROR;
    result.errorMessage() = errorMessage;
    return result;
  }

  CheckResult makeOK() const {
    CheckResult result;
    result.checkType() = getType();
    result.checkName() = getName();
    result.status() = CheckStatus::OK;
    return result;
  }

  CheckResult makeProblem(
      const std::string& errorMsg,
      RemediationType type,
      const std::string& remediation) const {
    CheckResult result;
    result.checkType() = getType();
    result.checkName() = getName();
    result.status() = CheckStatus::PROBLEM;
    result.errorMessage() = errorMsg;
    result.remediation() = type;
    result.remediationMessage() = remediation;
    return result;
  }

 private:
  CheckTarget target_;
};

} // namespace facebook::fboss::platform::platform_checks
