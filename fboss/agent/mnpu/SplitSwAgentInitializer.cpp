// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/mnpu/SplitSwAgentInitializer.h"
#include "fboss/agent/MultiSwitchThriftHandler.h"
#include "fboss/agent/PacketStreamHandler.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/mnpu/MultiSwitchHwSwitchHandler.h"
#include "fboss/lib/CommonFileUtils.h"

#include <thread>

#ifndef IS_OSS
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#endif
#endif

using namespace std::chrono;

namespace facebook::fboss {

void SplitSwSwitchInitializer::initImpl(
    HwSwitchCallback* /* callback */,
    const HwWriteBehavior& hwWriteBehavior) {
  // this blocks until at least one hardware switch is up
  sw_->init(hwWriteBehavior, setupFlags());
}

SplitSwAgentInitializer::SplitSwAgentInitializer() {
  sw_ = std::make_unique<SwSwitch>(
      [](const SwitchID& switchId, const cfg::SwitchInfo& info, SwSwitch* sw) {
        return std::make_unique<MultiSwitchHwSwitchHandler>(switchId, info, sw);
      },
      &agentDirectoryUtil_,
      true /* supportsAddRemovePort */,
      nullptr /* config */);
  initializer_ = std::make_unique<SplitSwSwitchInitializer>(sw_.get());
}

std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
SplitSwAgentInitializer::getThrifthandlers() {
  std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>> handlers;
  auto handler = std::make_shared<MultiSwitchThriftHandler>(sw_.get());
  multiSwitchThriftHandler_ = handler.get();
  handlers.push_back(handler);

  if (FLAGS_enable_aifm_ctrl_handler) {
    packetStreamHandler_ = std::make_shared<PacketStreamHandler>(
        sw_.get(), "aifm_ctrl_packet_stream", "AifmCtrlAgent");
    handlers.push_back(packetStreamHandler_);
    sw_->setPacketStreamHandler(packetStreamHandler_.get());
    XLOG(INFO) << "PacketStreamHandler enabled (split mode)";
  }

  return handlers;
}

void SplitSwAgentInitializer::handleExitSignal(bool gracefulExit) {
  if (!sw_->isInitialized()) {
    XLOG(WARNING)
        << "[Exit] Signal received before initializing sw switch, waiting for initialization to finish.";
  }
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
  initializer_->stopFunctionScheduler();
  multiSwitchThriftHandler_->cancelEventSyncers();
  steady_clock::time_point switchGracefulExitBegin = steady_clock::now();
  sw_->gracefulExit();
  steady_clock::time_point switchGracefulExitEnd = steady_clock::now();
  XLOG(DBG2) << "[Exit] Switch Graceful Exit time "
             << duration_cast<duration<float>>(
                    switchGracefulExitEnd - switchGracefulExitBegin)
                    .count()
             << std::endl;
  this->stopServer();
  steady_clock::time_point servicesStopped = steady_clock::now();
  XLOG(DBG2) << "[Exit] Server stop time "
             << duration_cast<duration<float>>(
                    servicesStopped - switchGracefulExitEnd)
                    .count();
  steady_clock::time_point switchGracefulExit = steady_clock::now();
  XLOG(DBG2)
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
  this->waitForServerStopped();
  if (gracefulExit) {
    exit(0);
  } else {
    exit(1);
  }
}

void SplitSwAgentInitializer::stopAgent(bool setupWarmboot, bool gracefulExit) {
  if (setupWarmboot) {
    exitForWarmBoot(gracefulExit);
  } else {
    exitForColdBoot();
  }
}

void SplitSwAgentInitializer::exitForColdBoot() {
  initializer_->stopFunctionScheduler();
  multiSwitchThriftHandler_->cancelEventSyncers();
  sw_->stop(false /* gracefulStop */, true /* revertToMinAlpmState */);
  sw_->getHwSwitchHandler()->stop();
  stopServer();
  initializer_.reset();
}

void SplitSwAgentInitializer::exitForWarmBoot(bool gracefulExit) {
  handleExitSignal(gracefulExit);
}
} // namespace facebook::fboss
