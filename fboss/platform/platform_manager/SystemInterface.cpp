// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/SystemInterface.h"

#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <filesystem>

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>

#include "fboss/platform/helpers/PlatformUtils.h"

DEFINE_string(
    local_rpm_path,
    "",
    "Path to the local rpm file that needs to be installed on the system.");

DEFINE_string(
    local_rpm_repo_path,
    "/usr/local/share/local_rpm_repo",
    "Directory holding the rpms shipped with the image. Used to resolve "
    "bspKmodsRpmVersion when it is set to '*', and to report which BSP "
    "versions are available when an install fails.");

namespace fs = std::filesystem;

namespace facebook::fboss::platform::platform_manager::package_manager {
namespace {
// dnf's wording when the requested package exists in no enabled repo.
constexpr std::array<std::string_view, 2> kRpmNotFoundMarkers = {
    "Unable to find a match",
    "No match for argument"};
} // namespace

std::optional<BspVersion> BspVersion::fromString(std::string_view version) {
  std::vector<std::string_view> parts;
  folly::split('.', version, parts);
  if (parts.size() < 3) {
    return std::nullopt;
  }
  auto major = folly::tryTo<int>(parts[0]);
  auto minor = folly::tryTo<int>(parts[1]);
  auto patch = folly::tryTo<int>(parts[2]);
  if (!major.hasValue() || !minor.hasValue() || !patch.hasValue()) {
    return std::nullopt;
  }
  return BspVersion{*major, *minor, *patch};
}

SystemInterface::SystemInterface(
    const std::shared_ptr<PlatformUtils>& platformUtils)
    : platformUtils_(platformUtils) {}

bool SystemInterface::loadKmod(const std::string& moduleName) const {
  int exitStatus{0};
  std::string standardOut{};
  auto unloadCmd = fmt::format("modprobe {}", moduleName);
  VLOG(1) << fmt::format("Running command ({})", unloadCmd);
  std::tie(exitStatus, standardOut) = platformUtils_->execCommand(unloadCmd);
  return exitStatus == 0;
}

bool SystemInterface::unloadKmod(const std::string& moduleName) const {
  int exitStatus{0};
  std::string standardOut{};
  auto unloadCmd = fmt::format("rmmod {}", moduleName);
  VLOG(1) << fmt::format("Running command ({})", unloadCmd);
  std::tie(exitStatus, standardOut) = platformUtils_->execCommand(unloadCmd);
  return exitStatus == 0;
}

RpmInstallResult SystemInterface::installRpm(
    const std::string& rpmFullName,
    const std::string& repoName) const {
  // Redirect stderr into stdout: dnf reports "Unable to find a match" there,
  // and execCommand only hands back stdout.
  auto cmd = fmt::format(
      "dnf install {} --assumeyes --setopt=*.skip_if_unavailable=True {} 2>&1",
      rpmFullName,
      repoName.empty() ? "" : "--disablerepo='*' --enablerepo=" + repoName);
  VLOG(1) << fmt::format("Running command ({})", cmd);
  auto [exitStatus, standardOut] = platformUtils_->execCommand(cmd);
  if (exitStatus == 0) {
    return RpmInstallResult{};
  }
  XLOG(ERR) << standardOut;
  bool notFound = std::any_of(
      kRpmNotFoundMarkers.begin(),
      kRpmNotFoundMarkers.end(),
      [&standardOut](const auto& marker) {
        return standardOut.find(marker) != std::string::npos;
      });
  return RpmInstallResult{exitStatus, notFound};
}

int SystemInterface::installLocalRpm() const {
  int exitStatus{0};
  std::string standardOut{};
  auto cmd = fmt::format("rpm -i --force {}", FLAGS_local_rpm_path);
  VLOG(1) << fmt::format("Running command ({})", cmd);
  std::tie(exitStatus, standardOut) = platformUtils_->execCommand(cmd);
  return exitStatus;
}

int SystemInterface::depmod() const {
  int exitStatus{0};
  std::string standardOut{};
  auto depmodCmd = "depmod -a";
  VLOG(1) << fmt::format("Running command ({})", depmodCmd);
  std::tie(exitStatus, standardOut) = platformUtils_->execCommand(depmodCmd);
  return exitStatus;
}

std::vector<std::string> SystemInterface::getInstalledRpms(
    const std::string& rpmBaseName) const {
  std::string standardOut{};
  int exitStatus{0};
  auto cmd = fmt::format("rpm -qa | grep ^{}", rpmBaseName);
  VLOG(1) << fmt::format("Running command ({})", cmd);
  std::tie(exitStatus, standardOut) = platformUtils_->execCommand(cmd);
  if (exitStatus) {
    return {};
  }
  std::vector<std::string> installedRpms;
  folly::split('\n', standardOut, installedRpms);
  return installedRpms;
}

int SystemInterface::removeRpms(
    const std::vector<std::string>& installedRpms) const {
  int exitStatus{0};
  std::string standardOut;
  auto removeOldRpmsCmd =
      fmt::format("rpm -e --allmatches {}", folly::join(" ", installedRpms));
  VLOG(1) << fmt::format("Running command ({})", removeOldRpmsCmd);
  std::tie(exitStatus, standardOut) =
      platformUtils_->execCommand(removeOldRpmsCmd);
  return exitStatus;
}

std::set<std::string> SystemInterface::lsmod() const {
  VLOG(1) << "Running command (lsmod)";
  auto result = platformUtils_->execCommand("lsmod");
  auto rows = result.second | ranges::views::split('\n') |
      ranges::views::drop(1) | ranges::to<std::vector<std::string>>;
  std::set<std::string> kmods;
  // row -> kmod | size | used by | dependent kmods
  for (const auto& row : rows) {
    auto tokens = row | ranges::views::split(' ') |
        ranges::views::filter(
                      [](const auto& token) { return !token.empty(); }) |
        ranges::to<std::vector<std::string>>;
    if (tokens.empty()) {
      XLOG(WARNING) << fmt::format("Failed to scan lsmod row -- {}", row);
      continue;
    }
    kmods.emplace(tokens.front());
  }
  return kmods;
}

std::string SystemInterface::getHostKernelVersion() const {
  VLOG(1) << "Using system name structure for host kernel version";
  utsname result{};
  uname(&result);
  return result.release;
}

bool SystemInterface::isRpmInstalled(const std::string& rpmFullName) const {
  auto cmd = fmt::format("dnf list {} --installed", rpmFullName);
  VLOG(1) << fmt::format("Running command ({})", cmd);
  auto [exitStatus, standardOut] = PlatformUtils().execCommand(cmd);
  return exitStatus == 0;
}

std::optional<std::string> SystemInterface::getBoundDriverVersion(
    const std::string& devicePath) const {
  std::string version;
  if (!folly::readFile(
          fmt::format("{}/driver/module/version", devicePath).c_str(),
          version)) {
    return std::nullopt;
  }
  auto trimmed = folly::trimWhitespace(version);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  return std::string(trimmed);
}

std::vector<std::string> SystemInterface::getLocalRepoBspRpmVersions(
    const std::string& rpmBaseName) const {
  const auto kernelVersion = getHostKernelVersion();
  if (kernelVersion.empty()) {
    return {};
  }
  std::error_code errorCode;
  if (!fs::is_directory(FLAGS_local_rpm_repo_path, errorCode)) {
    XLOG(ERR) << fmt::format(
        "Local rpm repo {} is not a directory", FLAGS_local_rpm_repo_path);
    return {};
  }
  // A BSP kmods rpm file is named
  //   {rpmBaseName}-{kernelVersion}-{version}-{release}-{version}-{release}.{arch}.rpm
  // where {rpmBaseName}-{kernelVersion}-{version}-{release} is the package name
  // (what dnf install takes) and the trailing {version}-{release}.{arch} is
  // rpm's usual file suffix. Pinning the prefix on the running kernel skips
  // rpms built for other kernels.
  const auto prefix = fmt::format("{}-{}-", rpmBaseName, kernelVersion);
  // {version, release, "{version}-{release}"}
  std::vector<std::tuple<BspVersion, int, std::string>> candidates;
  for (const auto& entry : fs::directory_iterator(
           FLAGS_local_rpm_repo_path,
           fs::directory_options::skip_permission_denied,
           errorCode)) {
    const auto filename = entry.path().filename().string();
    if (!filename.ends_with(".rpm") || !filename.starts_with(prefix)) {
      continue;
    }
    std::vector<std::string> tokens;
    folly::split('-', filename.substr(prefix.size()), tokens);
    if (tokens.size() < 2) {
      continue;
    }
    auto bspVersion = BspVersion::fromString(tokens[0]);
    if (!bspVersion) {
      XLOG(WARNING) << fmt::format(
          "Ignoring {}: unparseable BSP version '{}'", filename, tokens[0]);
      continue;
    }
    auto release = folly::tryTo<int>(tokens[1]);
    candidates.emplace_back(
        *bspVersion,
        release.hasValue() ? *release : 0,
        fmt::format("{}-{}", tokens[0], tokens[1]));
  }
  // Newest version first, then newest release.
  std::sort(
      candidates.begin(),
      candidates.end(),
      [](const auto& lhs, const auto& rhs) { return lhs > rhs; });
  std::vector<std::string> versions;
  for (const auto& candidate : candidates) {
    const auto& version = std::get<2>(candidate);
    if (versions.empty() || versions.back() != version) {
      versions.push_back(version);
    }
  }
  return versions;
}

} // namespace facebook::fboss::platform::platform_manager::package_manager
