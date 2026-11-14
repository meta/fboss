// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/PkgManager.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_set>

#include <fb303/ServiceData.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <range/v3/view/concat.hpp>
#include <re2/re2.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fstream>

DEFINE_bool(
    enable_pkg_mgmnt,
    true,
    "Enable download and installation of the BSP rpm");

DEFINE_bool(
    reload_kmods,
    false,
    "The kmods are usually reloaded only when the BSP RPM changes. "
    "But if this flag is set, the kmods will be reloaded everytime.");

DEFINE_int32(
    kmod_unload_retries,
    10,
    "Number of times to attempt the BSP kmod unload pass before giving up. "
    "Retrying lets a transient holder release a module so the unload (and thus "
    "the BSP reload) can succeed instead of crashing.");

DEFINE_int32(
    kmod_unload_retry_backoff_s,
    3,
    "Seconds to wait between kmod unload retries.");

namespace fs = std::filesystem;
namespace facebook::fboss::platform::platform_manager {
namespace {
constexpr auto kBspKmodsRpmName = "fboss_bsp_kmods_rpm";
constexpr auto kBspKmodsRpmVersionCounter = "bsp_kmods_rpm_version.{}";
constexpr auto kBspKmodsFilePath = "/usr/local/{}_bsp/{}/kmods.json";
const re2::RE2 kBspRpmNameRe = "(?P<KEYWORD>[a-z]+)_bsp_kmods";

// BSP kmods whose load failure is tolerated: PM logs and continues instead of
// aborting exploration. A kmod is listed in the BSP's kmods.json regardless of
// whether it was built, but a kmod can be absent from the RPM when the running
// kernel lacks a config it depends on. aadm1266 is built only when the kernel
// enables CONFIG_CRC8, so it is missing on kernels without it (e.g.
// 6.4.3-0_fbk1). It is only needed on blackwolf, so its absence is not a
// production issue and must not stop platform_manager.
const std::unordered_set<std::string_view> kTolerableKmodLoadFailures = {
    "aadm1266"};
} // namespace

void resolveBspKmodsRpmVersion(
    PlatformConfig& config,
    const package_manager::SystemInterface& systemInterface) {
  if (*config.bspKmodsRpmVersion() != kBspKmodsRpmVersionWildcard) {
    return;
  }
  const auto& rpmBaseName = *config.bspKmodsRpmName();
  auto availableVersions =
      systemInterface.getLocalRepoBspRpmVersions(rpmBaseName);
  if (availableVersions.empty()) {
    throw std::runtime_error(
        fmt::format(
            "bspKmodsRpmVersion is \"{}\", but no {} rpm for kernel {} was "
            "found in {}",
            kBspKmodsRpmVersionWildcard,
            rpmBaseName,
            systemInterface.getHostKernelVersion(),
            FLAGS_local_rpm_repo_path));
  }
  XLOG(INFO) << fmt::format(
      "Resolved bspKmodsRpmVersion \"{}\" to {} (available: {})",
      kBspKmodsRpmVersionWildcard,
      availableVersions.front(),
      folly::join(", ", availableVersions));
  config.bspKmodsRpmVersion() = availableVersions.front();
}

PkgManager::PkgManager(
    const PlatformConfig& config,
    const std::shared_ptr<package_manager::SystemInterface>& systemInterface,
    const std::shared_ptr<PlatformFsUtils>& platformFsUtils)
    : platformConfig_(config),
      systemInterface_(systemInterface),
      platformFsUtils_(platformFsUtils) {}

void PkgManager::processAll(bool enablePkgMgmnt, bool reloadKmods) const {
  SCOPE_SUCCESS {
    fb303::fbData->setCounter(kProcessAllFailure, 0);
  };
  SCOPE_FAIL {
    fb303::fbData->setCounter(kProcessAllFailure, 1);
  };

  auto processAllStart = std::chrono::steady_clock::now();
  SCOPE_EXIT {
    auto elapsedSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - processAllStart)
            .count();
    fb303::fbData->setCounter(kProcessAllTime, elapsedSeconds);
  };

