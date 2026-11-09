/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/session/Git.h"

#include <fcntl.h>
#include <fmt/format.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// RAII class to temporarily set umask for group writeability
// This ensures that files and directories created by Git are group-writable.
class ScopedUmask {
 public:
  explicit ScopedUmask(mode_t newMask) {
    oldMask_ = umask(newMask);
  }
  ~ScopedUmask() {
    umask(oldMask_);
  }
  ScopedUmask(const ScopedUmask&) = delete;
  ScopedUmask& operator=(const ScopedUmask&) = delete;
  ScopedUmask(ScopedUmask&&) = delete;
  ScopedUmask& operator=(ScopedUmask&&) = delete;

 private:
  mode_t oldMask_;
};

// RAII class to acquire an exclusive flock on a file.
// The lock is automatically released when the file descriptor is closed,
// which also happens when the process exits (even on SIGKILL or crash).
class ScopedFileLock {
 public:
  explicit ScopedFileLock(const std::string& lockPath) {
    // Ensure the lock file is created group-writable so a different
    // operator (same "switching" group) can later open it for O_RDWR,
    // regardless of this process's ambient umask.
    ScopedUmask scopedUmask(0002);
    fd_ = open(lockPath.c_str(), O_CREAT | O_RDWR, 0664);
    if (fd_ < 0) {
      throw std::runtime_error(
          fmt::format(
              "Failed to open lock file {}: {}",
              lockPath,
              folly::errnoStr(errno)));
    }
    if (flock(fd_, LOCK_EX) < 0) {
      int savedErrno = errno;
      close(fd_);
      throw std::runtime_error(
          fmt::format(
              "Failed to acquire lock on {}: {}",
              lockPath,
              folly::errnoStr(savedErrno)));
    }
  }
  ~ScopedFileLock() {
    if (fd_ >= 0) {
      flock(fd_, LOCK_UN);
      close(fd_);
    }
  }
  ScopedFileLock(const ScopedFileLock&) = delete;
  ScopedFileLock& operator=(const ScopedFileLock&) = delete;
  ScopedFileLock(ScopedFileLock&&) = delete;
  ScopedFileLock& operator=(ScopedFileLock&&) = delete;

 private:
  int fd_ = -1;
};

// Result of running a git command.
struct CommandResult {
  std::string stdoutStr;
  std::string stderrStr;
  int exitStatus;
};

CommandResult runGitCommandWithStatus(
    const std::string& repoPath,
    const std::vector<std::string>& args) {
  // Use full path to git to avoid PATH issues in test environments
  // Pass -c safe.directory=<path> to handle cases where the repository
  // is owned by a different user (e.g., /etc/coop owned by root)
  std::vector<std::string> fullArgs = {
      "/usr/bin/git", "-c", "safe.directory=" + repoPath, "-C", repoPath};
  fullArgs.insert(fullArgs.end(), args.begin(), args.end());

  try {
    folly::Subprocess proc(
        fullArgs, folly::Subprocess::Options().pipeStdout().pipeStderr());

    auto output = proc.communicate();
    int exitStatus = proc.wait().exitStatus();

    return {output.first, output.second, exitStatus};
  } catch (const std::exception& ex) {
    throw std::runtime_error(
        fmt::format("Failed to execute git command: {}", ex.what()));
  }
}

} // namespace

