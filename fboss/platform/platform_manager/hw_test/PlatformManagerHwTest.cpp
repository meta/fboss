// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>
#include <filesystem>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include "fboss/lib/ThriftServiceUtils.h"
#include "fboss/platform/helpers/Init.h"
#include "fboss/platform/platform_manager/ConfigUtils.h"
#include "fboss/platform/platform_manager/ExplorationErrors.h"
#include "fboss/platform/platform_manager/PkgManager.h"
#include "fboss/platform/platform_manager/PlatformExplorer.h"
#include "fboss/platform/platform_manager/PlatformManagerHandler.h"
#include "fboss/platform/xcvr_lib/XcvrLib.h"

namespace facebook::fboss::platform::platform_manager {
namespace {
const std::map<ExplorationStatus, std::vector<ExplorationStatus>>
    kExplorationStatusTransitions = {
        {ExplorationStatus::UNSTARTED, {ExplorationStatus::IN_PROGRESS}},
        {ExplorationStatus::IN_PROGRESS,
         {ExplorationStatus::SUCCEEDED,
          ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS,
          ExplorationStatus::FAILED}},
        {ExplorationStatus::SUCCEEDED, {}},
        {ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS, {}},
        {ExplorationStatus::FAILED, {}}};
} // namespace

namespace fs = std::filesystem;

class PlatformExplorerWrapper : public PlatformExplorer {
 public:
  explicit PlatformExplorerWrapper(
      const PlatformConfig& config,
      DataStore& dataStore)
      : PlatformExplorer(config, dataStore) {
    // Store the initial PlatformManagerStatus defined in PlatformExplorer.
    updatedPmStatuses_.push_back(getPMStatus());
  }
  std::vector<PlatformManagerStatus> updatedPmStatuses_;

 protected:
  void updatePmStatus(const PlatformManagerStatus& newStatus) override {
    PlatformExplorer::updatePmStatus(newStatus);
    updatedPmStatuses_.push_back(newStatus);
  }
};
class PlatformManagerHwTest : public ::testing::Test {
 public:
  void SetUp() override {
    thriftHandler_ = std::make_shared<PlatformManagerHandler>(
        platformExplorer_, ds.value(), platformConfig_);
    server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        thriftHandler_,
        facebook::fboss::ThriftServiceUtils::createThriftServerConfig());
    pmClient_ =
        server_->newClient<apache::thrift::Client<PlatformManagerService>>();
  }

  PlatformManagerStatus getPmStatus() {
    PlatformManagerStatus pmStatus;
    pmClient_->sync_getLastPMStatus(pmStatus);
    return pmStatus;
  }
  std::vector<PlatformManagerStatus> getUpdatedPmStatuses() {
    return platformExplorer_.updatedPmStatuses_;
  }
  void explorationOk() {
    auto startTime = std::chrono::system_clock::now();
    pkgManager_.processAll(FLAGS_enable_pkg_mgmnt, FLAGS_reload_kmods);
    platformExplorer_.explore();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - startTime);
    auto platformName = platformConfig_.platformName().value();
    auto maxSetupTime = PlatformExplorer::kMaxSetupTime;
    if (platformName == "MORGAN800CC") {
      maxSetupTime = PlatformExplorer::kMaxSetupTimeViolaters;
    }
    EXPECT_LE(duration, maxSetupTime) << fmt::format(
        "Exploration time {}s exceeded maximum allowed {}s",
        duration.count(),
        maxSetupTime.count());
    auto pmStatus = getPmStatus();
    EXPECT_TRUE(
        *pmStatus.explorationStatus() == ExplorationStatus::SUCCEEDED ||
        *pmStatus.explorationStatus() ==
            ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS)
        << fmt::format(
               "Ended with unexpected exploration status {}",
               apache::thrift::util::enumNameSafe(
                   *pmStatus.explorationStatus()));
    EXPECT_GE(
        *pmStatus.lastExplorationTime(),
        std::chrono::duration_cast<std::chrono::seconds>(
            startTime.time_since_epoch())
            .count());
  }

  PlatformConfig platformConfig_{ConfigUtils().getConfig()};
  fboss::XcvrLib xcvrLib_{platformConfig_};
  DataStore dataStore_{platformConfig_};
  PlatformExplorerWrapper platformExplorer_{platformConfig_, dataStore_};
  PkgManager pkgManager_{platformConfig_};
  std::optional<DataStore> ds =
      platformExplorer_.getDataStore().value_or(DataStore(platformConfig_));

  // Test Thrift Service related members
  std::shared_ptr<PlatformManagerHandler> thriftHandler_;
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
  std::unique_ptr<apache::thrift::Client<PlatformManagerService>> pmClient_;
};

