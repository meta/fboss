// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <gtest/gtest.h>
#include "fboss/fsdb/oper/ExtendedPathBuilder.h"
#include "fboss/fsdb/oper/Subscription.h"
#include "fboss/fsdb/server/ServiceHandler.h"
#include "fboss/fsdb/tests/client/FsdbTestClients.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"

#include <folly/ScopeGuard.h>
#include <folly/coro/BlockingWait.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/logging/LogLevel.h>
#include <folly/logging/LoggerDB.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <memory>

namespace {

const uint32_t kStateServeIntervalMs = 50;
const uint32_t kStatsServeIntervalMs = 50;
const uint32_t kSubscriptionServeQueueSize = 10;
auto constexpr kSubscriberId = "fsdb_test_subscriber";
auto constexpr kPublisherId = "fsdb_test_publisher";
auto constexpr kUnknownPublisherId = "publisher_unknown";
const std::vector<std::string> kPublishRoot{"agent"};
const std::string rawConfig = R"(
    {"publishers":{"fsdb_test_publisher":{"paths":[{"path":{"raw":["agent"]},"isExpected":false}]}},"subscribers":{":ExtSub_disallowed":{"trackReconnect":true,"allowExtendedSubscriptions":false},":ExtSub_allowed":{"trackReconnect":true,"allowExtendedSubscriptions":true}}}
)";
auto constexpr kUnknownPublisherPathConfig = R"(
    {"publishers":{"fsdb_test_publisher":{"paths":[{"path":{"raw":["unknown_path"]},"isExpected":false}]}},"subscribers":{":ExtSub_disallowed":{"trackReconnect":true,"allowExtendedSubscriptions":false},":ExtSub_allowed":{"trackReconnect":true,"allowExtendedSubscriptions":true}}}
)";
} // namespace

namespace facebook::fboss::fsdb::test {

template <typename TestParam>
class FsdbPubSubTest : public ::testing::Test {
 public:
  using PublisherT = typename TestParam::PublisherT;
  using PubUnitT = typename TestParam::PubUnitT;
  using SubscriberT = typename TestParam::SubscriberT;
  using SubUnitT = typename TestParam::SubUnitT;

  // Dispatch axes, so added parameterizations compose without touching helpers.
  static bool constexpr kIsStats = TestParam::PubSubStats;
  static bool constexpr kIsDelta = std::is_same_v<PubUnitT, OperDelta>;
  static bool constexpr kIsPath = std::is_same_v<PubUnitT, OperState>;
  static bool constexpr kIsPatch = std::is_same_v<PubUnitT, Patch>;
  static_assert(
      kIsDelta || kIsPath || kIsPatch,
      "TestParam::PubUnitT must be OperDelta, OperState or Patch");

  void SetUp() override {
    folly::LoggerDB::get().setLevel("fboss.thrift_cow", folly::LogLevel::DBG4);
    folly::LoggerDB::get().setLevel("fboss.fsdb", folly::LogLevel::DBG4);
    auto config = getFsdbConfig();
    FLAGS_deltaSubscriptionQueueFullMinSize = 4;
    FLAGS_deltaSubscriptionQueueMemoryLimit_mb = 1;
    // ServiceHandler reads the flag in its ctor.
    savedEnableHybridStateStorage_ = FLAGS_enableHybridStateStorage;
    FLAGS_enableHybridStateStorage = TestParam::EnableHybridStateStorage;
    fsdbTestServer_ = std::make_unique<FsdbTestServer>(
        std::move(config),
        0,
        kStateServeIntervalMs,
        kStatsServeIntervalMs,
        kSubscriptionServeQueueSize);
    publisherStreamEvbThread_ =
        std::make_unique<folly::ScopedEventBaseThread>();
    subscriberStreamEvbThread_ =
        std::make_unique<folly::ScopedEventBaseThread>();
    connRetryEvbThread_ = std::make_unique<folly::ScopedEventBaseThread>();
    std::vector<std::string> root;
    subscriber_ = createSubscriber(kSubscriberId);
    publisher_ = createPublisher(kPublisherId);
  }
  void TearDown() override {
    cancelConnections();
    fsdbTestServer_.reset();
    subscriber_.reset();
    publisher_.reset();
    subscriberStreamEvbThread_.reset();
    publisherStreamEvbThread_.reset();
    connRetryEvbThread_.reset();
    FLAGS_enableHybridStateStorage = savedEnableHybridStateStorage_;
  }

 protected:
  virtual std::shared_ptr<FsdbConfig> getFsdbConfig() {
    return FsdbConfig::fromRaw(rawConfig);
  }
  template <typename SubsT>
  std::unique_ptr<SubsT> createSubscriberImpl(
      const std::string& id,
      std::optional<std::function<void()>> onInitialSync = std::nullopt,
      std::optional<std::function<void()>> onDisconnect = std::nullopt) const {
    return std::make_unique<SubsT>(
        id,
        getSubscribePath<SubsT>(),
        subscriberStreamEvbThread_->getEventBase(),
        connRetryEvbThread_->getEventBase(),
        TestParam::PubSubStats,
        onInitialSync,
        onDisconnect);
  }
  template <typename SubsT>
  auto getSubscribePath() const {
    if constexpr (std::is_same_v<typename SubsT::SubUnitT, SubscriberChunk>) {
      RawOperPath p;
      p.path() = kPublishRoot;
      return std::map<SubscriptionKey, RawOperPath>{{0, std::move(p)}};
    } else {
      return kPublishRoot;
    }
  }
  std::unique_ptr<SubscriberT> createSubscriber(
      const std::string& id,
      std::optional<std::function<void()>> onInitialSync = std::nullopt,
      std::optional<std::function<void()>> onDisconnect = std::nullopt) const {
    return createSubscriberImpl<SubscriberT>(id, onInitialSync, onDisconnect);
  }
  std::unique_ptr<PublisherT> createPublisherImpl(
      const std::string& id,
      folly::EventBase* publisherEvb,
      const std::vector<std::string>& path = kPublishRoot) const {
    return std::make_unique<PublisherT>(
        id,
        path,
        publisherEvb,
        connRetryEvbThread_->getEventBase(),
        TestParam::PubSubStats);
  }
  std::unique_ptr<PublisherT> createPublisher(
      const std::string& id,
      const std::vector<std::string>& path = kPublishRoot) const {
    return createPublisherImpl(
        id, publisherStreamEvbThread_->getEventBase(), path);
  }
  void setupConnection(
      FsdbStreamClient& pubSub,
      bool updateServerPort = false) {
    pubSub.setConnectionOptions(
        utils::ConnectionOptions("::1", fsdbTestServer_->getFsdbPort()),
        updateServerPort);
    WITH_RETRIES_N(kConnectionRetries, {
      ASSERT_EVENTUALLY_TRUE(pubSub.isConnectedToServer())
          << pubSub.clientId() << " did not connected";
    });
  }
  void setupConnections(bool updateServerPort = false) {
    setupConnection(*publisher_, updateServerPort);
    checkPublishing({publisher_->clientId()});
    setupConnection(*subscriber_, updateServerPort);
    checkSubscribed({subscriber_->clientId()});
  }
  void checkSubscribed(
      const SubscriberIds& subscribers,
      int numSubscriptions = 1) {
    WITH_RETRIES({
      auto subscriberToInfo = folly::coro::blockingWait(
          fsdbTestServer_->getClient()->co_getOperSubscriberInfos(subscribers));
      for (auto subscriber : subscribers) {
        auto sitr = subscriberToInfo.find(subscriber);
        if (numSubscriptions) {
          ASSERT_EVENTUALLY_NE(sitr, subscriberToInfo.end());
          ASSERT_EVENTUALLY_EQ(sitr->second.size(), numSubscriptions);
        } else {
          ASSERT_EVENTUALLY_EQ(sitr, subscriberToInfo.end());
        }
      }
    });
  }
  void checkPublishing(const PublisherIds& publishers, int numActive = 1) {
    WITH_RETRIES({
      auto publisherToInfo = folly::coro::blockingWait(
          fsdbTestServer_->getClient()->co_getOperPublisherInfos(publishers));
      for (auto publisher : publishers) {
        auto pitr = publisherToInfo.find(publisher);
        auto metadata = fsdbTestServer_->getPublisherRootMetadata(
            *kPublishRoot.begin(), pubSubStats());
        if (numActive) {
          ASSERT_EVENTUALLY_NE(pitr, publisherToInfo.end());
          ASSERT_EVENTUALLY_EQ(pitr->second.size(), 1);
          ASSERT_EVENTUALLY_TRUE(metadata);
          ASSERT_EVENTUALLY_EQ(metadata->numOpenConnections, numActive);
        } else {
          ASSERT_EVENTUALLY_EQ(pitr, publisherToInfo.end());
          ASSERT_EVENTUALLY_FALSE(metadata);
        }
      }
    });
  }
  void cancelConnection(FsdbStreamClient& pubSub) {
    pubSub.cancel();
    WITH_RETRIES_N(kRetries, {
      ASSERT_EVENTUALLY_TRUE(!pubSub.isConnectedToServer());
      ASSERT_EVENTUALLY_TRUE(!pubSub.serviceLoopRunning());
    });
  }
  void cancelConnections() {
    if (subscriber_) {
      cancelConnection(*subscriber_);
    }
    if (publisher_) {
      cancelConnection(*publisher_);
    }
  }

