/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/session/ConfigFileManager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <fmt/format.h>
#include <folly/FileUtil.h>

namespace fs = std::filesystem;

namespace facebook::fboss {
namespace {

bool samePath(const fs::path& lhs, const fs::path& rhs) {
  return lhs.lexically_normal() == rhs.lexically_normal();
}

bool isBelow(const fs::path& path, const fs::path& directory) {
  const auto relative = path.lexically_relative(directory);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

} // namespace

ConfigFileManager::Snapshot::Snapshot(
    FileState desired,
    std::optional<FileState> current)
    : desired_(std::move(desired)), current_(std::move(current)) {}

std::optional<std::string_view> ConfigFileManager::Snapshot::desiredContent()
    const {
  if (desired_.type == FileType::MISSING) {
    return std::nullopt;
  }
  return desired_.data;
}

ConfigFileManager::ConfigFileManager(Paths paths) : paths_(std::move(paths)) {
  const auto systemConfigDir =
      fs::path(paths_.systemConfigDir).lexically_normal();
  const auto desired = fs::path(paths_.desired).lexically_normal();
  if (!systemConfigDir.is_absolute() || !isBelow(desired, systemConfigDir)) {
    throw std::runtime_error(
        fmt::format(
            "Desired config path {} must be below {}",
            desired.string(),
            systemConfigDir.string()));
  }

  paths_.systemConfigDir = systemConfigDir.string();
  paths_.desired = desired.string();
  auto current = fs::path(paths_.current).lexically_normal();
  if (!current.is_absolute()) {
    throw std::runtime_error(
        fmt::format(
            "Service returned a non-absolute config path: {}",
            current.string()));
  }
  if (!isBelow(current, systemConfigDir)) {
    std::error_code error;
    auto target = fs::read_symlink(current, error);
    if (error) {
      throw std::runtime_error(
          fmt::format(
              "Service config path {} is outside {} and is not a readable "
              "symlink: {}",
              current.string(),
              systemConfigDir.string(),
              error.message()));
    }
    if (!target.is_absolute()) {
      target = current.parent_path() / target;
    }
    current = target.lexically_normal();
    if (!isBelow(current, systemConfigDir)) {
      throw std::runtime_error(
          fmt::format(
              "Service config symlink points outside {}: {}",
              systemConfigDir.string(),
              current.string()));
    }
  }
  paths_.current = current.string();
  samePath_ = samePath(paths_.desired, paths_.current);
}

const std::string& ConfigFileManager::currentPath() const {
  return paths_.current;
}

void ConfigFileManager::validateWritable() const {
  const auto validateDirectory = [](const fs::path& path) {
    if (::faccessat(AT_FDCWD, path.c_str(), W_OK | X_OK, AT_EACCESS) != 0) {
      throw std::system_error(
          errno,
          std::generic_category(),
          fmt::format("Config directory is not writable: {}", path.string()));
    }
  };
  const auto desiredParent = fs::path(paths_.desired).parent_path();
  validateDirectory(desiredParent);
  const auto currentParent = fs::path(paths_.current).parent_path();
  if (currentParent != desiredParent) {
    validateDirectory(currentParent);
  }
}

ConfigFileManager::Snapshot ConfigFileManager::capture() const {
  auto desired = captureFile(paths_.desired);
  if (desired.type == FileType::SYMLINK) {
    throw std::runtime_error(
        fmt::format("Desired config path is a symlink: {}", paths_.desired));
  }
  if (samePath_) {
    return Snapshot(std::move(desired), std::nullopt);
  }
  return Snapshot(std::move(desired), captureFile(paths_.current));
}

bool ConfigFileManager::needsApply(
    const Snapshot& snapshot,
    std::optional<std::string_view> content) const {
  if (!content) {
    return snapshot.desired_.type != FileType::MISSING ||
        (snapshot.current_ && snapshot.current_->type != FileType::MISSING);
  }
  if (snapshot.desired_.type == FileType::MISSING ||
      snapshot.desired_.data != *content) {
    return true;
  }
  if (!snapshot.current_) {
    return false;
  }

  const auto& current = *snapshot.current_;
  if (current.type == FileType::MISSING ||
      (current.type == FileType::REGULAR && current.data != *content)) {
    return true;
  }
  if (current.type != FileType::SYMLINK) {
    return false;
  }
  fs::path target(current.data);
  if (!target.is_absolute()) {
    target = fs::path(paths_.current).parent_path() / target;
  }
  return !samePath(target, paths_.desired);
}

std::vector<std::string> ConfigFileManager::apply(
    const Snapshot& snapshot,
    std::optional<std::string_view> content) const {
  validateWritable();
  std::vector<std::string> changed;

  if (!content) {
    const auto remove = [&changed](
                            const std::string& path, const FileState& state) {
      if (state.type == FileType::MISSING) {
        return;
      }
      std::error_code error;
      fs::remove(path, error);
      if (error) {
        throw std::runtime_error(
            fmt::format("Failed to remove {}: {}", path, error.message()));
      }
      changed.push_back(path);
    };
    remove(paths_.desired, snapshot.desired_);
    if (snapshot.current_) {
      remove(paths_.current, *snapshot.current_);
    }
    return changed;
  }

  if (snapshot.desired_.type == FileType::MISSING ||
      snapshot.desired_.data != *content) {
    writeRegular(paths_.desired, *content);
    changed.push_back(paths_.desired);
  }

  if (!snapshot.current_) {
    return changed;
  }

  const auto& current = *snapshot.current_;
  const auto desiredTarget =
      fs::path(paths_.desired)
          .lexically_relative(fs::path(paths_.current).parent_path())
          .string();
  if (current.type == FileType::REGULAR && current.data != *content) {
    writeRegular(paths_.current, *content);
    changed.push_back(paths_.current);
  } else if (current.type == FileType::MISSING) {
    writeSymlink(paths_.current, desiredTarget);
    changed.push_back(paths_.current);
  } else if (current.type == FileType::SYMLINK) {
    fs::path target(current.data);
    if (!target.is_absolute()) {
      target = fs::path(paths_.current).parent_path() / target;
    }
    if (!samePath(target, paths_.desired)) {
      writeSymlink(paths_.current, desiredTarget);
      changed.push_back(paths_.current);
    }
  }
  return changed;
}

std::vector<std::string> ConfigFileManager::restore(
    const Snapshot& snapshot) const {
  std::vector<std::string> errors;
  const auto restore = [&errors](
                           const std::string& path, const FileState& state) {
    try {
      switch (state.type) {
        case FileType::MISSING: {
          std::error_code error;
          fs::remove(path, error);
          if (error) {
            throw std::runtime_error(
                fmt::format("Failed to remove {}: {}", path, error.message()));
          }
          return;
        }
        case FileType::REGULAR:
          writeRegular(path, state.data);
          return;
        case FileType::SYMLINK:
          writeSymlink(path, state.data);
          return;
      }
    } catch (const std::exception& ex) {
      errors.emplace_back(ex.what());
    }
  };

  restore(paths_.desired, snapshot.desired_);
  if (snapshot.current_) {
    restore(paths_.current, *snapshot.current_);
  }
  return errors;
}

ConfigFileManager::FileState ConfigFileManager::captureFile(
    const std::string& path) {
  struct stat status{};
  if (::lstat(path.c_str(), &status) != 0) {
    if (errno == ENOENT) {
      return {};
    }
    throw std::system_error(
        errno,
        std::generic_category(),
        fmt::format("Failed to inspect {}", path));
  }

  FileState state;
  if (S_ISREG(status.st_mode)) {
    state.type = FileType::REGULAR;
    if (!folly::readFile(path.c_str(), state.data)) {
      throw std::runtime_error(fmt::format("Failed to read {}", path));
    }
    return state;
  }
  if (S_ISLNK(status.st_mode)) {
    state.type = FileType::SYMLINK;
    std::error_code error;
    state.data = fs::read_symlink(path, error).string();
    if (error) {
      throw std::runtime_error(
          fmt::format("Failed to read symlink {}: {}", path, error.message()));
    }
    return state;
  }
  throw std::runtime_error(
      fmt::format("Config path is not a regular file or symlink: {}", path));
}

void ConfigFileManager::writeRegular(
    const std::string& path,
    std::string_view content) {
  folly::writeFileAtomic(path, content, 0644, folly::SyncType::WITH_SYNC);
}

void ConfigFileManager::writeSymlink(
    const std::string& symlinkPath,
    const std::string& newTarget) {
  std::error_code ec;
  fs::path symlinkFsPath(symlinkPath);

  // Generate a unique temporary path in the same directory as the target
  // symlink, we'll then atomically rename it to the final symlink name.
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  std::string tmpLinkName = fmt::format("fboss2_tmp_{}_{}", getpid(), ns);
  fs::path tempSymlinkPath = symlinkFsPath.parent_path() / tmpLinkName;

  // Create new symlink with temporary name
  fs::create_symlink(newTarget, tempSymlinkPath, ec);
  if (ec) {
    throw std::runtime_error(
        fmt::format(
            "Failed to create temporary symlink {} to {}: {}",
            tempSymlinkPath.string(),
            newTarget,
            ec.message()));
  }

  // A symlink cannot be retargeted in place. Removing and recreating it would
  // leave the service path missing between those operations, or permanently
  // missing if the process exits. rename() replaces the directory entry
  // atomically, so readers observe either the old link or the new one.
  fs::rename(tempSymlinkPath, symlinkPath, ec);
  if (ec) {
    // Clean up temp symlink
    fs::remove(tempSymlinkPath);
    throw std::runtime_error(
        fmt::format(
            "Failed to atomically update symlink {}: {}",
            symlinkPath,
            ec.message()));
  }
}

} // namespace facebook::fboss
