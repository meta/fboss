// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/client/FsdbPatchPublisher.h"
#include "fboss/fsdb/client/FsdbPatchSubscriber.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStateSubManager.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStatsSubManager.h"
#include "fboss/fsdb/common/Utils.h"
#include "fboss/fsdb/oper/ExtendedPathBuilder.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"

#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/logging/Init.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp2/op/Get.h>

#include <chrono>
#include <set>
#include <thread>
#include <utility>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG5; default:async=true");

namespace facebook::fboss::fsdb::test {

class TestAgentPublisher {
 public:
  TestAgentPublisher(bool isStats, utils::ConnectionOptions serverOptions)
      : isStats_(isStats),
        connectionOptions_(std::move(serverOptions)),
        publisherThread_(
            std::make_unique<folly::ScopedEventBaseThread>(
                "test-publisher-stream")),
        reconnectThread_(
            std::make_unique<folly::ScopedEventBaseThread>(
                "test-publisher-reconnect")) {}

  ~TestAgentPublisher() {
    disconnect();
  }

  void connect() {
    folly::Baton b;
    publisher_ = std::unique_ptr<FsdbPatchPublisher>(new FsdbPatchPublisher(
        "agent",
        {"agent"},
        publisherThread_->getEventBase(),
        reconnectThread_->getEventBase(),
        isStats_,
        [&](auto, auto newState) {
          if (newState == FsdbPatchPublisher::State::CONNECTED) {
            b.post();
          }
        }));
    publisher_->setConnectionOptions(connectionOptions_);
    b.wait();
  }

  void disconnect() {
    publisher_.reset();
  }

  template <typename PathT, typename Node = typename PathT::DataT>
  void publish(PathT path, Node data) {
    CHECK(publisher_);
    Patch patch;
    patch.basePath() = path.idTokens();
    thrift_cow::PatchNode rootPatch;
    auto prot = OperProtocol::BINARY;
    rootPatch.set_val(thrift_cow::serializeBuf<typename PathT::TC>(prot, data));
    patch.patch() = std::move(rootPatch);
    patch.protocol() = prot;
    ASSERT_TRUE(publisher_->write(std::move(patch)));
  }

  template <typename PathT>
  void publishDelete(const PathT& path) {
    CHECK(publisher_);
    auto tokens = path.idTokens();
    // Extract the map key (last token) and build a MapPatch with a del child
    auto mapKey = tokens.back();
    tokens.pop_back();

    Patch patch;
    patch.basePath() = std::move(tokens);

    thrift_cow::PatchNode delChild;
    delChild.set_del();

    thrift_cow::MapPatch mapPatch;
    mapPatch.children() = {{mapKey, std::move(delChild)}};

    thrift_cow::PatchNode rootPatch;
    rootPatch.set_map_node(std::move(mapPatch));

    patch.patch() = std::move(rootPatch);
    patch.protocol() = OperProtocol::BINARY;
    ASSERT_TRUE(publisher_->write(std::move(patch)));
  }

 private:
  bool isStats_;
  std::unique_ptr<FsdbPatchPublisher> publisher_;
  utils::ConnectionOptions connectionOptions_;
  std::unique_ptr<folly::ScopedEventBaseThread> publisherThread_;
  std::unique_ptr<folly::ScopedEventBaseThread> reconnectThread_;
};

template <bool IsStats>
struct DataFactory;

template <>
struct DataFactory<true /* IsStats */> {
  const auto& root() {
    static const thriftpath::RootThriftPath<FsdbOperStatsRoot> root;
    return root;
  }

  auto path1() {
    return root().agent().phyStats();
  }

  std::map<std::string, phy::PhyStats> data1(std::string val) {
    std::map<std::string, phy::PhyStats> stats;
    stats[val] = phy::PhyStats();
    stats[val].timeCollected() = 123;
    return stats;
  }

  std::map<std::string, phy::PhyStats> fetchData1(
      const FsdbOperStatsRoot& root) {
    return *root.agent()->phyStats();
  }

  // path2 is designed to be inside a container so it doesn't exists initially
  static constexpr auto path2Key = "key1";
  auto path2() {
    return root().agent().hwTrunkStats()[path2Key];
  }

  HwTrunkStats data2(int val) {
    HwTrunkStats stats;
    stats.capacity_() = val;
    return stats;
  }

  HwTrunkStats fetchData2(const FsdbOperStatsRoot& root) {
    return root.agent()->hwTrunkStats()->at(path2Key);
  }

  // path3 is a sibling of path2 (distinct map key), used when a test needs a
  // second raw path that does not overlap path1 or path2.
  auto path3() {
    return root().agent().hwTrunkStats()["key2"];
  }

  auto mapKeyPath(std::string key) {
    return root().agent().phyStats()[std::move(key)];
  }

  phy::PhyStats mapKeyData(int val) {
    phy::PhyStats stats;
    stats.timeCollected() = val;
    return stats;
  }

  bool hasMapKey(const FsdbOperStatsRoot& root, const std::string& key) {
    return root.agent()->phyStats()->count(key) > 0;
  }

  std::map<std::string, phy::PhyStats> mapData(
      const std::map<std::string, int>& entries) {
    std::map<std::string, phy::PhyStats> stats;
    for (auto& [key, val] : entries) {
      stats[key] = phy::PhyStats();
      stats[key].timeCollected() = val;
    }
    return stats;
  }
};

template <>
struct DataFactory<false /* IsStats */> {
  const auto& root() {
    static const thriftpath::RootThriftPath<FsdbOperStateRoot> root;
    return root;
  }

  auto path1() {
    return root().agent().config().defaultCommandLineArgs();
  }

  std::map<std::string, std::string> data1(std::string val) {
    std::map<std::string, std::string> cliArgs;
    cliArgs[val] = val;
    return cliArgs;
  }

  std::map<std::string, std::string> fetchData1(const FsdbOperStateRoot& root) {
    return *root.agent()->config()->defaultCommandLineArgs();
  }

  // path2 is designed to be inside a container so it doesn't exists initially
  static constexpr auto path2Key = 42;
  auto path2() {
    return root()
        .agent()
        .config()
        .sw()
        .switchSettings()
        .switchIdToSwitchInfo()[path2Key];
  }

  cfg::SwitchInfo data2(int val) {
    cfg::SwitchInfo info;
    info.switchIndex() = val;
    return info;
  }

  cfg::SwitchInfo fetchData2(const FsdbOperStateRoot& root) {
    return root.agent()
        ->config()
        ->sw()
        ->switchSettings()
        ->switchIdToSwitchInfo()
        ->at(path2Key);
  }

  // path3 is a sibling of path2 (distinct map key), used when a test needs a
  // second raw path that does not overlap path1 or path2.
  auto path3() {
    return root()
        .agent()
        .config()
        .sw()
        .switchSettings()
        .switchIdToSwitchInfo()[path2Key + 1];
  }

  auto mapKeyPath(std::string key) {
    return root().agent().config().defaultCommandLineArgs()[std::move(key)];
  }

  std::string mapKeyData(int val) {
    return folly::to<std::string>(val);
  }

  bool hasMapKey(const FsdbOperStateRoot& root, const std::string& key) {
    return root.agent()->config()->defaultCommandLineArgs()->count(key) > 0;
  }