  void publishAgentConfig(const cfg::AgentConfig& config) {
    if constexpr (std::is_same_v<typename TestParam::PubUnitT, OperDelta>) {
      publisher_->write(makeDelta(config));
    } else if constexpr (std::is_same_v<PubUnitT, OperState>) {
      publisher_->write(makeState(config));
    } else if constexpr (std::is_same_v<PubUnitT, Patch>) {
      publisher_->write(makePatch(config));
    }
  }
  void publishPortStats(
      const folly::F14FastMap<std::string, HwPortStats>& portStats) {
    if constexpr (std::is_same_v<PubUnitT, OperDelta>) {
      publisher_->write(makeDelta(portStats));
    } else if constexpr (std::is_same_v<PubUnitT, OperState>) {
      publisher_->write(makeState(portStats));
    } else if constexpr (std::is_same_v<PubUnitT, Patch>) {
      publisher_->write(makePatch(portStats));
    }
  }
  bool pubSubStats() const {
    return TestParam::PubSubStats;
  }
  bool isPath() const {
    return std::is_same_v<PubUnitT, OperState>;
  }
  bool isDelta() const {
    return std::is_same_v<PubUnitT, OperDelta>;
  }
  bool isPatch() const {
    return std::is_same_v<PubUnitT, Patch>;
  }
  template <typename SubRequestT>
  auto subscribe(const SubRequestT& reqIn) {
    auto req = std::make_unique<SubRequestT>(reqIn);
    if constexpr (kIsDelta && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeOperStateDelta(
              std::move(req)));
    } else if constexpr (kIsPath && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeOperStatePath(
              std::move(req)));
    } else if constexpr (kIsDelta && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeOperStatsDelta(
              std::move(req)));
    } else if constexpr (kIsPath && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeOperStatsPath(
              std::move(req)));
    } else if constexpr (kIsPatch && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeState(
              std::move(req)));
    } else {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeStats(
              std::move(req)));
    }
  }

  template <typename SubRequestT>
  auto subscribeExtended(const SubRequestT& reqIn) {
    if constexpr (kIsDelta && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->getClient()
              ->co_subscribeOperStateDeltaExtended(reqIn));
    } else if constexpr (kIsPath && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->getClient()->co_subscribeOperStatePathExtended(
              reqIn));
    } else if constexpr (kIsDelta && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->getClient()
              ->co_subscribeOperStatsDeltaExtended(reqIn));
    } else if constexpr (kIsPath && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->getClient()->co_subscribeOperStatsPathExtended(
              reqIn));
    } else if constexpr (kIsPatch && !kIsStats) {
      auto req = std::make_unique<SubRequestT>(reqIn);
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeStateExtended(
              std::move(req)));
    } else {
      auto req = std::make_unique<SubRequestT>(reqIn);
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_subscribeStatsExtended(
              std::move(req)));
    }
  }

  auto makePubRequest(std::string id, std::vector<std::string> path) {
    if constexpr (std::is_same_v<PubUnitT, Patch>) {
      PubRequest req;
      req.clientId()->client() = FsdbClient::ADHOC;
      req.clientId()->instanceId() = std::move(id);
      req.path()->path() = std::move(path);
      return req;
    } else {
      OperPubRequest req;
      req.publisherId() = std::move(id);
      req.path()->raw() = std::move(path);
      return req;
    }
  }

  auto makeSubRequest(std::string id, std::vector<std::string> path) {
    if constexpr (std::is_same_v<PubUnitT, Patch>) {
      SubRequest req;
      req.clientId()->client() = FsdbClient::ADHOC;
      req.clientId()->instanceId() = std::move(id);
      RawOperPath p;
      p.path() = std::move(path);
      req.paths() = {{0, std::move(p)}};
      return req;
    } else {
      OperSubRequest req;
      req.subscriberId() = std::move(id);
      req.path()->raw() = std::move(path);
      return req;
    }
  }

  auto makeExtendedSubRequest(
      std::string id,
      const std::vector<ExtendedOperPath>& paths) {
    if constexpr (std::is_same_v<PubUnitT, Patch>) {
      SubRequest req;
      req.clientId()->client() = FsdbClient::ADHOC;
      req.clientId()->instanceId() = std::move(id);
      req.extPaths() = {};
      int idx = 0;
      std::map<SubscriptionKey, ExtendedOperPath> subPaths;
      for (auto& path : paths) {
        req.extPaths()[idx] = path;
        idx++;
      }
      return req;
    } else {
      OperSubRequestExtended req;
      req.subscriberId() = std::move(id);
      req.paths() = paths;
      return req;
    }
  }

  template <typename Req>
  auto setupPublisher(const Req& reqIn) {
    auto req = std::make_unique<Req>(reqIn);
    if constexpr (kIsDelta && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishOperStateDelta(
              std::move(req)));
    } else if constexpr (kIsPath && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishOperStatePath(
              std::move(req)));
    } else if constexpr (kIsDelta && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishOperStatsDelta(
              std::move(req)));
    } else if constexpr (kIsPath && kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishOperStatsPath(
              std::move(req)));
    } else if constexpr (kIsPatch && !kIsStats) {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishState(
              std::move(req)));
    } else {
      return folly::coro::blockingWait(
          this->fsdbTestServer_->serviceHandler().co_publishStats(
              std::move(req)));
    }
  }

  static auto constexpr kRetries = 10;
  static auto constexpr kConnectionRetries = 60;
  std::unique_ptr<FsdbTestServer> fsdbTestServer_;
  std::unique_ptr<folly::ScopedEventBaseThread> subscriberStreamEvbThread_;
  std::unique_ptr<folly::ScopedEventBaseThread> publisherStreamEvbThread_;
  std::unique_ptr<folly::ScopedEventBaseThread> connRetryEvbThread_;
  std::unique_ptr<SubscriberT> subscriber_;
  std::unique_ptr<PublisherT> publisher_;
  bool savedEnableHybridStateStorage_{false};
};

