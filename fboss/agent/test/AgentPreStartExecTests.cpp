// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/test/MockAgentCommandExecutor.h"
#include "fboss/agent/test/MockAgentNetWhoAmI.h"
#include "fboss/agent/test/MockAgentPreExecDrainer.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/AgentPreStartExec.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/CommonFileUtils.h"

#include <folly/logging/xlog.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using ::testing::_;

namespace facebook::fboss {

namespace {
template <bool multiSwitch, bool cppRefactor, bool sai, bool brcm, bool netOS>
struct TestAttr {
  static constexpr bool kMultiSwitch = multiSwitch;
  static constexpr bool kCppRefactor = cppRefactor;
  static constexpr bool kSai = sai;
  static constexpr bool kBrcm = brcm;
  static constexpr bool kNetOS = netOS;
};

#define TEST_ATTR(NAME, a, b, c, d, e) \
  struct TestAttr##NAME : public TestAttr<a, b, c, d, e> {};

TEST_ATTR(
    NoMultiSwitchNoCppRefactorNoSaiBrcm,
    false,
    false,
    false,
    true,
    false);
TEST_ATTR(NoMultiSwitchNoCppRefactorSaiBrcm, false, false, true, true, false);
TEST_ATTR(
    NoMultiSwitchNoCppRefactorSaiNoBrcm,
    false,
    false,
    true,
    false,
    false);

TEST_ATTR(NoMultiSwitchCppRefactorNoSaiBrcm, false, true, false, true, false);
TEST_ATTR(NoMultiSwitchCppRefactorSaiBrcm, false, true, true, true, false);
TEST_ATTR(NoMultiSwitchCppRefactorSaiNoBrcm, false, true, true, false, false);

TEST_ATTR(MultiSwitchNoCppRefactorNoSaiBrcm, true, false, false, true, false);
TEST_ATTR(MultiSwitchNoCppRefactorSaiBrcm, true, false, true, true, false);
TEST_ATTR(MultiSwitchNoCppRefactorSaiNoBrcm, true, false, true, false, false);

TEST_ATTR(MultiSwitchCppRefactorNoSaiBrcm, true, true, false, true, false);
TEST_ATTR(MultiSwitchCppRefactorSaiBrcm, true, true, true, true, false);
TEST_ATTR(MultiSwitchCppRefactorSaiNoBrcm, true, true, true, false, false);

TEST_ATTR(MultiSwitchCppRefactorNetOsNoSaiBrcm, true, true, false, true, true);
TEST_ATTR(MultiSwitchCppRefactorNetOsSaiBrcm, true, true, true, true, true);
TEST_ATTR(MultiSwitchCppRefactorNetOsSaiNoBrcm, true, true, true, false, true);
} // namespace

template <typename TestAttr>
class AgentPreStartExecTests : public ::testing::Test {
 public:
  void SetUp() override {
    tmpDir_ = folly::to<std::string>(std::tmpnam(nullptr), "_PreStartExec");
    std::string volatileDir = folly::to<std::string>(tmpDir_, "/dev/shm/fboss");
    std::string persistentDir =
        folly::to<std::string>(tmpDir_, "/var/facebook/fboss");
    std::string packageDirectory =
        folly::to<std::string>(tmpDir_, "/packages/wedge_agent/current");
    std::string systemdDir = folly::to<std::string>(tmpDir_, "/systemd/system");
    std::string configDir = folly::to<std::string>(tmpDir_, "/coop/agent");
    std::string drainConfigDir =
        folly::to<std::string>(tmpDir_, "/coop/agent_drain");
    util_ = std::make_unique<AgentDirectoryUtil>(
        volatileDir,
        persistentDir,
        packageDirectory,
        systemdDir,
        configDir,
        drainConfigDir);
    removeDirectories(tmpDir_);
    setupDirectories();
    touchFile(util_->getAgentLiveConfig());
  }

  void TearDown() override {
    removeDirectories(tmpDir_);
  }

  cfg::AgentConfig getConfig() {
    cfg::AgentConfig config;
    setSdkVersion(config.sw().value());
    if constexpr (TestAttr::kMultiSwitch) {
      // set the multi switch mode in config
      setMultiSwitchMode(config.defaultCommandLineArgs().value());
    }
    setSwitchIdToSwitchInfo(config.sw().value());
    return config;
  }

  void run(
      bool coldBoot = false,
      bool drain = false,
      bool fdsw = false,
      bool voq = false) {
    runWithSwitchIndex(0, coldBoot, drain, fdsw, voq);
  }