  std::map<std::string, std::string> mapData(
      const std::map<std::string, int>& entries) {
    std::map<std::string, std::string> cliArgs;
    for (auto& [key, val] : entries) {
      cliArgs[key] = folly::to<std::string>(val);
    }
    return cliArgs;
  }
};

template <typename SubscriberT>
class FsdbSubManagerTest : public ::testing::Test,
                           public DataFactory<SubscriberT::IsStats> {
 public:
  using Subscriber = SubscriberT;
  static constexpr bool IsStats = Subscriber::IsStats;

  void SetUp() override {
    createFsdbServerAndPublisher();

    folly::LoggerDB::get().setLevel("fboss.thrift_cow", folly::LogLevel::DBG5);
    folly::LoggerDB::get().setLevel("fboss.fsdb", folly::LogLevel::DBG5);
  }

  bool isSubscribed(const std::string& clientId) {
    if (fsdbTestServer_) {
      auto subs = fsdbTestServer_->getActiveSubscriptions();
      for (const auto& [_, info] : subs) {
        for (const auto& subscription : info) {
          if (subscription.subscriberId() == clientId) {
            return true;
          }
        }
      }
    }
    return false;
  }

  bool publisherReady(const std::string& root) {
    if (fsdbTestServer_) {
      auto metadata = fsdbTestServer_->getPublisherRootMetadata(root, IsStats);
      return metadata.has_value();
    }
    return false;
  }

  size_t numActivePublishers() {
    if (fsdbTestServer_) {
      return fsdbTestServer_->serviceHandler().getActivePublishers().size();
    }
    return 0;
  }

  TestAgentPublisher& testPublisher() {
    return *testPublisher_;
  }

  const utils::ConnectionOptions& connectionOptions() const {
    return *connectionOptions_;
  }

  SubscriptionOptions getSubscriptionOptions(const std::string& clientId) {
    return SubscriptionOptions(clientId, IsStats);
  }

  SubscriptionOptions getSubscriptionOptions(
      const std::string& clientId,
      int grHoldTimer) {
    return SubscriptionOptions(clientId, IsStats, grHoldTimer);
  }

  std::unique_ptr<Subscriber> createSubscriber(SubscriptionOptions options) {
    CHECK(connectionOptions_);
    return std::make_unique<Subscriber>(
        std::move(options), *connectionOptions_);
  }

  template <typename PathT>
  std::unique_ptr<Subscriber> createSubscriber(
      SubscriptionOptions options,
      PathT path) {
    auto subscriber = createSubscriber(std::move(options));
    subscriber->addPath(std::move(path));
    return subscriber;
  }

  std::vector<ExtendedOperPath> extSubscriptionPaths() const {
    if constexpr (IsStats) {
      ExtendedOperPath path = ext_path_builder::raw(
                                  apache::thrift::op::get_field_id_v<
                                      FsdbOperStatsRoot,
                                      apache::thrift::ident::agent>)
                                  .raw(
                                      apache::thrift::op::get_field_id_v<
                                          AgentStats,
                                          apache::thrift::ident::phyStats>)
                                  .any()
                                  .raw(
                                      apache::thrift::op::get_field_id_v<
                                          phy::PhyStats,
                                          apache::thrift::ident::timeCollected>)
                                  .get();
      return {std::move(path)};
    } else {
      ExtendedOperPath path =
          ext_path_builder::raw(
              apache::thrift::op::get_field_id_v<
                  FsdbOperStateRoot,
                  apache::thrift::ident::agent>)
              .raw(
                  apache::thrift::op::
                      get_field_id_v<AgentData, apache::thrift::ident::config>)
              .raw(
                  apache::thrift::op::get_field_id_v<
                      cfg::AgentConfig,
                      apache::thrift::ident::defaultCommandLineArgs>)
              .any()
              .get();
      return {std::move(path)};
    }
  }

  std::vector<ExtendedOperPath> mapKeyExtPaths(
      const std::vector<std::string>& keys) const {
    std::vector<ExtendedOperPath> paths;
    for (const auto& key : keys) {
      if constexpr (IsStats) {
        paths.push_back(
            ext_path_builder::raw(
                apache::thrift::op::get_field_id_v<
                    FsdbOperStatsRoot,
                    apache::thrift::ident::agent>)
                .raw(
                    apache::thrift::op::get_field_id_v<
                        AgentStats,
                        apache::thrift::ident::phyStats>)
                .raw(key)
                .get());
      } else {
        paths.push_back(
            ext_path_builder::raw(
                apache::thrift::op::get_field_id_v<
                    FsdbOperStateRoot,
                    apache::thrift::ident::agent>)
                .raw(
                    apache::thrift::op::get_field_id_v<
                        AgentData,
                        apache::thrift::ident::config>)
                .raw(
                    apache::thrift::op::get_field_id_v<
                        cfg::AgentConfig,
                        apache::thrift::ident::defaultCommandLineArgs>)
                .raw(key)
                .get());
      }
    }
    return paths;
  }

  std::unique_ptr<Subscriber> createExtendedSubscriber(
      SubscriptionOptions options,
      std::vector<ExtendedOperPath> paths) {
    auto subscriber = createSubscriber(std::move(options));
    for (auto& path : paths) {
      subscriber->addExtendedPath(std::move(path));
    }
    return subscriber;
  }

  template <typename PathT, typename DataT>
  void connectPublisherAndPublish(PathT path, DataT data) {
    this->testPublisher().connect();
    this->testPublisher().publish(std::move(path), std::move(data));
    WITH_RETRIES(ASSERT_EVENTUALLY_TRUE(this->publisherReady("agent")));
  }

  void createFsdbServerAndPublisher() {
    fsdbTestServer_ = std::make_unique<FsdbTestServer>();
    connectionOptions_ =
        utils::ConnectionOptions("::1", fsdbTestServer_->getFsdbPort());
    testPublisher_ =
        std::make_unique<TestAgentPublisher>(IsStats, *connectionOptions_);
  }

  void killFsdb() {
    fsdbTestServer_.reset();
  }

 private:
  std::unique_ptr<FsdbTestServer> fsdbTestServer_;
  std::unique_ptr<TestAgentPublisher> testPublisher_;
  std::optional<utils::ConnectionOptions> connectionOptions_;
};

TEST(FsdbSubManagerTest, subscriberIdParsing) {
  SubscriberId agentId = "agent";
  EXPECT_EQ(subscriberId2ClientId(agentId).client(), FsdbClient::AGENT);
  EXPECT_EQ(subscriberId2ClientId(agentId).instanceId(), "");
  EXPECT_EQ(string2FsdbClient(agentId), FsdbClient::AGENT);
  EXPECT_EQ(fsdbClient2string(string2FsdbClient(agentId)), agentId);

  SubscriberId foo = "foo";
  EXPECT_EQ(subscriberId2ClientId(foo).client(), FsdbClient::UNSPECIFIED);
  EXPECT_EQ(subscriberId2ClientId(foo).instanceId(), "");
  EXPECT_EQ(string2FsdbClient(foo), FsdbClient::UNSPECIFIED);

  SubscriberId agentWithInstance = "agent:some_instance";
  EXPECT_EQ(
      subscriberId2ClientId(agentWithInstance).client(), FsdbClient::AGENT);
  EXPECT_EQ(
      subscriberId2ClientId(agentWithInstance).instanceId(), "some_instance");
  EXPECT_EQ(string2FsdbClient(agentWithInstance), FsdbClient::UNSPECIFIED);
}

using SubscriberTypes =
    ::testing::Types<FsdbCowStateSubManager, FsdbCowStatsSubManager>;

TYPED_TEST_SUITE(FsdbSubManagerTest, SubscriberTypes);

TYPED_TEST(FsdbSubManagerTest, subUnsub) {
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->isSubscribed("test")));
  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, subUnsubNoFsdb) {
  this->killFsdb();
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  sleep(3);
  subscriber.reset();
}

TYPED_TEST(FsdbSubManagerTest, subBeforeFsdbUp) {
  this->killFsdb();
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  sleep(3);
  this->createFsdbServerAndPublisher();
  sleep(3);
  subscriber.reset();
}

TYPED_TEST(FsdbSubManagerTest, subUpdateUnsub) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    EXPECT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });
  // update data and verify receieved
  data1 = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1);
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(
      this->fetchData1((*boundData.rlock())->toThrift()), data1));
  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, subBeforePublisher) {
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->isSubscribed("test")));
  EXPECT_FALSE(*boundData.rlock());
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    EXPECT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });
  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, subMultiPath) {
  auto data1 = this->data1("foo");
  auto data2 = this->data2(123);
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test"));
  SubscriptionKey path1SubKey = subscriber->addPath(this->path1());
  SubscriptionKey path2SubKey = subscriber->addPath(this->path2());

  bool publishedData2 = false;
  int numUpdates = 0;
  subscriber->subscribe([&](auto update) {
    numUpdates++;
    EXPECT_EQ(update.updatedKeys.size(), 1);
    EXPECT_EQ(update.updatedPaths.size(), 1);
    auto expectedKey = publishedData2 ? path2SubKey : path1SubKey;
    auto expectedPath =
        publishedData2 ? this->path2().idTokens() : this->path1().idTokens();
    EXPECT_EQ(update.updatedKeys[0], expectedKey);
    EXPECT_EQ(update.updatedPaths[0], expectedPath);

    EXPECT_EQ(this->fetchData1(update.data->toThrift()), data1);
    if (publishedData2) {
      EXPECT_EQ(this->fetchData2(update.data->toThrift()), data2);
    }
  });

  // wait for initial sync
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(numUpdates, 1));

  // publish second path
  publishedData2 = true;
  this->testPublisher().publish(this->path2(), data2);

  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(numUpdates, 2));

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, addPathAfterSubscribe) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test"));
  SubscriptionKey path1Key = subscriber->addPath(this->path1());
  auto boundData = subscriber->subscribeBound();

  // Wait for initial sync of path1 (subscription connected, uid round-tripped).
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    EXPECT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });

  // Append path2 to the already-live subscription.
  SubscriptionKey path2Key =
      subscriber->addPathToLiveSubscription(this->path2());
  EXPECT_NE(path2Key, path1Key);

  // Publishing path2 must now be delivered on the same stream, proving the
  // appended path was added to the existing server-side subscription. path2
  // lives inside a container that does not exist until published, so guard the
  // read until it appears (fetchData2 throws until then).
  auto data2 = this->data2(123);
  this->testPublisher().publish(this->path2(), data2);
  WITH_RETRIES({
    bool gotData2 = false;
    try {
      gotData2 = this->fetchData2((*boundData.rlock())->toThrift()) == data2;
    } catch (const std::exception&) {
      gotData2 = false;
    }
    ASSERT_EVENTUALLY_TRUE(gotData2);
  });

  // The originally subscribed path keeps working too.
  EXPECT_EQ(this->fetchData1((*boundData.rlock())->toThrift()), data1);

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

