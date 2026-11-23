// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/single/MonolithicAgentInitializer.h"
#include "fboss/agent/PacketStreamHandler.h"

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>

#include <folly/ScopeGuard.h>
#include <folly/SocketAddress.h>
#include <folly/executors/FunctionScheduler.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/FbossInit.h"
#include "fboss/agent/HwAgent.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/SetupThrift.h"
#include "fboss/agent/SwAgentInitializer.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/single/MonolithicHwSwitchHandler.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

#ifndef IS_OSS
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#endif
#endif

using apache::thrift::ThriftServer;
using folly::AsyncSignalHandler;
using folly::EventBase;
using folly::FunctionScheduler;
using folly::SocketAddress;
using std::condition_variable;
using std::mutex;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::chrono::seconds;
using namespace std::chrono;

using facebook::fboss::SwSwitch;
using facebook::fboss::ThriftHandler;

namespace facebook::fboss {

void MonolithicSwSwitchInitializer::initImpl(
    HwSwitchCallback* hwSwitchCallback,
    const HwWriteBehavior& hwWriteBehavior) {
  // Initialize the switch.  This operation can take close to a minute
  // on some of our current platforms.
  sw_->init(
      hwSwitchCallback,
      nullptr,
      [this](HwSwitchCallback* callback, bool failHwCallsOnWarmboot) {
        return hwAgent_->getPlatform()->getHwSwitch()->initLight(
            callback, failHwCallsOnWarmboot);
      },
      hwWriteBehavior,
      setupFlags());
}

MonolithicAgentInitializer::MonolithicAgentInitializer(
    std::unique_ptr<AgentConfig> config,
    uint32_t hwFeaturesDesired,
    PlatformInitFn initPlatform) {
  CHECK(!FLAGS_multi_switch)
      << "Multi-switch not supported for monolithic agent";
  createSwitch(std::move(config), hwFeaturesDesired, initPlatform);
}

void MonolithicAgentInitializer::createSwitch(
    std::unique_ptr<AgentConfig> config,
    uint32_t hwFeaturesDesired,
    PlatformInitFn initPlatform) {
  // Allow any flag overrides to kick in before we create SwSwitch
  setCmdLineFlagOverrides();

  hwAgent_ = std::make_unique<HwAgent>(
      std::move(config), hwFeaturesDesired, initPlatform, 0 /*switchIndex*/);

  auto platform = hwAgent_->getPlatform();
  CHECK(platform);

  // Create the SwSwitch and thrift handler
  sw_ = std::make_unique<SwSwitch>(
      [platform](
          const SwitchID& switchId, const cfg::SwitchInfo& info, SwSwitch* sw) {
        return std::make_unique<MonolithicHwSwitchHandler>(
            platform, switchId, info, sw);
      },
      platform->getDirectoryUtil(),
      platform->supportsAddRemovePort(),
      platform->config());
  initializer_ = std::make_unique<MonolithicSwSwitchInitializer>(
      sw_.get(), hwAgent_.get());
}

void MonolithicAgentInitializer::handleExitSignal(bool gracefulExit) {
  restart_time::mark(RestartEvent::SIGNAL_RECEIVED);
  XLOG(DBG2) << "[Exit] Signal received ";
  steady_clock::time_point begin = steady_clock::now();
  auto sleepOnSigTermFile = sw_->getDirUtil()->sleepSwSwitchOnSigTermFile();
  if (checkFileExists(sleepOnSigTermFile)) {
    SCOPE_EXIT {
      removeFile(sleepOnSigTermFile);
    };
    std::string timeStr;
    if (folly::readFile(sleepOnSigTermFile.c_str(), timeStr)) {
      // @lint-ignore CLANGTIDY
      std::this_thread::sleep_for(
          std::chrono::seconds(folly::to<uint32_t>(timeStr)));
    }
  }
  XLOG(DBG2) << "[Exit] Wait until initialization done ";
  initializer_->waitForInitDone();
  stopServices();
  steady_clock::time_point servicesStopped = steady_clock::now();
  XLOG(DBG2) << "[Exit] Services stop time "
             << duration_cast<duration<float>>(servicesStopped - begin).count();
  sw_->gracefulExit();
  steady_clock::time_point switchGracefulExit = steady_clock::now();
  XLOG(DBG2)
      << "[Exit] Switch Graceful Exit time "
      << duration_cast<duration<float>>(switchGracefulExit - servicesStopped)
             .count()
      << std::endl
      << "[Exit] Total graceful Exit time "
      << duration_cast<duration<float>>(switchGracefulExit - begin).count();

  restart_time::mark(RestartEvent::SHUTDOWN);
  __attribute__((unused)) auto leakedSw = sw_.release();
#ifndef IS_OSS
#if __has_feature(address_sanitizer)
  __lsan_ignore_object(leakedSw);
#endif
#endif
  initializer_.reset();
  stopCoverageCollection();
  // Delay exit if agent_exit_delay_s is set
  if (FLAGS_agent_exit_delay_s > 0) {
    XLOG(INFO) << "[Exit] Delaying exit by " << FLAGS_agent_exit_delay_s
               << " seconds";
    // @lint-ignore CLANGTIDY
    std::this_thread::sleep_for(std::chrono::seconds(FLAGS_agent_exit_delay_s));
    XLOG(INFO) << "[Exit] Delay complete, exiting now";
  }
  if (gracefulExit) {
    exit(0);
  } else {
    exit(1);
  }
}

void MonolithicAgentInitializer::stopServices() {
  if (hwAgent_) {
    hwAgent_->stopThreads();
  }
  SwAgentInitializer::stopServices();
}

std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
MonolithicAgentInitializer::getThrifthandlers() {
  std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
      handlers{};
  auto hwHandler = platform()->createHandler();
  if (hwHandler) {
    handlers.push_back(hwHandler);
  }

  if (FLAGS_enable_aifm_ctrl_handler) {
    packetStreamHandler_ = std::make_shared<PacketStreamHandler>(
        sw_.get(), "aifm_ctrl_packet_stream", "AifmCtrlAgent");
    handlers.push_back(packetStreamHandler_);
    sw_->setPacketStreamHandler(packetStreamHandler_.get());
    XLOG(INFO) << "PacketStreamHandler enabled";
  }

  return handlers;
}

} // namespace facebook::fboss