  void runWithSwitchIndex(
      int switchIndex,
      bool coldBoot = false,
      bool drain = false,
      bool fdsw = false,
      bool voq = false) {
    using ::testing::Return;
    const auto isNetOsSecondary = TestAttr::kNetOS && switchIndex > 0;

    MockAgentCommandExecutor executor;
    AgentPreStartExec exec;
    auto netwhoami = std::make_unique<MockAgentNetWhoAmI>();
    MockAgentPreExecDrainer drainer(util_.get());

    ON_CALL(*netwhoami, isSai()).WillByDefault(Return(TestAttr::kSai));
    ON_CALL(*netwhoami, isBcmSaiPlatform())
        .WillByDefault(Return(TestAttr::kBrcm && TestAttr::kSai));
    ON_CALL(*netwhoami, isTajoSaiPlatform())
        .WillByDefault(Return(!TestAttr::kBrcm));
    ON_CALL(*netwhoami, isTajoMorgan800ccPlatform())
        .WillByDefault(Return(!TestAttr::kBrcm));
    ON_CALL(*netwhoami, isTajoWedge800cPlatform())
        .WillByDefault(Return(!TestAttr::kBrcm));
    ON_CALL(*netwhoami, isBcmPlatform()).WillByDefault(Return(TestAttr::kBrcm));
    ON_CALL(*netwhoami, isTajoPlatform())
        .WillByDefault(Return(!TestAttr::kBrcm));
    ON_CALL(*netwhoami, isSdsw()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isChenabPlatform()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isBcmVoqPlatform()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isFdsw()).WillByDefault(Return(fdsw));
    ON_CALL(*netwhoami, isNotDrainable()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, hasRoutingProtocol()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, hasBgpRoutingProtocol()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isBcmVoqPlatform()).WillByDefault(Return(voq));

    if (coldBoot) {
      // touch cold_boot_once_0
      createDirectoryTree(util_->getWarmBootDir());
      touchFile(util_->getColdBootOnceFile());
    }
    if (drain) {
      // touch undrained flag
      createDirectoryTree(util_->getVolatileStateDir());
      touchFile(util_->getUndrainedFlag());
    }
    if (fdsw) {
      // drain config needed for fdsw
      touchFile(util_->getAgentDrainConfig());
    }
    if (TestAttr::kCppRefactor) {
      std::string kmodsInstaller = (TestAttr::kBrcm)
          ? "/usr/local/bin/fboss_bcm_kmods"
          : "/usr/local/bin/fboss_leaba_kmods";
      std::string kmodVersion = getAsicSdkVersion(getSdkVersion());
      std::vector<std::string> installKmod{
          kmodsInstaller, "install", "--sdk-upgrade", kmodVersion};
      if (isNetOsSecondary) {
        EXPECT_CALL(executor, runCommand(installKmod, true)).Times(0);
      } else {
        EXPECT_CALL(executor, runCommand(installKmod, true)).Times(1);
      }
      if (!TestAttr::kNetOS && !isNetOsSecondary) {
        if (TestAttr::kMultiSwitch) {
          EXPECT_CALL(
              executor,
              runCommand(
                  std::vector<std::string>{
                      "/usr/bin/systemctl",
                      "enable",
                      util_->getSwAgentServicePath()},
                  true));
          EXPECT_CALL(
              executor,
              runCommand(
                  std::vector<std::string>{
                      "/usr/bin/systemctl",
                      "enable",
                      util_->getHwAgentServiceInstance(0)},
                  true));

          EXPECT_CALL(
              executor,
              runCommand(
                  std::vector<std::string>{
                      "/usr/bin/systemctl",
                      "enable",
                      util_->getHwAgentServiceInstance(1)},
                  true));
        } else {
          EXPECT_CALL(
              executor,
              runCommand(
                  std::vector<std::string>{
                      "/usr/bin/systemctl", "disable", "fboss_sw_agent"},
                  false));

          EXPECT_CALL(
              executor,
              runCommand(
                  std::vector<std::string>{
                      "/usr/bin/systemctl",
                      "disable",
                      "fboss_hw_agent@0.service"},
                  false));

          if (TestAttr::kMultiSwitch) {
            EXPECT_CALL(
                executor,
                runCommand(
                    std::vector<std::string>{
                        "/usr/bin/systemctl",
                        "disable",
                        "fboss_hw_agent@1.service"},
                    false));
          }
        }

        // update-buildinfo
        EXPECT_CALL(
            executor, runShellCommand("/usr/local/bin/fboss-build-info", false))
            .Times(1);
      }
    }

    exec.run(
        &executor,
        std::move(netwhoami),
        *util_,
        std::make_unique<AgentConfig>(getConfig()),
        TestAttr::kCppRefactor,
        TestAttr::kNetOS,
        switchIndex);

    if (TestAttr::kCppRefactor) {
      auto verifySymLink = [&](const std::string& name,
                               const std::string& sdk) {
        auto agentSymLink = util_->getPackageDirectory() + "/" + name;
        auto actualTarget = std::filesystem::read_symlink(agentSymLink);
        auto expectedTarget =
            util_->getPackageDirectory() + "/" + sdk + "/" + name;
        EXPECT_EQ(actualTarget.string(), expectedTarget);
      };
      if (isNetOsSecondary) {
        verifySymLink(
            "fboss_hw_agent",
            TestAttr::kSai && TestAttr::kBrcm
                ? getSaiSdkVersion(getSdkVersion())
                : getAsicSdkVersion(getSdkVersion()));
        return;
      }
      if (!TestAttr::kNetOS) {
        verifySymLink(
            "wedge_agent",
            // sai + brcm has directories with sai sdk version
            // non-sai + brcm and non-brcm has directories with asic sdk version
            TestAttr::kSai && TestAttr::kBrcm
                ? getSaiSdkVersion(getSdkVersion())
                : getAsicSdkVersion(getSdkVersion()));
      }
      if (TestAttr::kMultiSwitch) {
        verifySymLink(
            "fboss_hw_agent",
            // sai + brcm has directories with sai sdk version
            // non-sai + brcm and non-brcm has directories with asic sdk version
            TestAttr::kSai && TestAttr::kBrcm
                ? getSaiSdkVersion(getSdkVersion())
                : getAsicSdkVersion(getSdkVersion()));
      }
      if (coldBoot) {
        EXPECT_TRUE(checkFileExists(util_->getSwColdBootOnceFile()));
        auto cfg = getConfig();
        for (auto [id, info] :
             *(cfg.sw()->switchSettings()->switchIdToSwitchInfo())) {
          EXPECT_TRUE(checkFileExists(
              util_->getHwColdBootOnceFile(info.switchIndex().value())));
        }
        EXPECT_FALSE(checkFileExists(util_->getColdBootOnceFile()));
      }
      auto drained = !checkFileExists(util_->getUndrainedFlag());
      checkFileExists(util_->getStartupConfig());
      auto actualStartUpConfig =
          std::filesystem::read_symlink(util_->getStartupConfig());
      auto expectedTarget = util_->getConfigDirectory() + "/current";

      if (fdsw && drained) {
        expectedTarget = util_->getDrainConfigDirectory() + "/current";
      }
      EXPECT_EQ(actualStartUpConfig.string(), expectedTarget);
      if (voq) {
        EXPECT_TRUE(checkFileExists(util_->getPackageDirectory() + "/db"));
      }
    }
  }