namespace facebook::fboss {

Git::Git(std::string repoPath) : repoPath_(std::move(repoPath)) {}

std::string Git::getRepoPath() const {
  return repoPath_;
}

bool Git::isRepository() const {
  auto result = runGitCommandWithStatus(repoPath_, {"rev-parse", "--git-dir"});
  return result.exitStatus == 0;
}

void Git::init(const std::string& initialBranch) {
  if (isRepository()) {
    return; // Already a repository
  }

  // Set umask to allow group write access (0002 means: keep all bits except
  // "other write"). This ensures .git directory and its contents are
  // group-writable when /etc/coop is group-writable (e.g., owned by root:admin)
  ScopedUmask scopedUmask(0002);

  // Create the directory if it doesn't exist
  std::error_code ec;
  fs::create_directories(repoPath_, ec);
  if (ec) {
    throw std::runtime_error(
        fmt::format(
            "Failed to create directory {}: {}", repoPath_, ec.message()));
  }

  // Initialize the repository with the specified initial branch
  // Use --shared=group to make the repository group-writable
  runGitCommand(
      {"init", "--initial-branch=" + initialBranch, "--shared=group"});

  // Configure user.name and user.email locally to avoid git config issues
  // Use "fboss-cli" as the default identity for automated commits
  runGitCommand({"config", "user.name", "fboss-cli"});
  runGitCommand({"config", "user.email", "fboss-cli@localhost"});
}

std::string Git::commit(
    const std::vector<std::string>& files,
    const std::string& message,
    const std::string& authorName,
    const std::string& authorEmail) {
  if (files.empty()) {
    throw std::runtime_error("No files specified for commit");
  }
  if (message.empty()) {
    throw std::runtime_error("Commit message cannot be empty");
  }

  // Acquire an exclusive lock to serialize concurrent commits.
  // This prevents race conditions where two processes could interleave
  // their git add and git commit operations.
  std::string lockPath = repoPath_ + "/.git/fboss2-commit.lock";
  ScopedFileLock lock(lockPath);

  // COOP ignores generated files by default, including CLI metadata. Force is
  // safe here because callers provide the exact paths to stage; this command
  // never expands a directory or wildcard.
  std::vector<std::string> addArgs = {"add", "--force", "--"};
  for (const auto& file : files) {
    addArgs.push_back(file);
  }
  runGitCommand(addArgs);

  // Build the commit command
  std::vector<std::string> args = {"commit", "-m", message};

  // If author is specified, use --author
  if (!authorName.empty() || !authorEmail.empty()) {
    std::string author = fmt::format(
        "{} <{}>",
        authorName.empty() ? "fboss-cli" : authorName,
        authorEmail.empty() ? "fboss-cli@localhost" : authorEmail);
    args.push_back("--author=" + author);
  }

  runGitCommand(args);

  // Return the SHA of the new commit
  return getHead();
}

std::vector<GitCommit> Git::log(const std::string& filePath, size_t limit)
    const {
  // Use a custom format with null byte separators to parse reliably
  // Format: SHA1, author_name, author_email, timestamp, subject
  // Use %s (subject) instead of %B (body) to get just the first line
  std::vector<std::string> args = {
      "log", "--format=%H%x00%an%x00%ae%x00%at%x00%s%x00", "--", filePath};

  if (limit > 0) {
    args.insert(args.begin() + 1, "-n");
    args.insert(args.begin() + 2, std::to_string(limit));
  }

  auto result = runGitCommandWithStatus(repoPath_, args);
  if (result.exitStatus != 0) {
    // No commits or file not in repo
    return {};
  }

  std::vector<GitCommit> commits;

  // Split output by null characters
  std::vector<folly::StringPiece> parts;
  folly::split('\0', result.stdoutStr, parts);

  // Each commit has 5 fields, so process in groups of 5
  // The last empty part after final \0 is ignored
  for (size_t i = 0; i + 4 < parts.size(); i += 5) {
    GitCommit commit;

    // Trim all fields - git log output can have newlines between commits
    commit.sha1 = folly::trimWhitespace(parts[i]).str();
    commit.authorName = folly::trimWhitespace(parts[i + 1]).str();
    commit.authorEmail = folly::trimWhitespace(parts[i + 2]).str();

    std::string timestampStr = folly::trimWhitespace(parts[i + 3]).str();
    if (timestampStr.empty()) { // Should never happen.
      throw std::runtime_error(
          fmt::format(
              "Git log returned empty timestamp for commit {}", commit.sha1));
    }
    commit.timestamp = std::stoll(timestampStr);

    commit.subject = folly::trimWhitespace(parts[i + 4]).str();

    // Skip empty commits (can happen if there are extra null separators)
    if (commit.sha1.empty()) {
      continue;
    }

    commits.push_back(std::move(commit));
  }

  return commits;
}

std::string Git::fileAtRevision(
    const std::string& revision,
    const std::string& filePath) const {
  // Use git show to get file contents at a specific revision
  std::string ref = fmt::format("{}:{}", revision, filePath);
  return runGitCommand({"show", ref});
}

std::string Git::resolveRef(const std::string& ref) const {
  return runGitCommand({"rev-parse", folly::trimWhitespace(ref).str()});
}

std::string Git::getHead() const {
  auto result = runGitCommandWithStatus(repoPath_, {"rev-parse", "HEAD"});
  if (result.exitStatus != 0) {
    return ""; // No commits yet
  }
  return folly::trimWhitespace(result.stdoutStr).str();
}

bool Git::hasCommits() const {
  // Check if HEAD can be resolved - if not, there are no commits
  auto result = runGitCommandWithStatus(repoPath_, {"rev-parse", "HEAD"});
  return result.exitStatus == 0;
}

std::string Git::runGitCommand(const std::vector<std::string>& args) const {
  auto result = runGitCommandWithStatus(repoPath_, args);
  if (result.exitStatus != 0) {
    std::string cmd = "git";
    for (const auto& arg : args) {
      cmd += " " + arg;
    }
    throw std::runtime_error(
        fmt::format(
            "Git command failed: {} (exit {}): {}",
            cmd,
            result.exitStatus,
            folly::trimWhitespace(result.stderrStr)));
  }
  return folly::trimWhitespace(result.stdoutStr).str();
}

std::string Git::shortSha1(const std::string& sha1) {
  // Return first 8 characters for consistency across the CLI
  // If the string is shorter than 8 characters, return the whole string
  return sha1.substr(0, std::min(sha1.length(), size_t(8)));
}

} // namespace facebook::fboss