// addPathToLiveSubscription() sends the new path's SubscriptionKey as the
// request's lastStreamRevision; the server stashes it and echoes it back as
// OperMetadata.streamRevision on every patch served after the add. Assert that
// the value round-trips into the SubUpdate the data callback receives.
TYPED_TEST(FsdbSubManagerTest, addPathToLiveSubscriptionStreamRevision) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test"));
  subscriber->addPath(this->path1());

  // Capture the most recent stream revision seen in the data callback.
  folly::Synchronized<std::optional<StreamRevision>> seenRevision;
  std::atomic<int> numUpdates{0};
  subscriber->subscribe([&](const auto& update) {
    if (update.streamRevision.has_value()) {
      *seenRevision.wlock() = update.streamRevision;
    }
    numUpdates++;
  });

  // Wait for path1's initial sync to actually reach the client (not just for
  // the server to register the subscription). This guarantees the init response
  // was processed and the server uid captured, so the live-extend RPC below
  // fires in place instead of silently deferring to a reconnect that never
  // happens.
  WITH_RETRIES(ASSERT_EVENTUALLY_GE(numUpdates.load(), 1));

  // Append path2 to the live subscription. The client sends path2Key as
  // lastStreamRevision; publishing path2 produces a served chunk that must come
  // back stamped with streamRevision == path2Key.
  SubscriptionKey path2Key =
      subscriber->addPathToLiveSubscription(this->path2());
  auto data2 = this->data2(123);
  this->testPublisher().publish(this->path2(), data2);

  WITH_RETRIES({
    auto rev = seenRevision.copy();
    ASSERT_EVENTUALLY_TRUE(rev.has_value());
    EXPECT_EVENTUALLY_EQ(*rev, path2Key);
  });

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

// supportsLiveAddPath() reflects whether the server on the current connection
// advertised a subscription uid (OperSubInitResponse.subscriptionUid). Before
// subscribe it is false (no active subscription); after the initial sync
// completes against a server that populates the uid, it is true. This is the
// signal callers use to decide between addPathToLiveSubscription and an
// older-server fallback (stop/re-subscribe).
TYPED_TEST(FsdbSubManagerTest, supportsLiveAddPath) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test"));
  subscriber->addPath(this->path1());

  // No active subscription yet -> not supported.
  EXPECT_FALSE(subscriber->supportsLiveAddPath());

  std::atomic<int> numUpdates{0};
  subscriber->subscribe([&](const auto&) { numUpdates++; });

  // Once the initial sync reaches the client, the init response (carrying the
  // server-assigned uid) has been processed, so the capability reads true.
  WITH_RETRIES({
    ASSERT_EVENTUALLY_GE(numUpdates.load(), 1);
    EXPECT_EVENTUALLY_TRUE(subscriber->supportsLiveAddPath());
  });

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

// A single addPaths() call can carry multiple paths. The client must send the
// largest SubscriptionKey (the most recently assigned) as lastStreamRevision,
// not the first entry in the ascending-ordered map. Drive
// FsdbPatchSubscriber::addPaths() directly with a multi-entry map whose largest
// key belongs to the non-first entry, and assert the server echoes that largest
// key back as OperMetadata.streamRevision on served chunks.
TYPED_TEST(FsdbSubManagerTest, addPathsMultiPathUsesLargestStreamRevision) {
  // Base subscription on path1 with data, so its initial sync reaches the
  // client
  // -- this proves the stream connected and the server uid was captured, which
  // the live-extend RPC in addPaths() needs to fire in place.
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  folly::ScopedEventBaseThread streamThread("test-raw-sub-stream");
  folly::ScopedEventBaseThread reconnectThread("test-raw-sub-reconnect");

  RawOperPath basePath;
  basePath.path() = this->path1().idTokens();
  std::map<SubscriptionKey, RawOperPath> subscribePaths{{0, basePath}};

  // Capture the most recent stream revision the server stamps on served chunks.
  // Only chunks served after a live add-paths carry a value, so this stays
  // unset until addPaths() takes effect on the stream.
  folly::Synchronized<std::optional<StreamRevision>> seenRevision;
  std::atomic<int> numChunks{0};
  auto subscriber = std::make_unique<FsdbPatchSubscriber>(
      SubscriptionOptions("test", this->IsStats),
      subscribePaths,
      streamThread.getEventBase(),
      reconnectThread.getEventBase(),
      [&](SubscriberChunk&& chunk) {
        for (const auto& [key, patchGroup] : *chunk.patchGroups()) {
          for (const auto& patch : patchGroup) {
            const auto& metadata = *patch.metadata();
            if (metadata.streamRevision().has_value()) {
              *seenRevision.wlock() = *metadata.streamRevision();
            }
          }
        }
        numChunks++;
      });
  subscriber->setConnectionOptions(this->connectionOptions());

  // Wait for path1's initial sync so the server uid is captured client-side.
  WITH_RETRIES(ASSERT_EVENTUALLY_GE(numChunks.load(), 1));

  // Add two paths in one addPaths() call. std::map orders keys ascending, so
  // the largest key (kLargestKey) is the last / non-first entry while
  // begin()->first is kSmallestKey (what the old behavior incorrectly sent).
  // Assign the largest key to path3 so the assertion proves the revision
  // follows the largest key, not the first path in the map.
  constexpr SubscriptionKey kSmallestKey = 5;
  constexpr SubscriptionKey kLargestKey = 9;
  RawOperPath addPath2;
  addPath2.path() = this->path2().idTokens();
  RawOperPath addPath3;
  addPath3.path() = this->path3().idTokens();
  std::map<SubscriptionKey, RawOperPath> newPaths{
      {kSmallestKey, addPath2}, {kLargestKey, addPath3}};
  EXPECT_EQ(subscriber->addPaths(newPaths), std::nullopt);

  // Publish on an added path to force a served chunk. streamRevision is stamped
  // per-subscription (the same value rides every post-add patch regardless of
  // which path changed), so it must come back as the largest key even though we
  // publish path2 (whose key is the smallest).
  auto data2 = this->data2(123);
  this->testPublisher().publish(this->path2(), data2);

  WITH_RETRIES({
    auto rev = seenRevision.copy();
    ASSERT_EVENTUALLY_TRUE(rev.has_value());
    EXPECT_EVENTUALLY_EQ(*rev, kLargestKey);
  });

  subscriber.reset();
}

// Paths added while the subscribe RPC is in flight are missing from the
// subscribe request; once the server uid arrives they must be appended to the
// live subscription instead of waiting for a reconnect.
TYPED_TEST(FsdbSubManagerTest, addPathsWhileSubscribeInFlight) {
  this->connectPublisherAndPublish(this->path1(), this->data1("foo"));
  this->testPublisher().publish(this->path2(), this->data2(123));

  folly::ScopedEventBaseThread streamThread("test-raw-sub-stream");
  folly::ScopedEventBaseThread reconnectThread("test-raw-sub-reconnect");

  RawOperPath basePath;
  basePath.path() = this->path1().idTokens();
  RawOperPath addedPath;
  addedPath.path() = this->path2().idTokens();
  constexpr SubscriptionKey kAddedKey = 1;

  folly::Synchronized<std::set<SubscriptionKey>> seenKeys;
  std::atomic<int> numConnects{0};
  auto subscriber = std::make_unique<FsdbPatchSubscriber>(
      SubscriptionOptions("test", this->IsStats),
      std::map<SubscriptionKey, RawOperPath>{{0, basePath}},
      streamThread.getEventBase(),
      reconnectThread.getEventBase(),
      [&](SubscriberChunk&& chunk) {
        for (const auto& [key, patchGroup] : *chunk.patchGroups()) {
          if (!patchGroup.empty()) {
            seenKeys.wlock()->insert(key);
          }
        }
      },
      std::nullopt,
      [&](auto /* oldState */, auto newState) {
        if (newState == FsdbStreamClient::State::CONNECTED) {
          ++numConnects;
        }
      });
  std::atomic<bool> added{false};
  subscriber->setOnSubscribeRequestCreatedForTest([&]() {
    if (!added.exchange(true)) {
      subscriber->addPaths({{kAddedKey, addedPath}});
    }
  });
  subscriber->setConnectionOptions(this->connectionOptions());

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(seenKeys.rlock()->contains(kAddedKey)));
  EXPECT_TRUE(subscriber->serverSupportsLiveAddPath());
  EXPECT_EQ(numConnects.load(), 1);

  subscriber.reset();
}

// Drives FsdbPatchSubscriber directly to inject live add-paths RPC failures.
template <typename SubscriberT>
class FsdbLiveAddRetryTest : public FsdbSubManagerTest<SubscriberT> {
 protected:
  void SetUp() override {
    FsdbSubManagerTest<SubscriberT>::SetUp();
    this->connectPublisherAndPublish(this->path1(), this->data1("foo"));
    this->testPublisher().publish(this->path2(), this->data2(123));
    this->testPublisher().publish(this->path3(), this->data2(456));
  }