  if (FLAGS_local_rpm_path.size()) {
    fb303::fbData->setExportedValue(
        kBspKmodsRpmName, "local_rpm: " + FLAGS_local_rpm_path);
  } else if (enablePkgMgmnt) {
    fb303::fbData->setExportedValue(kBspKmodsRpmName, getKmodsRpmName());
    fb303::fbData->setCounter(
        fmt::format(
            kBspKmodsRpmVersionCounter, *platformConfig_.bspKmodsRpmVersion()),
        1);
  } else {
    fb303::fbData->setExportedValue(
        kBspKmodsRpmName, "Not managing BSP package");
  }

  // BSP management
  // If new rpm is installed, PkgManager unload first to prevent kmods leak.
  // After install, PkgManager loads kmods.
  if (FLAGS_local_rpm_path.size()) {
    unloadBspKmods();
    processLocalRpms();
    loadRequiredKmods();
    return;
  }
  if (enablePkgMgmnt) {
    auto bspKmodsRpmName = getKmodsRpmName();
    if (!systemInterface_->isRpmInstalled(bspKmodsRpmName)) {
      XLOG(INFO) << fmt::format(
          "BSP Kmods {} is not installed", bspKmodsRpmName);
      // Install desired rpm
      processRpms();
      // In cases where kmods.json from previous BSP installation is absent
      // (like provisioning, where this is the first run of PM), the kmods might
      // be present in the initramfs OR ramdisk image.
      // This also helps in catching early the cases where a new BSP upgrade is
      // attempted with an ill-formatted kmods.json. TODO: figure out better
      // gate-keepers for this.
      XLOG(INFO)
          << "Re-attempting unloading of BSP kmods. This is to handle the cases "
          << "where this is the first run of PM, and the kmods are present in "
          << "the initramfs OR ramdisk image";
      unloadBspKmods();
      // Load required kmods from PM config.
      loadRequiredKmods();
      return;
    }
    XLOG(INFO) << fmt::format(
        "BSP Kmods {} is already installed", bspKmodsRpmName);
  }
  // Kmods management.
  // Only when no BSP management happened.
  if (reloadKmods) {
    unloadBspKmods();
  }
  try {
    loadRequiredKmods();
  } catch (const std::exception&) {
    // This can happen when kmods are loaded from initramfs - causing conflicts
    // We need to unload them before loading again.
    XLOG(ERR) << fmt::format("Failed to load kmods. Unloading and retrying.");
    unloadBspKmods();
    loadRequiredKmods();
  }
}

void PkgManager::processRpms() const {
  SCOPE_SUCCESS {
    fb303::fbData->setCounter(kProcessRpmFailure, 0);
  };
  SCOPE_FAIL {
    fb303::fbData->setCounter(kProcessRpmFailure, 1);
  };

  removeInstalledRpms();

  package_manager::RpmInstallResult result;
  auto bspKmodsRpmName = getKmodsRpmName();
  for (int attempt = 0; attempt < 3; attempt++) {
    XLOG(INFO) << fmt::format(
        "Installing BSP {}, Attempt #{}", bspKmodsRpmName, attempt);
    result = systemInterface_->installRpm(bspKmodsRpmName, "kernel");
    if (result.exitStatus == 0) {
      break;
    }
    // No repo has this package: retrying cannot make it appear.
    if (result.notFound) {
      auto availableVersions = systemInterface_->getLocalRepoBspRpmVersions(
          *platformConfig_.bspKmodsRpmName());
      throw std::runtime_error(
          fmt::format(
              "BSP rpm ({}) was not found in any enabled dnf repo. The "
              "bspKmodsRpmVersion ({}) in the platform_manager config most "
              "likely does not match any BSP built for kernel {}. Versions "
              "available in {}: {}. Set bspKmodsRpmVersion to \"*\" to always "
              "use the newest BSP present on the system.",
              bspKmodsRpmName,
              *platformConfig_.bspKmodsRpmVersion(),
              systemInterface_->getHostKernelVersion(),
              FLAGS_local_rpm_repo_path,
              availableVersions.empty()
                  ? std::string("none")
                  : folly::join(", ", availableVersions)));
    }
  }
  if (result.exitStatus != 0) {
    throw std::runtime_error(
        fmt::format(
            "Failed to install rpm ({}) with exit code {}",
            bspKmodsRpmName,
            result.exitStatus));
  }
  int exitStatus{0};
  XLOG(INFO) << "Caching kernel modules dependencies";
  if (exitStatus = systemInterface_->depmod(); exitStatus != 0) {
    XLOG(ERR) << fmt::format("Failed to depmod with exit code {}", exitStatus);
  }
}

