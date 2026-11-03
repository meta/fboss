// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentWrapperTest.h"

#include "fboss/agent/AgentCommandExecutor.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "tupperware/agent/system/systemd/Service.h"

#include <folly/FileUtil.h>
#include <folly/init/Init.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>

#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/AgentNetWhoAmI.h"
#include "fboss/lib/CommonFileUtils.h"

#include "fboss/agent/AgentConfig.h"

#include <fmt/core.h>

#include <filesystem>
#include <thread>
#include <type_traits>

DEFINE_int32(num_retries, 9, "number of retries for agent to start");
DEFINE_int32(wait_timeout, 15, "number of seconds to wait before retry");
DEFINE_string(
    agent_service_unit,
    "wedge_agent",
    "systemd unit of the agent under test. On NetOS the agent runs as a "
    "metald native service rather than the classic wedge_agent.service, so "
    "the wrapper contract has to be exercised against that unit instead.");
DEFINE_string(
    agent_config_path,
    "/etc/coop/agent/current",
    "agent config the unit under test reads. NetOS containers do not bind "
    "mount /etc/coop, so the config lives on the container's bind mount.");
DEFINE_bool(
    manage_core_analyzer_timer,
    true,
    "stop/start analyze_fboss_cores.timer around the crash cases. NetOS has "
    "no such timer -- it uses core_copier_setup -- so this is off there.");
DEFINE_string(
    sw_agent_service_unit,
    "fboss_sw_agent",
    "systemd unit of the sw agent in multi_switch mode. NetOS runs it as the "
    "metald native service netos.service.fboss_sw_agent.");
DEFINE_string(
    hw_agent_service_unit_fmt,
    "fboss_hw_agent@{}",
    "systemd unit of the per-index hw agent in multi_switch mode, formatted "
    "with the switch index. Classic uses one systemd template instance per "
    "index; NetOS has a separate container per index, named per SDK vendor, "
    "e.g. netos.service.fboss_wedge_agent_brcm_{}.");
DEFINE_bool(
    start_agent_units_individually,
    false,
    "start and stop the sw/hw agent units directly rather than the single "
    "orchestrating unit. Classic starts wedge_agent.service, whose executor "
    "then enables and starts the sw and hw units. NetOS runs each agent as an "
    "independent metald container with nothing above them, so there is no one "
    "unit to start.");

