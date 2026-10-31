/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/HwAgentMain.h"
#include <fb303/FollyLoggingHandler.h>
#include <fb303/ServiceData.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/AgentConfig.h"
#ifndef IS_OSS
#include "common/fb303/cpp/DefaultControl.h"
#include "common/fb303/cpp/DefaultMonitor.h"
#include "common/fb303/cpp/DefaultStatus.h"
#endif
#ifdef IS_OSS
#include "common/fb303/cpp/FacebookBase2.h"
#endif
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/CommonInit.h"
#include "fboss/agent/FbossInit.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/SetupThrift.h"
#include "fboss/agent/TestUtils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/mnpu/SplitAgentThriftSyncer.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include <chrono>
#include <cstdlib>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG2; default:async=true");

DEFINE_int32(swswitch_port, 5959, "Port for SwSwitch");

DEFINE_bool(enable_stats_update_thread, true, "Run stats update thread");

using namespace std::chrono;
using facebook::fboss::SwitchRunState;

namespace {

// Convert std::map<PortID, phy::PhyInfo> to std::map<int, phy::PhyInfo>
std::map<int, facebook::fboss::phy::PhyInfo> getPhyInfoForSwitchStats(
    const std::map<facebook::fboss::PortID, facebook::fboss::phy::PhyInfo>&
        phyInfo) {
  std::map<int, facebook::fboss::phy::PhyInfo> phyInfoForSwitchStats;
  for (auto& [portId, phyInfoPerPort] : phyInfo) {
    phyInfoForSwitchStats.emplace(portId, phyInfoPerPort);
  }
  return phyInfoForSwitchStats;
}

/*
 * This function is executed periodically by the UpdateStats thread.
 */
void updateStats(
    facebook::fboss::HwSwitch* hw,
    facebook::fboss::SplitAgentThriftSyncer* syncer) {
  if (hw->getRunState() == SwitchRunState::CONFIGURED) {
    hw->updateStats();
    auto hwSwitchStats = hw->getHwSwitchStats();
    hw->updateAllPhyInfo();
    hwSwitchStats.phyInfo() = getPhyInfoForSwitchStats(hw->getAllPhyInfo());
    syncer->updateHwSwitchStats(std::move(hwSwitchStats));
  }
}
} // namespace

namespace facebook::fboss {

void SplitHwAgentSignalHandler::signalReceived(int /*signum*/) noexcept {
  restart_time::mark(RestartEvent::SIGNAL_RECEIVED);
  XLOG(DBG2) << "[Exit] Signal received ";
  if (!hwAgent_->isInitialized()) {
    XLOG(WARNING)
        << "[Exit] Signal received before initializing hw switch, waiting for initialization to finish.";
    hwAgent_->waitForInitDone();
    XLOG(DBG2) << "[Exit] Wait for initialization done";
  }
  steady_clock::time_point begin = steady_clock::now();
  // Mark HwSwitch run state as exiting
  hwAgent_->getPlatform()->getHwSwitch()->switchRunStateChanged(
      SwitchRunState::EXITING);
  // rx pkt and fdb callback handlers might be waiting on event enqueue. Cancel
  // any pending events to avoid sdk callbak unregistration getting stuck
  syncer_->cancelPendingRxPktEnqueue();
  syncer_->cancelPendingFdbEnqueue();
  // unregister sdk callbacks so that we do not get sdk updates while shutting
  // down
  XLOG(DBG2) << "[Exit] unregistering callbacks";
  hwAgent_->getPlatform()->getHwSwitch()->unregisterCallbacks();
  steady_clock::time_point unregisterCallbacks = steady_clock::now();
  syncer_->stopOperDeltaSync();
  stopServices();
  steady_clock::time_point servicesStopped = steady_clock::now();
  auto dirUtil = hwAgent_->getPlatform()->getDirectoryUtil();
  auto switchIndex = hwAgent_->getPlatform()->getAsic()->getSwitchIndex();
  auto exitForColdBootFile = dirUtil->exitHwSwitchForColdBootFile(switchIndex);
  if (!checkFileExists(dirUtil->exitHwSwitchForColdBootFile(switchIndex))) {
    hwAgent_->getPlatform()->getHwSwitch()->gracefulExit();
  } else {
    SCOPE_EXIT {
      removeFile(exitForColdBootFile);
    };
    XLOG(DBG2) << "[Exit] Cold boot detected, skipping warmboot";
    if (hwAgent_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ROUTE_PROGRAMMING)) {
      auto programmedState =
          hwAgent_->getPlatform()->getHwSwitch()->getProgrammedState();
      auto alpmState = getMinAlpmRouteState(programmedState);
      XLOG(DBG2) << "[Exit] programming minimum ALPM routes";
      // minimum ALPM state retains default routes while removes all other
      // routes this is required because default routes can not be removed
      // before other routes are removed and this restriction must be respected
      // while exiting for cold boot. this is ensured by both SwAgent shutdown
      // for cold boot and HwAgent shutdown for cold boot. this is because two
      // agents may indepdently shutdown for cold boot and regardless of the
      // order of shutdown this constraint must be satisfied.
      std::vector<StateDelta> deltas;
      deltas.emplace_back(programmedState, alpmState);
      hwAgent_->getPlatform()->getHwSwitch()->stateChanged(deltas);
    }
    // invoke destructors
    XLOG(DBG2) << "[Exit] destroying hardware agent";
    hwAgent_.reset();
  }
  steady_clock::time_point switchGracefulExit = steady_clock::now();
  XLOG(DBG2)
      << "[Exit] Switch Graceful Exit time "
      << duration_cast<duration<float>>(switchGracefulExit - servicesStopped)
             .count()
      << std::endl
      << "[Exit] Services Exit time "
      << duration_cast<duration<float>>(servicesStopped - unregisterCallbacks)
             .count()
      << std::endl
      << "[Exit] Total graceful Exit time "
      << duration_cast<duration<float>>(switchGracefulExit - begin).count();
  restart_time::mark(RestartEvent::SHUTDOWN);