  void TearDown() override {
    subscriber_.reset();
    FsdbSubManagerTest<SubscriberT>::TearDown();
  }

  // Subscribes to path1 with fast retry options and waits for the stream uid.
  void subscribe(FsdbPatchSubscriber::LiveAddRpcInterceptor interceptor) {
    RawOperPath basePath;
    basePath.path() = this->path1().idTokens();
    subscriber_ = std::make_unique<FsdbPatchSubscriber>(
        SubscriptionOptions("test", this->IsStats),
        std::map<SubscriptionKey, RawOperPath>{{0, basePath}},
        streamThread_.getEventBase(),
        reconnectThread_.getEventBase(),
        [this](SubscriberChunk&& chunk) {
          for (const auto& [key, patchGroup] : *chunk.patchGroups()) {
            if (!patchGroup.empty()) {
              seenKeys_.wlock()->insert(key);
            }
          }
        },
        std::nullopt,
        [this](auto /* oldState */, auto newState) {
          if (newState == FsdbStreamClient::State::CONNECTED) {
            ++numConnects_;
          }
        });
    subscriber_->setLiveAddRetryOptionsForTest(
        {.rpcTimeout = std::chrono::seconds(5),
         .initialBackoff = std::chrono::milliseconds(10),
         .maxBackoff = std::chrono::milliseconds(50),
         .budget = std::chrono::milliseconds(500)});
    subscriber_->setLiveAddRpcInterceptorForTest(std::move(interceptor));
    subscriber_->setConnectionOptions(this->connectionOptions());
    WITH_RETRIES({
      ASSERT_EVENTUALLY_TRUE(seenKeys_.rlock()->contains(0));
      EXPECT_EVENTUALLY_TRUE(subscriber_->serverSupportsLiveAddPath());
    });
  }

  RawOperPath rawPath(const auto& path) {
    RawOperPath raw;
    raw.path() = path.idTokens();
    return raw;
  }

  bool seen(SubscriptionKey key) {
    return seenKeys_.rlock()->contains(key);
  }

  static folly::coro::Task<void> fail(FsdbErrorCode code) {
    throw Utils::createFsdbException(code, "injected");
    co_return;
  }

  static folly::coro::Task<void> timeOut() {
    throw apache::thrift::transport::TTransportException(
        apache::thrift::transport::TTransportException::TIMED_OUT, "injected");
    co_return;
  }

  // Live-adds key 2, which the server rejects with `code`, then key 1, which
  // it applies; then reconnects and waits for the new stream's initial sync.
  void rejectKey2ThenReconnect(FsdbErrorCode code) {
    auto key2Attempts = std::make_shared<std::atomic<int>>(0);
    subscribe(
        [key2Attempts, code](
            auto request, const auto& next) -> folly::coro::Task<void> {
          if (request.paths()->contains(2)) {
            ++*key2Attempts;
            return fail(code);
          }
          return next(std::move(request));
        });
    subscriber_->addPaths({{2, rawPath(this->path3())}});
    WITH_RETRIES(EXPECT_EVENTUALLY_EQ(key2Attempts->load(), 1));
    // The drain is serialized, so key 1 applies after key 2's verdict.
    subscriber_->addPaths({{1, rawPath(this->path2())}});
    WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(seen(1)));

    seenKeys_.wlock()->clear();
    subscriber_->reconnect();
    WITH_RETRIES({
      ASSERT_EVENTUALLY_EQ(numConnects_.load(), 2);
      EXPECT_EVENTUALLY_TRUE(seen(0));
      EXPECT_EVENTUALLY_TRUE(seen(1));
    });
    EXPECT_EQ(key2Attempts->load(), 1);
  }

  folly::ScopedEventBaseThread streamThread_{"test-raw-sub-stream"};
  folly::ScopedEventBaseThread reconnectThread_{"test-raw-sub-reconnect"};
  std::unique_ptr<FsdbPatchSubscriber> subscriber_;
  folly::Synchronized<std::set<SubscriptionKey>> seenKeys_;
  std::atomic<int> numConnects_{0};
};

TYPED_TEST_SUITE(FsdbLiveAddRetryTest, SubscriberTypes);

// ID_NOT_FOUND (an FSDB that registers new subscriptions lazily) is retried
// until the add is accepted, with no reconnect.
TYPED_TEST(FsdbLiveAddRetryTest, retriesIdNotFoundThenApplies) {
  std::atomic<int> attempts{0};
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        if (++attempts <= 3) {
          return TestFixture::fail(FsdbErrorCode::ID_NOT_FOUND);
        }
        return next(std::move(request));
      });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(1)));
  EXPECT_EQ(attempts.load(), 4);
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// An attempt whose outcome is unknown (here: applied, then timed out) followed
// by ID_ALREADY_EXISTS for the same batch counts as applied.
TYPED_TEST(FsdbLiveAddRetryTest, alreadyExistsAfterUnknownOutcomeIsApplied) {
  std::atomic<int> attempts{0};
  this->subscribe([&](auto request, auto next) -> folly::coro::Task<void> {
    if (++attempts == 1) {
      return folly::coro::co_invoke(
          [request = std::move(request),
           next = std::move(next)]() mutable -> folly::coro::Task<void> {
            co_await next(std::move(request));
            throw apache::thrift::transport::TTransportException(
                apache::thrift::transport::TTransportException::TIMED_OUT,
                "injected");
          });
    }
    return next(std::move(request));
  });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(1)));
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(attempts.load(), 2));
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(attempts.load(), 2);
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// When retries run out, the subscriber reconnects and the subscribe request
// carries the added path.
TYPED_TEST(FsdbLiveAddRetryTest, exhaustedRetriesFallBackToReconnect) {
  this->subscribe([](const auto&, const auto&) -> folly::coro::Task<void> {
    return TestFixture::fail(FsdbErrorCode::ID_NOT_FOUND);
  });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES({
    ASSERT_EVENTUALLY_GE(this->numConnects_.load(), 2);
    EXPECT_EVENTUALLY_TRUE(this->seen(1));
  });
}

// A rejection a retry cannot change is not retried and does not reconnect.
TYPED_TEST(FsdbLiveAddRetryTest, terminalErrorIsNotRetried) {
  std::atomic<int> attempts{0};
  this->subscribe([&](const auto&, const auto&) -> folly::coro::Task<void> {
    ++attempts;
    return TestFixture::fail(FsdbErrorCode::INVALID_REQUEST);
  });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(attempts.load(), 1));
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(attempts.load(), 1);
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// A path rejected for a reason a reconnect would hit too is left out of the
// next subscribe request, which keeps the healthy paths.
TYPED_TEST(FsdbLiveAddRetryTest, invalidPathRejectIsDroppedFromResubscribe) {
  this->rejectKey2ThenReconnect(FsdbErrorCode::INVALID_PATH);

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(this->seen(2));
}

// An INVALID_REQUEST reject may not apply to a fresh subscribe, so the path is
// kept for the next one.
TYPED_TEST(FsdbLiveAddRetryTest, invalidRequestRejectIsKeptForResubscribe) {
  this->rejectKey2ThenReconnect(FsdbErrorCode::INVALID_REQUEST);

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(2)));
}

// Failures other than ID_NOT_FOUND (here: timeouts that never reach the
// server) within the budget are retried without reconnecting.
TYPED_TEST(FsdbLiveAddRetryTest, transientFailuresWithinBudgetDoNotReconnect) {
  std::atomic<int> attempts{0};
  folly::Synchronized<std::optional<std::chrono::steady_clock::time_point>>
      failUntil;
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        ++attempts;
        const auto until = failUntil.withWLock([](auto& locked) {
          if (!locked) {
            locked = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(200);
          }
          return *locked;
        });
        if (std::chrono::steady_clock::now() < until) {
          return TestFixture::timeOut();
        }
        return next(std::move(request));
      });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(1)));
  EXPECT_GT(attempts.load(), 1);
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// Persistent failures other than ID_NOT_FOUND also fall back to reconnect, so a
// stuck batch cannot block later adds.
TYPED_TEST(
    FsdbLiveAddRetryTest,
    persistentTransientFailuresFallBackToReconnect) {
  this->subscribe([](const auto&, const auto&) -> folly::coro::Task<void> {
    return TestFixture::timeOut();
  });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});
  this->subscriber_->addPaths({{2, this->rawPath(this->path3())}});

  WITH_RETRIES({
    ASSERT_EVENTUALLY_GE(this->numConnects_.load(), 2);
    EXPECT_EVENTUALLY_TRUE(this->seen(1));
    EXPECT_EVENTUALLY_TRUE(this->seen(2));
  });
}