using TestTypes = ::testing::Types<
    DeltaPubSubForState,
    StatePubSubForState,
    PatchPubSubForState,
    DeltaPubSubForStats,
    StatePubSubForStats,
    PatchPubSubForStats,
    // State variants only: the stats tree is always COW.
    DeltaPubSubForHybridState,
    StatePubSubForHybridState,
    PatchPubSubForHybridState>;

TYPED_TEST_SUITE(FsdbPubSubTest, TestTypes);

TYPED_TEST(FsdbPubSubTest, connectAndCancel) {
  if (this->pubSubStats()) {
    EXPECT_EQ(
        this->publisher_->getCounterPrefix(),
        this->isDelta()       ? "fsdbDeltaStatPublisher_agent"
            : this->isPatch() ? "fsdbPatchStatPublisher_agent"
                              : "fsdbPathStatPublisher_agent");
    EXPECT_EQ(
        this->subscriber_->getCounterPrefix(),
        this->isDelta()       ? "fsdbDeltaStatSubscriber_agent"
            : this->isPatch() ? "fsdbPatchStatSubscriber_agent"
                              : "fsdbPathStatSubscriber_agent");
  } else {
    EXPECT_EQ(
        this->publisher_->getCounterPrefix(),
        this->isDelta()       ? "fsdbDeltaStatePublisher_agent"
            : this->isPatch() ? "fsdbPatchStatePublisher_agent"
                              : "fsdbPathStatePublisher_agent");
    EXPECT_EQ(
        this->subscriber_->getCounterPrefix(),
        this->isDelta()       ? "fsdbDeltaStateSubscriber_agent"
            : this->isPatch() ? "fsdbPatchStateSubscriber_agent"
                              : "fsdbPathStateSubscriber_agent");
  }
  this->setupConnections();
  this->cancelConnections();

  // In tests, we don't start the publisher threads
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ServiceData::get()->getCounter("watchdog_thread_heartbeat_missed"),
      0);
}

TYPED_TEST(FsdbPubSubTest, rePublishSubscribe) {
  // Cancel and resubscribe
  this->setupConnections();
  this->cancelConnections();
  // Setup same connections again, should go through, since the prev
  // ones were cancelled and thus state on server should be cleared.
  auto publisher = this->createPublisher(kPublisherId);
  this->setupConnection(*publisher);
  auto subscriber = this->createSubscriber(kSubscriberId);
  this->setupConnection(*subscriber);
}

TYPED_TEST(FsdbPubSubTest, dupSubscriber) {
  auto req = this->makeSubRequest("test", {"agent"});
  auto res = this->subscribe(req);
  // we've had errors sneak through this in the past because the subscription
  // key depended on the current timestamp. Introducing a sleep To make sure we
  // validate subscriber unique-ness even if timing is a factor
  // @lint-ignore CLANGTIDY
  std::this_thread::sleep_for(std::chrono::seconds(3));
  EXPECT_THROW({ auto res2 = this->subscribe(req); }, FsdbException);

  req.forceSubscribe() = true;
  EXPECT_NO_THROW({ auto res2 = this->subscribe(req); });
}

TYPED_TEST(FsdbPubSubTest, multiplePublishers) {
  this->setupConnections();
  folly::ScopedEventBaseThread pub2Thread;
  // Same path different publisher Id
  auto publisher2 =
      this->createPublisherImpl("publisher2", pub2Thread.getEventBase());
  this->setupConnection(*publisher2);
  // Same publisherId, different path
  folly::ScopedEventBaseThread pub3Thread;
  auto publisher3 =
      this->createPublisherImpl("publisher3", pub3Thread.getEventBase());
  this->setupConnection(*publisher3);
  // Check publishing status
  this->checkPublishing({publisher2->clientId()}, 3);
  this->checkPublishing({kPublisherId}, 3 /*paths*/);
  // Cancel connections before evb thread are stopped
  this->cancelConnection(*publisher2);
  // Publisher 2 should be pruned
  this->checkPublishing({"publisher3"}, 2);
  this->cancelConnection(*publisher3);
  // Back to 1
  this->checkPublishing({kPublisherId});
}

TYPED_TEST(FsdbPubSubTest, publish) {
  this->setupConnections();
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  // initial sync after first publish
  this->subscriber_->assertQueue(1, this->kRetries);
}

TYPED_TEST(FsdbPubSubTest, dupPublisher) {
  auto req = this->makePubRequest("test", {"agent"});
  auto res = this->setupPublisher(req);
  // we've had errors sneak through this in the past because the publisher
  // key depended on the current timestamp. Introducing a sleep To make sure
  // we validate publisher unique-ness even if timing is a factor
  // @lint-ignore CLANGTIDY
  std::this_thread::sleep_for(std::chrono::seconds(3));
  EXPECT_THROW(this->setupPublisher(req), FsdbException);
}

TYPED_TEST(FsdbPubSubTest, reconnect) {
  this->setupConnections();
  std::array<std::string, 2> counterPrefixes{
      this->publisher_->getCounterPrefix(),
      this->subscriber_->getCounterPrefix()};

  for (const auto& counterPrefix : counterPrefixes) {
    EXPECT_EQ(
        fb303::ServiceData::get()->getCounter(counterPrefix + ".connected"), 1);
  }
  // Break server, assert for disconnect
  this->fsdbTestServer_.reset();
  // Need to publish something in order to detect that server has
  // gone away
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  WITH_RETRIES_N(this->kRetries, {
    ASSERT_EVENTUALLY_TRUE(!this->publisher_->isConnectedToServer());
    ASSERT_EVENTUALLY_TRUE(!this->subscriber_->isConnectedToServer());
    // In tests, we don't start the publisher threads
    fb303::ThreadCachedServiceData::get()->publishStats();
    for (const auto& counterPrefix : counterPrefixes) {
      EXPECT_EVENTUALLY_TRUE(
          fb303::ServiceData::get()->getCounter(
              counterPrefix + ".disconnects.sum.60") >= 1);
      EXPECT_EQ(
          fb303::ServiceData::get()->getCounter(counterPrefix + ".connected"),
          0);
    }
    // check aggregate counter
    EXPECT_EVENTUALLY_TRUE(
        fb303::ServiceData::get()->getCounter(
            "fsdb_streams.disconnects.sum.60") >= 1);
  });
  // Setup server again and assert for reconnect
  this->fsdbTestServer_ = std::make_unique<FsdbTestServer>();
  // Need to update server address since we are binding to ephemeral port
  // which will change on server recreate
  this->setupConnections(true);
  // Publish new delta after reconnect, assert that delta gets through
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(2));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}, {"bar", "baz"}}));
  }
  // Initial sync on first publish
  this->subscriber_->assertQueue(1, this->kRetries);
}