TEST_F(PlatformManagerHwTest, ExploreAsDeployed) {
  explorationOk();
}

TEST_F(PlatformManagerHwTest, ExploreAfterUninstallingKmods) {
  pkgManager_.removeInstalledRpms();
  explorationOk();
}

TEST_F(PlatformManagerHwTest, ExploreAfterUnloadingKmods) {
  pkgManager_.unloadBspKmods();
  explorationOk();
}

TEST_F(PlatformManagerHwTest, PmExplorationStatusTransitions) {
  explorationOk();
  std::optional<ExplorationStatus> currStatus;
  std::vector<ExplorationStatus> nextStatuses = {ExplorationStatus::UNSTARTED};
  auto updatedPmStatuses = getUpdatedPmStatuses();
  EXPECT_FALSE(updatedPmStatuses.empty());
  for (const auto& nextPmStatus : updatedPmStatuses) {
    auto nextExplorationStatus = *nextPmStatus.explorationStatus();
    EXPECT_NE(
        std::find(
            nextStatuses.begin(), nextStatuses.end(), nextExplorationStatus),
        nextStatuses.end())
        << fmt::format(
               "Detected invalid ExplorationStatus transition from {} to {}",
               currStatus ? apache::thrift::util::enumNameSafe(*currStatus)
                          : "<NULL>",
               apache::thrift::util::enumNameSafe(nextExplorationStatus));
    EXPECT_NO_THROW(
        nextStatuses = kExplorationStatusTransitions.at(nextExplorationStatus))
        << fmt::format(
               "Undefined next ExplorationStatus mapping in kExplorationStatusTransitions for {}",
               apache::thrift::util::enumNameSafe(nextExplorationStatus));
    currStatus = nextExplorationStatus;
  }
  EXPECT_TRUE(currStatus.has_value());
  EXPECT_TRUE(
      *currStatus == ExplorationStatus::SUCCEEDED ||
      *currStatus == ExplorationStatus::SUCCEEDED_WITH_EXPECTED_ERRORS)
      << fmt::format(
             "Ended with unexpected exploration status {}",
             apache::thrift::util::enumNameSafe(*currStatus));
}

TEST_F(PlatformManagerHwTest, Symlinks) {
  // /run/devmap is a bind mount in platform_manager's NetOS environment, so it
  // can only be emptied, not removed.
  if (fs::exists("/run/devmap")) {
    for (const auto& entry : fs::directory_iterator("/run/devmap")) {
      fs::remove_all(entry.path());
    }
    EXPECT_TRUE(fs::is_empty("/run/devmap"));
  }
  explorationOk();
  for (const auto& [symlink, devicePath] :
       *platformConfig_.symbolicLinkToDevicePath()) {
    if (isExpectedError(
            platformConfig_,
            ExplorationErrorType::RUN_DEVMAP_SYMLINK,
            devicePath)) {
      continue;
    }
    EXPECT_TRUE(fs::exists(symlink))
        << fmt::format("{} doesn't exist", symlink);
    EXPECT_TRUE(fs::is_symlink(symlink))
        << fmt::format("{} isn't a symlink", symlink);
  }
}