// The key-by-key resend of a rejected batch shares the batch's deadline.
TYPED_TEST(FsdbLiveAddRetryTest, keyByKeyResendSharesBatchDeadline) {
  // Each single-key resend gets ID_NOT_FOUND for 400ms: either fits in the
  // 500ms budget alone, both together do not.
  folly::Synchronized<
      std::map<SubscriptionKey, std::chrono::steady_clock::time_point>>
      firstAttempt;
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        if (request.paths()->size() > 1) {
          return TestFixture::fail(FsdbErrorCode::INVALID_REQUEST);
        }
        const auto key = request.paths()->begin()->first;
        const auto start = firstAttempt.withWLock([&](auto& locked) {
          return locked.try_emplace(key, std::chrono::steady_clock::now())
              .first->second;
        });
        if (std::chrono::steady_clock::now() - start <
            std::chrono::milliseconds(400)) {
          return TestFixture::fail(FsdbErrorCode::ID_NOT_FOUND);
        }
        return next(std::move(request));
      });

  this->subscriber_->addPaths(
      {{1, this->rawPath(this->path2())}, {2, this->rawPath(this->path3())}});

  WITH_RETRIES({
    ASSERT_EVENTUALLY_GE(this->numConnects_.load(), 2);
    EXPECT_EVENTUALLY_TRUE(this->seen(1));
    EXPECT_EVENTUALLY_TRUE(this->seen(2));
  });
}

// Re-adding a known key with a different path is rejected synchronously;
// re-adding it with its path is a no-op. Neither sends an RPC.
TYPED_TEST(FsdbLiveAddRetryTest, reAddedKeyIsNotResent) {
  std::atomic<int> attempts{0};
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        ++attempts;
        return next(std::move(request));
      });

  EXPECT_EQ(
      this->subscriber_->addPaths({{1, this->rawPath(this->path2())}}),
      std::nullopt);
  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(1)));

  EXPECT_EQ(
      this->subscriber_->addPaths(
          {{2, this->rawPath(this->path3())},
           {1, this->rawPath(this->path3())}}),
      FsdbErrorCode::ID_ALREADY_EXISTS);
  EXPECT_EQ(
      this->subscriber_->addPaths({{0, this->rawPath(this->path3())}}),
      FsdbErrorCode::ID_ALREADY_EXISTS);
  EXPECT_EQ(
      this->subscriber_->addPaths({{1, this->rawPath(this->path2())}}),
      std::nullopt);

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(attempts.load(), 1);
  EXPECT_FALSE(this->seen(2));
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// A batch rejected as a whole is resent key by key, so only the offending key
// is dropped.
TYPED_TEST(FsdbLiveAddRetryTest, rejectedBatchIsResentKeyByKey) {
  folly::Synchronized<std::vector<std::set<SubscriptionKey>>> batches;
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        std::set<SubscriptionKey> keys;
        for (const auto& [key, _] : *request.paths()) {
          keys.insert(key);
        }
        batches.wlock()->push_back(keys);
        if (keys.contains(2)) {
          return TestFixture::fail(FsdbErrorCode::INVALID_REQUEST);
        }
        return next(std::move(request));
      });

  this->subscriber_->addPaths(
      {{1, this->rawPath(this->path2())}, {2, this->rawPath(this->path3())}});

  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->seen(1)));
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(batches.rlock()->size(), 3));
  const std::vector<std::set<SubscriptionKey>> expected{{1, 2}, {1}, {2}};
  EXPECT_EQ(*batches.rlock(), expected);
  EXPECT_FALSE(this->seen(2));
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// A batch being retried is abandoned when the stream changes; the new
// subscribe request carries its paths.
TYPED_TEST(FsdbLiveAddRetryTest, retryAbandonedOnReconnect) {
  std::atomic<int> attempts{0};
  this->subscribe([&](const auto&, const auto&) -> folly::coro::Task<void> {
    if (++attempts == 1) {
      this->subscriber_->reconnect();
    }
    return TestFixture::fail(FsdbErrorCode::ID_NOT_FOUND);
  });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(this->numConnects_.load(), 2);
    EXPECT_EVENTUALLY_TRUE(this->seen(1));
  });
  EXPECT_EQ(attempts.load(), 1);
}

// A key added while a batch is being retried is sent in the next batch.
TYPED_TEST(FsdbLiveAddRetryTest, keyAddedDuringRetryGoesInNextBatch) {
  folly::Synchronized<std::vector<std::set<SubscriptionKey>>> batches;
  this->subscribe(
      [&](auto request, const auto& next) -> folly::coro::Task<void> {
        std::set<SubscriptionKey> keys;
        for (const auto& [key, _] : *request.paths()) {
          keys.insert(key);
        }
        auto numBatches = batches.withWLock([&](auto& locked) {
          locked.push_back(keys);
          return locked.size();
        });
        if (numBatches == 1) {
          this->subscriber_->addPaths({{2, this->rawPath(this->path3())}});
          return TestFixture::fail(FsdbErrorCode::ID_NOT_FOUND);
        }
        return next(std::move(request));
      });

  this->subscriber_->addPaths({{1, this->rawPath(this->path2())}});

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->seen(1));
    EXPECT_EVENTUALLY_TRUE(this->seen(2));
  });
  const std::vector<std::set<SubscriptionKey>> expected{{1}, {1}, {2}};
  EXPECT_EQ(*batches.rlock(), expected);
  EXPECT_EQ(this->numConnects_.load(), 1);
}

// A path appended via addPathToLiveSubscription() must survive a reconnect: it
// is recorded in the subscriber and merged back into the re-subscribe request
// (FsdbPatchSubscriber::createRequest()), so after the stream drops and
// re-establishes the added path is still subscribed and keeps receiving
// updates. Without the reconnect merge, path2 would silently disappear once the
// stream was torn down.
TYPED_TEST(FsdbSubManagerTest, addPathToLiveSubscriptionSurvivesReconnect) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  // Record state transitions so we can confirm a genuine
  // CONNECTED -> DISCONNECTED -> CONNECTED cycle happened before asserting
  // delivery on the new stream.
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test"));
  subscriber->addPath(this->path1());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        stateChanges.wlock()->push_back(newState);
      });

  // Wait for initial sync of path1.
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    EXPECT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });

  // Append path2 to the live subscription and confirm it is delivered on the
  // current stream (exercises the live-extend RPC).
  subscriber->addPathToLiveSubscription(this->path2());
  auto data2 = this->data2(123);
  this->testPublisher().publish(this->path2(), data2);
  WITH_RETRIES({
    bool gotData2 = false;
    try {
      gotData2 = this->fetchData2((*boundData.rlock())->toThrift()) == data2;
    } catch (const std::exception&) {
      gotData2 = false;
    }
    ASSERT_EVENTUALLY_TRUE(gotData2);
  });

  // Force a reconnect and wait for the full transition cycle so the assertions
  // below run against a freshly re-established stream, not the original one.
  size_t preReconnectChanges = stateChanges.rlock()->size();
  subscriber->reconnect();
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });

  // After reconnect, a fresh publish on the appended path must still be
  // delivered -- proving path2 was re-subscribed via the createRequest() merge
  // rather than dropped when the stream was torn down. Use a distinct value so
  // a lingering pre-reconnect value cannot make this pass spuriously.
  auto data2New = this->data2(456);
  this->testPublisher().publish(this->path2(), data2New);
  WITH_RETRIES({
    bool gotData2New = false;
    try {
      gotData2New =
          this->fetchData2((*boundData.rlock())->toThrift()) == data2New;
    } catch (const std::exception&) {
      gotData2New = false;
    }
    ASSERT_EVENTUALLY_TRUE(gotData2New);
  });

  // The originally subscribed path is still delivered post-reconnect too.
  auto data1New = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1New);
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(
      this->fetchData1((*boundData.rlock())->toThrift()), data1New));

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, addExtendedPathAfterSubscribe) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  // Subscribe with an extended (wildcard) path. Use the callback form to
  // observe which SubscriptionKeys are served: the appended path overlaps the
  // wildcard, so the data tree alone can't prove the appended path itself is
  // delivering.
  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"), this->extSubscriptionPaths());
  folly::Synchronized<std::vector<SubscriptionKey>> servedKeys;
  std::atomic<int> numUpdates{0};
  subscriber->subscribe([&](auto update) {
    servedKeys.withWLock([&](auto& keys) {
      keys.insert(
          keys.end(), update.updatedKeys.begin(), update.updatedKeys.end());
    });
    numUpdates++;
  });

  // Wait for the wildcard initial sync to reach the client first, so the server
  // uid is captured and the live-extend RPC fires in place (rather than
  // deferring to a reconnect that never happens).
  WITH_RETRIES(ASSERT_EVENTUALLY_GE(numUpdates.load(), 1));

  // Append a second extended path (a specific map key) to the live
  // subscription.
  auto extPaths2 = this->mapKeyExtPaths({"newKey"});
  ASSERT_FALSE(extPaths2.empty());
  SubscriptionKey addedKey =
      subscriber->addExtendedPathToLiveSubscription(std::move(extPaths2[0]));
  EXPECT_GT(addedKey, 0);

  // Publish to the appended path's key. The added subscription must serve a
  // patch tagged with addedKey -- proving the appended path is itself live,
  // not merely that the original wildcard keeps working.
  this->testPublisher().publish(
      this->mapKeyPath("newKey"), this->mapKeyData(7));
  WITH_RETRIES({
    bool sawAddedKey = false;
    servedKeys.withRLock([&](const auto& keys) {
      for (auto key : keys) {
        if (key == addedKey) {
          sawAddedKey = true;
        }
      }
    });
    ASSERT_EVENTUALLY_TRUE(sawAddedKey);
  });

  // The original wildcard path keeps delivering too: re-publishing it must be
  // served under its own key. Clear servedKeys first so the assertion measures
  // only post-republish activity, not keys from the initial wildcard sync.
  servedKeys.wlock()->clear();
  data1 = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1);
  WITH_RETRIES({
    bool sawOriginalKey = false;
    servedKeys.withRLock([&](const auto& keys) {
      for (auto key : keys) {
        if (key != addedKey) {
          sawOriginalKey = true;
        }
      }
    });
    ASSERT_EVENTUALLY_TRUE(sawOriginalKey);
  });

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

