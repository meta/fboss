// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fb303/ServiceData.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/platform_manager/PkgManager.h"

using namespace ::testing;
namespace facebook::fboss::platform::platform_manager {
class MockSystemInterface : public package_manager::SystemInterface {
 public:
  explicit MockSystemInterface() : package_manager::SystemInterface() {}
  MOCK_METHOD(bool, isRpmInstalled, (const std::string&), (const));
  MOCK_METHOD(
      std::vector<std::string>,
      getInstalledRpms,
      (const std::string&),
      (const));
  MOCK_METHOD(int, removeRpms, (const std::vector<std::string>&), (const));
  MOCK_METHOD(
      package_manager::RpmInstallResult,
      installRpm,
      (const std::string&, const std::string&),
      (const));
  MOCK_METHOD(
      std::vector<std::string>,
      getLocalRepoBspRpmVersions,
      (const std::string&),
      (const));
  MOCK_METHOD(int, depmod, (), (const));
  MOCK_METHOD(std::set<std::string>, lsmod, (), (const));
  MOCK_METHOD(bool, unloadKmod, (const std::string&), (const));
  MOCK_METHOD(bool, loadKmod, (const std::string&), (const));
  MOCK_METHOD(std::string, getHostKernelVersion, (), (const));
};
class MockPkgManager : public PkgManager {
 public:
  explicit MockPkgManager(
      const PlatformConfig& config,
      const std::shared_ptr<package_manager::SystemInterface>& systemInterface)
      : PkgManager(config, systemInterface) {}
  MOCK_METHOD(void, processRpms, (), (const));
  MOCK_METHOD(void, processLocalRpms, (), (const));
  MOCK_METHOD(void, unloadBspKmods, (), (const));
  MOCK_METHOD(void, loadRequiredKmods, (), (const));
};
class MockPlatformFsUtils : public PlatformFsUtils {
 public:
  MOCK_METHOD(
      std::optional<std::string>,
      getStringFileContent,
      (const std::filesystem::path& path),
      (const));
};

class PkgManagerTest : public testing::Test {
 public:
  void SetUp() override {
    FLAGS_local_rpm_path = "";
    // Keep unit tests fast and deterministic: small retry count, no real sleep.
    FLAGS_kmod_unload_retries = 3;
    FLAGS_kmod_unload_retry_backoff_s = 0;
    platformConfig_.bspKmodsRpmName() = "fboss_bsp_kmods";
    platformConfig_.bspKmodsRpmVersion() = "11.44.63-14";
    platformConfig_.nonBspKmodsToLoad() = {"fboss_iob_pci", "spidev"};

    bspKmodsFile_.bspKmods() = {"fboss_iob_pci", "fboss_iob_spi"};
    bspKmodsFile_.sharedKmods() = {"scd"};
    jsonBspKmodsFile_ =
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(
            bspKmodsFile_);
  }
  std::string jsonBspKmodsFile_;
  BspKmodsFile bspKmodsFile_;
  PlatformConfig platformConfig_;
  std::shared_ptr<MockSystemInterface> mockSystemInterface_{
      std::make_shared<MockSystemInterface>()};
  std::shared_ptr<MockPlatformFsUtils> mockPlatformFsUtils_{
      std::make_shared<MockPlatformFsUtils>()};
  // For testing high level flow in PkgManager::processAll
  MockPkgManager mockPkgManager_{platformConfig_, mockSystemInterface_};
  // For testing individual member functions such as PkgManager::unloadBspKmods
  PkgManager pkgManager_{
      platformConfig_,
      mockSystemInterface_,
      mockPlatformFsUtils_};
};

TEST_F(PkgManagerTest, EnablePkgMgmnt) {
  EXPECT_CALL(mockPkgManager_, processLocalRpms()).Times(0);
  // Case 1: When new rpm installed. processRpms() unloads kmods internally (via
  // removeInstalledRpms), then processAll re-attempts the unload before
  // loading.
  {
    InSequence seq;
    EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_))
        .WillOnce(Return(false));
    EXPECT_CALL(mockPkgManager_, processRpms()).Times(1);
    EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(1);
    EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  }
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/true, /*reloadKmods=*/false));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
  // Case 2: When rpm is already installed
  {
    InSequence seq;
    EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_))
        .WillOnce(Return(true));
    EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  }
  EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(0);
  EXPECT_CALL(mockPkgManager_, processRpms()).Times(0);
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/true, /*reloadKmods=*/false));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
}

