// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fboss/fsdb/oper/Subscription.h>
#include <fboss/fsdb/oper/SubscriptionStore.h>
#include <limits>

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Timeout.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <gtest/gtest.h>

#include <unordered_map>

namespace facebook::fboss::fsdb::test {

constexpr int32_t kSubscriptionServeQueueSize = 100;

namespace {
template <typename Gen>
folly::coro::Task<typename Gen::value_type> consumeOne(Gen& generator) {
  auto item = co_await generator.next();
  auto&& value = *item;
  co_return std::move(value);
}
} // namespace

template <typename SubscriptionT>
class SubscriptionTests : public ::testing::Test {
 public:
  void SetUp() override {
    heartbeatThread_ = std::make_unique<folly::ScopedEventBaseThread>(
        "SubscriptionHeartbeats");
  }

  auto makeSubscription() {
    std::vector<std::string> path = {"test"};
    if constexpr (std::is_same_v<SubscriptionT, ExtendedPatchSubscription>) {
      return SubscriptionT::create(
          SubscriptionIdentifier("test-sub"),
          path,
          OperProtocol::BINARY,
          std::nullopt,
          heartbeatThread_->getEventBase(),
          std::chrono::milliseconds(100),
          kSubscriptionServeQueueSize);
    } else {
      return SubscriptionT::create(
          SubscriptionIdentifier("test-sub"),
          path.begin(),
          path.end(),
          OperProtocol::BINARY,
          std::nullopt,
          heartbeatThread_->getEventBase(),
          std::chrono::milliseconds(100),
          kSubscriptionServeQueueSize);
    }
  }

 private:
  std::shared_ptr<folly::ScopedEventBaseThread> heartbeatThread_;
};

using SimpleSubTypes = ::testing::
    Types<PathSubscription, DeltaSubscription, ExtendedPatchSubscription>;
TYPED_TEST_SUITE(SubscriptionTests, SimpleSubTypes);

TYPED_TEST(SubscriptionTests, verifyHeartbeat) {
  auto [gen, sub] = this->makeSubscription();
  // Should get a heartbeat chunk
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(gen), std::chrono::seconds{1}));
}

TEST(SubscriptionIdentifierTest, equalityComparesBothFields) {
  SubscriptionIdentifier base("subA", 5);
  EXPECT_EQ(base, SubscriptionIdentifier("subA", 5));
  EXPECT_FALSE(base == SubscriptionIdentifier("subB", 5)); // subscriberId
  EXPECT_FALSE(base == SubscriptionIdentifier("subA", 6)); // uid
}

TEST(SubscriptionIdentifierTest, hashConsistentWithEquality) {
  SubscriptionIdentifier::Hash hash;
  // uid set: equal ids hash equal
  EXPECT_EQ(
      hash(SubscriptionIdentifier("subA", 5)),
      hash(SubscriptionIdentifier("subA", 5)));
  // uid unset (0): hash derives from subscriberId, equal ids hash equal
  EXPECT_EQ(
      hash(SubscriptionIdentifier("subA")),
      hash(SubscriptionIdentifier("subA")));
}

TEST(SubscriptionIdentifierTest, usableAsUnorderedMapKey) {
  std::unordered_map<SubscriptionIdentifier, int, SubscriptionIdentifier::Hash>
      index;
  index.emplace(SubscriptionIdentifier("subA", 1), 1);
  index.emplace(SubscriptionIdentifier("subB"), 2); // uid 0
  EXPECT_EQ(index.at(SubscriptionIdentifier("subA", 1)), 1);
  EXPECT_EQ(index.at(SubscriptionIdentifier("subB")), 2);
  EXPECT_EQ(index.find(SubscriptionIdentifier("subA", 2)), index.end());
}

namespace {
ExtendedOperPath makeExtendedPath(const std::vector<std::string>& tokens) {
  ExtendedOperPath extPath;
  std::vector<OperPathElem> elems;
  elems.reserve(tokens.size());
  for (const auto& tok : tokens) {
    elems.emplace_back().set_raw(tok);
  }
  extPath.path() = std::move(elems);
  return extPath;
}

ExtendedOperPath makeRegexPath(std::string regex) {
  ExtendedOperPath extPath;
  extPath.path()->emplace_back().set_raw("root");
  extPath.path()->emplace_back().set_regex(std::move(regex));
  return extPath;
}

std::unique_ptr<ExtendedPatchSubscription> makePatchSubscription(
    folly::ScopedEventBaseThread& heartbeatThread,
    ExtSubPathMap paths) {
  auto [gen, sub] = ExtendedPatchSubscription::create(
      SubscriptionIdentifier("test-sub"),
      std::move(paths),
      OperProtocol::BINARY,
      std::nullopt,
      heartbeatThread.getEventBase(),
      std::chrono::milliseconds(100),
      kSubscriptionServeQueueSize);
  return std::move(sub);
}
} // namespace