// Extended-path counterpart of addPathToLiveSubscriptionStreamRevision:
// addExtendedPathToLiveSubscription() sends the new path's SubscriptionKey as
// lastStreamRevision; the server echoes it back as OperMetadata.streamRevision
// on patches served after the add. Assert it round-trips into the SubUpdate.
TYPED_TEST(
    FsdbSubManagerTest,
    addExtendedPathToLiveSubscriptionStreamRevision) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"), this->extSubscriptionPaths());

  // Capture the most recent stream revision seen in the data callback.
  folly::Synchronized<std::optional<StreamRevision>> seenRevision;
  std::atomic<int> numUpdates{0};
  subscriber->subscribe([&](const auto& update) {
    if (update.streamRevision.has_value()) {
      *seenRevision.wlock() = update.streamRevision;
    }
    numUpdates++;
  });

  // Wait for the wildcard initial sync to reach the client first, so the server
  // uid is captured and the live-extend RPC fires (rather than deferring to a
  // reconnect that never happens).
  WITH_RETRIES(ASSERT_EVENTUALLY_GE(numUpdates.load(), 1));

  // Append a second extended path; the client sends addedKey as
  // lastStreamRevision. Re-publishing the original path produces a served chunk
  // that must come back stamped with streamRevision == addedKey.
  auto extPaths2 = this->mapKeyExtPaths({"newKey"});
  ASSERT_FALSE(extPaths2.empty());
  SubscriptionKey addedKey =
      subscriber->addExtendedPathToLiveSubscription(std::move(extPaths2[0]));
  data1 = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1);

  WITH_RETRIES({
    auto rev = seenRevision.copy();
    ASSERT_EVENTUALLY_TRUE(rev.has_value());
    EXPECT_EVENTUALLY_EQ(*rev, addedKey);
  });

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

// Extended-path counterpart of addPathToLiveSubscriptionSurvivesReconnect: an
// extended path appended via addExtendedPathToLiveSubscription() must survive a
// reconnect. It is recorded in the subscriber and merged back into the
// re-subscribe request (FsdbPatchSubscriber::createRequest()), so after the
// stream drops and re-establishes the appended path keeps serving patches under
// its key. Without the reconnect merge it would silently disappear once the
// stream was torn down.
TYPED_TEST(
    FsdbSubManagerTest,
    addExtendedPathToLiveSubscriptionSurvivesReconnect) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  // Track served SubscriptionKeys (to prove the appended path itself is live --
  // it overlaps the wildcard, so the data tree alone cannot prove delivery) and
  // state transitions (to confirm a genuine reconnect cycle).
  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"), this->extSubscriptionPaths());
  folly::Synchronized<std::vector<SubscriptionKey>> servedKeys;
  std::atomic<int> numUpdates{0};
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  subscriber->subscribe(
      [&](auto update) {
        servedKeys.withWLock([&](auto& keys) {
          keys.insert(
              keys.end(), update.updatedKeys.begin(), update.updatedKeys.end());
        });
        numUpdates++;
      },
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        stateChanges.wlock()->push_back(newState);
      });

  // Wait for the wildcard initial sync so the server uid is captured and the
  // live-extend RPC fires in place (rather than deferring to a reconnect).
  WITH_RETRIES(ASSERT_EVENTUALLY_GE(numUpdates.load(), 1));

  // Append a specific map-key extended path to the live subscription.
  auto extPaths2 = this->mapKeyExtPaths({"newKey"});
  ASSERT_FALSE(extPaths2.empty());
  SubscriptionKey addedKey =
      subscriber->addExtendedPathToLiveSubscription(std::move(extPaths2[0]));
  EXPECT_GT(addedKey, 0);

  // Confirm the appended path is live on the current stream before reconnect.
  this->testPublisher().publish(
      this->mapKeyPath("newKey"), this->mapKeyData(7));
  WITH_RETRIES({
    bool sawAddedKey = false;
    servedKeys.withRLock([&](const auto& keys) {
      for (auto key : keys) {
        if (key == addedKey) {
          sawAddedKey = true;
        }
      }
    });
    ASSERT_EVENTUALLY_TRUE(sawAddedKey);
  });

  // Force a reconnect and wait for a genuine
  // CONNECTED -> DISCONNECTED -> CONNECTED cycle. Clear servedKeys so the
  // post-reconnect assertion cannot pass on a stale pre-reconnect observation.
  size_t preReconnectChanges = stateChanges.rlock()->size();
  servedKeys.wlock()->clear();
  subscriber->reconnect();
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });

  // After reconnect, a fresh publish on the appended path must still be served
  // under addedKey -- proving it was re-subscribed via the createRequest()
  // merge rather than dropped when the stream was torn down.
  this->testPublisher().publish(
      this->mapKeyPath("newKey"), this->mapKeyData(8));
  WITH_RETRIES({
    bool sawAddedKey = false;
    servedKeys.withRLock([&](const auto& keys) {
      for (auto key : keys) {
        if (key == addedKey) {
          sawAddedKey = true;
        }
      }
    });
    ASSERT_EVENTUALLY_TRUE(sawAddedKey);
  });

  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, subDeleteKeys) {
  // Publish a map with 3 keys
  auto allData = this->mapData({{"key1", 10}, {"key2", 20}, {"key3", 30}});
  this->connectPublisherAndPublish(this->path1(), allData);

  // Subscribe to only key1 and key2 using extended paths
  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"),
      this->mapKeyExtPaths({"key1", "key2"}));

  auto boundData = subscriber->subscribeBound();

  // Verify initial sync delivers key1 and key2
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    ASSERT_EVENTUALLY_TRUE(
        this->hasMapKey((*boundData.rlock())->toThrift(), "key1"));
    ASSERT_EVENTUALLY_TRUE(
        this->hasMapKey((*boundData.rlock())->toThrift(), "key2"));
  });
  // key3 not subscribed, should not be present
  EXPECT_FALSE(this->hasMapKey((*boundData.rlock())->toThrift(), "key3"));

  // Delete key1 and key2 (leave key3)
  this->testPublisher().publishDelete(this->mapKeyPath("key1"));
  this->testPublisher().publishDelete(this->mapKeyPath("key2"));

  // Verify subscriber sees deletions
  WITH_RETRIES({
    EXPECT_EVENTUALLY_FALSE(
        this->hasMapKey((*boundData.rlock())->toThrift(), "key1"));
    EXPECT_EVENTUALLY_FALSE(
        this->hasMapKey((*boundData.rlock())->toThrift(), "key2"));
  });

  // Delete key3 (unsubscribed) - subscriber should not be affected
  this->testPublisher().publishDelete(this->mapKeyPath("key3"));

  // key3 was never in subscriber's data and deleting it should not
  // affect the subscriber's state
  EXPECT_FALSE(this->hasMapKey((*boundData.rlock())->toThrift(), "key1"));
  EXPECT_FALSE(this->hasMapKey((*boundData.rlock())->toThrift(), "key2"));
  EXPECT_FALSE(this->hasMapKey((*boundData.rlock())->toThrift(), "key3"));
}

TYPED_TEST(FsdbSubManagerTest, subscribeExtended) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"), this->extSubscriptionPaths());

  // verify initial sync
  auto boundData = subscriber->subscribeBound();
  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    EXPECT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });

  // update data and verify receieved
  data1 = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1);
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(
      this->fetchData1((*boundData.rlock())->toThrift()), data1));
  subscriber.reset();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
}

TYPED_TEST(FsdbSubManagerTest, restartPublisher) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  std::optional<SubscriptionState> lastStateSeen;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    // received data
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
    ASSERT_EVENTUALLY_TRUE(*boundData.rlock());
    ASSERT_EVENTUALLY_EQ(
        this->fetchData1((*boundData.rlock())->toThrift()), data1);
  });
  this->testPublisher().disconnect();
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::DISCONNECTED));
  data1 = this->data1("bar");
  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
                   this->fetchData1((*boundData.rlock())->toThrift()), data1););
}