TEST_F(PkgManagerTest, EnablePkgMgmntWithReloadKmods) {
  EXPECT_CALL(mockPkgManager_, processLocalRpms()).Times(0);
  // Case 1: When new rpm installed. reloadKmods has no effect here because the
  // BSP-management branch returns early after processRpms()/unloadBspKmods().
  {
    InSequence seq;
    EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_))
        .WillOnce(Return(false));
    EXPECT_CALL(mockPkgManager_, processRpms()).Times(1);
    EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(1);
    EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  }
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/true, /*reloadKmods=*/true));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
  // Case 2: When rpm is already installed and still expect to unload kmods
  // once because reloadKmods is true.
  {
    InSequence seq;
    EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_))
        .WillOnce(Return(true));
    EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(1);
    EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  }
  EXPECT_CALL(mockPkgManager_, processRpms()).Times(0);
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/true, /*reloadKmods=*/true));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
}

TEST_F(PkgManagerTest, DisablePkgMgmnt) {
  EXPECT_CALL(mockPkgManager_, processLocalRpms()).Times(0);
  EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_)).Times(0);
  EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(0);
  EXPECT_CALL(mockPkgManager_, processRpms()).Times(0);
  EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/false, /*reloadKmods=*/false));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
}

TEST_F(PkgManagerTest, DisablePkgMgmntWithReloadKmods) {
  EXPECT_CALL(mockPkgManager_, processLocalRpms()).Times(0);
  EXPECT_CALL(*mockSystemInterface_, isRpmInstalled(_)).Times(0);
  EXPECT_CALL(mockPkgManager_, processRpms()).Times(0);
  {
    InSequence seq;
    EXPECT_CALL(mockPkgManager_, unloadBspKmods()).Times(1);
    EXPECT_CALL(mockPkgManager_, loadRequiredKmods()).Times(1);
  }
  EXPECT_NO_THROW(mockPkgManager_.processAll(
      /*enablePkgMgmnt=*/false, /*reloadKmods=*/true));
  EXPECT_GE(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessAllTime), 0);
}

TEST_F(PkgManagerTest, processRpms) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  // removeInstalledRpms() now unloads BSP kmods first. Present a kmods.json
  // with nothing currently loaded so the embedded unloadBspKmods() is a no-op
  // here.
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillRepeatedly(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillRepeatedly(Return(std::set<std::string>{}));
  // No installed rpms
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*mockSystemInterface_, removeRpms(_)).Times(0);
  EXPECT_CALL(
      *mockSystemInterface_,
      installRpm(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d-{}",
              *platformConfig_.bspKmodsRpmName(),
              *platformConfig_.bspKmodsRpmVersion()),
          "kernel"))
      .WillOnce(Return(package_manager::RpmInstallResult{}));
  EXPECT_CALL(*mockSystemInterface_, depmod()).WillOnce(Return(0));
  EXPECT_NO_THROW(pkgManager_.processRpms());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 0);
  // Installed rpms
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}));
  EXPECT_CALL(
      *mockSystemInterface_,
      removeRpms(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}))
      .WillOnce(Return(0));
  EXPECT_CALL(
      *mockSystemInterface_,
      installRpm(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d-{}",
              *platformConfig_.bspKmodsRpmName(),
              *platformConfig_.bspKmodsRpmVersion()),
          "kernel"))
      .WillOnce(Return(package_manager::RpmInstallResult{}));
  EXPECT_CALL(*mockSystemInterface_, depmod()).WillOnce(Return(0));
  EXPECT_NO_THROW(pkgManager_.processRpms());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 0);
  // Remove installed rpms failed.
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}));
  EXPECT_CALL(
      *mockSystemInterface_,
      removeRpms(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}))
      .WillOnce(Return(1));
  EXPECT_THROW(pkgManager_.processRpms(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 1);
  // depmod failed
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *mockSystemInterface_,
      removeRpms(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}))
      .Times(0);
  EXPECT_CALL(
      *mockSystemInterface_,
      installRpm(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d-{}",
              *platformConfig_.bspKmodsRpmName(),
              *platformConfig_.bspKmodsRpmVersion()),
          "kernel"))
      .WillOnce(Return(package_manager::RpmInstallResult{}));
  EXPECT_CALL(*mockSystemInterface_, depmod()).WillOnce(Return(1));
  EXPECT_NO_THROW(pkgManager_.processRpms());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 0);
  // Rpm install failed
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *mockSystemInterface_,
      removeRpms(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}))
      .Times(0);
  EXPECT_CALL(
      *mockSystemInterface_,
      installRpm(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d-{}",
              *platformConfig_.bspKmodsRpmName(),
              *platformConfig_.bspKmodsRpmVersion()),
          "kernel"))
      .Times(3)
      .WillRepeatedly(Return(package_manager::RpmInstallResult{1, false}));
  EXPECT_THROW(pkgManager_.processRpms(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 1);
}

