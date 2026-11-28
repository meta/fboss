// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/mnpu/SplitAgentThriftSyncerClient.h"

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#if FOLLY_HAS_COROUTINES
using namespace facebook::fboss;

namespace {
class NormalCompletionClient : public SplitAgentThriftClient {
 public:
  NormalCompletionClient(
      std::shared_ptr<folly::ScopedEventBaseThread> streamEvbThread,
      folly::EventBase* connRetryEvb)
      : SplitAgentThriftClient(
            "normal_completion_client",
            std::move(streamEvbThread),
            connRetryEvb,
            "normal_completion_client",
            nullptr,
            0,
            SwitchID(0)) {}

  void start() {
    setState(State::CONNECTING);
  }

  std::atomic<int> served{0};
  std::atomic<int> connectAttempts{0};
  std::atomic<int> disconnectedCalls{0};

 protected:
  void connectToServer(const utils::ConnectionOptions& /* options */) override {
    ++connectAttempts;
    setState(State::CONNECTING);
  }
  void startClientService() override {}
  folly::coro::Task<void> serveStream() override {
    ++served;
    co_return;
  }
  void connected() override {}
  void disconnected() override {
    ++disconnectedCalls;
  }
  void onCancellation() override {}
};

template <typename Pred>
bool waitFor(Pred pred) {
  for (int i = 0; i < 500 && !pred(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}
} // namespace

TEST(SplitAgentThriftClientTest, NormalStreamEndReconnects) {
  auto streamEvbThread = std::make_shared<folly::ScopedEventBaseThread>();
  folly::ScopedEventBaseThread connRetryEvbThread;
  auto client = std::make_unique<NormalCompletionClient>(
      streamEvbThread, connRetryEvbThread.getEventBase());

  client->start();
  ASSERT_TRUE(waitFor([&] { return client->served >= 1; }));
  EXPECT_TRUE(waitFor([&] {
    return client->getState() == ReconnectingThriftClient::State::DISCONNECTED;
  }));
  EXPECT_GE(client->disconnectedCalls, 1);

  connRetryEvbThread.getEventBase()->runInEventBaseThreadAndWait(
      [&] { client->timeoutExpired(); });
  EXPECT_GE(client->connectAttempts, 1);
  EXPECT_TRUE(waitFor([&] { return client->served >= 2; }));

  client->cancel();
}
#endif