TYPED_TEST(FsdbSubManagerTest, reconnectTriggersDisconnectAndReconnect) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  std::optional<SubscriptionState> lastStateSeen;
  // Record every transition the SubscriptionStateChangeCb receives so we can
  // assert the CONNECTED -> DISCONNECTED -> CONNECTED sequence is delivered,
  // not just that the final state is CONNECTED.
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });
  // Snapshot the size of the transition log right before the reconnect so we
  // can scan only the post-reconnect tail for the CONNECTED -> DISCONNECTED
  // -> CONNECTED sequence.
  size_t preReconnectChanges = stateChanges.rlock()->size();

  subscriber->reconnect();

  // Wait for the full transition sequence to land in the callback log.
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  // The exact transitions delivered to the SubscriptionStateChangeCb after
  // reconnect must be DISCONNECTED followed by CONNECTED.
  {
    auto changes = stateChanges.rlock();
    std::vector<SubscriptionState> postReconnect(
        changes->begin() + preReconnectChanges, changes->end());
    EXPECT_EQ(postReconnect.front(), SubscriptionState::DISCONNECTED);
    EXPECT_EQ(postReconnect.back(), SubscriptionState::CONNECTED);
  }
  // And a fresh publish after the reconnect is delivered (the subscription
  // is functionally restored, not just its state).
  auto data1New = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1New);
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(
      this->fetchData1((*boundData.rlock())->toThrift()), data1New));
}

TYPED_TEST(FsdbSubManagerTest, reconnectValidationWithGR) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  std::optional<SubscriptionState> lastStateSeen;
  // Record every transition the SubscriptionStateChangeCb receives so we can
  // assert the CONNECTED -> DISCONNECTED_GR_HOLD -> CONNECTED sequence is
  // delivered, not just that the final state is CONNECTED.
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 5), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });
  // Snapshot the size of the transition log right before the reconnect so we
  // can scan only the post-reconnect tail for the CONNECTED ->
  // DISCONNECTED_GR_HOLD -> CONNECTED sequence.
  size_t preReconnectChanges = stateChanges.rlock()->size();

  subscriber->reconnect();

  // Wait for the full transition sequence to land in the callback log.
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  // The exact transitions delivered to the SubscriptionStateChangeCb after
  // reconnect must be DISCONNECTED_GR_HOLD followed by CONNECTED.
  {
    auto changes = stateChanges.rlock();
    std::vector<SubscriptionState> postReconnect(
        changes->begin() + preReconnectChanges, changes->end());
    EXPECT_EQ(postReconnect.front(), SubscriptionState::DISCONNECTED_GR_HOLD);
    EXPECT_EQ(postReconnect.back(), SubscriptionState::CONNECTED);
  }
  // And a fresh publish after the reconnect is delivered (the subscription
  // is functionally restored, not just its state).
  auto data1New = this->data1("bar");
  this->testPublisher().publish(this->path1(), data1New);
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(
      this->fetchData1((*boundData.rlock())->toThrift()), data1New));
}

TYPED_TEST(FsdbSubManagerTest, reconnectAfterStopIsNoop) {
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound();
  WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(this->isSubscribed("test")));
  subscriber->stop();
  WITH_RETRIES(EXPECT_EVENTUALLY_FALSE(this->isSubscribed("test")));
  // After stop(), the underlying subscriber is destroyed and getInfo() has
  // no value to report. Capture this baseline before invoking reconnect().
  EXPECT_FALSE(subscriber->getInfo().has_value());

  // Should be safe to call after stop -- no crash, no-op.
  subscriber->reconnect();

  // Verify reconnect was a true no-op: no resubscription happened and the
  // subscriber remains in its post-stop state.
  EXPECT_FALSE(subscriber->getInfo().has_value());
  EXPECT_FALSE(this->isSubscribed("test"));
}

TYPED_TEST(FsdbSubManagerTest, reconnectOnExtendedSubscriber) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  std::optional<SubscriptionState> lastStateSeen;
  bool sawDisconnected = false;
  auto subscriber = this->createExtendedSubscriber(
      this->getSubscriptionOptions("test"), this->extSubscriptionPaths());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        if (newState == SubscriptionState::DISCONNECTED) {
          sawDisconnected = true;
        }
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });

  subscriber->reconnect();

  WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(sawDisconnected); });
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED); });
}

TYPED_TEST(FsdbSubManagerTest, reconnectInvalidateGRSkipsGRHold) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 5), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });
  size_t preReconnectChanges = stateChanges.rlock()->size();

  auto reconnectStart = std::chrono::steady_clock::now();
  subscriber->reconnect(true);

  // Wait for the post-reconnect transitions to land in the callback log.
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  // The first post-reconnect transition must be DISCONNECTED_GR_HOLD_EXPIRED
  // (NOT DISCONNECTED_GR_HOLD), proving GR hold was skipped entirely.
  std::chrono::steady_clock::time_point grHoldExpiredAt;
  {
    auto changes = stateChanges.rlock();
    std::vector<SubscriptionState> postReconnect(
        changes->begin() + preReconnectChanges, changes->end());
    EXPECT_EQ(
        postReconnect.front(), SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
    EXPECT_EQ(postReconnect.back(), SubscriptionState::CONNECTED);
    grHoldExpiredAt = std::chrono::steady_clock::now();
  }
  // Sanity: the expired transition landed well before the 5s GR hold would
  // have elapsed, proving we did not just wait it out.
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      grHoldExpiredAt - reconnectStart);
  EXPECT_LT(elapsed.count(), 5);
}

TYPED_TEST(FsdbSubManagerTest, reconnectInvalidateGRWhileInGRHold) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  // grHoldTimer=30 so the natural GR expiry will not race with the test.
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 30), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });

  // Drop the publisher to land the subscription in DISCONNECTED_GR_HOLD.
  this->testPublisher().disconnect();
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD));

  // Now invalidate GR -- subscription should immediately transition to
  // DISCONNECTED_GR_HOLD_EXPIRED via the override's "already in GR_HOLD" path,
  // not by waiting out the 30s timer.
  auto invalidateStart = std::chrono::steady_clock::now();
  subscriber->reconnect(true);
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED));
  auto invalidateElapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - invalidateStart);
  EXPECT_LT(invalidateElapsed.count(), 30);

  // Bring publisher back; subscription returns to CONNECTED.
  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED));
}

TYPED_TEST(FsdbSubManagerTest, reconnectInvalidateGRFlagDoesNotLeak) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 5), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });

  // Round 1: reconnect(noGR=true) -- consumes the flag. Wait for
  // the full DISCONNECTED_GR_HOLD_EXPIRED -> CONNECTED cycle to land in the
  // transition log (lastStateSeen is racy because the brief GR_HOLD_EXPIRED
  // value is overwritten by CONNECTED before the retry loop wakes up).
  size_t round1Snapshot = stateChanges.rlock()->size();
  subscriber->reconnect(true);
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), round1Snapshot + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  {
    auto changes = stateChanges.rlock();
    std::vector<SubscriptionState> round1(
        changes->begin() + round1Snapshot, changes->end());
    EXPECT_EQ(round1.front(), SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
  }

  // Round 2: an UNRELATED publisher disconnect must follow the normal GR
  // path (DISCONNECTED_GR_HOLD), proving the flag was consumed and did not
  // leak into a subsequent unrelated flap.
  size_t round2Snapshot = stateChanges.rlock()->size();
  this->testPublisher().disconnect();
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GT(changes->size(), round2Snapshot);
    ASSERT_EVENTUALLY_EQ(
        (*changes)[round2Snapshot], SubscriptionState::DISCONNECTED_GR_HOLD);
  });
}

TYPED_TEST(
    FsdbSubManagerTest,
    reconnectInvalidateGRWhileInGRHoldExpiredDoesNotLeak) {
  // Bug regression: reconnect(noGR=true) called while subscription is
  // in DISCONNECTED_GR_HOLD_EXPIRED must not leak forceGRExpired_=true. There
  // is no upcoming CONNECTED->DISCONNECTED transition for the flag to be
  // consumed against, so without the fix the stale flag survives until the
  // next legitimate disconnect (after the publisher recovers) and incorrectly
  // skips GR hold then.
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  // Short GR timer so we can wait it out quickly within the test.
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 2), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });

  // Drop publisher and let the GR timer expire naturally so the subscription
  // settles in DISCONNECTED_GR_HOLD_EXPIRED -- the state in which the bug
  // would set forceGRExpired_ with no consumer to clear it.
  this->testPublisher().disconnect();
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED));

  // Trigger reconnect(noGR=true) while in GR_HOLD_EXPIRED. Pre-fix
  // this leaks forceGRExpired_=true; with the fix it is a no-op because no
  // upcoming CONNECTED->DISCONNECTED edge will consume it.
  subscriber->reconnect(true);

  // Bring publisher back; subscription returns to CONNECTED.
  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED));

  // An UNRELATED publisher disconnect that follows MUST traverse the normal
  // GR path (DISCONNECTED_GR_HOLD), NOT skip to DISCONNECTED_GR_HOLD_EXPIRED.
  // With the buggy code the stale forceGRExpired_ is consumed here and this
  // assertion fires. With the fix the first post-flap transition is GR_HOLD.
  size_t snapshot = stateChanges.rlock()->size();
  this->testPublisher().disconnect();
  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GT(changes->size(), snapshot);
    EXPECT_EQ((*changes)[snapshot], SubscriptionState::DISCONNECTED_GR_HOLD);
  });
}