TEST_F(PkgManagerTest, unloadBspKmods) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  // No kmods.json and no old rpms
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*mockSystemInterface_, lsmod()).Times(0);
  EXPECT_FALSE(pkgManager_.wereKmodsUnloaded());
  EXPECT_NO_THROW(pkgManager_.unloadBspKmods());
  EXPECT_FALSE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 0);

  // No kmods.json when it should exist
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(
      *mockSystemInterface_,
      getInstalledRpms(
          fmt::format(
              "{}-6.4.3-0_fbk1_755_ga25447393a1d",
              *platformConfig_.bspKmodsRpmName())))
      .WillOnce(Return(
          std::vector<std::string>{
              "fboss_bsp_kmods-6.4.3-0_fbk1_755_ga25447393a1d-2.4.0-1"}));
  EXPECT_THROW(pkgManager_.unloadBspKmods(), std::runtime_error);
  EXPECT_FALSE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 1);

  // kmods.json exist and all kmods are loaded
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillOnce(Return(
          ranges::views::concat(
              *bspKmodsFile_.bspKmods(), *bspKmodsFile_.sharedKmods()) |
          ranges::to<std::set<std::string>>));
  EXPECT_CALL(*mockSystemInterface_, unloadKmod(_))
      .Times(
          bspKmodsFile_.sharedKmods()->size() +
          bspKmodsFile_.bspKmods()->size())
      .WillRepeatedly(Return(true));
  EXPECT_NO_THROW(pkgManager_.unloadBspKmods());
  EXPECT_TRUE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 0);

  // kmods.json exists but unload keeps failing. The whole unload pass is
  // retried FLAGS_kmod_unload_retries times -- re-reading lsmod each pass --
  // before giving up and throwing.
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .Times(FLAGS_kmod_unload_retries)
      .WillRepeatedly(Return(
          ranges::views::concat(
              *bspKmodsFile_.bspKmods(), *bspKmodsFile_.sharedKmods()) |
          ranges::to<std::set<std::string>>));
  EXPECT_CALL(*mockSystemInterface_, unloadKmod(_))
      .Times(FLAGS_kmod_unload_retries)
      .WillRepeatedly(Return(false));
  EXPECT_THROW(pkgManager_.unloadBspKmods(), std::runtime_error);
  EXPECT_TRUE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 1);

  // kmods.json exist and all kmods aren't loaded
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillOnce(Return(std::set<std::string>{}));
  EXPECT_CALL(*mockSystemInterface_, unloadKmod(_)).Times(0);
  EXPECT_NO_THROW(pkgManager_.unloadBspKmods());
  EXPECT_TRUE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 0);
}