TYPED_TEST(FsdbPubSubTest, publishToMultipleSubscribers) {
  this->setupConnections();
  auto subscriber2 = this->createSubscriber("fsdb_test_subscriber2");
  this->setupConnection(*subscriber2);
  auto subscribers = {this->subscriber_->clientId(), subscriber2->clientId()};
  this->checkSubscribed(subscribers);
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  // Initial sync only after first publish
  this->subscriber_->assertQueue(1, this->kRetries);
  subscriber2->assertQueue(1, this->kRetries);
  // check subscription serve queue data stats
  WITH_RETRIES({
    auto subscriberToInfo = folly::coro::blockingWait(
        this->fsdbTestServer_->getClient()->co_getOperSubscriberInfos(
            subscribers));
    for (auto subscriber : subscribers) {
      auto sitr = subscriberToInfo.find(subscriber);
      ASSERT_EVENTUALLY_NE(sitr, subscriberToInfo.end());
      if (sitr != subscriberToInfo.end()) {
        OperSubscriberInfo expectedInfo = sitr->second[0];
        ASSERT_EQ(expectedInfo.enqueuedDataSize().has_value(), true);
        ASSERT_EQ(expectedInfo.servedDataSize().has_value(), true);
        if (this->isPath()) {
          // for path subscriptions, queue data size is not tracked
          ASSERT_EQ(*expectedInfo.enqueuedDataSize(), 0);
          ASSERT_EQ(*expectedInfo.servedDataSize(), 0);
        } else {
          ASSERT_GT(*expectedInfo.enqueuedDataSize(), 0);
          ASSERT_GT(*expectedInfo.servedDataSize(), 0);
        }
      }
    }
  });
}

TYPED_TEST(FsdbPubSubTest, publisherDropCausesSubscribersReset) {
  this->setupConnections();
  auto subscriber2 = this->createSubscriber("fsdb_test_subscriber2");
  this->setupConnection(*subscriber2);
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  // initial sync after first publish
  WITH_RETRIES_N(this->kRetries, {
    ASSERT_EVENTUALLY_TRUE(this->subscriber_->initialSyncDone());
    ASSERT_EVENTUALLY_TRUE(subscriber2->initialSyncDone());
  });
  this->cancelConnection(*this->publisher_);
  // Subscriber should reset after publisher goes away
  WITH_RETRIES_N(this->kRetries, {
    ASSERT_EVENTUALLY_FALSE(this->subscriber_->initialSyncDone());
    ASSERT_EVENTUALLY_FALSE(subscriber2->initialSyncDone());
  });
  // Subscriber should auto reconnect after resceiving a reset
  WITH_RETRIES_N(this->kRetries, {
    ASSERT_EVENTUALLY_TRUE(this->subscriber_->isConnectedToServer());
    ASSERT_EVENTUALLY_TRUE(subscriber2->isConnectedToServer());
  });
}

TYPED_TEST(FsdbPubSubTest, publishToMultipleSubscribersCancelOne) {
  this->setupConnections();
  auto subscriber2 = this->createSubscriber("fsdb_test_subscriber2");
  this->setupConnection(*subscriber2);
  this->checkSubscribed(
      {this->subscriber_->clientId(), subscriber2->clientId()});
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  // Initial sync only after first publish
  this->subscriber_->assertQueue(1, this->kRetries);
  subscriber2->assertQueue(1, this->kRetries);
  this->cancelConnection(*this->subscriber_);
  this->checkSubscribed({this->subscriber_->clientId()}, 0 /* not subscribed*/);
  this->checkSubscribed({subscriber2->clientId()});
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(2));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}, {"bar", "baz"}}));
  }
  this->subscriber_->assertQueue(1, this->kRetries);
  subscriber2->assertQueue(2, this->kRetries);
}

TYPED_TEST(FsdbPubSubTest, publishToMultipleSubscribersOfDifferentTypes) {
  this->setupConnections();
  auto subscriber2 =
      this->template createSubscriberImpl<TestFsdbStateSubscriber>(
          "fsdb_test_subscriber2");
  auto subscriber3 =
      this->template createSubscriberImpl<TestFsdbDeltaSubscriber>(
          "fsdb_test_subscriber3");
  this->setupConnection(*subscriber2);
  this->checkSubscribed({subscriber2->clientId()});
  this->setupConnection(*subscriber3);
  this->checkSubscribed({subscriber2->clientId(), subscriber3->clientId()});
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }
  // Initial sync only after first publish
  this->subscriber_->assertQueue(1, this->kRetries);
  subscriber2->assertQueue(1, this->kRetries);
  subscriber3->assertQueue(1, this->kRetries);
}

TYPED_TEST(FsdbPubSubTest, subscriberReadyBeforePublisher) {
  auto publisherId = this->publisher_->clientId();
  this->publisher_.reset();
  this->checkPublishing({publisherId}, 0);
  this->subscriber_.reset();
  this->subscriber_ = this->createSubscriber(kSubscriberId);
  this->setupConnection(*this->subscriber_);
  // No publisher sync ever. Subscriber just waits
  EXPECT_THROW(
      checkWithRetry([this]() { return this->subscriber_->queueSize() == 1; }),
      FbossError);
}

TYPED_TEST(FsdbPubSubTest, verifyExtendedSubRestrictionNotEnforced) {
  FLAGS_checkSubscriberConfig = true;
  FLAGS_enforceSubscriberConfig = false;
  std::vector<ExtendedOperPath> paths;
  paths.emplace_back(ext_path_builder::raw("agent").get());
  auto req = this->makeExtendedSubRequest("TestSub:ExtSub_disallowed", paths);
  EXPECT_NO_THROW(this->subscribeExtended(req));
}

TYPED_TEST(FsdbPubSubTest, verifyExtendedSubAllowed) {
  FLAGS_checkSubscriberConfig = true;
  FLAGS_enforceSubscriberConfig = true;
  std::vector<ExtendedOperPath> paths;
  paths.emplace_back(ext_path_builder::raw("agent").get());
  auto req = this->makeExtendedSubRequest("TestSub:ExtSub_allowed", paths);
  EXPECT_NO_THROW(this->subscribeExtended(req));
}

TYPED_TEST(FsdbPubSubTest, verifyWildcardSubDenied) {
  FLAGS_checkSubscriberConfig = true;
  FLAGS_enforceSubscriberConfig = true;
  std::vector<ExtendedOperPath> paths;
  paths.emplace_back(
      ext_path_builder::raw("agent").raw("switchState").regex("port.*").get());
  auto req = this->makeExtendedSubRequest("TestSub:ExtSub_allowed", paths);
  EXPECT_THROW(this->subscribeExtended(req), FsdbException);
}

TYPED_TEST(FsdbPubSubTest, verifyExtendedSubRestricted) {
  FLAGS_checkSubscriberConfig = true;
  FLAGS_enforceSubscriberConfig = true;
  std::vector<ExtendedOperPath> paths;
  paths.emplace_back(ext_path_builder::raw("agent").get());
  auto req = this->makeExtendedSubRequest("TestSub:ExtSub_disallowed", paths);
  EXPECT_THROW(this->subscribeExtended(req), FsdbException);
}

TYPED_TEST(FsdbPubSubTest, verifyUnknownPublisherNotEnforced) {
  FLAGS_checkOperOwnership = true;
  FLAGS_enforcePublisherConfig = false;
  auto publisher = this->createPublisher(kUnknownPublisherId);
  this->setupConnection(*publisher);
  this->checkPublishing({publisher->clientId()});
}

