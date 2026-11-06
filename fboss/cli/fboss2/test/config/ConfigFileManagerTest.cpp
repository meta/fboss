// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/session/ConfigFileManager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <boost/filesystem/operations.hpp>
#include <folly/FileUtil.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace facebook::fboss {

class ConfigFileManagerTest : public ::testing::Test {
 protected:
  struct Metadata {
    mode_t mode;
    uid_t uid;

    bool operator==(const Metadata&) const = default;
  };

  void SetUp() override {
    root_ = makeTempPath("config_file_manager_%%%%-%%%%");
    runtimeRoot_ = makeTempPath("config_file_manager_runtime_%%%%-%%%%");
    fs::create_directories(root_);
    fs::create_directories(runtimeRoot_);
  }

  void TearDown() override {
    std::error_code error;
    fs::remove_all(root_, error);
    fs::remove_all(runtimeRoot_, error);
  }

  static fs::path makeTempPath(const std::string& pattern) {
    return fs::temp_directory_path() /
        boost::filesystem::unique_path(pattern).string();
  }

  void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    ASSERT_TRUE(folly::writeFile(content, path.c_str()));
  }

  std::string readFile(const fs::path& path) {
    std::string content;
    EXPECT_TRUE(folly::readFile(path.c_str(), content));
    return content;
  }

  Metadata metadata(const fs::path& path) {
    struct stat status{};
    EXPECT_EQ(::lstat(path.c_str(), &status), 0);
    return {static_cast<mode_t>(status.st_mode & 07777), status.st_uid};
  }

  ConfigFileManager manager(const fs::path& desired, const fs::path& current)
      const {
    return ConfigFileManager(
        {root_.string(), desired.string(), current.string()});
  }

  fs::path root_;
  fs::path runtimeRoot_;
};

TEST_F(ConfigFileManagerTest, AppliesAndRestoresRegularFiles) {
  const auto desired = root_ / "cli/agent.conf";
  const auto current = root_ / "agent/current";
  writeFile(desired, "old desired");
  writeFile(current, "old current");
  const auto fileManager = manager(desired, current);
  EXPECT_NO_THROW(fileManager.validateWritable());
  const auto snapshot = fileManager.capture();

  EXPECT_EQ(
      fileManager.apply(snapshot, "new config"),
      (std::vector<std::string>{desired.string(), current.string()}));
  EXPECT_EQ(readFile(desired), "new config");
  EXPECT_EQ(readFile(current), "new config");
  const Metadata expectedMetadata{0644, geteuid()};
  EXPECT_EQ(metadata(desired), expectedMetadata);
  EXPECT_EQ(metadata(current), expectedMetadata);

  EXPECT_TRUE(fileManager.restore(snapshot).empty());
  EXPECT_EQ(readFile(desired), "old desired");
  EXPECT_EQ(readFile(current), "old current");
  EXPECT_EQ(metadata(desired), expectedMetadata);
  EXPECT_EQ(metadata(current), expectedMetadata);
}

TEST_F(ConfigFileManagerTest, ResolvesExternalLinkAndRestoresCurrentLink) {
  const auto desired = root_ / "cli/bgpcpp.conf";
  const auto generated = root_ / "generated/bgpcpp.conf";
  const auto current = root_ / "bgpcpp/current";
  const auto runtime = runtimeRoot_ / "bgpcpp_startup_config";
  writeFile(desired, "old desired");
  writeFile(generated, "old current");
  fs::create_directories(current.parent_path());
  const auto oldTarget = generated.lexically_relative(current.parent_path());
  const auto desiredTarget = desired.lexically_relative(current.parent_path());
  fs::create_symlink(oldTarget, current);
  fs::create_symlink(current, runtime);
  const auto fileManager = manager(desired, runtime);
  const auto snapshot = fileManager.capture();

  EXPECT_EQ(fileManager.currentPath(), current.string());
  EXPECT_EQ(
      fileManager.apply(snapshot, "new config"),
      (std::vector<std::string>{desired.string(), current.string()}));
  EXPECT_EQ(fs::read_symlink(runtime), current);
  EXPECT_EQ(fs::read_symlink(current), desiredTarget);
  EXPECT_EQ(readFile(runtime), "new config");

  EXPECT_TRUE(fileManager.restore(snapshot).empty());
  EXPECT_EQ(fs::read_symlink(runtime), current);
  EXPECT_EQ(fs::read_symlink(current), oldTarget);
  EXPECT_EQ(readFile(runtime), "old current");
}

TEST_F(ConfigFileManagerTest, CreatesLinkForMissingCurrentAndRestoresAbsence) {
  const auto desired = root_ / "cli/agent.conf";
  const auto current = root_ / "agent/current";
  fs::create_directories(desired.parent_path());
  fs::create_directories(current.parent_path());
  const auto fileManager = manager(desired, current);
  const auto snapshot = fileManager.capture();

  EXPECT_EQ(
      fileManager.apply(snapshot, "new config"),
      (std::vector<std::string>{desired.string(), current.string()}));
  EXPECT_TRUE(fs::is_regular_file(desired));
  EXPECT_TRUE(fs::is_symlink(current));
  EXPECT_EQ(metadata(desired), (Metadata{0644, geteuid()}));
  EXPECT_EQ(
      fs::read_symlink(current),
      desired.lexically_relative(current.parent_path()));
  EXPECT_EQ(readFile(current), "new config");

  EXPECT_TRUE(fileManager.restore(snapshot).empty());
  EXPECT_FALSE(fs::exists(desired));
  EXPECT_FALSE(fs::exists(current));
}

TEST_F(ConfigFileManagerTest, HandlesSameDesiredAndCurrentPathOnce) {
  const auto config = root_ / "agent.conf";
  writeFile(config, "old config");
  const auto fileManager = manager(config, config);
  const auto snapshot = fileManager.capture();

  EXPECT_EQ(
      fileManager.apply(snapshot, "new config"),
      (std::vector<std::string>{config.string()}));
  EXPECT_TRUE(fileManager.restore(snapshot).empty());
  EXPECT_EQ(readFile(config), "old config");

  const auto restored = fileManager.capture();
  EXPECT_TRUE(fileManager.apply(restored, "old config").empty());
}

TEST_F(ConfigFileManagerTest, RejectsCurrentPathsOutsideSystemConfigDir) {
  const auto desired = root_ / "cli/agent.conf";
  const auto outside = runtimeRoot_ / "agent.conf";
  const auto outsideLink = runtimeRoot_ / "agent_link";
  writeFile(desired, "desired");
  writeFile(outside, "outside");

  EXPECT_THROW(manager(desired, outside), std::runtime_error);

  fs::create_symlink(outside.filename(), outsideLink);
  EXPECT_THROW(manager(desired, outsideLink), std::runtime_error);
}

TEST_F(ConfigFileManagerTest, RestoreContinuesAfterOnePathFails) {
  const auto desired = root_ / "cli/agent.conf";
  const auto current = root_ / "agent/current";
  writeFile(desired, "old desired");
  writeFile(current, "old current");
  const auto fileManager = manager(desired, current);
  const auto snapshot = fileManager.capture();
  fileManager.apply(snapshot, "new config");

  ASSERT_TRUE(fs::remove(desired));
  ASSERT_TRUE(fs::create_directory(desired));
  const auto errors = fileManager.restore(snapshot);

  EXPECT_EQ(errors.size(), 1);
  EXPECT_EQ(readFile(current), "old current");
}

} // namespace facebook::fboss
