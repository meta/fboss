/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/LocalHost.h"

#include "fboss/platform/helpers/ExpectSession.h"

#include <algorithm>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

using namespace facebook::fboss::platform::platform_checks;

namespace {

// Exercises the run()-based defaults of Host by not overriding file access.
class ShellOnlyHost : public Host {
 public:
  explicit ShellOnlyHost(
      std::optional<CommandResult> fixedResult = std::nullopt)
      : fixedResult_(std::move(fixedResult)) {}

  std::string name() const override {
    return "shell";
  }
  bool isLocal() const override {
    return true;
  }
  CommandResult run(const std::string& cmd, std::chrono::seconds timeout)
      const override {
    return fixedResult_ ? *fixedResult_ : LocalHost().run(cmd, timeout);
  }
  void copyTo(
      const std::filesystem::path&,
      const std::filesystem::path&,
      std::chrono::seconds) const override {}
  std::unique_ptr<facebook::fboss::platform::ExpectSession> spawnInteractive()
      const override {
    return nullptr;
  }

 private:
  std::optional<CommandResult> fixedResult_;
};

} // namespace

TEST(LocalHostTest, RunCapturesOutputAndExitCode) {
  auto result = LocalHost().run("echo out; echo err >&2; exit 3");

  EXPECT_EQ(result.exitCode, 3);
  EXPECT_EQ(result.standardOut, "out\n");
  EXPECT_EQ(result.standardErr, "err\n");
}

TEST(LocalHostTest, RunTimesOut) {
  auto result = LocalHost().run("sleep 10", std::chrono::seconds(1));

  EXPECT_EQ(result.exitCode, 124);
}

TEST(LocalHostTest, FileAccessIsRelativeToRootDir) {
  folly::test::TemporaryDirectory tmpDir;
  const std::filesystem::path root{tmpDir.path().string()};
  std::filesystem::create_directories(root / "dir");
  folly::writeFile(std::string(" hello\n"), (root / "dir/file").c_str());
  LocalHost host(root);

  EXPECT_EQ(host.readFile("/dir/file"), " hello\n");
  EXPECT_TRUE(host.exists("/dir/file"));
  EXPECT_FALSE(host.exists("/dir/missing"));
  EXPECT_EQ(
      host.listDirectory("/dir"),
      std::vector<std::filesystem::path>{"/dir/file"});
}

TEST(LocalHostTest, CopyTo) {
  folly::test::TemporaryDirectory tmpDir;
  const std::filesystem::path root{tmpDir.path().string()};
  folly::writeFile(std::string("payload"), (root / "src").c_str());

  LocalHost().copyTo(root / "src", root / "dst", std::chrono::seconds(10));

  EXPECT_EQ(LocalHost().readFile(root / "dst"), "payload");
}

TEST(HostTest, DefaultFileAccessUsesShell) {
  folly::test::TemporaryDirectory tmpDir;
  const auto dir =
      std::filesystem::path{tmpDir.path().string()} / "dir with space";
  std::filesystem::create_directories(dir);
  folly::writeFile(std::string(" hello\n"), (dir / "a").c_str());
  folly::writeFile(std::string(), (dir / ".b").c_str());
  folly::writeFile(std::string(), (dir / "new\nline").c_str());
  ShellOnlyHost host;

  EXPECT_EQ(host.readFile(dir / "a"), " hello\n");
  EXPECT_EQ(host.readFile(dir / "missing"), std::nullopt);
  EXPECT_TRUE(host.exists(dir / "a"));
  EXPECT_FALSE(host.exists(dir / "missing"));
  auto entries = host.listDirectory(dir);
  std::sort(entries.begin(), entries.end());
  const std::vector<std::filesystem::path> expected{
      dir / ".b", dir / "a", dir / "new\nline"};
  EXPECT_EQ(entries, expected);
}

TEST(HostTest, FailedListingIsEmpty) {
  folly::test::TemporaryDirectory tmpDir;
  const std::filesystem::path file =
      std::filesystem::path{tmpDir.path().string()} / "file";
  folly::writeFile(std::string("x"), file.c_str());

  EXPECT_TRUE(LocalHost().listDirectory(file).empty());
  EXPECT_TRUE(ShellOnlyHost().listDirectory(file).empty());
  EXPECT_TRUE(LocalHost().listDirectory("/nonexistent").empty());
}

TEST(HostTest, ReadFilesInOneCommand) {
  folly::test::TemporaryDirectory tmpDir;
  const std::filesystem::path dir{tmpDir.path().string()};
  folly::writeFile(std::string("a\nb"), (dir / "text").c_str());
  folly::writeFile(std::string("\0\n\xff", 3), (dir / "binary").c_str());
  folly::writeFile(std::string(), (dir / "empty").c_str());
  const std::vector<std::filesystem::path> paths{
      dir / "text", dir / "missing", dir / "binary", dir / "empty"};
  const std::vector<std::optional<std::string>> expected{
      "a\nb", std::nullopt, std::string("\0\n\xff", 3), ""};

  EXPECT_EQ(ShellOnlyHost().readFiles(paths), expected);
  EXPECT_EQ(LocalHost().readFiles(paths), expected);
}

TEST(HostTest, ReadFilesStopsAtCorruptOutput) {
  // "-1" marks an unreadable file; "99" is more than the rest of the output.
  ShellOnlyHost host(
      CommandResult{.exitCode = 0, .standardOut = "3\nabc-1\n2\nxy99\nzz"});
  const std::vector<std::filesystem::path> paths{"a", "b", "c", "d", "e"};
  const std::vector<std::optional<std::string>> expected{
      "abc", std::nullopt, "xy", std::nullopt, std::nullopt};

  EXPECT_EQ(host.readFiles(paths), expected);
}

TEST(HostTest, UnreachableHostIsNotAMissingFile) {
  // What RemoteHost returns when the connection drops.
  ShellOnlyHost host(CommandResult{.exitCode = 255, .standardErr = "gone"});

  EXPECT_THROW(host.exists("/etc"), HostCommandError);
  EXPECT_THROW(host.readFile("/etc/hostname"), HostCommandError);
  EXPECT_THROW(host.listDirectory("/etc"), HostCommandError);
  EXPECT_THROW(host.readFiles({"/etc/hostname"}), HostCommandError);
}