TYPED_TEST(FsdbPubSubTest, verifyUnknownPublisherRejected) {
  FLAGS_checkOperOwnership = true;
  FLAGS_enforcePublisherConfig = true;
  auto publisher = this->createPublisher(kUnknownPublisherId);
  publisher->setConnectionOptions(
      utils::ConnectionOptions("::1", this->fsdbTestServer_->getFsdbPort()),
      false);
  WITH_RETRIES_N(
      this->kRetries, ASSERT_FALSE(publisher->isConnectedToServer()));
}

template <typename TestParam>
class FsdbUnknownPublisherPathTest : public FsdbPubSubTest<TestParam> {
 protected:
  std::shared_ptr<FsdbConfig> getFsdbConfig() override {
    return FsdbConfig::fromRaw(kUnknownPublisherPathConfig);
  }
};

TYPED_TEST_SUITE(FsdbUnknownPublisherPathTest, TestTypes);

TYPED_TEST(
    FsdbUnknownPublisherPathTest,
    verifyUnknownPublisherPathNotEnforced) {
  FLAGS_checkOperOwnership = true;
  FLAGS_enforcePublisherConfig = false;
  auto publisher = this->createPublisher(kPublisherId);
  this->setupConnection(*publisher);
  this->checkPublishing({publisher->clientId()});
}

TYPED_TEST(FsdbUnknownPublisherPathTest, verifyUnknownPublisherPathRejected) {
  FLAGS_checkOperOwnership = true;
  FLAGS_enforcePublisherConfig = true;
  auto publisher = this->createPublisher(kPublisherId);
  publisher->setConnectionOptions(
      utils::ConnectionOptions("::1", this->fsdbTestServer_->getFsdbPort()),
      false);
  WITH_RETRIES_N(
      this->kRetries, ASSERT_FALSE(publisher->isConnectedToServer()));
}

class DeltaStatePubSubTest : public FsdbPubSubTest<DeltaPubSubForState> {
 protected:
  void publishRawJson(const std::string& json1, const std::string& json2) {
    OperPath deltaPath;
    deltaPath.raw() = {"agent"};
    OperDeltaUnit deltaUnit;
    deltaUnit.path() = deltaPath;
    deltaUnit.newState() = json1;
    OperDelta delta;
    delta.changes()->push_back(deltaUnit);
    deltaUnit.newState() = json2;
    delta.changes()->push_back(deltaUnit);
    delta.protocol() = OperProtocol::SIMPLE_JSON;
    publisher_->write(std::move(delta));
  }
};

TEST_F(DeltaStatePubSubTest, dropsInvalidChange) {
  this->setupConnections();
  cfg::AgentConfig config;
  config.defaultCommandLineArgs() = {{"foo", "bar"}};

  // Craft delta with a valid change and an invalid change
  OperPath validPath;
  validPath.raw() = {"agent", "config"};
  OperDeltaUnit validUnit;
  validUnit.path() = validPath;
  validUnit.newState() =
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(config);

  OperPath nonExistentPath;
  OperDeltaUnit nonExistentUnit;
  nonExistentPath.raw() = {"agent", "nonExistentPath"};
  nonExistentUnit.path() = nonExistentPath;
  nonExistentUnit.newState() = "some value";

  OperPath invalidPath;
  OperDeltaUnit typeMistmatchUnit;
  invalidPath.raw() = {"agent", "switchState"};
  typeMistmatchUnit.path() = nonExistentPath;
  typeMistmatchUnit.newState() = "some value"; // should be a struct

  OperDelta delta;
  delta.changes() = {validUnit, nonExistentUnit, typeMistmatchUnit};
  delta.protocol() =
      OperProtocol::BINARY; // TODO: extend test to cover all protocols

  // Publisher should not be rejected, and subscriber should still get served
  this->subscriber_->assertQueue(0, this->kRetries);
  publisher_->write(std::move(delta));
  this->subscriber_->assertQueue(1, this->kRetries);

  auto counterPrefix = this->publisher_->getCounterPrefix();
  WITH_RETRIES_N(this->kRetries, {
    EXPECT_EVENTUALLY_TRUE(this->publisher_->isConnectedToServer());
    // In tests, we don't start the publisher threads
    fb303::ThreadCachedServiceData::get()->publishStats();
    EXPECT_EVENTUALLY_EQ(
        fb303::ServiceData::get()->getCounter(
            counterPrefix + ".disconnects.sum.60"),
        0);
  });
}

using PathSubscriberTestTypes =
    ::testing::Types<StatePubSubForState, StatePubSubForStats>;

template <typename TestParam>
class FsdbSlowPathSubscriberTest : public FsdbPubSubTest<TestParam> {};

TYPED_TEST_SUITE(FsdbSlowPathSubscriberTest, PathSubscriberTestTypes);