  void runWithoutConfig(
      bool removeLiveConfig = false,
      bool removeDrainConfig = false) {
    MockAgentCommandExecutor executor;
    AgentPreStartExec exec;
    auto netwhoami = std::make_unique<MockAgentNetWhoAmI>();
    MockAgentPreExecDrainer drainer(util_.get());
    ON_CALL(*netwhoami, isFdsw()).WillByDefault(::testing::Return(true));
    if (removeLiveConfig) {
      removeFile(util_->getAgentLiveConfig());
    }
    if (removeDrainConfig) {
      removeFile(util_->getAgentLiveConfig());
    }
    if (removeLiveConfig || removeDrainConfig) {
      EXPECT_THROW(
          exec.run(
              &executor,
              std::move(netwhoami),
              *util_,
              std::make_unique<AgentConfig>(getConfig()),
              TestAttr::kCppRefactor,
              TestAttr::kNetOS,
              0),
          FbossError);
    } else {
      exec.run(
          &executor,
          std::move(netwhoami),
          *util_,
          std::make_unique<AgentConfig>(getConfig()),
          TestAttr::kCppRefactor,
          TestAttr::kNetOS,
          0);
    }
  }

  // A failing kmods install (e.g. fboss_leaba_kmods unable to load
  // libpython at boot) surfaces as a non-FbossError std::exception from
  // runCommand. It must propagate to the runner, which exits gracefully.
  void runWithKmodsFailure() {
    using ::testing::Return;
    using ::testing::Throw;
    MockAgentCommandExecutor executor;
    AgentPreStartExec exec;
    auto netwhoami = std::make_unique<MockAgentNetWhoAmI>();
    MockAgentPreExecDrainer drainer(util_.get());

    ON_CALL(*netwhoami, isBcmPlatform()).WillByDefault(Return(TestAttr::kBrcm));
    ON_CALL(*netwhoami, isTajoPlatform())
        .WillByDefault(Return(!TestAttr::kBrcm));
    ON_CALL(*netwhoami, isSdsw()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isChenabPlatform()).WillByDefault(Return(false));
    ON_CALL(*netwhoami, isFdsw()).WillByDefault(Return(false));

    std::string kmodsInstaller = (TestAttr::kBrcm)
        ? "/usr/local/bin/fboss_bcm_kmods"
        : "/usr/local/bin/fboss_leaba_kmods";
    std::string kmodVersion = getAsicSdkVersion(getSdkVersion());
    EXPECT_CALL(
        executor,
        runCommand(
            std::vector<std::string>{
                kmodsInstaller, "install", "--sdk-upgrade", kmodVersion},
            true))
        .WillOnce(Throw(std::runtime_error("kmods install failed")));

    EXPECT_THROW(
        exec.run(
            &executor,
            std::move(netwhoami),
            *util_,
            std::make_unique<AgentConfig>(getConfig()),
            TestAttr::kCppRefactor,
            TestAttr::kNetOS,
            0),
        std::runtime_error);
  }

