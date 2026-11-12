// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/ScopeGuard.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <algorithm>
#include <future>
#include <thread>

namespace facebook::fboss {
namespace {

class L3TrafficLoopExpiryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto config = testConfigA();
    config.switchSettings()->switchIdToSwitchInfo()->at(0).asicType() =
        cfg::AsicType::ASIC_TYPE_YUBA;
    config.ports()[0].name() = "loop1";
    config.ports()[4].name() = "loop2";
    config.interfaces()[0].ipAddresses() = {"2001:db8:1::1/64"};
    config.maxNeighborProbes() = 300;
    config.staleEntryInterval() = 300;
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    sw_->updateStateBlocking("initialize loop TTL", [](const auto& state) {
      auto newState = state;
      for (auto id : {PortID(1), PortID(5)}) {
        state->getPorts()
            ->getNode(id)
            ->modify(&newState)
            ->setTTLDisableDecrement(false);
      }
      return newState;
    });
    handler_ = std::make_unique<ThriftHandler>(sw_);
  }

  void TearDown() override {
    // Tests clean up their loops explicitly, not through handler destruction.
    if (handler_) {
      if (hasLoopRoute()) {
        teardownLoop();
      }
      if (hasLoopRoute(true)) {
        teardownLoop(true);
      }
    }
    handler_.reset();
  }

  void setupLoop(int32_t timeout, bool second = false) {
    handler_->setupL3TrafficLoops(
        std::make_unique<std::vector<ThriftHandler::BinaryAddress>>(
            std::vector{network::toBinaryAddress(
                folly::IPAddress(second ? "100::2" : "100::1"))}),
        std::make_unique<std::vector<std::string>>(
            std::vector<std::string>{second ? "loop2" : "loop1"}),
        std::make_unique<std::vector<ThriftHandler::BinaryAddress>>(
            std::vector{network::toBinaryAddress(
                folly::IPAddress(second ? "2001:db8:1::3" : "2001:db8:1::2"))}),
        std::make_unique<std::vector<std::string>>(std::vector<std::string>{
            second ? "02:00:00:00:00:03" : "02:00:00:00:00:02"}),
        std::make_unique<std::vector<int32_t>>(std::vector{timeout}));
  }

  void extendLoop(int32_t seconds) {
    handler_->extendL3TrafficLoops(
        std::make_unique<std::vector<std::string>>(
            std::vector<std::string>{"loop1"}),
        seconds);
  }

  void teardownLoop(bool second = false) {
    handler_->teardownL3TrafficLoops(
        std::make_unique<std::vector<std::string>>(
            std::vector<std::string>{second ? "loop2" : "loop1"}));
  }

  bool hasLoopRoute(bool second = false) {
    std::vector<UnicastRoute> routes;
    handler_->getRouteTable(routes);
    return std::any_of(
        routes.begin(), routes.end(), [second](const auto& route) {
          return network::toIPAddress(*route.dest()->ip()) ==
              folly::IPAddress(second ? "100::2" : "100::1");
        });
  }

  bool waitForExpiry(bool second = false) {
    return checkWithRetryErrorReturn(
        [&] { return !hasLoopRoute(second); },
        60,
        std::chrono::milliseconds(100),
        true);
  }

  void drainCallbacks() {
    sw_->getBackgroundEvb()->runInEventBaseThreadAndWait([] {});
  }

  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_{nullptr};
  std::unique_ptr<ThriftHandler> handler_;
};

