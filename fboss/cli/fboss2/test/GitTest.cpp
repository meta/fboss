/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/filesystem/operations.hpp>
#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <gtest/gtest.h>
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include "fboss/cli/fboss2/session/Git.h"

namespace fs = std::filesystem;

namespace facebook::fboss {

class GitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temporary directory for each test
    auto tempBase = fs::temp_directory_path();
    auto uniquePath =
        boost::filesystem::unique_path("git_test_%%%%-%%%%-%%%%-%%%%");
    testRepoPath_ = tempBase / uniquePath.string();
    fs::create_directories(testRepoPath_);
  }

  void TearDown() override {
    // Clean up test directory
    std::error_code ec;
    if (fs::exists(testRepoPath_)) {
      fs::remove_all(testRepoPath_, ec);
    }
  }

  void writeFile(const std::string& filename, const std::string& content) {
    if (!folly::writeFile(content, (testRepoPath_ / filename).c_str())) {
      throw std::runtime_error(
          fmt::format(
              "Failed to write file {}: {}", filename, folly::errnoStr(errno)));
    }
  }

  std::string readFile(const std::string& filename) {
    std::string content;
    if (!folly::readFile((testRepoPath_ / filename).c_str(), content)) {
      throw std::runtime_error(
          fmt::format(
              "Failed to read file {}: {}", filename, folly::errnoStr(errno)));
    }
    return content;
  }

  fs::path testRepoPath_;
};

TEST_F(GitTest, BasicOperations) {
  Git git(testRepoPath_.string());

  // Before init
  EXPECT_FALSE(git.isRepository());

  // After init
  git.init();
  EXPECT_TRUE(git.isRepository());
  EXPECT_TRUE(fs::exists(testRepoPath_ / ".git"));
  EXPECT_FALSE(git.hasCommits());
  EXPECT_TRUE(git.getHead().empty());

  // First commit
  writeFile("config.txt", "version 1");
  std::string sha1First =
      git.commit({"config.txt"}, "Version 1", "User", "user@test.com");
  EXPECT_FALSE(sha1First.empty());
  EXPECT_EQ(40, sha1First.length()); // SHA1 is 40 hex characters
  EXPECT_TRUE(git.hasCommits());
  EXPECT_EQ(sha1First, git.getHead());

  // Check log after first commit
  auto commits = git.log("config.txt");
  ASSERT_EQ(1, commits.size());
  EXPECT_EQ(sha1First, commits[0].sha1);
  EXPECT_EQ("User", commits[0].authorName);
  EXPECT_EQ("user@test.com", commits[0].authorEmail);
  EXPECT_EQ("Version 1", commits[0].subject);
  EXPECT_GT(commits[0].timestamp, 0);

  // Second commit - modify the same file
  writeFile("config.txt", "version 2");
  std::string sha1Second =
      git.commit({"config.txt"}, "Version 2", "User", "user@test.com");
  EXPECT_EQ(sha1Second, git.getHead());

  // Check log with no limit (should return both commits, most recent first)
  commits = git.log("config.txt");
  ASSERT_EQ(2, commits.size());
  EXPECT_EQ(sha1Second, commits[0].sha1);
  EXPECT_EQ(sha1First, commits[1].sha1);

  // Check log with limit of 1 (should return only most recent)
  auto limitedCommits = git.log("config.txt", 1);
  ASSERT_EQ(1, limitedCommits.size());
  EXPECT_EQ(sha1Second, limitedCommits[0].sha1);

  // Retrieve file content from first commit
  std::string contentAtFirst = git.fileAtRevision(sha1First, "config.txt");
  EXPECT_EQ("version 1", contentAtFirst);

  // Retrieve file content from second commit
  std::string contentAtSecond = git.fileAtRevision(sha1Second, "config.txt");
  EXPECT_EQ("version 2", contentAtSecond);
}

TEST_F(GitTest, CommitExplicitlyIgnoredFile) {
  Git git(testRepoPath_.string());
  git.init();

  writeFile(".gitignore", "*\n!*/\n!*.conf\n");
  fs::create_directories(testRepoPath_ / "cli");
  writeFile("cli/cli_metadata.json", "metadata");

  const auto sha = git.commit(
      {"cli/cli_metadata.json"}, "Add metadata", "User", "user@test.com");

  EXPECT_EQ("metadata", git.fileAtRevision(sha, "cli/cli_metadata.json"));
}

} // namespace facebook::fboss