 private:
  void setSdkVersion(cfg::SwitchConfig& config) {
    config.sdkVersion() = getSdkVersion();
  }

  cfg::SdkVersion getSdkVersion() {
    cfg::SdkVersion sdkVersion;
    sdkVersion.asicSdk() = "asic_1.0";
    if constexpr (TestAttr::kSai && TestAttr::kBrcm) {
      sdkVersion.saiSdk() = "sai_1.1";
    }
    return sdkVersion;
  }

  std::string getSaiSdkVersion(const cfg::SdkVersion& sdkVersion) {
    if (!sdkVersion.saiSdk().has_value()) {
      throw FbossError("SAI SDK version not found");
    }
    return sdkVersion.saiSdk().value();
  }

  std::string getAsicSdkVersion(const cfg::SdkVersion& sdkVersion) {
    if (!sdkVersion.asicSdk().has_value()) {
      throw FbossError("Asic SDK version not found");
    }
    return sdkVersion.asicSdk().value();
  }

  void setMultiSwitchMode(std::map<std::string, std::string>& args) {
    FLAGS_multi_switch = TestAttr::kMultiSwitch ? true : false;
  }

  void setSwitchIdToSwitchInfo(cfg::SwitchConfig& config) {
    std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;
    auto numSwitches = TestAttr::kMultiSwitch ? 2 : 1;
    for (auto i = 0; i < numSwitches; ++i) {
      switchIdToSwitchInfo.emplace(i, getSwitchInfo(i));
    }
    config.switchSettings()->switchIdToSwitchInfo() = switchIdToSwitchInfo;
  }

  cfg::SwitchInfo getSwitchInfo(int64_t id) {
    cfg::SwitchInfo info;
    info.switchType() = cfg::SwitchType::FABRIC;
    info.asicType() = cfg::AsicType::ASIC_TYPE_MOCK;
    info.switchIndex() = id;
    return info;
  }

  void createDirectory(const std::string& path) {
    XLOG(INFO) << "Creating directory : " << path;
    createDirectoryTree(path);
  }

  void removeDirectories(const std::string& path) {
    XLOG(INFO) << "Removing directory : " << path;
    std::filesystem::remove_all(path);
  }

  void setupDirectories() {
    createDirectory(util_->getPackageDirectory());
    if (TestAttr::kMultiSwitch) {
      createDirectory(util_->getMultiSwitchScriptsDirectory());
      touchFile(util_->getSwAgentServicePath());
      touchFile(util_->getHwAgentServiceTemplatePath());
    }
    createDirectory(util_->getSystemdDirectory());
    createDirectory(util_->getConfigDirectory());
    createDirectory(util_->getDrainConfigDirectory());
    auto sdkVersion = getSdkVersion();
    std::string binDir = util_->getPackageDirectory() + "/" +
        ((TestAttr::kSai && TestAttr::kBrcm) ? getSaiSdkVersion(sdkVersion)
                                             : getAsicSdkVersion(sdkVersion));
    createDirectoryTree(binDir);
    touchFile(binDir + "/wedge_agent");
    touchFile(binDir + "/fboss_hw_agent");
    createDirectory(binDir + "/db");
    touchFile(util_->getPackageDirectory() + "/fboss_sw_agent");
  }