TEST_F(PkgManagerTest, unloadBspKmodsRecoversAfterRetry) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  // Only scd is loaded, so it's the only kmod the unload pass attempts. lsmod
  // is re-read on each retry pass.
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillRepeatedly(Return(std::set<std::string>{"scd"}));
  // Fails twice, then succeeds on the third pass -- no throw.
  EXPECT_CALL(*mockSystemInterface_, unloadKmod("scd"))
      .WillOnce(Return(false))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_NO_THROW(pkgManager_.unloadBspKmods());
  EXPECT_TRUE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 0);
}

TEST_F(PkgManagerTest, unloadBspKmodsExhaustsRetries) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .Times(FLAGS_kmod_unload_retries)
      .WillRepeatedly(Return(std::set<std::string>{"scd"}));
  // Never succeeds: the whole pass is retried the configured number of times,
  // then throws.
  EXPECT_CALL(*mockSystemInterface_, unloadKmod("scd"))
      .Times(FLAGS_kmod_unload_retries)
      .WillRepeatedly(Return(false));
  EXPECT_THROW(pkgManager_.unloadBspKmods(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 1);
}

TEST_F(PkgManagerTest, unloadBspKmodsAttemptsOnceWhenRetriesNonPositive) {
  // A misconfigured retries flag (<= 0) must still make one real unload
  // attempt.
  FLAGS_kmod_unload_retries = 0;
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillOnce(Return(std::set<std::string>{"scd"}));
  EXPECT_CALL(*mockSystemInterface_, unloadKmod("scd")).WillOnce(Return(true));
  EXPECT_NO_THROW(pkgManager_.unloadBspKmods());
  EXPECT_TRUE(pkgManager_.wereKmodsUnloaded());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kUnloadKmodsFailure), 0);
}

TEST_F(PkgManagerTest, loadRequiredKmods) {
  // The required (bootstrap) kmods are loaded first in config order, then
  // every kmod from kmods.json -- shared kmods before bsp kmods.
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillRepeatedly(Return(jsonBspKmodsFile_));
  {
    InSequence seq;
    for (const auto& kmod : *platformConfig_.nonBspKmodsToLoad()) {
      EXPECT_CALL(*mockSystemInterface_, loadKmod(kmod)).WillOnce(Return(true));
    }
    for (const auto& kmod : ranges::views::concat(
             *bspKmodsFile_.sharedKmods(), *bspKmodsFile_.bspKmods())) {
      EXPECT_CALL(*mockSystemInterface_, loadKmod(kmod)).WillOnce(Return(true));
    }
  }
  EXPECT_NO_THROW(pkgManager_.loadRequiredKmods());
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kLoadKmodsFailure), 0);
}

TEST_F(PkgManagerTest, loadRequiredKmodsFailsWhenKmodsFileAbsent) {
  // The required (bootstrap) kmods succeed, then reading kmods.json fails
  // (getStringFileContent returns nullopt) -- loadBspKmods throws via
  // readKmodsFile.
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*mockSystemInterface_, loadKmod(_))
      .Times(static_cast<int>(platformConfig_.nonBspKmodsToLoad()->size()))
      .WillRepeatedly(Return(true));
  EXPECT_THROW(pkgManager_.loadRequiredKmods(), std::runtime_error);
}

TEST_F(PkgManagerTest, loadRequiredKmodsFailsOnRequiredKmod) {
  // A failed required kmod aborts before kmods.json is ever looked up.
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_)).Times(0);
  EXPECT_CALL(*mockSystemInterface_, loadKmod(_)).WillOnce(Return(false));
  EXPECT_THROW(pkgManager_.loadRequiredKmods(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kLoadKmodsFailure), 1);
}