TYPED_TEST(FsdbSubManagerTest, reconnectInvalidateGRWithoutGRHoldIsNoop) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  // grHoldTimer=0 -- noGR should be a no-op (default disconnect path).
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });
  size_t preReconnectChanges = stateChanges.rlock()->size();

  subscriber->reconnect(true);

  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  // First post-reconnect transition must be plain DISCONNECTED, not
  // DISCONNECTED_GR_HOLD_EXPIRED -- proves noGR is a no-op when
  // grHoldTimeSec_ == 0.
  auto changes = stateChanges.rlock();
  std::vector<SubscriptionState> postReconnect(
      changes->begin() + preReconnectChanges, changes->end());
  EXPECT_EQ(postReconnect.front(), SubscriptionState::DISCONNECTED);
  EXPECT_EQ(postReconnect.back(), SubscriptionState::CONNECTED);
}

TYPED_TEST(FsdbSubManagerTest, reconnectInvalidateGROnExtendedSubscriber) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  folly::Synchronized<std::vector<SubscriptionState>> stateChanges;
  std::optional<SubscriptionState> lastStateSeen;
  // Use createSubscriber to set grHoldTimer, then convert to extended by
  // adding an extended path before subscribing. createExtendedSubscriber
  // uses createSubscriber("...") which defaults grHoldTimer=0, so we build
  // it manually here to get GR.
  auto subscriber =
      this->createSubscriber(this->getSubscriptionOptions("test", 5));
  for (auto& path : this->extSubscriptionPaths()) {
    subscriber->addExtendedPath(std::move(path));
  }
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
        stateChanges.wlock()->push_back(newState);
      });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(this->isSubscribed("test"));
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
  });
  size_t preReconnectChanges = stateChanges.rlock()->size();

  subscriber->reconnect(true);

  WITH_RETRIES({
    auto changes = stateChanges.rlock();
    ASSERT_EVENTUALLY_GE(changes->size(), preReconnectChanges + 2);
    ASSERT_EVENTUALLY_EQ(changes->back(), SubscriptionState::CONNECTED);
  });
  auto changes = stateChanges.rlock();
  std::vector<SubscriptionState> postReconnect(
      changes->begin() + preReconnectChanges, changes->end());
  EXPECT_EQ(
      postReconnect.front(), SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
  EXPECT_EQ(postReconnect.back(), SubscriptionState::CONNECTED);
}

TYPED_TEST(FsdbSubManagerTest, verifyGR) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  std::optional<SubscriptionState> lastStateSeen;
  int numUpdates = 0;
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test", 3), this->root().agent());
  subscriber->subscribe(
      [&](auto update) { numUpdates++; },
      [&](auto, auto newState, std::optional<bool> /*initialSyncHasData*/) {
        lastStateSeen = newState;
      });
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
    ASSERT_EVENTUALLY_EQ(numUpdates, 1);
  });

  this->testPublisher().disconnect();
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD));
  this->connectPublisherAndPublish(this->path1(), data1);

  this->testPublisher().disconnect();
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
    ASSERT_EVENTUALLY_EQ(numUpdates, 1);
  });

  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED);
    ASSERT_EVENTUALLY_EQ(numUpdates, 2);
  });
}

template <typename SubscriberT>
class FsdbSubManagerHbTest : public FsdbSubManagerTest<SubscriberT> {
 public:
  void SetUp() override {
    FLAGS_serveHeartbeats = true;
    FLAGS_statsSubscriptionHeartbeat_s = 1;
    FLAGS_stateSubscriptionHeartbeat_s = 1;
    FsdbSubManagerTest<SubscriberT>::SetUp();
  }
};

TYPED_TEST_SUITE(FsdbSubManagerHbTest, SubscriberTypes);

TYPED_TEST(FsdbSubManagerHbTest, verifyHeartbeatCb) {
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);

  std::optional<SubscriptionState> lastStateSeen;
  std::optional<int64_t> lastPublishedAt;
  int numHeartbeats{0};
  auto subscriber = this->createSubscriber(
      this->getSubscriptionOptions("test"), this->root().agent());
  subscriber->subscribe(
      [&](auto update) {
        if (update.lastPublishedAt.has_value()) {
          lastPublishedAt = update.lastPublishedAt.value();
        }
      },
      [&](auto, auto newState, std::optional<bool> initialSyncHasData) {
        lastStateSeen = newState;
      },
      [&](std::optional<OperMetadata> md) { numHeartbeats++; });

  WITH_RETRIES(
      { ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED); });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_TRUE(
        lastPublishedAt.has_value() && lastPublishedAt.value() > 0);
  });

  // Verify heartbeat callback is called
  WITH_RETRIES_N(100, { EXPECT_EVENTUALLY_GT(numHeartbeats, 0); });

  // Verify heartbeat callback continues to be called
  numHeartbeats = 0;
  WITH_RETRIES_N(100, { EXPECT_EVENTUALLY_GT(numHeartbeats, 0); });
}

TYPED_TEST(FsdbSubManagerHbTest, verifyPublisherRestartWithGR) {
  // Use a longer GR hold time so the stale state timer (which starts at
  // subscriber creation) doesn't fire before heartbeats arrive, even when
  // server setup takes a few seconds.
  int grHoldTime = 10;
  int numHeartbeatsBeforePublisherConnect = 1;
  int numHeartbeatsAfterPublisherDisconnect = 3;
  int numHeartbeats = 0;

  // Step 0: Create subscription before publisher is connected
  std::optional<SubscriptionState> lastStateSeen;
  SubscriptionOptions options =
      this->getSubscriptionOptions("test", grHoldTime);
  options.requireInitialSyncToMarkConnect_ = true;
  auto subscriber = this->createSubscriber(options, this->root().agent());
  auto boundData = subscriber->subscribeBound(
      [&](auto, auto newState, std::optional<bool> initialSyncHasData) {
        XLOG(INFO) << "DBG: SubscriptionState: "
                   << subscriptionStateToString(newState)
                   << " initialSyncHasData: "
                   << (initialSyncHasData.has_value() ? "true" : "false");
        lastStateSeen = newState;
      },
      [&](std::optional<OperMetadata> /*md*/) {
        numHeartbeats++;
        XLOG(INFO) << "DBG: numHeartbeats: " << numHeartbeats;
      });

  // Step 1: Verify that before publisher connect, heartbeats are received
  // but SubscriptionState does not transition yet to CONNECTED.
  WITH_RETRIES_N(30, {
    ASSERT_EVENTUALLY_GE(numHeartbeats, numHeartbeatsBeforePublisherConnect);
  });
  // The subscription must NOT be in CONNECTED state yet (requireInitialSync
  // prevents CONNECTED without initial data). Other states (e.g.
  // DISCONNECTED_GR_HOLD_EXPIRED) are allowed if the stale timer happened to
  // fire concurrently.
  ASSERT_FALSE(
      lastStateSeen.has_value() &&
      lastStateSeen.value() == SubscriptionState::CONNECTED);

  // Step 2: Connect publisher and publish initial data.
  // Note: the state change to CONNECTED fires before the data callback
  // (see FsdbPatchSubscriber.cpp serveStream), so we must wait separately
  // for boundData to be populated after seeing CONNECTED.
  auto data1 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data1);
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED));
  WITH_RETRIES({
    auto data = *boundData.rlock();
    ASSERT_EVENTUALLY_NE(data, nullptr);
    if (data) {
      ASSERT_EVENTUALLY_EQ(this->fetchData1(data->toThrift()), data1);
    }
  });

  // Step 2: Disconnect publisher, verify DISCONNECTED_GR_HOLD state
  this->testPublisher().disconnect();
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(this->numActivePublishers(), 0));
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD));

  // Step 3: Verify that subscriber is receiving heartbeats from connected
  // FSDB server
  int heartbeatsBefore = numHeartbeats;
  WITH_RETRIES_N(30, {
    ASSERT_EVENTUALLY_GE(
        numHeartbeats,
        heartbeatsBefore + numHeartbeatsAfterPublisherDisconnect);
  });

  // Step 4: Verify subscription eventually goes to DISCONNECTED_GR_HOLD_EXPIRED
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      lastStateSeen, SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED));

  // Step 5: Connect publisher and verify subscription is CONNECTED
  auto data2 = this->data1("foo");
  this->connectPublisherAndPublish(this->path1(), data2);
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(lastStateSeen, SubscriptionState::CONNECTED));
  // Same as Step 2: state change fires before data callback, so retry.
  WITH_RETRIES({
    auto data = *boundData.rlock();
    ASSERT_EVENTUALLY_NE(data, nullptr);
    if (data) {
      ASSERT_EVENTUALLY_EQ(this->fetchData1(data->toThrift()), data2);
    }
  });
}

} // namespace facebook::fboss::fsdb::test