// Verifies the control files expected by qsfp_service are properly created.
TEST_F(PlatformManagerHwTest, XcvrCtrlFiles) {
  fs::remove_all("/run/devmap/xcvrs");
  EXPECT_FALSE(fs::exists("/run/devmap/xcvrs"));
  explorationOk();
  for (auto xcvrId = 1; xcvrId <= xcvrLib_.getNumTransceivers(); xcvrId++) {
    auto presenceFile =
        fs::path(xcvrLib_.getXcvrPresenceSysfsPath(xcvrId).value());
    auto resetFile = fs::path(xcvrLib_.getXcvrResetSysfsPath(xcvrId).value());
    EXPECT_TRUE(fs::exists(presenceFile))
        << fmt::format("{} doesn't exist", presenceFile.string());
    EXPECT_TRUE(fs::exists(resetFile))
        << fmt::format("{} doesn't exist", resetFile.string());
    if (fs::exists(presenceFile)) {
      auto presenceFileContent =
          PlatformFsUtils().getStringFileContent(presenceFile);
      ASSERT_TRUE(presenceFileContent);
      int presenceValue{-1};
      ASSERT_NO_THROW(
          presenceValue = std::stoi(*presenceFileContent, nullptr, 0));
      EXPECT_TRUE(presenceValue == 0 || presenceValue == 1);
    }
  }
}

TEST_F(PlatformManagerHwTest, XcvrIoFiles) {
  fs::remove_all("/run/devmap/xcvrs");
  EXPECT_FALSE(fs::exists("/run/devmap/xcvrs"));
  explorationOk();
  for (auto xcvrId = 1; xcvrId <= xcvrLib_.getNumTransceivers(); xcvrId++) {
    auto xcvrIoPath = fs::path(xcvrLib_.getXcvrIODevicePath(xcvrId).value());
    EXPECT_TRUE(fs::is_character_file(xcvrIoPath))
        << fmt::format("{} isn't a character file", xcvrIoPath.string());
  }
}

TEST_F(PlatformManagerHwTest, XcvrLedFiles) {
  fs::remove_all("/run/devmap/xcvrs");
  EXPECT_FALSE(fs::exists("/run/devmap/xcvrs"));
  explorationOk();

  auto primaryColor = xcvrLib_.getPrimaryLedColor();
  auto secondaryColor = fboss::XcvrLib::LedColor::AMBER;

  for (auto xcvrNum = 1; xcvrNum <= xcvrLib_.getNumTransceivers(); xcvrNum++) {
    auto numLeds = xcvrLib_.getNumLedsForTransceiver(xcvrNum);
    if (!numLeds) {
      continue;
    }
    for (auto ledNum = 1; ledNum <= *numLeds; ledNum++) {
      auto primaryLedDir = fs::path(
          xcvrLib_.getLedSysfsPath(xcvrNum, ledNum, primaryColor).value());
      auto secondaryLedDir = fs::path(
          xcvrLib_.getLedSysfsPath(xcvrNum, ledNum, secondaryColor).value());

      XLOG(DBG2) << fmt::format(
          "Checking {} and {}",
          primaryLedDir.string(),
          secondaryLedDir.string());
      for (auto& ledFile : {"brightness", "max_brightness", "trigger"}) {
        auto primaryLedFullPath = primaryLedDir / fs::path(ledFile);
        auto secondaryLedFullPath = secondaryLedDir / fs::path(ledFile);
        EXPECT_TRUE(fs::exists(primaryLedFullPath))
            << fmt::format("{} doesn't exist", primaryLedFullPath.string());
        EXPECT_TRUE(fs::exists(secondaryLedFullPath))
            << fmt::format("{} doesn't exist", secondaryLedFullPath.string());
      }
    }
  }
}

} // namespace facebook::fboss::platform::platform_manager

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  facebook::fboss::platform::helpers::init(&argc, &argv);
  return RUN_ALL_TESTS();
}