TEST_F(PkgManagerTest, loadRequiredKmodsFailsOnBspKmod) {
  // Required kmods succeed, then the first kmod from kmods.json fails -> throw.
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillRepeatedly(Return(jsonBspKmodsFile_));
  {
    InSequence seq;
    for (const auto& kmod : *platformConfig_.nonBspKmodsToLoad()) {
      EXPECT_CALL(*mockSystemInterface_, loadKmod(kmod)).WillOnce(Return(true));
    }
    // First kmod from kmods.json (shared) fails; later ones are not attempted.
    EXPECT_CALL(
        *mockSystemInterface_, loadKmod((*bspKmodsFile_.sharedKmods())[0]))
        .WillOnce(Return(false));
  }
  EXPECT_THROW(pkgManager_.loadRequiredKmods(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kLoadKmodsFailure), 1);
}

TEST_F(PkgManagerTest, loadRequiredKmodsToleratesAllowlistedKmodFailure) {
  // aadm1266 is listed in kmods.json but is absent from the BSP RPM on kernels
  // without CONFIG_CRC8 (e.g. 6.4.3-0_fbk1). Its load failure must be tolerated
  // so platform_manager still comes up, and later kmods are still attempted.
  BspKmodsFile kmodsFile;
  kmodsFile.sharedKmods() = {"scd"};
  kmodsFile.bspKmods() = {"aadm1266", "amax20830"};
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillRepeatedly(Return(
          apache::thrift::SimpleJSONSerializer::serialize<std::string>(
              kmodsFile)));
  EXPECT_CALL(*mockSystemInterface_, loadKmod(_)).WillRepeatedly(Return(true));
  // aadm1266 fails, but the failure is tolerated (not rethrown) and the
  // subsequent kmod is still attempted.
  EXPECT_CALL(*mockSystemInterface_, loadKmod("aadm1266"))
      .WillOnce(Return(false));
  EXPECT_CALL(*mockSystemInterface_, loadKmod("amax20830"))
      .WillOnce(Return(true));
  EXPECT_NO_THROW(pkgManager_.loadRequiredKmods());
  // The tolerated failure does not throw but is still recorded in the counter.
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kLoadKmodsFailure), 1);
}

TEST_F(PkgManagerTest, ReadKmodsFileSuccess) {
  const std::string kernelVersion = "6.4.3-0_fbk1_755_ga25447393a1d";
  const std::filesystem::path expectedFilePath =
      "/usr/local/fboss_bsp/6.4.3-0_fbk1_755_ga25447393a1d/kmods.json";

  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillOnce(Return(kernelVersion));

  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(expectedFilePath))
      .WillOnce(Return(jsonBspKmodsFile_));

  BspKmodsFile result = pkgManager_.readKmodsFile();

  EXPECT_EQ(result.bspKmods()->size(), bspKmodsFile_.bspKmods()->size());
  EXPECT_EQ(result.sharedKmods()->size(), bspKmodsFile_.sharedKmods()->size());

  for (size_t i = 0; i < result.bspKmods()->size(); i++) {
    EXPECT_EQ((*result.bspKmods())[i], (*bspKmodsFile_.bspKmods())[i]);
  }

  for (size_t i = 0; i < result.sharedKmods()->size(); i++) {
    EXPECT_EQ((*result.sharedKmods())[i], (*bspKmodsFile_.sharedKmods())[i]);
  }
}

TEST_F(PkgManagerTest, ReadKmodsFileFailure) {
  const std::string kernelVersion = "6.4.3-0_fbk1_755_ga25447393a1d";
  const std::filesystem::path expectedFilePath =
      "/usr/local/fboss_bsp/6.4.3-0_fbk1_755_ga25447393a1d/kmods.json";

  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillOnce(Return(kernelVersion));

  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(expectedFilePath))
      .WillOnce(Return(std::nullopt));

  EXPECT_THROW(pkgManager_.readKmodsFile(), std::runtime_error);
}