TYPED_TEST(FsdbSlowPathSubscriberTest, slowSubscriberCoalescedUpdates) {
  uint32_t queueSize = this->pubSubStats()
      ? FLAGS_statsSubscriptionServeQueueSize
      : kSubscriptionServeQueueSize;
  uint32_t updatesPublished = 100 + queueSize;
  uint32_t subscriptionServeIntervalMs =
      this->pubSubStats() ? kStatsServeIntervalMs : kStateServeIntervalMs;
  uint32_t publishIntervalMs = subscriptionServeIntervalMs + 20;
  this->setupConnection(*this->publisher_, false);
  this->checkPublishing({this->publisher_->clientId()});

  // pause subscriber on initial sync long enough for all updates to be
  // published and served so that queue builds up.
  folly::Baton<> waitForInitialSync, waitForDisconnect;
  folly::Baton<> resumeDataCb, resumeReconnect;
  bool initialSyncOnce{false}, disconnectOnce{false};
  auto slowSub = this->createSubscriber(
      "fsdb_slow_subscriber",
      [&waitForInitialSync, &resumeDataCb, &initialSyncOnce]() {
        if (!initialSyncOnce) {
          initialSyncOnce = true;
          waitForInitialSync.post();
          resumeDataCb.wait();
        }
      },
      [&waitForDisconnect, &resumeReconnect, &disconnectOnce]() {
        if (!disconnectOnce) {
          disconnectOnce = true;
          waitForDisconnect.post();
          resumeReconnect.wait();
        }
      });
  this->setupConnection(*slowSub);
  this->checkSubscribed({slowSub->clientId()});

  // publish update to trigger initial sync
  int updateNum{0};
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  updateNum++;

  // wait for initial sync - subscriber is now blocked in data callback
  waitForInitialSync.wait();

  // publish remaining updates while subscriber is blocked
  for (; updateNum < updatesPublished; updateNum++) {
    if (this->pubSubStats()) {
      this->publishPortStats(makePortStats(updateNum));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // post another update
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));

  // validate fb303 counter is incremented when coalescing starts
  WITH_RETRIES_N(90, {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = folly::to<std::string>(
        "fsdb.handler.",
        slowSub->clientId(),
        ".",
        kNumSubscriptionServesCoalesced,
        ".sum");
    auto counterValue = fb303::ServiceData::get()->getCounter(counterName);
    EXPECT_EVENTUALLY_GE(counterValue, 1);
  });

  // resume subscriber data callback after all updates are published
  resumeDataCb.post();

  // wait for last update to be served
  WITH_RETRIES_N(90, {
    auto received = slowSub->queuedUnits();
    EXPECT_EVENTUALLY_GT(received.size(), 0);
    fsdb::OperState lastUpdate = received.back();
    if (this->pubSubStats()) {
      auto agentStats =
          apache::thrift::BinarySerializer::deserialize<AgentStats>(
              *lastUpdate.contents());
      folly::F14FastMap<std::string, HwPortStats> portStats =
          *agentStats.hwPortStats();
      for (auto& [_, stats] : portStats) {
        EXPECT_EVENTUALLY_EQ(stats.inBytes_(), updateNum);
      }
    } else {
      auto agentData = apache::thrift::BinarySerializer::deserialize<AgentData>(
          *lastUpdate.contents());
      cfg::AgentConfig agentConfig = *agentData.config();
      EXPECT_EVENTUALLY_EQ(agentConfig.defaultCommandLineArgs()->size(), 1);
      std::string testStr = folly::to<std::string>("bar", updateNum);
      EXPECT_EVENTUALLY_EQ(
          agentConfig.defaultCommandLineArgs()["foo"], testStr);
    }
  });
  // verify that updates got coalesced
  EXPECT_LT(slowSub->queueSize(), updatesPublished);

  // validate subscription is not disconnected
  auto info = slowSub->getInfo();
  ASSERT_EQ(
      info.state, FsdbStreamClient::ReconnectingThriftClient::State::CONNECTED);

  // validate counter for chunks coalesced
  WITH_RETRIES_N(90, {
    auto subscriberId = slowSub->clientId();
    auto subscriberToInfo = folly::coro::blockingWait(
        this->fsdbTestServer_->getClient()->co_getOperSubscriberInfos(
            {subscriberId}));
    auto sitr = subscriberToInfo.find(subscriberId);
    ASSERT_EVENTUALLY_NE(sitr, subscriberToInfo.end());
    ASSERT_EVENTUALLY_EQ(sitr->second.size(), 1);
    if (sitr != subscriberToInfo.end()) {
      OperSubscriberInfo expectedInfo = sitr->second[0];
      ASSERT_EVENTUALLY_EQ(
          expectedInfo.subscriptionChunksCoalesced().has_value(), true);
      if (expectedInfo.subscriptionChunksCoalesced().has_value()) {
        ASSERT_EVENTUALLY_GT(*expectedInfo.subscriptionChunksCoalesced(), 0);
      }
    }
  });

  // validate fb303 counter does not increment for transition from coalescing to
  // not coalescing
  WITH_RETRIES_N(90, {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = folly::to<std::string>(
        "fsdb.handler.",
        slowSub->clientId(),
        ".",
        kNumSubscriptionServesCoalesced,
        ".sum.60");
    auto counterValue = fb303::ServiceData::get()->getCounter(counterName);
    // Counter should eventually get back to 0
    EXPECT_EVENTUALLY_EQ(counterValue, 0);
  });

  resumeReconnect.post();
}

using DeltaSubscriberTestTypes = ::testing::Types<
    DeltaPubSubForState,
    PatchPubSubForState,
    DeltaPubSubForStats,
    PatchPubSubForStats>;

template <typename TestParam>
class FsdbSlowDeltaSubscriberTest : public FsdbPubSubTest<TestParam> {};

TYPED_TEST_SUITE(FsdbSlowDeltaSubscriberTest, DeltaSubscriberTestTypes);

TYPED_TEST(FsdbSlowDeltaSubscriberTest, slowSubscriberDisconnectThreshold) {
  // verify threshold for number of pending updates for slow subscriber
  // disconnect

  uint32_t queueSize = this->pubSubStats()
      ? FLAGS_statsSubscriptionServeQueueSize
      : kSubscriptionServeQueueSize;
  uint32_t updatesPublished = 100 + queueSize;
  uint32_t subscriptionServeIntervalMs =
      this->pubSubStats() ? kStatsServeIntervalMs : kStateServeIntervalMs;
  uint32_t publishIntervalMs = subscriptionServeIntervalMs + 20;
  this->setupConnection(*this->publisher_, false);
  this->checkPublishing({this->publisher_->clientId()});

  // pause subscriber on initial sync long enough for all updates to be
  // published and served so that queue builds up.
  folly::Baton<> waitForInitialSync, waitForDisconnect;
  folly::Baton<> resumeDataCb, resumeReconnect;
  bool initialSyncOnce{false}, disconnectOnce{false};
  auto slowSub = this->createSubscriber(
      "fsdb_slow_subscriber",
      [&waitForInitialSync, &resumeDataCb, &initialSyncOnce]() {
        if (!initialSyncOnce) {
          initialSyncOnce = true;
          waitForInitialSync.post();
          resumeDataCb.wait();
        }
      },
      [&waitForDisconnect, &resumeReconnect, &disconnectOnce]() {
        if (!disconnectOnce) {
          disconnectOnce = true;
          waitForDisconnect.post();
          resumeReconnect.wait();
        }
      });
  this->setupConnection(*slowSub);
  this->checkSubscribed({slowSub->clientId()});

  // publish update to trigger initial sync
  int updateNum{0};
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  updateNum++;

  // wait for initial sync - subscriber is now blocked in data callback
  waitForInitialSync.wait();

  // publish remaining updates while subscriber is blocked
  for (; updateNum < updatesPublished; updateNum++) {
    if (this->pubSubStats()) {
      this->publishPortStats(makePortStats(updateNum));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // validate server does not disconnect subscriber yet
  SubscriptionInfo info = slowSub->getInfo();
  ASSERT_EQ(
      info.state, FsdbStreamClient::ReconnectingThriftClient::State::CONNECTED);

  // post another update and validate that server disconnects the slow
  // subscriber
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));

  // resume subscriber data callback after all updates are published
  resumeDataCb.post();

  ASSERT_TRUE(waitForDisconnect.try_wait_for(std::chrono::seconds(120)));
  info = slowSub->getInfo();
  ASSERT_EQ(
      info.disconnectReason, FsdbErrorCode::SUBSCRIPTION_SERVE_QUEUE_FULL);

  WITH_RETRIES_N(90, {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = fmt::format(
        "{}.subscriber.{}.disconnects.slow_subscriber.count",
        this->pubSubStats() ? "stats" : "fsdb",
        "unspecified");
    EXPECT_EVENTUALLY_GT(fb303::ServiceData::get()->getCounter(counterName), 0);
  });

  resumeReconnect.post();
}