  // Delay exit if agent_exit_delay_s is set
  if (FLAGS_agent_exit_delay_s > 0) {
    XLOG(INFO) << "[Exit] Delaying exit by " << FLAGS_agent_exit_delay_s
               << " seconds";
    // @lint-ignore CLANGTIDY
    std::this_thread::sleep_for(std::chrono::seconds(FLAGS_agent_exit_delay_s));
    XLOG(INFO) << "[Exit] Delay complete, exiting now";
  }

  // _Exit: skip global dtors that re-drive the live EventBase on warmboot exit.
  std::_Exit(0);
}

int hwAgentMain(
    int argc,
    char** argv,
    uint32_t hwFeaturesDesired,
    PlatformInitFn initPlatformFn) {
  fb303::registerFollyLoggingOptionHandlers();
  // Allow the fb303 setOption() call to update the command line flag
  // settings.  This allows us to change the log levels on the fly using
  //  setOption().
  fb303::fbData->setUseOptionsAsFlags(true);
  auto config = fbossCommonInit(argc, argv, true /*useBitsflowAclFileSuffix*/);

  if (FLAGS_thrift_test_utils_thrift_handler || FLAGS_hw_agent_for_testing) {
    config = getConfigFileForTesting(FLAGS_switchIndex);
    initFlagDefaults(*config->thrift.defaultCommandLineArgs());
  }

  auto hwAgent = std::make_unique<HwAgent>(
      std::move(config), hwFeaturesDesired, initPlatformFn, FLAGS_switchIndex);

  auto thriftSyncer = std::make_unique<SplitAgentThriftSyncer>(
      hwAgent->getPlatform()->getHwSwitch(),
      FLAGS_swswitch_port,
      SwitchID(*hwAgent->getPlatform()->getAsic()->getSwitchId()),
      FLAGS_switchIndex,
      hwAgent->getPlatform()->getMultiSwitchStatsPrefix());

  auto ret =
      hwAgent->initAgent(true /* failHwCallsOnWarmboot */, thriftSyncer.get());

  hwAgent->getPlatform()->onHwInitialized(nullptr /*sw*/);

  hwAgent->getPlatform()->getHwSwitch()->switchRunStateChanged(
      SwitchRunState::INITIALIZED);

  restart_time::init(
      hwAgent->getPlatform()->getDirectoryUtil()->getWarmBootDir() +
          "/hw_switch@" + folly::to<std::string>(FLAGS_switchIndex),
      ret.bootType == BootType::WARM_BOOT);

  thriftSyncer->start();

  std::unique_ptr<folly::FunctionScheduler> fs{nullptr};
  if (FLAGS_enable_stats_update_thread) {
    // Start the UpdateSwitchStatsThread
    fs.reset(new folly::FunctionScheduler());
    fs->setThreadName("UpdateStatsThread");
    std::function<void()> callback(
        std::bind(
            updateStats,
            hwAgent->getPlatform()->getHwSwitch(),
            thriftSyncer.get()));
    auto timeInterval = std::chrono::seconds(FLAGS_update_stats_interval_s);
    fs->addFunction(callback, timeInterval, "updateStats");
    fs->start();
    XLOG(DBG2) << "Started background thread: UpdateStatsThread";
  }

  std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
      handlers{};
  if (auto handler = hwAgent->getPlatform()->createHandler()) {
    handlers.push_back(handler);
  } else {
    XLOG(FATAL) << "handler does not exist for platform";
  }
  if (FLAGS_thrift_test_utils_thrift_handler || FLAGS_hw_agent_for_testing) {
    // Add HwTestThriftHandler to the thrift server
    auto testUtilsHandler = utility::createHwTestThriftHandler(
        hwAgent->getPlatform()->getHwSwitch());
    handlers.push_back(std::move(testUtilsHandler));
  }

#ifdef IS_OSS
  // In OSS the thrift server has no monitoring extra-interface
  // (createDefaultExtraInterfaces returns null), so the HwAgent would not
  // answer fb303 getCounters/getRegexCounters. Add a FacebookBase2 handler to
  // the multiplex to serve them from fbData. Pushed last so the platform
  // handler wins any method-name overlap (e.g. getStatus/aliveSince).
  handlers.push_back(
      std::make_shared<facebook::fb303::FacebookBase2>("hwagent"));
#endif

  folly::EventBase eventBase;
  auto server = setupThriftServer(
      eventBase,
      handlers,
      {FLAGS_hwagent_port_base + FLAGS_switchIndex},
      true /*setupSSL*/);
#ifndef IS_OSS
  auto monitor = std::make_shared<facebook::fb303::DefaultMonitor>();
  auto status = std::make_shared<facebook::fb303::DefaultStatus>();
  markThriftMethodsInternalAndBypassLimits(*server, {monitor, status});
  server->setMonitoringInterface(std::move(monitor));
  server->setControlInterface(
      std::make_shared<facebook::fb303::DefaultControl>());
  // Serve fb303 getStatus() for external health checks (NHS/netstate) that
  // query the HwAgent thrift port directly.
  server->setStatusInterface(std::move(status));
#endif

  SplitHwAgentSignalHandler signalHandler(
      &eventBase,
      [&thriftSyncer, &fs, &server]() {
        XLOG(DBG2) << "[Exit] Stopping Thrift Syncer";
        thriftSyncer->stop();
        XLOG(DBG2) << "[Exit] Stop listening on thrift server";

        // stopListening behavior:
        //  - Thrift server stops accepting new thrift requests
        //  - expects the queued requests to complete execution within
        //    JOIN_TIMEOUT or else, Thrift server crashes with FATAL error.
        //
        // However, the queued requests continue to get processed, and can thus
        // cause us to go over the JOIN_TIMEOUT.
        // Avoid it by flushing the queue.
        server->setQueueTimeout(std::chrono::seconds(1));

        server->stopListening();

        XLOG(DBG2) << "[Exit] Stopping Thrift Server";
        auto stopController = server->getStopController();
        if (auto lockedPtr = stopController.lock()) {
          lockedPtr->stop();
          XLOG(DBG2) << "[Exit] Stopped Thrift Server";
          clearThriftModules();
          XLOG(DBG2) << "[Exit] Cleared thrift modules";
        } else {
          LOG(WARNING) << "Unable to stop Thrift Server";
        }
        if (fs) {
          XLOG(DBG2) << "[Exit] Stopping Function Scheduler";
          fs->shutdown();
        }
      },
      std::move(hwAgent),
      thriftSyncer.get());

  /*
   * Updating stats could be expensive as each update must acquire lock. To
   * avoid this overhead, we use ThreadLocal version for updating stats, and
   * start a publish thread to aggregate the counters periodically.
   */
  facebook::fb303::ThreadCachedServiceData::get()->startPublishThread(
      std::chrono::milliseconds(FLAGS_stat_publish_interval_ms));

  restart_time::mark(RestartEvent::INITIALIZED);

  // we are sharing thrift server setup with swswitch which uses legacy
  // thrift server framework. This will be removed once the shared code
  // is migrated to ServiceFramework.
  // @lint-ignore CLANGTIDY
  server->serve();
  server.reset();
  return 0;
}

} // namespace facebook::fboss