TEST_F(PkgManagerTest, isValidRpm) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.11.1-0_fbk3_647_gc1af76fcc8cb"));

  // RPM with valide kernel version
  FLAGS_local_rpm_path =
      "/path/to/fboss_bsp_kmods-6.11.1-0_fbk3_647_gc1af76fcc8cb-3.3.0-1.x86_64.rpm";
  EXPECT_TRUE(mockPkgManager_.isValidRpm());

  // RPM with invalid kernel version
  FLAGS_local_rpm_path =
      "/path/to/fboss_bsp_kmods-6.11.2-0_fbk3_647_gc1af76fcc8cb-3.3.0-1.x86_64.rpm";
  EXPECT_FALSE(mockPkgManager_.isValidRpm());

  // Invalid RPM file
  FLAGS_local_rpm_path =
      "/path/to/fboss_bsp_kmods-6.11.1-0_fbk3_647_gc1af76fcc8cb-3.3.0-2.x86_64";
  EXPECT_FALSE(mockPkgManager_.isValidRpm());

  // Invalid file name
  FLAGS_local_rpm_path = "/path/to/invalid.txt";
  EXPECT_FALSE(mockPkgManager_.isValidRpm());

  // Empty file name
  FLAGS_local_rpm_path = "";
  EXPECT_FALSE(mockPkgManager_.isValidRpm());
}

TEST_F(PkgManagerTest, processRpmsRpmNotFound) {
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockPlatformFsUtils_, getStringFileContent(_))
      .WillRepeatedly(Return(jsonBspKmodsFile_));
  EXPECT_CALL(*mockSystemInterface_, lsmod())
      .WillRepeatedly(Return(std::set<std::string>{}));
  EXPECT_CALL(*mockSystemInterface_, getInstalledRpms(_))
      .WillOnce(Return(std::vector<std::string>{}));
  // A missing rpm is permanent, so there is exactly one install attempt.
  EXPECT_CALL(*mockSystemInterface_, installRpm(_, "kernel"))
      .Times(1)
      .WillOnce(Return(package_manager::RpmInstallResult{1, true}));
  EXPECT_CALL(
      *mockSystemInterface_,
      getLocalRepoBspRpmVersions(*platformConfig_.bspKmodsRpmName()))
      .WillOnce(Return(std::vector<std::string>{"4.4.2-1", "4.3.0-1"}));
  EXPECT_CALL(*mockSystemInterface_, depmod()).Times(0);
  EXPECT_THROW(pkgManager_.processRpms(), std::runtime_error);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounter(PkgManager::kProcessRpmFailure), 1);
}

TEST_F(PkgManagerTest, resolveBspKmodsRpmVersionWildcard) {
  platformConfig_.bspKmodsRpmVersion() = kBspKmodsRpmVersionWildcard;
  EXPECT_CALL(
      *mockSystemInterface_,
      getLocalRepoBspRpmVersions(*platformConfig_.bspKmodsRpmName()))
      .WillOnce(Return(std::vector<std::string>{"4.4.2-1", "4.3.0-1"}));
  resolveBspKmodsRpmVersion(platformConfig_, *mockSystemInterface_);
  EXPECT_EQ(*platformConfig_.bspKmodsRpmVersion(), "4.4.2-1");
}

TEST_F(PkgManagerTest, resolveBspKmodsRpmVersionWildcardNoRpmAvailable) {
  platformConfig_.bspKmodsRpmVersion() = kBspKmodsRpmVersionWildcard;
  EXPECT_CALL(*mockSystemInterface_, getHostKernelVersion())
      .WillRepeatedly(Return("6.4.3-0_fbk1_755_ga25447393a1d"));
  EXPECT_CALL(*mockSystemInterface_, getLocalRepoBspRpmVersions(_))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_THROW(
      resolveBspKmodsRpmVersion(platformConfig_, *mockSystemInterface_),
      std::runtime_error);
}

TEST_F(PkgManagerTest, resolveBspKmodsRpmVersionPinned) {
  EXPECT_CALL(*mockSystemInterface_, getLocalRepoBspRpmVersions(_)).Times(0);
  resolveBspKmodsRpmVersion(platformConfig_, *mockSystemInterface_);
  EXPECT_EQ(*platformConfig_.bspKmodsRpmVersion(), "11.44.63-14");
}
}; // namespace facebook::fboss::platform::platform_manager