TYPED_TEST(FsdbSlowDeltaSubscriberTest, memoryAwareDisconnect) {
  // verify memory-based threshold for subscription disconnect

  uint32_t updatesPublished = 100;
  uint32_t subscriptionServeIntervalMs =
      this->pubSubStats() ? kStatsServeIntervalMs : kStateServeIntervalMs;
  uint32_t publishIntervalMs = subscriptionServeIntervalMs + 20;
  this->setupConnection(*this->publisher_, false);
  this->checkPublishing({this->publisher_->clientId()});

  // pause subscriber on initial sync long enough for all updates to be
  // published and served so that queue builds up.
  folly::Baton<> waitForInitialSync, waitForDisconnect;
  folly::Baton<> resumeDataCb, resumeReconnect;
  bool initialSyncOnce{false}, disconnectOnce{false};
  auto slowSub = this->createSubscriber(
      "fsdb_slow_subscriber",
      [&waitForInitialSync, &resumeDataCb, &initialSyncOnce]() {
        if (!initialSyncOnce) {
          initialSyncOnce = true;
          waitForInitialSync.post();
          resumeDataCb.wait();
        }
      },
      [&waitForDisconnect, &resumeReconnect, &disconnectOnce]() {
        if (!disconnectOnce) {
          disconnectOnce = true;
          waitForDisconnect.post();
          resumeReconnect.wait();
        }
      });
  this->setupConnection(*slowSub);
  this->checkSubscribed({slowSub->clientId()});

  // publish update to trigger initial sync
  int updateNum{0};
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  updateNum++;

  // wait for initial sync - subscriber is now blocked in data callback
  waitForInitialSync.wait();

  // publish remaining normal updates while subscriber is blocked
  for (; updateNum <= updatesPublished; updateNum++) {
    if (this->pubSubStats()) {
      this->publishPortStats(makePortStats(updateNum));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // post large update to build up queue memory
  int updateSize = (1024 * 1024 * FLAGS_deltaSubscriptionQueueMemoryLimit_mb);
  if (this->pubSubStats()) {
    int statsUpdateSize = 25;
    this->publishPortStats(makeLargePortStats(updateNum, statsUpdateSize));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  } else {
    char val = 'a';
    this->publishAgentConfig(makeLargeAgentConfig("foo", updateSize, val));
  }
  updateNum++;
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));

  // validate server does not disconnect subscriber yet
  SubscriptionInfo info = slowSub->getInfo();
  ASSERT_EQ(
      info.state, FsdbStreamClient::ReconnectingThriftClient::State::CONNECTED);

  // trigger memory-based queue full detection
  for (int i = 0; i < FLAGS_deltaSubscriptionQueueFullMinSize; i++) {
    if (this->pubSubStats()) {
      int statsUpdateSize = 25;
      this->publishPortStats(makePortStats(updateNum + i));
      this->publishPortStats(makeLargePortStats(updateNum, statsUpdateSize));
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum + i);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    updateNum++;
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // resume subscriber data callback after all updates are published
  // and validate that server disconnects the slow subscriber
  resumeDataCb.post();

  ASSERT_TRUE(waitForDisconnect.try_wait_for(std::chrono::seconds(120)));
  info = slowSub->getInfo();
  ASSERT_EQ(
      info.disconnectReason, FsdbErrorCode::SUBSCRIPTION_SERVE_QUEUE_FULL);

  WITH_RETRIES_N(90, {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = fmt::format(
        "{}.subscriber.{}.disconnects.slow_subscriber.count",
        this->pubSubStats() ? "stats" : "fsdb",
        "unspecified");
    EXPECT_EVENTUALLY_GT(fb303::ServiceData::get()->getCounter(counterName), 0);
  });

  resumeReconnect.post();
}

TYPED_TEST(FsdbSlowDeltaSubscriberTest, slowSubscriber) {
  uint32_t subscriptionServeIntervalMs =
      this->pubSubStats() ? kStatsServeIntervalMs : kStateServeIntervalMs;
  uint32_t publishIntervalMs = subscriptionServeIntervalMs + 20;
  this->setupConnection(*this->publisher_, false);
  this->checkPublishing({this->publisher_->clientId()});

  folly::Baton<> waitForInitialSync, waitForDisconnect;
  folly::Baton<> resumeDataCb, resumeReconnect;
  bool initialSyncOnce{false}, disconnectOnce{false};
  auto slowSub = this->createSubscriber(
      "fsdb_slow_subscriber",
      [&waitForInitialSync, &resumeDataCb, &initialSyncOnce]() {
        if (!initialSyncOnce) {
          initialSyncOnce = true;
          waitForInitialSync.post();
          resumeDataCb.wait();
        }
      },
      [&waitForDisconnect, &resumeReconnect, &disconnectOnce]() {
        if (!disconnectOnce) {
          disconnectOnce = true;
          waitForDisconnect.post();
          resumeReconnect.wait();
        }
      });
  this->setupConnection(*slowSub);
  this->checkSubscribed({slowSub->clientId()});

  // publish update to trigger initial sync
  int updateNum = 1;
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  updateNum++;

  // wait for initial sync - subscriber is now blocked in data callback
  waitForInitialSync.wait();

  // publish remaining updates while subscriber is blocked so queue builds up
  uint32_t queueSize = this->pubSubStats()
      ? FLAGS_statsSubscriptionServeQueueSize
      : kSubscriptionServeQueueSize;
  uint32_t updatesPublished = 100 + queueSize + 1;
  for (; updateNum <= updatesPublished; updateNum++) {
    if (this->pubSubStats()) {
      this->publishPortStats(makePortStats(updateNum));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // resume subscriber data callback after all updates are published
  resumeDataCb.post();

  // validate server disconnects slow subscriber
  ASSERT_TRUE(waitForDisconnect.try_wait_for(std::chrono::seconds(120)));
  SubscriptionInfo info = slowSub->getInfo();
  ASSERT_EQ(
      info.state,
      FsdbStreamClient::ReconnectingThriftClient::State::DISCONNECTED);
  ASSERT_EQ(
      info.disconnectReason, FsdbErrorCode::SUBSCRIPTION_SERVE_QUEUE_FULL);

  // Also validate fb303 counter for slow subscriber disconnects
  WITH_RETRIES_N(90, {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = fmt::format(
        "{}.subscriber.{}.disconnects.slow_subscriber.count",
        this->pubSubStats() ? "stats" : "fsdb",
        "unspecified");
    EXPECT_EVENTUALLY_GT(fb303::ServiceData::get()->getCounter(counterName), 0);
  });

  resumeReconnect.post();
}

TYPED_TEST(FsdbSlowDeltaSubscriberTest, slowSubscriberQueueWatermark) {
  FLAGS_forceCloseSlowSubscriber = false;

  // publishInterval: wait for subscriptionServeIntervalMs+delta to prevent
  // published updates from being coalesced
  uint32_t updatesPublished = 200;
  uint32_t subscriptionServeIntervalMs =
      this->pubSubStats() ? kStatsServeIntervalMs : kStateServeIntervalMs;
  uint32_t publishIntervalMs = subscriptionServeIntervalMs + 20;
  this->setupConnection(*this->publisher_, false);
  this->checkPublishing({this->publisher_->clientId()});

  folly::Baton<> waitForInitialSync, resumeDataCb;
  bool initialSyncOnce{false};
  auto slowSub = this->createSubscriber(
      "fsdb_slow_subscriber",
      [&waitForInitialSync, &resumeDataCb, &initialSyncOnce]() {
        if (!initialSyncOnce) {
          initialSyncOnce = true;
          waitForInitialSync.post();
          resumeDataCb.wait();
        }
      });
  this->setupConnection(*slowSub);
  this->checkSubscribed({slowSub->clientId()});

  // publish update to trigger initial sync
  int updateNum = 1;
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(updateNum));
  } else {
    std::string testStr = folly::to<std::string>("bar", updateNum);
    this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  updateNum++;

  // wait for initial sync - subscriber is now blocked in data callback
  waitForInitialSync.wait();

  // publish remaining updates while subscriber is blocked
  for (; updateNum <= updatesPublished; updateNum++) {
    if (this->pubSubStats()) {
      this->publishPortStats(makePortStats(updateNum));
    } else {
      std::string testStr = folly::to<std::string>("bar", updateNum);
      this->publishAgentConfig(makeAgentConfig({{"foo", testStr}}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(publishIntervalMs));
  }

  // validate subscription serve queue watermark counter
  WITH_RETRIES({
    auto subscriberId = slowSub->clientId();
    auto subscriberToInfo = folly::coro::blockingWait(
        this->fsdbTestServer_->getClient()->co_getOperSubscriberInfos(
            {subscriberId}));
    auto sitr = subscriberToInfo.find(subscriberId);
    ASSERT_EVENTUALLY_NE(sitr, subscriberToInfo.end());
    ASSERT_EVENTUALLY_EQ(sitr->second.size(), 1);
    if (sitr != subscriberToInfo.end()) {
      auto info = sitr->second;
      OperSubscriberInfo expectedInfo = sitr->second[0];
      ASSERT_EVENTUALLY_EQ(
          expectedInfo.subscriptionQueueWatermark().has_value(), true);
      if (expectedInfo.subscriptionQueueWatermark().has_value()) {
        ASSERT_EVENTUALLY_GT(*expectedInfo.subscriptionQueueWatermark(), 0);
      }
      ASSERT_EQ(expectedInfo.enqueuedDataSize().has_value(), true);
      ASSERT_EVENTUALLY_GT(*expectedInfo.enqueuedDataSize(), 0);
    }
    // Also validate fb303 counter for subscription serve queue watermark
    // In tests, we don't start the publisher threads
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counterName = fmt::format(
        "{}.subscriber.{}.queue_watermark.avg.60",
        this->pubSubStats() ? "stats" : "fsdb",
        "unspecified");
    EXPECT_EVENTUALLY_GT(fb303::ServiceData::get()->getCounter(counterName), 0);
  });
  // resume subscriber data callback after all updates are published
  resumeDataCb.post();
}

TYPED_TEST(FsdbPubSubTest, subscriberStreamMetadata) {
  // Enable heartbeats with short interval for this test.
  auto origServeHeartbeats = FLAGS_serveHeartbeats;
  auto origStateHeartbeat = FLAGS_stateSubscriptionHeartbeat_s;
  auto origStatsHeartbeat = FLAGS_statsSubscriptionHeartbeat_s;
  FLAGS_serveHeartbeats = true;
  FLAGS_stateSubscriptionHeartbeat_s = 1;
  FLAGS_statsSubscriptionHeartbeat_s = 1;
  SCOPE_EXIT {
    FLAGS_serveHeartbeats = origServeHeartbeats;
    FLAGS_stateSubscriptionHeartbeat_s = origStateHeartbeat;
    FLAGS_statsSubscriptionHeartbeat_s = origStatsHeartbeat;
  };

  // Recreate server with heartbeats enabled (flag is read at construction).
  this->publisher_.reset();
  this->subscriber_.reset();
  this->fsdbTestServer_.reset();
  auto config = this->getFsdbConfig();
  this->fsdbTestServer_ = std::make_unique<FsdbTestServer>(
      std::move(config),
      0,
      kStateServeIntervalMs,
      kStatsServeIntervalMs,
      kSubscriptionServeQueueSize);
  this->subscriber_ = this->createSubscriber(kSubscriberId);
  this->publisher_ = this->createPublisher(kPublisherId);
  this->setupConnections();

  // Publish data
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }

  // Wait for data to be processed and served to subscriber, then verify
  auto subscriberId = this->subscriber_->clientId();
  WITH_RETRIES_N(30, {
    auto subInfos = folly::coro::blockingWait(
        this->fsdbTestServer_->getClient()->co_getAllOperSubscriberInfos());
    auto sitr = subInfos.find(subscriberId);
    ASSERT_EVENTUALLY_NE(sitr, subInfos.end());
    ASSERT_EVENTUALLY_GE(sitr->second.size(), 1);
    auto& info = sitr->second[0];

    // initialSyncCompletedAt is set on simple subscriptions (Delta, Path).
    // For Patch (ExtendedSubscription), initial sync happens on
    // fully-resolved child subscriptions, not the parent.
    ASSERT_EVENTUALLY_TRUE(info.initialSyncCompletedAt().has_value());
    EXPECT_EVENTUALLY_GT(*info.initialSyncCompletedAt(), 0);

    // lastUpdateEnqueuedAt should be set after data was enqueued
    ASSERT_EVENTUALLY_TRUE(info.lastUpdateEnqueuedAt().has_value());
    EXPECT_EVENTUALLY_GT(*info.lastUpdateEnqueuedAt(), 0);

    // lastHeartbeatSentAt may or may not be set depending on heartbeat config
    ASSERT_EVENTUALLY_TRUE(info.lastHeartbeatSentAt().has_value());
    EXPECT_EVENTUALLY_GT(*info.lastHeartbeatSentAt(), 0);

    // Stream-reader-side fields (from SubscriptionStreamInfo)
    // lastUpdateWrittenAt should be set after data was yielded to stream
    // (only for delta/patch subscriptions that have streamReader)
    if (this->isDelta() || this->isPatch()) {
      ASSERT_EVENTUALLY_TRUE(info.lastUpdateWrittenAt().has_value());
      EXPECT_EVENTUALLY_GT(*info.lastUpdateWrittenAt(), 0);
      ASSERT_EVENTUALLY_TRUE(info.numUpdatesServed().has_value());
      EXPECT_EVENTUALLY_GE(*info.numUpdatesServed(), 1);
    }
  });
}

TYPED_TEST(FsdbPubSubTest, publisherStreamMetadata) {
  auto beforeConnect = std::time(nullptr);
  // Set up publisher only - skip checkPublishing to avoid timeout
  this->setupConnection(*this->publisher_);

  // Publish data
  if (this->pubSubStats()) {
    this->publishPortStats(makePortStats(1));
  } else {
    this->publishAgentConfig(makeAgentConfig({{"foo", "bar"}}));
  }

  // Verify publisher stream state via getActivePublishers.
  // Use retries: the server-side sink consumer may need time to process the
  // published data, especially under stress.
  auto publisherId = this->publisher_->clientId();
  WITH_RETRIES({
    auto activePublishers =
        this->fsdbTestServer_->serviceHandler().getActivePublishers();
    bool found = false;
    for (const auto& [key, entry] : activePublishers) {
      if (*entry.info.publisherId() == publisherId) {
        found = true;
        ASSERT_EVENTUALLY_TRUE(entry.streamState != nullptr);
        if (entry.streamState) {
          auto state = entry.streamState->rlock();
          // connectedAt should be set to a reasonable value
          EXPECT_EVENTUALLY_GE(state->connectedAt, beforeConnect);
          EXPECT_EVENTUALLY_LE(state->connectedAt, std::time(nullptr));
          // Update timestamps should be set after publishing
          EXPECT_EVENTUALLY_GT(state->lastUpdateReceivedAt, 0);
          EXPECT_EVENTUALLY_GT(state->lastUpdatePublishedAt, 0);
          EXPECT_EVENTUALLY_GT(state->initialSyncCompletedAt, 0);
          // At least 1 update received
          EXPECT_EVENTUALLY_GE(state->numUpdatesReceived, 1);
          EXPECT_EVENTUALLY_GE(state->receivedDataSize, 0);
        }
        break;
      }
    }
    ASSERT_EVENTUALLY_TRUE(found);
  });
}

} // namespace facebook::fboss::fsdb::test