namespace facebook::fboss {

namespace {

template <bool cppWrapper = false, bool multiSwitch = false>
struct Wrapper {
  static constexpr bool kCppWrapper = cppWrapper;
  static constexpr bool kMultiSwitch = multiSwitch;
};
using CppWrapper = Wrapper<true, false>;
using CppMultiSwitchWrapper = Wrapper<true, true>;
using TestTypes = ::testing::Types<CppWrapper, CppMultiSwitchWrapper>;

void setCoreAnalyzerTimer(const std::string& action) {
  if (!FLAGS_manage_core_analyzer_timer) {
    return;
  }
  runCommand({"/usr/bin/systemctl", action, "analyze_fboss_cores.timer"});
}

std::string hwAgentUnit(int switchIndex) {
  return fmt::format(
      fmt::runtime(FLAGS_hw_agent_service_unit_fmt), switchIndex);
}
} // namespace

template <typename T>
void AgentWrapperTest<T>::SetUp() {
  whoami_ = std::make_unique<AgentNetWhoAmI>();
  if constexpr (!T::kCppWrapper) {
    if (whoami_->isChenabPlatform()) {
      GTEST_SKIP() << "Chenab platform will have only cpp wrapper";
    }
  }
  config_ = AgentConfig::fromFile(FLAGS_agent_config_path);

  auto newConfigThrift = config_->thrift;
  newConfigThrift.defaultCommandLineArgs()["agent_graceful_exit_timeout_ms"] =
      "130000";
  createDirectoryTree(util_.getWarmBootDir());

  if constexpr (T::kMultiSwitch) {
    newConfigThrift.defaultCommandLineArgs()["multi_switch"] = "true";
    newConfigThrift.defaultCommandLineArgs()["multi_npu_platform_mapping"] =
        "true";
  }
  config_ = std::make_unique<AgentConfig>(newConfigThrift);
  std::filesystem::copy_options opt =
      std::filesystem::copy_options::overwrite_existing;
  std::filesystem::copy(
      FLAGS_agent_config_path, FLAGS_agent_config_path + ".backup", opt);
  config_->dumpConfig(FLAGS_agent_config_path);
}

template <typename T>
void AgentWrapperTest<T>::TearDown() {
  stop();
  // stop() leaves the hw agents up for waitForStop(); a test that failed
  // before reaching it would otherwise hand them to the next one still
  // running, and `systemctl start` on an active unit is a silent no-op.
  stopHwAgents();
  if constexpr (T::kMultiSwitch) {
    std::filesystem::copy_options opt =
        std::filesystem::copy_options::overwrite_existing;
    std::filesystem::copy(
        FLAGS_agent_config_path + ".backup", FLAGS_agent_config_path, opt);
  }
  removeDir(util_.getWarmBootDir());
}
template <typename T>
bool AgentWrapperTest<T>::isMultiSwitch() const {
  return T::kMultiSwitch;
}

template <typename T>
std::vector<std::string> AgentWrapperTest<T>::getAgentUnits() const {
  if constexpr (T::kMultiSwitch) {
    // hw agents first. Only they carry agent_pre_start_exec_runner, and its
    // setStartupConfigSymlink() is what creates the startup config the sw
    // agent's executor reads -- the sw agent unit has no ExecStartPre of its
    // own, so started first it dies on a symlink that does not exist yet.
    std::vector<std::string> units;
    for (auto switchIndex : getHwSwitchIndices()) {
      units.push_back(hwAgentUnit(switchIndex));
    }
    units.push_back(FLAGS_sw_agent_service_unit);
    return units;
  }
  return {FLAGS_agent_service_unit};
}

template <typename T>
void AgentWrapperTest<T>::stopHwAgents() {
  if (!FLAGS_start_agent_units_individually) {
    return;
  }
  AgentCommandExecutor executor;
  for (auto switchIndex : getHwSwitchIndices()) {
    executor.stopService(hwAgentUnit(switchIndex), false /* throwOnError */);
  }
}

template <typename T>
void AgentWrapperTest<T>::start() {
  AgentCommandExecutor executor;
  if (!FLAGS_start_agent_units_individually) {
    executor.startService(FLAGS_agent_service_unit);
    return;
  }
  for (const auto& unit : getAgentUnits()) {
    executor.startService(unit);
  }
}

template <typename T>
void AgentWrapperTest<T>::stop() {
  AgentCommandExecutor executor;
  if (!FLAGS_start_agent_units_individually) {
    executor.stopService(FLAGS_agent_service_unit);
    return;
  }
  // sw agent only, and non-blocking. Two constraints meet here:
  //
  //  - The hw agents must outlive it. HwSwitchConnectionStatusTable::
  //    disconnected() calls std::exit(EXIT_SUCCESS) the instant the last hw
  //    agent drops, abandoning whatever shutdown the sw agent was in the
  //    middle of -- no graceful-exit wait, no SIGABRT, no core. Its own
  //    comment states the rule: "In normal shutdown sequence, we exit
  //    SwSwitch first before shutting down HwSwitch."
  //  - waitForStop() has to observe this unit while it still has a main
  //    process; `systemctl stop` returns only once it is reaped, and a reaped
  //    unit reports no ProcessStatus.
  //
  // Classic satisfies both for free by stopping one orchestrating unit whose
  // executor serialises the two. stopHwAgents() runs once waitForStop() is
  // done with the sw agent.
  executor.runCommand(
      {"/usr/bin/systemctl",
       "stop",
       "--no-block",
       FLAGS_sw_agent_service_unit});
}

template <typename T>
void AgentWrapperTest<T>::wait(bool started) {
  if (started) {
    waitForStart();
  } else {
    waitForStop();
  }
}

template <typename T>
void AgentWrapperTest<T>::waitForStart(const std::string& unit) {
  WITH_RETRIES_N_TIMED(
      FLAGS_num_retries, std::chrono::seconds(FLAGS_wait_timeout), {
        facebook::tupperware::systemd::Service service{unit};
        auto status = service.getStatus();
        EXPECT_EVENTUALLY_EQ(
            status.value().serviceState,
            facebook::tupperware::systemd::ProcessStatus::ServiceState::
                RUNNING);
      });

  auto client = utils::createWedgeAgentClient();
  apache::thrift::RpcOptions options;
  options.setTimeout(std::chrono::seconds(1));
  WITH_RETRIES_N_TIMED(
      FLAGS_num_retries, std::chrono::seconds(FLAGS_wait_timeout), {
        SwitchRunState runState = SwitchRunState::UNINITIALIZED;
        try {
          runState = client->sync_getSwitchRunState(options);
        } catch (const std::exception&) {
          XLOG(INFO) << "Waiting for wedge agent to start";
          continue;
        }
        EXPECT_EVENTUALLY_EQ(runState, SwitchRunState::CONFIGURED);
      });
}

template <typename T>
void AgentWrapperTest<T>::waitForStart() {
  if constexpr (T::kMultiSwitch) {
    waitForStart(FLAGS_sw_agent_service_unit + ".service");
    // add support for other hw_agent instances
    waitForStart(hwAgentUnit(0) + ".service");
  } else {
    waitForStart(FLAGS_agent_service_unit + ".service");
  }
}

template <typename T>
void AgentWrapperTest<T>::waitForStop(const std::string& unit, bool crash) {
  bool wrapperRefactored = FLAGS_cpp_wedge_agent_wrapper;
  constexpr auto multiSwitch = T::kMultiSwitch;
  WITH_RETRIES_N_TIMED(
      FLAGS_num_retries, std::chrono::seconds(FLAGS_wait_timeout), {
        facebook::tupperware::systemd::Service service{unit};
        auto status = service.waitForExit(
            std::chrono::microseconds(FLAGS_wait_timeout * 1000000));
        // Guard before .value(): a unit that is already fully reaped has no
        // ProcessStatus to report, and .value() on the error would escape the
        // retry block as an exception rather than failing the expectation.
        ASSERT_EVENTUALLY_TRUE(status.hasValue());
        EXPECT_EVENTUALLY_EQ(
            status.value().serviceState,
            facebook::tupperware::systemd::ProcessStatus::ServiceState::EXITED);
        if (crash) {
          if (wrapperRefactored) {
            EXPECT_EVENTUALLY_EQ(status.value().exitStatus, 134);
          } else {
            if (multiSwitch) {
              EXPECT_EVENTUALLY_EQ(status.value().exitStatus, 134);
            } else {
              EXPECT_EVENTUALLY_EQ(status.value().exitStatus, 255);
            }
          }
        }
      });
}

template <typename T>
void AgentWrapperTest<T>::waitForStop(bool crash) {
  if constexpr (T::kMultiSwitch) {
    waitForStop(FLAGS_sw_agent_service_unit + ".service", crash);
    // TODO: wait for hw_agent as well
    // Only now: the sw agent has to reach its own exit before the hw agents
    // drop, or their disconnect std::exit()s it out from under this check.
    stopHwAgents();
  } else {
    waitForStop(FLAGS_agent_service_unit + ".service", crash);
  }
}

template <typename T>
BootType AgentWrapperTest<T>::getBootType() {
  auto client = utils::createWedgeAgentClient();
  apache::thrift::RpcOptions options;
  options.setTimeout(std::chrono::seconds(1));
  return client->sync_getBootType(options);
}

template <typename T>
pid_t AgentWrapperTest<T>::getAgentPid(const std::string& agentName) const {
  std::string pidStr;
  auto pidFile = util_.pidFile(agentName);
  if (!checkFileExists(pidFile)) {
    throw FbossError(pidFile, " not found");
  }
  folly::readFile(util_.pidFile(agentName).c_str(), pidStr);
  return folly::to<pid_t>(pidStr);
}

template <typename T>
std::optional<std::string> AgentWrapperTest<T>::getCoreDirectory(
    const std::string& agentName,
    pid_t pid) const {
  auto fbossCores = std::filesystem::path("/var/tmp/cores/fboss-cores/");

  if (!std::filesystem::exists(fbossCores) ||
      !std::filesystem::is_directory(fbossCores)) {
    return std::nullopt;
  }

  for (const auto& entry : std::filesystem::directory_iterator(fbossCores)) {
    if (entry.path().string().find(agentName) != std::string::npos &&
        entry.path().string().find(folly::to<std::string>(pid)) !=
            std::string::npos) {
      return entry.path().string();
    }
  }

  return std::nullopt;
}

template <typename T>
std::string AgentWrapperTest<T>::getCoreFile(
    const std::string& directory) const {
  return directory + "/core";
}

template <typename T>
std::string AgentWrapperTest<T>::getCoreMetaData(
    const std::string& directory) const {
  return directory + "/metadata";
}

template <typename T>
std::vector<int> AgentWrapperTest<T>::getHwSwitchIndices() const {
  std::vector<int> switchIndices;
  if (T::kMultiSwitch) {
    auto iter = this->config_->thrift.sw()
                    ->switchSettings()
                    ->switchIdToSwitchInfo()
                    ->begin();
    while (iter !=
           this->config_->thrift.sw()
               ->switchSettings()
               ->switchIdToSwitchInfo()
               ->end()) {
      switchIndices.push_back(*(iter->second.switchIndex()));
      iter++;
    }
  }
  if (switchIndices.empty()) {
    switchIndices.push_back(0);
  }
  return switchIndices;
}

template <typename T>
std::vector<std::string> AgentWrapperTest<T>::getDrainFiles() const {
  std::vector<std::string> drainFiles;
  drainFiles.push_back(this->util_.getRoutingProtocolColdBootDrainTimeFile());

  if (T::kMultiSwitch) {
    for (auto switchIndex : this->getHwSwitchIndices()) {
      drainFiles.push_back(
          this->util_.getRoutingProtocolColdBootDrainTimeFile(switchIndex));
    }
  }
  return drainFiles;
}
template <typename T>
void AgentWrapperTest<T>::setupDrainFiles() {
  std::vector<char> data = {'0', '5'};
  for (const auto& file : this->getDrainFiles()) {
    if (!this->whoami_->isNotDrainable() && !this->whoami_->isFdsw()) {
      touchFile(file);
      folly::writeFile(data, file.c_str());
    }
  }
}

template <typename T>
void AgentWrapperTest<T>::cleanupDrainFiles() {
  for (const auto& file : this->getDrainFiles()) {
    removeFile(file);
  }
}

template <typename T>
bool AgentWrapperTest<T>::isSai() const {
  if (whoami_->isTajoSaiPlatform()) {
    return true;
  }
  if (auto sdkVersion = config_->thrift.sw()->sdkVersion()) {
    return sdkVersion->saiSdk().has_value();
  }
  throw FbossError("No sdkVersion found in config");
}

template <typename T>
bool AgentWrapperTest<T>::skipTest() const {
  if (T::kMultiSwitch && !isSai()) {
    // multi-switch is only for SAI
    return true;
  }
  if (!T::kMultiSwitch &&
      config_->thrift.sw()->switchSettings()->switchIdToSwitchInfo()->size() >
          1) {
    // Multi-NPU platforms (e.g. Ladakh) only run the split mNPU stack, so the
    // mono wedge_agent type does not apply.
    return true;
  }
  return false;
}

TYPED_TEST_SUITE(AgentWrapperTest, TestTypes);

TYPED_TEST(AgentWrapperTest, ColdBootStartAndStop) {
  if (this->skipTest()) {
    GTEST_SKIP();
    return;
  }
  SCOPE_EXIT {
    this->cleanupDrainFiles();
    removeFile(this->util_.getUndrainedFlag());
    removeFile(this->util_.getColdBootOnceFile());
  };
  this->setupDrainFiles();
  touchFile(this->util_.getColdBootOnceFile());
  touchFile(this->util_.getUndrainedFlag());
  this->start();
  this->waitForStart();
  if (!this->whoami_->isNotDrainable() && !this->whoami_->isFdsw()) {
    // @lint-ignore CLANGTIDY
    for (auto file : this->getDrainFiles()) {
      EXPECT_FALSE(checkFileExists(file));
    }
  }
  EXPECT_EQ(this->getBootType(), BootType::COLD_BOOT);
  this->stop();
  this->waitForStop();
}

TYPED_TEST(AgentWrapperTest, StartAndStopAndStart) {
  if (this->skipTest()) {
    GTEST_SKIP();
    return;
  }
  SCOPE_EXIT {
    removeFile(this->util_.getColdBootOnceFile());
  };
  touchFile(this->util_.getColdBootOnceFile());
  this->start();
  this->waitForStart();
  EXPECT_EQ(this->getBootType(), BootType::COLD_BOOT);
  this->stop();
  this->waitForStop();
  EXPECT_FALSE(checkFileExists(this->util_.getColdBootOnceFile()));
  checkFileExists(this->util_.exitTimeFile("wedge_agent"));
  this->start();
  this->waitForStart();
  EXPECT_EQ(this->getBootType(), BootType::WARM_BOOT);
  this->stop();
  this->waitForStop();
  checkFileExists(this->util_.restartDurationFile("wedge_agent"));
}

TYPED_TEST(AgentWrapperTest, StartAndCrash) {
  if (this->skipTest()) {
    GTEST_SKIP();
    return;
  }
  SCOPE_EXIT {
    removeFile(this->util_.sleepSwSwitchOnSigTermFile());
    removeFile(this->util_.getMaxPostSignalWaitTimeFile());
    setCoreAnalyzerTimer("start");
  };
  setCoreAnalyzerTimer("stop");
  this->start();
  this->waitForStart();
  touchFile(this->util_.sleepSwSwitchOnSigTermFile());
  std::vector<char> sleepTime = {'3', '0', '0'};
  folly::writeFile(sleepTime, this->util_.sleepSwSwitchOnSigTermFile().c_str());
  auto maxPostSignalWaitTime = this->util_.getMaxPostSignalWaitTimeFile();
  touchFile(maxPostSignalWaitTime);
  std::vector<char> data = {'1'};
  folly::writeFile(data, maxPostSignalWaitTime.c_str());
  auto agent = this->isMultiSwitch() ? "fboss_sw_agent" : "wedge_agent";
  auto pid = this->getAgentPid(agent);
  this->stop();
  this->waitForStop(true /* expect sigabrt to crash */);
  // core copier should copy cores here, analyze fboss core timer will remove
  // these
  WITH_RETRIES_N_TIMED(
      FLAGS_num_retries, std::chrono::seconds(FLAGS_wait_timeout), {
        auto coreDir = this->getCoreDirectory(agent, pid);
        ASSERT_EVENTUALLY_TRUE(coreDir.has_value());
        auto coreFile = this->getCoreFile(*coreDir);
        auto coreMetaData = this->getCoreMetaData(*coreDir);
        EXPECT_EVENTUALLY_TRUE(checkFileExists(coreFile));
        EXPECT_EVENTUALLY_TRUE(checkFileExists(coreMetaData));
      });
}

TYPED_TEST(AgentWrapperTest, StartStopRemoveHwSwitchWarmBoot) {
  if (this->skipTest()) {
    GTEST_SKIP();
    return;
  }
  SCOPE_EXIT {
    removeFile(this->util_.getColdBootOnceFile());
    removeFile(this->util_.getUndrainedFlag());
  };
  touchFile(this->util_.getColdBootOnceFile());
  this->start();
  this->waitForStart();
  EXPECT_EQ(this->getBootType(), BootType::COLD_BOOT);
  this->stop();
  this->waitForStop();
  EXPECT_FALSE(checkFileExists(this->util_.getColdBootOnceFile()));
  EXPECT_TRUE(checkFileExists(this->util_.getSwSwitchCanWarmBootFile()));
  for (auto switchIndex : this->getHwSwitchIndices()) {
    EXPECT_TRUE(
        checkFileExists(this->util_.getHwSwitchCanWarmBootFile(switchIndex)));
  }
  removeFile(this->util_.getSwSwitchCanWarmBootFile());
  for (auto switchIndex : this->getHwSwitchIndices()) {
    removeFile(this->util_.getHwSwitchCanWarmBootFile(switchIndex));
  }

  this->setupDrainFiles();
  touchFile(this->util_.getUndrainedFlag());
  this->start();
  this->waitForStart();
  if (!this->whoami_->isNotDrainable() && !this->whoami_->isFdsw()) {
    // @lint-ignore CLANGTIDY
    for (auto file : this->getDrainFiles()) {
      EXPECT_FALSE(checkFileExists(file));
    }
  }
  EXPECT_EQ(this->getBootType(), BootType::COLD_BOOT);
}

TYPED_TEST(AgentWrapperTest, StartAndCrashWithTimeoutCap) {
  if (this->skipTest()) {
    GTEST_SKIP();
    return;
  }
  // Save original config before modifying
  std::filesystem::copy_options copyOpt =
      std::filesystem::copy_options::overwrite_existing;
  const auto timeoutCapBackup = FLAGS_agent_config_path + ".timeout_cap_backup";
  std::filesystem::copy(FLAGS_agent_config_path, timeoutCapBackup, copyOpt);
  SCOPE_EXIT {
    removeFile(this->util_.sleepSwSwitchOnSigTermFile());
    removeFile(this->util_.getColdBootOnceFile());
    std::filesystem::copy(
        timeoutCapBackup,
        FLAGS_agent_config_path,
        std::filesystem::copy_options::overwrite_existing);
    removeFile(timeoutCapBackup);
    setCoreAnalyzerTimer("start");
  };
  setCoreAnalyzerTimer("stop");

  // Set graceful exit timeout to 180s which exceeds the cap (150s).
  // dispatchSignal() should clamp postSignalWaitTime to 150s.
  auto newConfigThrift = AgentConfig::fromFile(FLAGS_agent_config_path)->thrift;
  newConfigThrift.defaultCommandLineArgs()["agent_graceful_exit_timeout_ms"] =
      "180000";
  auto newConfig = std::make_unique<AgentConfig>(newConfigThrift);
  newConfig->dumpConfig(FLAGS_agent_config_path);

  touchFile(this->util_.getColdBootOnceFile());
  this->start();
  this->waitForStart();

  // Agent sleeps 300s on SIGTERM so it won't exit gracefully
  touchFile(this->util_.sleepSwSwitchOnSigTermFile());
  std::vector<char> sleepTime = {'3', '0', '0'};
  folly::writeFile(sleepTime, this->util_.sleepSwSwitchOnSigTermFile().c_str());

  auto agent = this->isMultiSwitch() ? "fboss_sw_agent" : "wedge_agent";
  auto pid = this->getAgentPid(agent);
  auto stopStart = std::chrono::steady_clock::now();
  this->stop();
  this->waitForStop(true /* expect sigabrt */);
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - stopStart);

  // Wrapper should wait ~150s (capped from 180s), then SIGABRT.
  // Exit code 134 = SIGABRT (cap worked, systemd didn't SIGKILL).
  // Exit code 137 = SIGKILL (cap failed, systemd killed at 180s).
  XLOG(INFO) << "Agent stopped after " << elapsed.count() << "s";
  EXPECT_GE(elapsed.count(), 140);
  EXPECT_LE(elapsed.count(), 170);

  WITH_RETRIES_N_TIMED(
      FLAGS_num_retries, std::chrono::seconds(FLAGS_wait_timeout), {
        auto coreDir = this->getCoreDirectory(agent, pid);
        ASSERT_EVENTUALLY_TRUE(coreDir.has_value());
        EXPECT_EVENTUALLY_TRUE(checkFileExists(this->getCoreFile(*coreDir)));
      });
}

} // namespace facebook::fboss

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG4; default:async=true");

int main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv, true);

  // Run the tests
  return RUN_ALL_TESTS();
}