bool PkgManager::isValidRpm() const {
  // Check kernel version to match BSP rpm's kernel version
  auto hostKernelVersion = systemInterface_->getHostKernelVersion();

  // Expected format: {bspKmodsRpmName}-{kernelVersion}.rpm
  // Example: fboss_bsp_kmods-6.11.1-0_fbk3_647_gc1af76fcc8cb.x86_64
  std::string filename = fs::path(FLAGS_local_rpm_path).filename().string();

  // Check if the file is a RPM file
  if (filename.ends_with(".rpm")) {
    filename = filename.substr(0, filename.length() - 4);
  } else {
    XLOG(ERR) << fmt::format("Invalid RPM filename: {}", filename);
    return false;
  }

  // Does not match the exact kernel version in RPM filename
  if (filename.find(hostKernelVersion) == std::string::npos) {
    XLOG(ERR) << fmt::format(
        "Running kernel version ({}) does not match kernel version in BSP RPM "
        "filename({})",
        hostKernelVersion,
        filename);
    return false;
  }

  XLOG(INFO) << fmt::format(
      "Kernel version verified: Host kernel version ({}) matches "
      "RPM kernel version ({})",
      hostKernelVersion,
      filename);
  return true;
}

void PkgManager::processLocalRpms() const {
  uint exitStatus{0};

  if (!isValidRpm()) {
    throw std::runtime_error(
        fmt::format("Invalid rpm provided ({})", FLAGS_local_rpm_path));
  }
  for (auto [success, attempt] = std::pair{false, 0}; attempt < 3 && !success;
       attempt++) {
    XLOG(INFO) << fmt::format(
        "Installing local rpm at {}, Attempt #{}",
        FLAGS_local_rpm_path,
        attempt);
    exitStatus = systemInterface_->installLocalRpm();
    success = exitStatus == 0;
  }
  if (exitStatus != 0) {
    throw std::runtime_error(
        fmt::format(
            "Failed to install rpm ({}) with exit code {}",
            FLAGS_local_rpm_path,
            exitStatus));
  }
}

void PkgManager::closeWatchdogs() const {
  const std::string watchdogsDir = "/run/devmap/watchdogs";
  if (!fs::exists(watchdogsDir)) {
    return;
  }
  XLOG(INFO) << "Closing Watchdogs to prevent watchdogs holding kmods";
  try {
    for (const auto& entry : fs::directory_iterator(watchdogsDir)) {
      const std::string filePath = entry.path().string();
      if (!fs::is_character_file(filePath)) {
        XLOG(WARN) << fmt::format(
            "{} does not exist or is not a character file. "
            "Not performing watchdog magic close",
            filePath);
        continue;
      }
      std::ofstream outFile(filePath);
      if (!outFile) {
        XLOG(ERR) << "Failed to open file: " << filePath;
        continue;
      }
      outFile << "V" << std::endl;
      XLOG(INFO) << fmt::format("Closed watchdog file {}", filePath);
    }
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to close watchdog: " << e.what();
    return;
  }
}

