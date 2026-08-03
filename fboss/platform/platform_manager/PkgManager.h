// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <string>
#include <vector>

#include "fboss/platform/helpers/PlatformFsUtils.h"
#include "fboss/platform/platform_manager/SystemInterface.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

DECLARE_bool(enable_pkg_mgmnt);
DECLARE_bool(reload_kmods);
DECLARE_string(local_rpm_path);
DECLARE_string(local_rpm_repo_path);
DECLARE_int32(kmod_unload_retries);
DECLARE_int32(kmod_unload_retry_backoff_s);

namespace facebook::fboss::platform::platform_manager {

// bspKmodsRpmVersion value asking PM to use whichever BSP rpm the image happens
// to ship, instead of a version pinned in the config.
constexpr auto kBspKmodsRpmVersionWildcard = "*";

// If config.bspKmodsRpmVersion() is the wildcard, rewrites it in-place with the
// newest BSP version available in the local rpm repo for the running kernel, so
// that everything downstream (rpm name, ODS counters, thrift getBspVersion)
// sees a concrete version. Throws if the wildcard cannot be resolved. No-op for
// a pinned version.
void resolveBspKmodsRpmVersion(
    PlatformConfig& config,
    const package_manager::SystemInterface& systemInterface);

class PkgManager {
 public:
  // ODS Counters
  constexpr static auto kProcessAllFailure =
      "package_manager.process_all_failure";
  constexpr static auto kLoadKmodsFailure =
      "package_manager.load_kmods_failure";
  constexpr static auto kUnloadKmodsFailure =
      "package_manager.unload_kmods_failure";
  constexpr static auto kProcessRpmFailure =
      "package_manager.process_rpm_failure";
  constexpr static auto kProcessAllTime = "package_manager.process_all_time";

  explicit PkgManager(
      const PlatformConfig& config,
      const std::shared_ptr<package_manager::SystemInterface>& systemInterface =
          std::make_shared<package_manager::SystemInterface>(),
      const std::shared_ptr<PlatformFsUtils>& platformFsUtils =
          std::make_shared<PlatformFsUtils>());
  virtual ~PkgManager() = default;
  virtual void processAll(bool enablePkgMgmnt, bool reloadKmods) const;
  virtual bool isValidRpm() const;
  virtual void processRpms() const;
  void processLocalRpms() const;
  virtual void unloadBspKmods() const;
  // Loads the platform's required (bootstrap) kmods from the config, then
  // additionally loads every kmod enumerated in kmods.json
  virtual void loadRequiredKmods() const;
  void removeInstalledRpms() const;
  BspKmodsFile readKmodsFile() const;
  bool wereKmodsUnloaded() const;

 private:
  std::string getKmodsRpmName() const;
  std::string getKmodsRpmBaseWithKernelName() const;
  std::string getBspKmodsFilePath() const;
  void closeWatchdogs() const;
  // Loads every kmod enumerated in kmods.json (shared kmods first, then bsp
  // kmods -- the reverse of the unload order).
  void loadBspKmods() const;
  // Makes a single pass over the BSP and shared kmods, unloading each one that
  // is currently loaded. Returns false as soon as an unload fails, so the
  // caller can retry the whole pass.
  bool unloadKmodsOnce(const BspKmodsFile& bspKmodsFile) const;

  const PlatformConfig& platformConfig_;
  const std::shared_ptr<package_manager::SystemInterface> systemInterface_;
  const std::shared_ptr<PlatformFsUtils> platformFsUtils_;
  mutable bool kmodsUnloaded_{false};
};

} // namespace facebook::fboss::platform::platform_manager