  std::unique_ptr<AgentDirectoryUtil> util_;
  std::string tmpDir_;
};

#define TestFixtureName(NAME)                                    \
  class NAME : public AgentPreStartExecTests<TestAttr##NAME> {}; \
  TEST_F(NAME, PreStartExec) {                                   \
    run(false, false);                                           \
  }                                                              \
  TEST_F(NAME, PreStartExecColdBoot) {                           \
    run(true);                                                   \
  }                                                              \
  TEST_F(NAME, PreStartExecColdBootAndDrain) {                   \
    run(true, true);                                             \
  }                                                              \
  TEST_F(NAME, PreStartExecVoqColdBoot) {                        \
    run(true, false, false, true);                               \
  }                                                              \
  TEST_F(NAME, PreStartExecVoqDrainColdBoot) {                   \
    run(true, true, false, true);                                \
  }                                                              \
  TEST_F(NAME, PreStartExecVoqWarmBoot) {                        \
    run(false, false, false, true);                              \
  }                                                              \
  TEST_F(NAME, PreStartExecVoqDrainWarmBoot) {                   \
    run(false, true, false, true);                               \
  }

#define TestFixtureNameFdsw(NAME)                  \
  TEST_F(NAME, PreStartExecFdsw) {                 \
    run(false, false, true);                       \
  }                                                \
  TEST_F(NAME, PreStartExecFdswColdBoot) {         \
    run(true, false, true);                        \
  }                                                \
  TEST_F(NAME, PreStartExecFdswColdBootAndDrain) { \
    run(true, true, true);                         \
  }                                                \
  TEST_F(NAME, PreStartExecFdswNoLiveConfig) {     \
    runWithoutConfig(true, false);                 \
  }                                                \
  TEST_F(NAME, PreStartExecFdswNoDrainConfig) {    \
    runWithoutConfig(false, true);                 \
  }

/* TODO: retire NoCpp refactor subsequently */
TestFixtureName(NoMultiSwitchNoCppRefactorNoSaiBrcm);
TestFixtureName(NoMultiSwitchNoCppRefactorSaiBrcm);
TestFixtureName(NoMultiSwitchNoCppRefactorSaiNoBrcm);

TestFixtureName(NoMultiSwitchCppRefactorNoSaiBrcm);
TestFixtureName(NoMultiSwitchCppRefactorSaiBrcm);
TestFixtureName(NoMultiSwitchCppRefactorSaiNoBrcm);

TestFixtureName(MultiSwitchNoCppRefactorNoSaiBrcm);
TestFixtureName(MultiSwitchNoCppRefactorSaiBrcm);
TestFixtureName(MultiSwitchNoCppRefactorSaiNoBrcm);

TestFixtureName(MultiSwitchCppRefactorNoSaiBrcm);
TestFixtureName(MultiSwitchCppRefactorSaiBrcm);
TestFixtureName(MultiSwitchCppRefactorSaiNoBrcm);

TestFixtureName(MultiSwitchCppRefactorNetOsNoSaiBrcm);
TestFixtureName(MultiSwitchCppRefactorNetOsSaiBrcm);
TestFixtureName(MultiSwitchCppRefactorNetOsSaiNoBrcm);

TEST_F(
    MultiSwitchCppRefactorNetOsSaiBrcm,
    PreStartExecSecondarySwitchPreparesBinary) {
  runWithSwitchIndex(1);
}

TEST_F(
    MultiSwitchCppRefactorNetOsSaiNoBrcm,
    PreStartExecKmodsFailurePropagatesNonFbossError) {
  runWithKmodsFailure();
}

TestFixtureNameFdsw(NoMultiSwitchCppRefactorSaiBrcm);
TestFixtureNameFdsw(NoMultiSwitchCppRefactorNoSaiBrcm);
TestFixtureNameFdsw(NoMultiSwitchCppRefactorSaiNoBrcm);
TestFixtureNameFdsw(MultiSwitchCppRefactorSaiBrcm);

} // namespace facebook::fboss