bool PkgManager::unloadKmodsOnce(const BspKmodsFile& bspKmodsFile) const {
  const auto loadedKmods = systemInterface_->lsmod();
  for (const auto& kmod : ranges::views::concat(
           *bspKmodsFile.bspKmods(), *bspKmodsFile.sharedKmods())) {
    if (!loadedKmods.contains(kmod)) {
      XLOG(INFO) << fmt::format(
          "Skipping to unload {}. Reason: Already unloaded", kmod);
      continue;
    }
    XLOG(INFO) << fmt::format("Unloading {}", kmod);
    if (!systemInterface_->unloadKmod(kmod)) {
      XLOG(WARN) << fmt::format("Failed to unload {}", kmod);
      return false;
    }
  }
  return true;
}

void PkgManager::unloadBspKmods() const {
  SCOPE_SUCCESS {
    fb303::fbData->setCounter(kUnloadKmodsFailure, 0);
  };
  SCOPE_FAIL {
    fb303::fbData->setCounter(kUnloadKmodsFailure, 1);
  };

  std::string bspKmodsFilePath = getBspKmodsFilePath();
  auto jsonBspKmodsFile =
      platformFsUtils_->getStringFileContent(bspKmodsFilePath);
  if (!jsonBspKmodsFile) {
    // No old rpms, most likely first ever rpm installation.
    if (auto installedRpms =
            systemInterface_->getInstalledRpms(getKmodsRpmBaseWithKernelName());
        installedRpms.empty()) {
      XLOG(WARNING) << fmt::format(
          "Skipping BSP kmods unloading. Reason: {} doesn't exist; "
          "most likely because this is the first RPM installation.",
          bspKmodsFilePath);
      return;
    }
    // Unexpected read failures. Can't proceed forward.
    // This shouldn't happen in production.
    throw std::runtime_error(
        fmt::format(
            "Failed to unload kmods. Reason: Failed to read {}; "
            "most likely because RPM is incompatible. A possible remediation: {}",
            bspKmodsFilePath,
            fmt::format("dnf install {} --assumeyes", getKmodsRpmName())));
  }
  BspKmodsFile bspKmodsFile;
  apache::thrift::SimpleJSONSerializer::deserialize<BspKmodsFile>(
      *jsonBspKmodsFile, bspKmodsFile);
  XLOG(INFO)
      << "kmods.json file found. Unloading kernel modules based on kmods.json";
  // Watchdogs would prevent module unloading if they are not stopped correctly.
  // Try to close all of them before proceeding. This will help in cases where
  // the watchdog managing service crashed.
  closeWatchdogs();

  // A kmod unload can fail if another process loads shared kmods at the same
  // time as PM unloads them. To avoid this, just retry a few times.
  const int maxAttempts = std::max(1, FLAGS_kmod_unload_retries);
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (unloadKmodsOnce(bspKmodsFile)) {
      kmodsUnloaded_ = true;
      return;
    }
    if (attempt < maxAttempts && FLAGS_kmod_unload_retry_backoff_s > 0) {
      // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
      std::this_thread::sleep_for(
          std::chrono::seconds(FLAGS_kmod_unload_retry_backoff_s));
    }
  }
  throw std::runtime_error(
      fmt::format("Failed to unload BSP kmods after {} attempts", maxAttempts));
}

void PkgManager::loadRequiredKmods() const {
  // Reset upfront; any load failure below sets it, including tolerated ones
  // that are logged but not rethrown, so the counter reflects the failure.
  fb303::fbData->setCounter(kLoadKmodsFailure, 0);
  XLOG(INFO) << fmt::format(
      "Loading {} non-BSP kernel modules",
      platformConfig_.nonBspKmodsToLoad()->size());
  for (const auto& nonBspKmod : *platformConfig_.nonBspKmodsToLoad()) {
    XLOG(INFO) << fmt::format("Loading {}", nonBspKmod);
    if (!systemInterface_->loadKmod(nonBspKmod)) {
      fb303::fbData->setCounter(kLoadKmodsFailure, 1);
      throw std::runtime_error(fmt::format("Failed to load ({})", nonBspKmod));
    }
  }
  loadBspKmods();
}

