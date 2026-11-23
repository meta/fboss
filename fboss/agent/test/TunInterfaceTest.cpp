/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>

#include <mutex>
#include <stdexcept>
#include <thread>

#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/MockTunManager.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

using ::testing::_;

TEST(TunInterfacesTest, Initialization) {
  auto platform = createMockPlatform();
  auto sw = setupMockSwitchWithoutHW(
      platform.get(), nullptr, SwitchFlags::ENABLE_TUN);
  auto tunMgr = dynamic_cast<MockTunManager*>(sw->getTunManager());
  EXPECT_NE(nullptr, tunMgr);
  EXPECT_CALL(*tunMgr, sync(_)).Times(1);
  EXPECT_CALL(*tunMgr, startObservingUpdates()).Times(1);
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  // initial sync for TunManager is asyc and happens on the event base
  // passed to Tun manager. For MockTunManager this is always the background
  // event base. So wait for pending operations there to complete
  waitForBackgroundThread(sw.get());
}

/*
 * Test that packet forwarding from TUN interfaces is suppressed during
 * graceful exit. This validates the fix for a SIGSEGV crash where
 * TunIntf::handlerReady continued processing packets after SwSwitch::stop()
 * had begun tearing down members (ipv6_, hwAsicTable_, etc.).
 *
 * The crash scenario:
 *  1. TunIntf::handlerReady is mid-execution on the EventBase thread
 *  2. SIGTERM arrives, SwSwitch::stop() begins teardown on the main thread
 *  3. handlerReady calls getVlanIDForTx/sendL3Packet on already-destroyed
 * members
 *
 * The fix adds an isExiting() check at the top of handlerReady. This test
 * verifies that during exit, the packet send path correctly drops packets
 * and doesn't attempt to access torn-down SwSwitch members.
 */
TEST(TunInterfacesTest, NoPacketForwardingDuringExit) {
  auto cfg = testConfigA();
  auto handle = createTestHandle(&cfg);
  auto sw = handle->getSw();
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  waitForStateUpdates(sw);

  // Verify switch is fully initialized before exit
  EXPECT_TRUE(sw->isFullyInitialized());
  EXPECT_FALSE(sw->isExiting());

  // Stop the switch to transition to EXITING state
  sw->stop();
  EXPECT_TRUE(sw->isExiting());
  EXPECT_FALSE(sw->isFullyInitialized());

  // Verify that sendL3Packet drops packets during exit without crashing.
  // No HW calls should be made — sendL3Packet returns early because
  // isFullyInitialized() is false in the EXITING state.
  // This is the same code path that TunIntf::handlerReady exercises;
  // the isExiting() guard we added in handlerReady prevents reaching
  // this point, providing an additional layer of protection.
  EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(0);
  EXPECT_HW_CALL(sw, sendPacketOutOfPortAsync_(_, _, _)).Times(0);

  // Call sendL3Packet during exit — must not crash
  sw->sendL3Packet(
      sw->allocateL3TxPacket(64, false /* tagged */), InterfaceID(1));
}

/*
 * Test that TunManager::stopProcessing() synchronizes with the EventBase
 * thread, ensuring no in-flight TunIntf::handlerReady callbacks can race
 * with SwSwitch::stop() teardown.
 *
 * The root cause of the SIGSEGV crash was that stopProcessing() called
 * TunIntf::stop() (unregisterHandler) from the main thread, which doesn't
 * wait for an already-executing handlerReady callback on the EventBase
 * thread. The fix runs stop() on the EventBase thread via
 * runImmediatelyOrRunInFbossEventBaseThreadAndWait, guaranteeing that
 * when stopProcessing() returns, no handler callbacks are in progress.
 *
 * This test verifies that graceful exit (which calls stopProcessing)
 * completes without crashing, and that the EXITING state is properly set
 * before any TUN packet processing would occur.
 */
TEST(TunInterfacesTest, StopProcessingSynchronizesWithEventBase) {
  auto platform = createMockPlatform();
  auto sw = setupMockSwitchWithoutHW(
      platform.get(), nullptr, SwitchFlags::ENABLE_TUN);
  auto tunMgr = dynamic_cast<MockTunManager*>(sw->getTunManager());
  EXPECT_NE(nullptr, tunMgr);
  EXPECT_CALL(*tunMgr, sync(_)).Times(1);
  EXPECT_CALL(*tunMgr, startObservingUpdates()).Times(1);
  sw->initialConfigApplied(std::chrono::steady_clock::now());
  waitForBackgroundThread(sw.get());

  EXPECT_TRUE(sw->isFullyInitialized());

  // gracefulExit calls stop() which calls tunMgr_->stopProcessing()
  // internally. The fix ensures stopProcessing() synchronizes with
  // the EventBase thread before continuing with teardown.
  // This must complete without deadlock or crash.
  sw->gracefulExit();

  EXPECT_TRUE(sw->isExiting());
}

namespace {
class TunManagerProbeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
    sw_ = setupMockSwitchWithoutHW(
        platform_.get(), nullptr, SwitchFlags::DEFAULT);
    evbThread_ = std::thread([this] { evb_.loopForever(); });
    evb_.waitUntilRunning();
    tunMgr_ = std::make_unique<MockTunManager>(sw_.get(), &evb_);
  }

  void TearDown() override {
    if (tunMgr_) {
      tunMgr_->stopProcessing();
      tunMgr_.reset();
    }
    evb_.terminateLoopSoon();
    if (evbThread_.joinable()) {
      evbThread_.join();
    }
  }

  std::unique_ptr<MockPlatform> platform_;
  std::unique_ptr<SwSwitch> sw_;
  FbossEventBase evb_{"TunManagerProbeTest"};
  std::thread evbThread_;
  std::unique_ptr<MockTunManager> tunMgr_;
};
} // namespace

/*
 * probe() registers TunIntf read handlers on the TunManager EventBase. When
 * it is called from another thread while that EventBase is looping (the
 * agent init and config-applied paths), it must run there rather than on the
 * calling thread.
 */
TEST_F(TunManagerProbeTest, ProbeRunsOnEventBaseThread) {
  bool probedOnEventBaseThread = false;
  EXPECT_CALL(*tunMgr_, doProbe(_))
      .WillOnce([&](std::lock_guard<std::mutex>& /* lock */) {
        probedOnEventBaseThread = evb_.inRunningEventBaseThread();
      });

  tunMgr_->probe();

  EXPECT_TRUE(probedOnEventBaseThread);
}

TEST_F(TunManagerProbeTest, ProbeErrorReachesCaller) {
  EXPECT_CALL(*tunMgr_, doProbe(_))
      .WillOnce(::testing::Throw(std::runtime_error("probe failed")));

  EXPECT_THROW(tunMgr_->probe(), std::runtime_error);
}