// The timer expires the loop on backgroundEvb, no earlier than its deadline.
TEST_F(L3TrafficLoopExpiryTest, ExpiresAtDeadline) {
  const auto started = std::chrono::steady_clock::now();
  setupLoop(1);
  ASSERT_TRUE(waitForExpiry());
  EXPECT_GE(
      std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
  EXPECT_EQ(
      sw_->getState()->getPorts()->getNode(PortID(1))->getTTLDisableDecrement(),
      std::optional<bool>(false));
}

// A loop with a later deadline remains configured before that deadline.
TEST_F(L3TrafficLoopExpiryTest, NoCleanupBeforeDeadline) {
  setupLoop(30);
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  EXPECT_TRUE(hasLoopRoute());
}

// Adding loop2 with an earlier deadline replaces loop1's 30-second timeout.
TEST_F(L3TrafficLoopExpiryTest, NewEarlierDeadlineReplacesTimer) {
  setupLoop(30);
  setupLoop(1, true);
  ASSERT_TRUE(waitForExpiry(true));
  EXPECT_TRUE(hasLoopRoute());
}

// After loop2 expires, loop1 must expire too without another RPC to rearm it.
TEST_F(L3TrafficLoopExpiryTest, ConsecutiveDeadlinesExpireWithoutRpc) {
  setupLoop(5);
  setupLoop(1, true);
  ASSERT_TRUE(waitForExpiry(true));
  EXPECT_TRUE(hasLoopRoute());
  ASSERT_TRUE(waitForExpiry());
}

// Extending loop1 past its old deadline must move the timer, not just its
// state.
TEST_F(L3TrafficLoopExpiryTest, ExtensionMovesDeadline) {
  setupLoop(1);
  extendLoop(30);
  drainCallbacks();
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_TRUE(hasLoopRoute());
}

// A timeout delayed by the EventBase must honor a lease extended meanwhile.
TEST_F(L3TrafficLoopExpiryTest, DelayedTimeoutHonorsExtension) {
  setupLoop(2);
  drainCallbacks();
  std::promise<void> paused;
  auto entered = paused.get_future();
  std::promise<void> resume;
  auto gate = resume.get_future().share();
  sw_->getBackgroundEvb()->runInEventBaseThread([&paused, gate] {
    paused.set_value();
    gate.wait();
  });
  bool resumed = false;
  SCOPE_EXIT {
    if (!resumed) {
      resume.set_value();
    }
  };
  entered.get();
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  extendLoop(3);
  resume.set_value();
  resumed = true;
  drainCallbacks();
  EXPECT_TRUE(hasLoopRoute());
  ASSERT_TRUE(waitForExpiry());
}

// Removing the earliest loop must leave only the other loop's later timeout.
TEST_F(L3TrafficLoopExpiryTest, RemovingEarliestLoopMovesDeadline) {
  setupLoop(1);
  setupLoop(30, true);
  teardownLoop();
  drainCallbacks();
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_FALSE(hasLoopRoute());
  EXPECT_TRUE(hasLoopRoute(true));
}

// A bulk teardown error must still move the timer past the removed first loop.
TEST_F(L3TrafficLoopExpiryTest, PartialTeardownRearmsRemainingLease) {
  setupLoop(1);
  setupLoop(3, true);
  EXPECT_THROW(
      handler_->teardownL3TrafficLoops(
          std::make_unique<std::vector<std::string>>(
              std::vector<std::string>{"loop1", "missing"})),
      FbossError);
  EXPECT_FALSE(hasLoopRoute());
  ASSERT_TRUE(waitForExpiry(true));
}

// Removing the only loop must cancel its timeout: no later empty expiry job.
TEST_F(L3TrafficLoopExpiryTest, RemovingLastLoopCancelsTimer) {
  setupLoop(1);
  teardownLoop();
  drainCallbacks();
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_FALSE(hasLoopRoute());
}

// Deleting the handler cancels its timeout without another callback using it.
TEST_F(L3TrafficLoopExpiryTest, HandlerDeletionCancelsTimeout) {
  setupLoop(1);
  handler_.reset();
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_EQ(
      sw_->getState()->getPorts()->getNode(PortID(1))->getTTLDisableDecrement(),
      std::optional<bool>(true));
  sw_->stop(false);
}

// A queued rearm callback may run after the handler has been deleted.
// Pause only the background EventBase; the switch and update threads stay
// alive.
TEST_F(L3TrafficLoopExpiryTest, LateCallbackIgnoresDeletedHandler) {
  setupLoop(30);
  drainCallbacks();
  auto* evb = sw_->getBackgroundEvb();
  evb->runInEventBaseThread([evb] { evb->terminateLoopSoon(); });
  ASSERT_TRUE(checkWithRetryErrorReturn(
      [evb] { return !evb->isRunning(); },
      60,
      std::chrono::milliseconds(100),
      true));
  extendLoop(30); // Queues rearming while the EventBase is stopped.
  teardownLoop(); // Test cleanup leaves the queued callback to check lifetime.
  handler_.reset();
  std::thread restartedBackground([evb] { evb->loopForever(); });
  SCOPE_EXIT {
    sw_->stop(false);
    restartedBackground.join();
  };
  std::promise<void> callback;
  auto done = callback.get_future();
  evb->runInEventBaseThread([&] { callback.set_value(); });
  EXPECT_EQ(done.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  done.get();
  EXPECT_EQ(
      sw_->getState()->getPorts()->getNode(PortID(1))->getTTLDisableDecrement(),
      std::optional<bool>(false));
}

// Leases still expire while the handler lives after its RPC server stops.
// Both ports must be restored without another RPC to rearm the second deadline.
TEST_F(L3TrafficLoopExpiryTest, RpcLeasesExpireAfterServerStops) {
  auto handler = std::shared_ptr<ThriftHandler>(std::move(handler_));
  {
    apache::thrift::ScopedServerInterfaceThread server(
        handler, [](auto& server) { server.setNumCPUWorkerThreads(1); });
    auto client = server.newClient<apache::thrift::Client<FbossCtrl>>();
    client->sync_setupL3TrafficLoops(
        {network::toBinaryAddress(folly::IPAddress("100::1")),
         network::toBinaryAddress(folly::IPAddress("100::2"))},
        {"loop1", "loop2"},
        {network::toBinaryAddress(folly::IPAddress("2001:db8:1::2")),
         network::toBinaryAddress(folly::IPAddress("2001:db8:1::3"))},
        {"02:00:00:00:00:02", "02:00:00:00:00:03"},
        {2, 4});
  }
  EXPECT_TRUE(checkWithRetryErrorReturn(
      [&] {
        auto ports = sw_->getState()->getPorts();
        return ports->getNode(PortID(1))->getTTLDisableDecrement() ==
            std::optional<bool>(false) &&
            ports->getNode(PortID(5))->getTTLDisableDecrement() ==
            std::optional<bool>(false);
      },
      60,
      std::chrono::milliseconds(100),
      true));
}

// A real RPC enables automatic expiry, with no further RPC needed.
// Deleting the handler cancels a long lease without tearing down the loop.
TEST_F(L3TrafficLoopExpiryTest, RpcExpiryAndServerShutdown) {
  auto handler = std::shared_ptr<ThriftHandler>(std::move(handler_));
  {
    apache::thrift::ScopedServerInterfaceThread server(
        handler, [](auto& server) { server.setNumCPUWorkerThreads(1); });
    auto client = server.newClient<apache::thrift::Client<FbossCtrl>>();
    auto setupRpcLoop = [&](int32_t seconds) {
      client->sync_setupL3TrafficLoops(
          {network::toBinaryAddress(folly::IPAddress("100::1"))},
          {"loop1"},
          {network::toBinaryAddress(folly::IPAddress("2001:db8:1::2"))},
          {"02:00:00:00:00:02"},
          {seconds});
    };
    setupRpcLoop(1);
    EXPECT_TRUE(checkWithRetryErrorReturn(
        [&] {
          return sw_->getState()
                     ->getPorts()
                     ->getNode(PortID(1))
                     ->getTTLDisableDecrement() == std::optional<bool>(false);
        },
        60,
        std::chrono::milliseconds(100),
        true));
    setupRpcLoop(3600);
  }
  handler.reset();
  EXPECT_EQ(
      sw_->getState()->getPorts()->getNode(PortID(1))->getTTLDisableDecrement(),
      std::optional<bool>(true));
  sw_->stop(false);
}

} // namespace
} // namespace facebook::fboss