void PkgManager::loadBspKmods() const {
  const auto bspKmodsFile = readKmodsFile();
  for (const auto& kmod : ranges::views::concat(
           *bspKmodsFile.sharedKmods(), *bspKmodsFile.bspKmods())) {
    XLOG(INFO) << fmt::format("Loading {}", kmod);
    if (!systemInterface_->loadKmod(kmod)) {
      fb303::fbData->setCounter(kLoadKmodsFailure, 1);
      if (!kTolerableKmodLoadFailures.contains(kmod)) {
        throw std::runtime_error(fmt::format("Failed to load ({})", kmod));
      }
      XLOG(ERR) << fmt::format(
          "Ignoring load failure for non-critical kmod ({})", kmod);
    }
  }
}

void PkgManager::removeInstalledRpms() const {
  // Uninstalling the RPM can leave the system in a weird state if kmods
  // themselves are not unloaded. Bundling these two operations together
  // ensures that removing an RPM actually results in a clean system
  unloadBspKmods();
  auto installedRpms =
      systemInterface_->getInstalledRpms(getKmodsRpmBaseWithKernelName());
  if (installedRpms.empty()) {
    XLOG(INFO) << "Skipping removing old rpms. Reason: No rpms installed.";
    return;
  }
  int exitStatus{0};
  XLOG(INFO) << fmt::format(
      "Removing old rpms: {}", folly::join(", ", installedRpms));
  if (exitStatus = systemInterface_->removeRpms(installedRpms);
      exitStatus != 0) {
    throw std::runtime_error(
        fmt::format(
            "Failed to remove old rpms ({}) with exit code {}",
            folly::join(" ", installedRpms),
            exitStatus));
  }
  XLOG(INFO) << fmt::format(
      "Removed old rpms: {}", folly::join(", ", installedRpms));
}

BspKmodsFile PkgManager::readKmodsFile() const {
  std::string bspKmodsFilePath = getBspKmodsFilePath();
  auto jsonBspKmodsFile =
      platformFsUtils_->getStringFileContent(bspKmodsFilePath);
  if (!jsonBspKmodsFile) {
    throw std::runtime_error(
        fmt::format(
            "Failed to read kmods file. Reason: Failed to read {}",
            bspKmodsFilePath));
  }
  BspKmodsFile bspKmodsFile;
  apache::thrift::SimpleJSONSerializer::deserialize<BspKmodsFile>(
      *jsonBspKmodsFile, bspKmodsFile);
  return bspKmodsFile;
}

std::string PkgManager::getKmodsRpmName() const {
  return fmt::format(
      "{}-{}-{}",
      *platformConfig_.bspKmodsRpmName(),
      systemInterface_->getHostKernelVersion(),
      *platformConfig_.bspKmodsRpmVersion());
}

std::string PkgManager::getKmodsRpmBaseWithKernelName() const {
  return fmt::format(
      "{}-{}",
      *platformConfig_.bspKmodsRpmName(),
      systemInterface_->getHostKernelVersion());
}

std::string PkgManager::getBspKmodsFilePath() const {
  std::string keyword{};
  re2::RE2::FullMatch(
      *platformConfig_.bspKmodsRpmName(), kBspRpmNameRe, &keyword);
  return fmt::format(
      kBspKmodsFilePath, keyword, systemInterface_->getHostKernelVersion());
}

bool PkgManager::wereKmodsUnloaded() const {
  return kmodsUnloaded_;
}
} // namespace facebook::fboss::platform::platform_manager