TEST(ExtendedPatchSubscriptionTest, compilesRegexesOnCreate) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeRegexPath("key[0-9]+");

  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));

  ASSERT_TRUE(sub->hasWildcardPath());
  const auto& regexes = sub->compiledRegexesAt(1);
  ASSERT_EQ(regexes.size(), 2);
  EXPECT_EQ(regexes[0], nullptr);
  ASSERT_NE(regexes[1], nullptr);
  EXPECT_TRUE(re2::RE2::FullMatch("key42", *regexes[1]));
}

TEST(ExtendedPatchSubscriptionTest, refreshesRegexesOnAppend) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeExtendedPath({"root", "raw"});
  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));
  ExtendedSubscription* baseSub = sub.get();

  ExtSubPathMap addedPaths;
  addedPaths[2] = makeRegexPath("new.*");
  EXPECT_EQ(
      baseSub->addPaths(std::move(addedPaths)),
      std::vector<SubscriptionKey>({2}));

  ASSERT_TRUE(sub->hasWildcardPath());
  const auto& regexes = sub->compiledRegexesAt(2);
  ASSERT_EQ(regexes.size(), 2);
  ASSERT_NE(regexes[1], nullptr);
  EXPECT_TRUE(re2::RE2::FullMatch("new-key", *regexes[1]));
  EXPECT_FALSE(sub->isInitialSyncPending(2));
}

TEST(ExtendedPatchSubscriptionTest, tracksPendingAppendedKeysAfterInitialSync) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeRegexPath("existing.*");
  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));
  sub->recordInitialSyncCompleted();

  ExtSubPathMap addedPaths;
  addedPaths[2] = makeRegexPath("new.*");
  ASSERT_EQ(
      sub->addPaths(std::move(addedPaths)), std::vector<SubscriptionKey>({2}));
  EXPECT_FALSE(sub->isInitialSyncPending(1));
  EXPECT_TRUE(sub->isInitialSyncPending(2));

  sub->clearInitialSyncPending({2});
  EXPECT_FALSE(sub->isInitialSyncPending(2));
}

TEST(
    ExtendedPatchSubscriptionTest,
    rejectedDuplicateDoesNotChangeWildcardState) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeExtendedPath({"root", "raw"});
  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));

  ExtSubPathMap duplicatePath;
  duplicatePath[1] = makeRegexPath("duplicate.*");
  EXPECT_TRUE(sub->addPaths(std::move(duplicatePath)).empty());

  EXPECT_FALSE(sub->hasWildcardPath());
  EXPECT_EQ(
      sub->compiledRegexesAt(1),
      ExtendedPatchSubscription::CompiledRegexPath({nullptr, nullptr}));
}

TEST(ExtendedPatchSubscriptionTest, invalidRegexMatchesNothing) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeRegexPath("[");

  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));

  const auto& regex = sub->compiledRegexesAt(1).at(1);
  ASSERT_NE(regex, nullptr);
  EXPECT_FALSE(regex->ok());
  EXPECT_FALSE(re2::RE2::FullMatch("anything", *regex));
}

TEST(ExtendedPatchSubscriptionTest, rawOnlyHasNoWildcard) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  ExtSubPathMap paths;
  paths[1] = makeExtendedPath({"root", "raw"});

  auto sub = makePatchSubscription(heartbeatThread, std::move(paths));

  EXPECT_FALSE(sub->hasWildcardPath());
  EXPECT_EQ(
      sub->compiledRegexesAt(1),
      ExtendedPatchSubscription::CompiledRegexPath({nullptr, nullptr}));
}

