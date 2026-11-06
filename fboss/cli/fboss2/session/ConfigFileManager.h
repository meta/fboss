/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::fboss {

/**
 * Applies one CLI-owned desired config to the path read by a service.
 *
 * Both managed paths must be below systemConfigDir so their changes can be
 * recorded in the system config repository. The service path may instead be
 * one external runtime symlink, such as a /dev/shm link into /etc/coop; in
 * that case the in-repository target is managed and the runtime link is left
 * untouched.
 *
 * ConfigSession remains responsible for config validation, Git, and service
 * actions. This class only captures filesystem state, applies desired content,
 * and restores the captured state after a failed transaction. Regular files
 * are written atomically with mode 0644.
 * systemConfigDir and its parent hierarchy are trusted; containment checks are
 * lexical so the current path itself can remain an in-repository symlink.
 */
class ConfigFileManager {
 private:
  enum class FileType { MISSING, REGULAR, SYMLINK };

  struct FileState {
    FileType type{FileType::MISSING};
    // Regular-file content or symlink target, depending on type.
    std::string data;
  };

 public:
  struct Paths {
    std::string systemConfigDir;
    std::string desired;
    // The service-reported path, before resolving one external symlink.
    std::string current;
  };

  class Snapshot {
   public:
    Snapshot(Snapshot&&) noexcept = default;
    Snapshot& operator=(Snapshot&&) noexcept = default;
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    std::optional<std::string_view> desiredContent() const;

   private:
    friend class ConfigFileManager;

    Snapshot(FileState desired, std::optional<FileState> current);

    FileState desired_;
    // Absent when desired and current are the same path.
    std::optional<FileState> current_;
  };

  explicit ConfigFileManager(Paths paths);

  const std::string& currentPath() const;

  // Checks permissions needed for same-directory atomic replacement without
  // reading or changing either config file.
  void validateWritable() const;

  Snapshot capture() const;

  bool needsApply(
      const Snapshot& snapshot,
      std::optional<std::string_view> content) const;

  // Installs content, or removes both managed paths when content is absent.
  // Returns only the paths changed by the operation.
  std::vector<std::string> apply(
      const Snapshot& snapshot,
      std::optional<std::string_view> content) const;

  // Attempts both restorations even if one fails. Each returned string
  // describes a path that could not be restored.
  std::vector<std::string> restore(const Snapshot& snapshot) const;

 private:
  static FileState captureFile(const std::string& path);
  static void writeRegular(const std::string& path, std::string_view content);
  static void writeSymlink(
      const std::string& symlinkPath,
      const std::string& newTarget);

  Paths paths_;
  bool samePath_{false};
};

} // namespace facebook::fboss
