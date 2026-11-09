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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace facebook::fboss {

/**
 * Represents a single commit in the Git log.
 */
struct GitCommit {
  std::string sha1; // Full 40-character SHA1 hash
  std::string authorName;
  std::string authorEmail;
  int64_t timestamp{}; // Unix timestamp in seconds
  std::string subject; // First line of commit message
};

/**
 * Git provides a simple interface for Git operations in a local repository.
 *
 * This class is designed to be:
 * 1. Easy to use for common operations like init, commit, log, and show
 * 2. Easy to mock for unit tests
 * 3. Thread-safe for concurrent usage (all operations are atomic)
 *
 * The commit() method uses git add followed by git commit. This two-step
 * process is required to handle both tracked and untracked files, as git
 * commit --include only works for already-tracked files.
 */
class Git {
 public:
  /**
   * Create a Git instance for the specified repository path.
   * @param repoPath The path to the Git repository (or where to create one)
   */
  explicit Git(std::string repoPath);
  ~Git() = default;

  // Non-copyable and non-movable
  Git(const Git&) = delete;
  Git& operator=(const Git&) = delete;
  Git(Git&&) = delete;
  Git& operator=(Git&&) = delete;

  /**
   * Get the repository path.
   */
  std::string getRepoPath() const;

  /**
   * Check if the repository path is already a Git repository.
   */
  bool isRepository() const;

  /**
   * Initialize a new Git repository if one doesn't exist.
   * If the repository already exists, this is a no-op.
   * @param initialBranch The name of the initial branch (default: "main")
   */
  void init(const std::string& initialBranch = "main");

  /**
   * Commit the specified files with the given message.
   * Explicitly stages every path, including paths matched by .gitignore.
   *
   * @param files List of file paths within the repository to commit
   * @param message Commit message
   * @param authorName Author name (optional, uses system default if empty)
   * @param authorEmail Author email (optional, uses system default if empty)
   * @return The SHA1 of the new commit
   * @throws std::runtime_error if the commit fails
   */
  std::string commit(
      const std::vector<std::string>& files,
      const std::string& message,
      const std::string& authorName = "",
      const std::string& authorEmail = "");

  /**
   * Get the commit log for a specific file, optionally limited to N entries.
   *
   * @param filePath Path to the file (relative to repo root)
   * @param limit Maximum number of commits to return (0 = no limit)
   * @return Vector of GitCommit objects, most recent first
   */
  std::vector<GitCommit> log(const std::string& filePath, size_t limit = 0)
      const;

  /**
   * Get the contents of a file at a specific revision.
   *
   * @param revision A Git revision: commit SHA (full or abbreviated),
   *                 branch name, tag name, or other git syntax (e.g. HEAD~4)
   * @param filePath Path to the file (relative to repo root)
   * @return The file contents at that revision
   * @throws std::runtime_error if the file or revision doesn't exist
   */
  std::string fileAtRevision(
      const std::string& revision,
      const std::string& filePath) const;

  /**
   * Get the full SHA for a commit reference (SHA, HEAD, branch name, etc).
   *
   * @param ref Git reference (commit SHA, HEAD, branch name, etc)
   * @return Full SHA of the commit
   * @throws std::runtime_error if the reference is invalid
   */
  std::string resolveRef(const std::string& ref) const;

  /**
   * Get the current HEAD commit SHA.
   * @return Full SHA of HEAD, or empty string if repo has no commits
   */
  std::string getHead() const;

  /**
   * Check if the repository has any commits.
   * @return true if the repository has at least one commit, false otherwise
   */
  bool hasCommits() const;

  /**
   * Get a short (8-character) version of a SHA1 hash for display purposes.
   * This provides a consistent way to abbreviate commit hashes across the CLI.
   *
   * @param sha1 Full or partial SHA1 hash
   * @return First 8 characters of the SHA1, or the full string if shorter
   */
  static std::string shortSha1(const std::string& sha1);

 private:
  /**
   * Execute a git command and return its output.
   * @param args Arguments to pass to git (not including 'git' itself)
   * @return The stdout output of the command
   * @throws std::runtime_error if the command fails
   */
  std::string runGitCommand(const std::vector<std::string>& args) const;

  std::string repoPath_;
};

} // namespace facebook::fboss