TEST(ExtendedSubscriptionTest, addPathsAppendsAndSkipsCollisions) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");

  ExtSubPathMap initialPaths;
  initialPaths[1] = makeExtendedPath({"a"});
  auto [gen, sub] = ExtendedPatchSubscription::create(
      SubscriptionIdentifier("test-sub"),
      initialPaths,
      OperProtocol::BINARY,
      std::nullopt,
      heartbeatThread.getEventBase(),
      std::chrono::milliseconds(100),
      kSubscriptionServeQueueSize);

  // Append two new keys; both are inserted.
  ExtSubPathMap newPaths;
  newPaths[2] = makeExtendedPath({"b"});
  newPaths[3] = makeExtendedPath({"c"});
  const std::vector<SubscriptionKey> expectedAdded{2, 3};
  EXPECT_EQ(sub->addPaths(std::move(newPaths)), expectedAdded);
  EXPECT_EQ(sub->size(), 3);

  // Colliding key (2) is skipped; only the new key (4) is inserted.
  ExtSubPathMap collidingPaths;
  collidingPaths[2] = makeExtendedPath({"b"});
  collidingPaths[4] = makeExtendedPath({"d"});
  const std::vector<SubscriptionKey> expectedAfterCollision{4};
  EXPECT_EQ(sub->addPaths(std::move(collidingPaths)), expectedAfterCollision);
  EXPECT_EQ(sub->size(), 4);
}

// Unregistering an extended subscription with pending added-path work must
// clear its extendedSubsWithAddedPaths_ entry -- that list holds a strong
// shared_ptr, so a leftover entry would leak the cancelled subscription.
TEST(ExtendedSubscriptionTest, unregisterClearsAddedPathsTracking) {
  folly::ScopedEventBaseThread heartbeatThread("SubscriptionHeartbeats");
  SubscriptionStore store;

  ExtSubPathMap initialPaths;
  initialPaths[1] = makeExtendedPath({"a"});
  auto [gen, sub] = ExtendedPatchSubscription::create(
      SubscriptionIdentifier("test-sub"),
      initialPaths,
      OperProtocol::BINARY,
      std::nullopt,
      heartbeatThread.getEventBase(),
      std::chrono::milliseconds(100),
      kSubscriptionServeQueueSize);

  std::shared_ptr<ExtendedSubscription> shared = std::move(sub);
  const auto id = shared->subscriptionId();
  store.registerExtendedSubscription(shared);
  ASSERT_EQ(store.extendedSubscriptions().size(), 1);
  const std::string name = store.extendedSubscriptions().begin()->first;

  // Simulate initial sync already done, so appended paths are recorded as
  // deferred work (extendedSubsWithAddedPaths_) rather than folded into it.
  store.initialSyncNeededExtended().erase(shared);

  ExtSubPathMap newPaths;
  newPaths[2] = makeExtendedPath({"b"});
  EXPECT_EQ(
      store.addPatchSubscriptionPaths(
          id, std::move(newPaths), std::nullopt /* publisherRoot */),
      std::nullopt);
  ASSERT_EQ(store.extendedSubsWithAddedPaths().size(), 1);

  // Drop the local strong ref so only the store holds it; weak_ptr then detects
  // whether the subscription is pinned alive after unregister.
  std::weak_ptr<ExtendedSubscription> weak = shared;
  shared.reset();

  store.unregisterExtendedSubscription(name);

  EXPECT_TRUE(store.extendedSubsWithAddedPaths().empty());
  // If unregister failed to erase the tracking entry, its strong shared_ptr
  // would keep the cancelled subscription alive here.
  EXPECT_TRUE(weak.expired());
}

TEST(ServeInterval, RoundsUpAndClamps) {
  constexpr uint32_t kTick = 1000;
  constexpr uint32_t kMax = 10000;

  // Sub-tick and non-multiples round UP to the next whole tick.
  EXPECT_EQ(normalizeServeIntervalMs(1, kTick, kMax), 1000u);
  EXPECT_EQ(normalizeServeIntervalMs(1500, kTick, kMax), 2000u);
  EXPECT_EQ(normalizeServeIntervalMs(2000, kTick, kMax), 2000u);

  // Zero clamps up to the floor; anything above the default clamps down to it.
  EXPECT_EQ(normalizeServeIntervalMs(0, kTick, kMax), 1000u);
  EXPECT_EQ(normalizeServeIntervalMs(60000, kTick, kMax), kMax);

  // The largest cadence a client can express: i32 seconds converted to ms.
  // Must clamp to the default rather than wrap through the round-up.
  constexpr uint64_t kMaxRequestMs =
      static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) * 1000;
  EXPECT_EQ(normalizeServeIntervalMs(kMaxRequestMs, kTick, kMax), kMax);

  // A disabled tick collapses every request onto the default interval.
  EXPECT_EQ(normalizeServeIntervalMs(2000, 0, kMax), kMax);
}

TEST(ServeInterval, ResolveServeTick) {
  constexpr uint32_t kDefault = 10000;

  // Non-positive means "no sub-default intervals": everyone at the default.
  EXPECT_EQ(resolveServeTickMs(0, kDefault), kDefault);
  EXPECT_EQ(resolveServeTickMs(-1, kDefault), kDefault);

  // Ticks that divide the default are taken as requested.
  EXPECT_EQ(resolveServeTickMs(2000, kDefault), 2000u);
  EXPECT_EQ(resolveServeTickMs(2500, kDefault), 2500u);
  EXPECT_EQ(resolveServeTickMs(5000, kDefault), 5000u);

  // Below the floor: raised, so the bucket count cannot exceed the cap.
  EXPECT_EQ(resolveServeTickMs(1, kDefault), 2000u);
  EXPECT_EQ(resolveServeTickMs(1500, kDefault), 2000u);

  // At or above the default: one bucket.
  EXPECT_EQ(resolveServeTickMs(kDefault, kDefault), kDefault);
  EXPECT_EQ(resolveServeTickMs(20000, kDefault), kDefault);

  // A tick that does not divide the default is rejected outright: the slowest
  // bucket would otherwise land ahead of the default cadence.
  EXPECT_EQ(resolveServeTickMs(3000, kDefault), kDefault);
  EXPECT_EQ(resolveServeTickMs(4000, kDefault), kDefault);
  EXPECT_EQ(resolveServeTickMs(6000, kDefault), kDefault);
  EXPECT_EQ(resolveServeTickMs(9000, kDefault), kDefault);

  // The invariant the snapping exists to protect: whatever an operator asks
  // for, the slowest bucket lands exactly on the default cadence and the
  // bucket count stays within the cap.
  for (int64_t requested = 1; requested <= 2 * kDefault; ++requested) {
    const uint32_t tick = resolveServeTickMs(requested, kDefault);
    ASSERT_GT(tick, 0u) << "requested " << requested;
    const size_t buckets = serveBucketCount(tick, kDefault);
    EXPECT_EQ(buckets * tick, kDefault) << "requested " << requested;
    EXPECT_LE(buckets, kMaxServeBuckets) << "requested " << requested;
  }
}

TEST(ServeInterval, BucketIndexing) {
  constexpr uint32_t kTick = 1000;
  constexpr uint32_t kMax = 10000;

  // Bucket 0 is served every tick; the last bucket is the default interval.
  EXPECT_EQ(serveBucketIndex(1000, kTick), 0u);
  EXPECT_EQ(serveBucketIndex(2000, kTick), 1u);
  EXPECT_EQ(serveBucketIndex(10000, kTick), 9u);
  EXPECT_EQ(serveBucketCount(kTick, kMax), 10u);

  // Every normalized interval must land inside the allocated bucket space.
  for (uint32_t requested = 0; requested <= 2 * kMax; requested += 137) {
    const auto interval = normalizeServeIntervalMs(requested, kTick, kMax);
    EXPECT_LT(serveBucketIndex(interval, kTick), serveBucketCount(kTick, kMax));
  }
}

TEST(SubscriptionStoreSubscriberCount, TracksRegisterAndUnregister) {
  folly::ScopedEventBaseThread evbThread("ServeIntervalTest");
  SubscriptionStore store;
  EXPECT_FALSE(store.hasAny());

  std::vector<std::string> path = {"test"};
  auto [gen, sub] = PathSubscription::create(
      SubscriptionIdentifier("interval-sub"),
      path.begin(),
      path.end(),
      OperProtocol::BINARY,
      std::nullopt,
      evbThread.getEventBase(),
      std::chrono::milliseconds(100),
      kSubscriptionServeQueueSize);
  store.registerSubscription(std::move(sub));

  EXPECT_TRUE(store.hasAny());

  ASSERT_EQ(store.subscriptions().size(), 1);
  store.unregisterSubscription(store.subscriptions().begin()->first);
  EXPECT_FALSE(store.hasAny());
}

} // namespace facebook::fboss::fsdb::test
