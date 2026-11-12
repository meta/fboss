// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/json/dynamic.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <utility>

#include <fboss/fsdb/oper/NaivePeriodicSubscribableStorage.h>
#include <fboss/lib/CommonUtils.h>
#include <fboss/thrift_cow/visitors/PatchBuilder.h>
#include <folly/Random.h>
#include <folly/Utility.h>
#include <folly/coro/AsyncGenerator.h>
#include <thread>

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/coro/Timeout.h>
#include <thrift/lib/cpp2/op/Get.h>
#include "fboss/fsdb/oper/ExtendedPathBuilder.h"
#include "fboss/fsdb/oper/tests/TestHelpers.h"
#include "fboss/fsdb/tests/gen-cpp2-thriftpath/thriftpath_test.h" // @manual=//fboss/fsdb/tests:thriftpath_test_thrift-cpp2-thriftpath
#include "fboss/fsdb/tests/gen-cpp2/thriftpath_test_types.h"

using folly::dynamic;
using namespace testing;

namespace {

using namespace facebook::fboss::fsdb;

// TODO: templatize test cases
constexpr auto kTestStructMapOfStringToI32FieldId = folly::to_underlying(
    apache::thrift::op::
        get_field_id_v<TestStruct, apache::thrift::ident::mapOfStringToI32>);

constexpr auto kSubscriber = "testSubscriber";

template <typename value_type>
folly::coro::Task<value_type> consumeOne(
    SubscriptionStreamReader<value_type>& reader) {
  auto& generator = reader.generator_;
  auto item = co_await generator.next();
  auto&& value = *item;
  co_return std::move(value);
}

template <typename Gen>
folly::coro::Task<typename Gen::value_type> consumeOne(Gen& generator) {
  auto item = co_await generator.next();
  auto&& value = *item;
  co_return std::move(value);
}

// Builds a multi-depth self-referential RecursiveStruct tree for
// TestStruct.recursiveMember:
//   recursiveMember[0]            name="r0"  simpleMember{min=11,  max=111}
//     children[0]                 name="c0"  simpleMember{min=22,  max=222}
//     children[1]                 name="c1"  simpleMember{min=33,  max=333}
//       children[0]               name="gc0" simpleMember{min=44,  max=444}
std::vector<RecursiveStruct> makeRecursiveMember() {
  auto makeNode = [](std::string name, int32_t min, int32_t max) {
    RecursiveStruct node;
    node.name() = std::move(name);
    node.simpleMember()->min() = min;
    node.simpleMember()->max() = max;
    return node;
  };
  auto r0 = makeNode("r0", 11, 111);
  auto c0 = makeNode("c0", 22, 222);
  auto c1 = makeNode("c1", 33, 333);
  auto gc0 = makeNode("gc0", 44, 444);
  c1.children()->push_back(std::move(gc0));
  r0.children()->push_back(std::move(c0));
  r0.children()->push_back(std::move(c1));
  return {std::move(r0)};
}

// Drain patches (initial sync + subsequent deltas) from the subscription
// generator, apply each to the target storage, and stop once the leaf at
// minPath reaches the expected value.
template <typename Gen, typename Storage>
folly::coro::Task<void> applyPatchesUntilMin(
    Gen& generator,
    Storage& tgtStorage,
    const std::vector<std::string>& minPath,
    int32_t expected) {
  while (true) {
    auto element = co_await folly::coro::timeout(
        consumeOne(generator), std::chrono::seconds(5));
    auto msg = std::move(element.val);
    if (msg.getType() != SubscriberMessage::Type::chunk) {
      continue;
    }
    auto chunk = msg.get_chunk();
    for (auto& [key, patches] : *chunk.patchGroups()) {
      for (auto& patch : patches) {
        EXPECT_EQ(tgtStorage.patch(std::move(patch)), std::nullopt);
      }
    }
    if (tgtStorage.template get<int32_t>(minPath).value() == expected) {
      co_return;
    }
  }
}

} // namespace

template <bool EnableHybridStorage>
struct TestParams {
  static constexpr auto hybridStorage = EnableHybridStorage;
};

using SubscribableStorageTestTypes =
    ::testing::Types<TestParams<false>, TestParams<true>>;

template <typename TestParams>
class SubscribableStorageTests : public Test {
 public:
  void SetUp() override {
    testStruct = initializeTestStruct();
  }

  auto initStorage(auto& val) {
    auto constexpr isHybridStorage = TestParams::hybridStorage;
    using RootType = std::remove_cvref_t<decltype(val)>;
    return NaivePeriodicSubscribableCowStorage<RootType, isHybridStorage>(val);
  }

  // Metadata tracking on: subscriptions stay pending initial sync until the
  // publisher root is confirmed. Exercises appending paths before initial sync.
  auto initStorageTrackMetadata(auto& val) {
    auto constexpr isHybridStorage = TestParams::hybridStorage;
    using RootType = std::remove_cvref_t<decltype(val)>;
    return NaivePeriodicSubscribableCowStorage<RootType, isHybridStorage>(
        val,
        NaivePeriodicSubscribableStorageBase::StorageParams(
            std::chrono::milliseconds(50),
            std::chrono::seconds(5),
            /*trackMetadata=*/true));
  }

  auto createCowStorage(auto val) {
    auto constexpr isHybridStorage = TestParams::hybridStorage;
    using RootType = std::remove_cvref_t<decltype(val)>;
    return CowStorage<
        RootType,
        facebook::fboss::thrift_cow::ThriftStructNode<
            RootType,
            facebook::fboss::thrift_cow::
                ThriftStructResolver<RootType, isHybridStorage>,
            isHybridStorage>>(val);
  }

  constexpr bool isHybridStorage() {
    return TestParams::hybridStorage;
  }

 protected:
  thriftpath::RootThriftPath<TestStruct> root;
  TestStruct testStruct;
};

TYPED_TEST_SUITE(SubscribableStorageTests, SubscribableStorageTestTypes);

TYPED_TEST(SubscribableStorageTests, GetThrift) {
  auto storage = this->initStorage(this->testStruct);
  EXPECT_EQ(storage.get(this->root.tx()).value(), true);
  EXPECT_EQ(storage.get(this->root.rx()).value(), false);
  EXPECT_EQ(
      storage.get(this->root.member()).value(),
      this->testStruct.member().value());
  EXPECT_EQ(
      storage.get(this->root.structMap()[3]).value(),
      this->testStruct.structMap()->at(3));
  EXPECT_EQ(storage.get(this->root).value(), this->testStruct);
}

TYPED_TEST(SubscribableStorageTests, SubscribeUnsubscribe) {
  auto storage = this->initStorage(this->testStruct);
  auto txPath = this->root.tx();
  storage.start();
  {
    auto generator = storage.subscribe(
        std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
        std::move(txPath));
    WITH_RETRIES(EXPECT_EVENTUALLY_EQ(storage.numSubscriptions(), 1));
  }
  WITH_RETRIES(EXPECT_EVENTUALLY_EQ(storage.numSubscriptions(), 0));
}

TYPED_TEST(SubscribableStorageTests, SubscribeOne) {
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);

  auto txPath = this->root.tx();
  storage.start();
  auto generator = storage.subscribe(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      std::move(txPath));
  auto deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  EXPECT_EQ(deltaVal.newVal, true);
  EXPECT_EQ(deltaVal.oldVal, std::nullopt);
  EXPECT_EQ(storage.set(this->root.tx(), false), std::nullopt);

  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  EXPECT_EQ(deltaVal.oldVal, true);
  EXPECT_EQ(deltaVal.newVal, false);
}

TYPED_TEST(SubscribableStorageTests, SubscribePathAddRemoveParent) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  auto path = this->root.structMap()[99].min();
  auto generator = storage.subscribe(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      std::move(path));
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);
  auto deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  EXPECT_EQ(deltaVal.oldVal, std::nullopt);
  EXPECT_EQ(deltaVal.newVal, 999);

  // now delete the parent and verify we see the deletion delta too
  storage.remove(this->root.structMap()[99]);
  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  EXPECT_EQ(deltaVal.oldVal, 999);
  EXPECT_EQ(deltaVal.newVal, std::nullopt);
}

TYPED_TEST(SubscribableStorageTests, SubscribeDelta) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);

  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root,
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  storage.start();
  // First sync post subscription setup
  auto deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_EQ(deltaVal.val.changes()->size(), 1);
  auto first = deltaVal.val.changes()->at(0);
  // Synced entire tree from root
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({})));

  // Make changes, we should see that come in as delta now
  EXPECT_EQ(storage.set(this->root.tx(), false), std::nullopt);

  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  EXPECT_EQ(deltaVal.val.changes()->size(), 1);
  first = deltaVal.val.changes()->at(0);
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({"tx"})));

  // Should eventually recv heartbeat
  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  EXPECT_EQ(deltaVal.val.changes()->size(), 0);
}

TYPED_TEST(SubscribableStorageTests, SubscribeHybridDelta) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);

  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root,
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  storage.start();

  // First sync post subscription setup
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 1);
  auto first = deltaVal.changes()->at(0);
  // Synced entire tree from root
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({})));

  // Make change under hybrid node and verify delta
  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);

  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  deltaVal = std::move(element.val);

  EXPECT_EQ(deltaVal.changes()->size(), 1);
  EXPECT_TRUE(deltaVal.metadata());
  first = deltaVal.changes()->at(0);

  // verify base path
  EXPECT_EQ(first.path()->raw()->size(), 2);
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({"structMap", "99"})));

  EXPECT_FALSE(first.oldState());

  // verify received newState...
  TestStructSimple deserialized = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::structure, TestStructSimple>(
          OperProtocol::SIMPLE_JSON, *first.newState());
  EXPECT_EQ(deserialized.min(), 999);

  // now delete the parent and verify we see the deletion delta too
  storage.remove(this->root.structMap()[99]);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  deltaVal = std::move(element.val);

  EXPECT_EQ(deltaVal.changes()->size(), 1);
  EXPECT_TRUE(deltaVal.metadata());
  first = deltaVal.changes()->at(0);

  // verify base path
  EXPECT_EQ(first.path()->raw()->size(), 2);
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({"structMap", "99"})));

  EXPECT_FALSE(first.newState());
  deserialized = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::structure, TestStructSimple>(
          OperProtocol::SIMPLE_JSON, *first.oldState());
  EXPECT_EQ(deserialized.min(), 999);

  // Should eventually recv heartbeat
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 0);
}

TYPED_TEST(SubscribableStorageTests, GetSetOptionalHybridStruct) {
  // Build a TestHybridStruct with some data
  TestHybridStruct hybridStruct;
  hybridStruct.optionalIntegral() = 1000;
  hybridStruct.str() = "test-stmt";
  hybridStruct.integralSet() = {1, 2, 3};

  // Initialize testStruct with the optionalAnnotatedStruct set
  this->testStruct.optionalAnnotatedStruct() = hybridStruct;
  auto storage = this->initStorage(this->testStruct);

  // Verify via root get that optionalAnnotatedStruct round-trips through
  // storage
  auto rootVal = storage.get(this->root);
  EXPECT_TRUE(rootVal.hasValue());
  EXPECT_TRUE(rootVal->optionalAnnotatedStruct().has_value());
  EXPECT_TRUE(
      rootVal->optionalAnnotatedStruct()->optionalIntegral().has_value());
  EXPECT_EQ(rootVal->optionalAnnotatedStruct()->integralSet()->size(), 3);
  EXPECT_EQ(
      rootVal->optionalAnnotatedStruct()->optionalIntegral().value(), 1000);
  EXPECT_EQ(rootVal->optionalAnnotatedStruct()->str(), "test-stmt");

  // Verify direct path get also works for optional hybrid fields
  auto directResult = storage.get(this->root.optionalAnnotatedStruct());
  EXPECT_TRUE(directResult.hasValue());
  EXPECT_TRUE(directResult->optionalIntegral().has_value());
  EXPECT_EQ(directResult->integralSet()->size(), 3);
  EXPECT_EQ(directResult->optionalIntegral().value(), 1000);
  EXPECT_EQ(directResult->str(), "test-stmt");

  // Verify direct path get returns error when optionalAnnotatedStruct is NOT
  // set
  TestStruct emptyStruct = initializeTestStruct();
  auto storage2 = this->initStorage(emptyStruct);
  auto unsetResult = storage2.get(this->root.optionalAnnotatedStruct());
  EXPECT_FALSE(unsetResult.hasValue());
  EXPECT_EQ(unsetResult.error().code(), StorageError::Code::INVALID_PATH);
}

TYPED_TEST(SubscribableStorageTests, SetGetOptionalHybridStructRoundTrip) {
  // Build an initial TestHybridStruct
  TestHybridStruct initialStruct;
  initialStruct.optionalIntegral() = 1000;
  initialStruct.str() = "init-stmt";
  initialStruct.integralSet() = {1, 2, 3};

  // Initialize storage with optionalAnnotatedStruct set
  this->testStruct.optionalAnnotatedStruct() = initialStruct;
  auto storage = this->initStorage(this->testStruct);

  // Set a NEW struct via storage.set
  TestHybridStruct updatedStruct;
  updatedStruct.optionalIntegral() = 2000;
  updatedStruct.str() = "updated-stmt";
  updatedStruct.integralSet() = {4, 5, 6, 7};

  auto setResult = storage.set(
      this->root.optionalAnnotatedStruct(), std::move(updatedStruct));
  EXPECT_EQ(setResult, std::nullopt) << "storage.set failed";

  // Must publish after set: NaivePeriodicSubscribableStorage's get() reads
  // from lastPublishedState_ by default (serveGetRequestsWithLastPublishedState
  // defaults to true), not from currentState_.
  storage.refreshPublishedStateForReads();

  // Verify via direct path that updated struct is returned
  auto directResult = storage.get(this->root.optionalAnnotatedStruct());
  ASSERT_TRUE(directResult.hasValue())
      << "storage.get(root.optionalAnnotatedStruct()) failed: "
      << directResult.error().toString();
  EXPECT_TRUE(directResult->optionalIntegral().has_value());
  EXPECT_EQ(directResult->optionalIntegral().value(), 2000);
  EXPECT_EQ(directResult->str(), "updated-stmt");
  EXPECT_EQ(directResult->integralSet()->size(), 4);

  // Verify via root that updated struct is also visible
  auto rootResult = storage.get(this->root);
  ASSERT_TRUE(rootResult.hasValue());
  ASSERT_TRUE(rootResult->optionalAnnotatedStruct().has_value());
  EXPECT_EQ(
      rootResult->optionalAnnotatedStruct()->optionalIntegral().value(), 2000);
  EXPECT_EQ(rootResult->optionalAnnotatedStruct()->str(), "updated-stmt");
}

TYPED_TEST(SubscribableStorageTests, SubscribeDeltaOptionalHybridStruct) {
  FLAGS_serveHeartbeats = true;

  // Initialize with a optionalAnnotatedStruct so the field exists
  TestHybridStruct initialStruct;
  initialStruct.optionalIntegral() = 1000;
  initialStruct.str() = "init-stmt";
  initialStruct.integralSet() = {1, 2, 3};
  this->testStruct.optionalAnnotatedStruct() = initialStruct;

  auto storage = this->initStorage(this->testStruct);

  // Subscribe at the optionalAnnotatedStruct level so the delta path stays at
  // or above the TestHybridStruct boundary in both hybrid and non-hybrid modes.
  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root.optionalAnnotatedStruct(),
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  storage.start();

  // First sync post subscription setup
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 1);

  // Update the TestHybridStruct and verify delta
  TestHybridStruct updatedStruct;
  updatedStruct.optionalIntegral() = 5000;
  updatedStruct.str() = "delta-stmt";
  updatedStruct.integralSet() = {4, 5, 6, 7, 8};

  EXPECT_EQ(
      storage.set(this->root.optionalAnnotatedStruct(), updatedStruct),
      std::nullopt);

  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  deltaVal = std::move(element.val);

  EXPECT_GE(deltaVal.changes()->size(), 1);
  EXPECT_TRUE(deltaVal.metadata());
  auto first = deltaVal.changes()->at(0);

  // checks for HybridStorage enabled only if TestHybridStruct is annotated
  // Create a CowStorage to access the root ThriftStructNode and check
  // if optionalAnnotatedStruct field has the HasSkipThriftCow attribute
  auto cowStorage = this->createCowStorage(this->testStruct);
  bool isTestHybridStructAnnotated =
      cowStorage.root()
          ->template isSkipThriftCowEnabled<
              apache::thrift::ident::optionalAnnotatedStruct>();
  bool isMapFieldAnnotated =
      cowStorage.root()
          ->template isSkipThriftCowEnabled<
              apache::thrift::ident::fieldAnnotatedMap>();
  EXPECT_EQ(isMapFieldAnnotated, this->isHybridStorage());
  EXPECT_EQ(isTestHybridStructAnnotated, this->isHybridStorage());

  if (this->isHybridStorage() && isTestHybridStructAnnotated) {
    // In hybrid mode, TestHybridStruct is a hybrid leaf node.
    // The delta is reported at the optionalAnnotatedStruct boundary with full
    // struct.
    EXPECT_THAT(
        *first.path()->raw(),
        ::testing::ContainerEq(std::vector<std::string>({})));

    EXPECT_TRUE(first.newState());

    auto deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::structure, TestHybridStruct>(
            OperProtocol::SIMPLE_JSON, *first.newState());
    EXPECT_TRUE(deserialized.optionalIntegral().has_value());
    EXPECT_EQ(deserialized.optionalIntegral().value(), 5000);
    EXPECT_EQ(deserialized.str(), "delta-stmt");
    EXPECT_EQ(deserialized.integralSet()->size(), 5);
  } else {
    // In non-hybrid mode, TestHybridStruct is a regular cow struct node.
    // The delta is reported at the sub-field level relative to
    // optionalAnnotatedStruct.
    EXPECT_GE(first.path()->raw()->size(), 1);
  }
}

TYPED_TEST(SubscribableStorageTests, SubscribePatch) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);

  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))), this->root);
  auto generator = std::move(streamReader.generator_);
  storage.start();

  // Initial sync post subscription setup
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto msg = std::move(element.val);
  auto patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  auto patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  auto patch = patches.front();
  auto rootPatch = patch.patch()->val_ref();
  EXPECT_TRUE(rootPatch);
  //   initial sync should just be a whole blob
  auto deserialized = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::structure, TestStruct>(
          *patch.protocol(), std::move(*rootPatch));
  EXPECT_EQ(deserialized, this->testStruct);

  // Make changes, we should see that come in as a patch now
  EXPECT_EQ(storage.set(this->root.tx(), false), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  msg = std::move(element.val);
  patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  patch = patches.front();
  auto newVal =
      patch.patch()
          ->struct_node_ref()
          ->children()
          ->at(
              folly::to_underlying(
                  apache::thrift::op::
                      get_field_id_v<TestStruct, apache::thrift::ident::tx>))
          .val_ref();
  auto deserializedVal = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, bool>(
          OperProtocol::COMPACT, std::move(*newVal));
  EXPECT_FALSE(deserializedVal);
}

TYPED_TEST(SubscribableStorageTests, SubscribePatchUpdate) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path = this->root.stringToStruct()["test"].max();
  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))), path);
  auto generator = std::move(streamReader.generator_);

  // set and check
  EXPECT_EQ(storage.set(path, 1), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  auto msg = std::move(element.val);
  auto patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  auto patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  auto patch = patches.front();
  auto newVal = *patch.patch()->val_ref();
  auto deserializedVal = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, int>(
          *patch.protocol(), std::move(newVal));
  EXPECT_EQ(deserializedVal, 1);

  // update and check
  EXPECT_EQ(storage.set(path, 10), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  msg = std::move(element.val);
  patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  patch = patches.front();
  newVal = *patch.patch()->val_ref();
  deserializedVal = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, int>(
          OperProtocol::COMPACT, std::move(newVal));
  EXPECT_EQ(deserializedVal, 10);
}

TYPED_TEST(SubscribableStorageTests, SubscribePatchRecursiveStruct) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  // Populate a self-referential tree so the subscription path (which ends at a
  // recursive struct parent) exists at subscribe time. Patch apply does not
  // auto-vivify list indices, so the target storage is seeded with the same
  // structure and received patches are applied to it for validation.
  this->testStruct.recursiveMember() = makeRecursiveMember();
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // Subscribe at a path that ends at a recursive struct parent
  // (recursiveMember[0], a RecursiveStruct).
  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root.recursiveMember()[0]);
  auto generator = std::move(streamReader.generator_);

  auto tgtStorage = this->createCowStorage(this->testStruct);
  const std::vector<std::string> minPath = {
      "recursiveMember", "0", "simpleMember", "min"};

  auto applyUntilMin = [&](int32_t expected) {
    folly::coro::blockingWait(
        applyPatchesUntilMin(generator, tgtStorage, minPath, expected));
  };

  // set: change a leaf inside the recursive struct parent
  EXPECT_EQ(
      storage.set(this->root.recursiveMember()[0].simpleMember().min(), 777),
      std::nullopt);
  applyUntilMin(777);
  EXPECT_EQ(tgtStorage.template get<int32_t>(minPath).value(), 777);

  // update: change the same leaf again
  EXPECT_EQ(
      storage.set(this->root.recursiveMember()[0].simpleMember().min(), 888),
      std::nullopt);
  applyUntilMin(888);
  EXPECT_EQ(tgtStorage.template get<int32_t>(minPath).value(), 888);
}

TYPED_TEST(SubscribableStorageTests, SubscribePatchMulti) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path1 = this->root.stringToStruct()["test1"].max();
  const auto& path2 = this->root.stringToStruct()["test2"].max();
  RawOperPath p1, p2;
  // validate both tokens and idTokens work
  p1.path() = path1.tokens();
  p2.path() = path2.idTokens();
  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {
          {1, std::move(p1)},
          {2, std::move(p2)},
      });
  auto generator = std::move(streamReader.generator_);

  // set and check, should only recv one patch on the path that exists
  EXPECT_EQ(storage.set(path1, 123), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  auto msg = std::move(element.val);
  auto patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  EXPECT_EQ(patchGroups.begin()->first, 1);
  auto patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  auto patch = patches.front();
  EXPECT_EQ(patch.basePath()[1], "test1");
  auto rootPatch = patch.patch()->move_val();
  //   initial sync should just be a whole blob
  auto deserialized = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, int32_t>(
          *patch.protocol(), std::move(rootPatch));
  EXPECT_EQ(deserialized, 123);

  std::map<std::string, TestStructSimple> stringToStruct =
      storage.get(this->root.stringToStruct()).value();
  // update both structs now, should recv both patches
  stringToStruct["test1"].max() = 100;
  stringToStruct["test2"].max() = 200;
  EXPECT_EQ(
      storage.set(this->root.stringToStruct(), stringToStruct), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  msg = std::move(element.val);
  patchGroups = *msg.get_chunk().patchGroups();

  EXPECT_EQ(patchGroups.size(), 2);
  for (auto& [key, patchGroup] : patchGroups) {
    EXPECT_EQ(patchGroup.size(), 1); // only one patch per raw path
    auto& currentPatch = patchGroup.front();
    EXPECT_EQ(currentPatch.basePath()[1], fmt::format("test{}", key));
    auto currentRootPatch = currentPatch.patch()->move_val();
    deserialized = facebook::fboss::thrift_cow::
        deserializeBuf<apache::thrift::type_class::integral, int32_t>(
            *currentPatch.protocol(), std::move(currentRootPatch));
    // above we set values such that it's sub key * 100 for easier testing
    EXPECT_EQ(deserialized, key * 100);
  }
}

namespace {
// Test-only storage: drives one serve cycle deterministically and exposes
// manager-level addPatchSubscriptionPaths (to append paths mid-initial-sync).
class DoubleResolveStorage : public SynchronousServeStorage<TestStruct> {
 public:
  using Base = SynchronousServeStorage<TestStruct>;
  using Base::Base;

  std::optional<FsdbErrorCode> addPatchPaths(
      const SubscriptionIdentifier& id,
      std::map<SubscriptionKey, RawOperPath> newPaths,
      const std::optional<std::string>& publisherRoot) {
    return this->subMgr().addPatchSubscriptionPaths(
        id, std::move(newPaths), publisherRoot);
  }

  std::optional<std::string> publisherRootOf(
      const std::vector<std::string>& rawPath) {
    return this->getPublisherRoot(rawPath.begin(), rawPath.end());
  }
};

// Encoded i32 leaf with confirmed metadata; publishing this confirms the
// publisher root (makes it "ready" for initial sync).
OperState makeConfirmedI32(int32_t value) {
  OperState state;
  state.protocol() = OperProtocol::COMPACT;
  state.contents() = facebook::fboss::thrift_cow::serialize<
      apache::thrift::type_class::integral>(OperProtocol::COMPACT, value);
  state.metadata() = OperMetadata();
  state.metadata()->lastConfirmedAt() = 1000;
  return state;
}
} // namespace

// A patch subscription can have paths appended while still awaiting its first
// extended sync (publisher root not yet ready). doInitialSyncExtended then
// resolves the full path set including the appended keys, which must NOT be
// resolved again by resolveAddedPatchPaths. Asserts each key resolves once.
TEST(
    CowSubscriptionManagerDoubleResolveTest,
    AddPathsWhileAwaitingInitialSync) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto testStruct = initializeTestStruct();
  thriftpath::RootThriftPath<TestStruct> root;

  DoubleResolveStorage storage(
      testStruct,
      NaivePeriodicSubscribableStorageBase::StorageParams(
          std::chrono::milliseconds(50),
          std::chrono::seconds(5),
          /*trackMetadata=*/true,
          "fsdb",
          /*convertToIDPaths=*/true));

  const auto& path1 = root.stringToStruct()["test1"].max();
  const auto& path2 = root.stringToStruct()["test2"].max();

  // Publisher connects: root registered but not yet confirmed (not ready).
  auto rootTokens = path1.tokens();
  storage.registerPublisher(
      rootTokens.begin(),
      rootTokens.end(),
      /*skipThriftStreamLivenessCheck=*/true);

  // Subscribe with a single key. Non-zero uid so the subscription can be
  // located by identifier when appending paths below.
  SubscriptionIdentifier id(SubscriberId(kSubscriber), /*uid=*/42);
  RawOperPath p1;
  p1.path() = path1.tokens();
  auto streamReader =
      storage.subscribe_patch(SubscriptionIdentifier(id), {{1, std::move(p1)}});
  auto generator = std::move(streamReader.generator_);

  // Serve cycle #1: subscription becomes live/indexed but stays awaiting
  // initial extended sync because the publisher root is not ready.
  storage.serveOnce();

  // Append a second key while the subscription is still awaiting initial sync.
  // Pass id tokens directly: the manager-level append does not run storage
  // path conversion, and the stored subscription paths are already id paths.
  RawOperPath p2;
  p2.path() = path2.idTokens();
  EXPECT_EQ(
      storage.addPatchPaths(
          id, {{2, std::move(p2)}}, storage.publisherRootOf(path2.tokens())),
      std::nullopt);

  // Publisher initial sync: publish data for both paths with confirmed
  // metadata, which makes the publisher root ready.
  EXPECT_EQ(storage.set_encoded(path1, makeConfirmedI32(111)), std::nullopt);
  EXPECT_EQ(storage.set_encoded(path2, makeConfirmedI32(222)), std::nullopt);

  // Serve cycle #2: both keys resolve and get their initial-sync patch. On the
  // double-resolution bug, key 2 resolves twice.
  storage.serveOnce();

  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto msg = std::move(element.val);
  auto patchGroups = *msg.chunk().as_const()->patchGroups();

  // Each subscribed key resolves exactly once (both are served in one chunk).
  EXPECT_EQ(patchGroups.size(), 2);
  EXPECT_EQ(patchGroups.at(1).size(), 1);
  EXPECT_EQ(patchGroups.at(2).size(), 1);
  // One resolved child subscription per key (would be 3 on double resolution).
  EXPECT_EQ(storage.numSubscriptions(), 2);
}

namespace {
RawOperPath makeRawPath(std::vector<std::string> tokens) {
  RawOperPath path;
  path.path() = std::move(tokens);
  return path;
}

// Publisher root is registered but unconfirmed; subscriptions are created but
// no serve cycle has run, so they are still pending registration.
class CowSubscriptionManagerPendingAddTest : public ::testing::Test {
 protected:
  CowSubscriptionManagerPendingAddTest()
      : storage_(
            initializeTestStruct(),
            NaivePeriodicSubscribableStorageBase::StorageParams(
                std::chrono::milliseconds(50),
                std::chrono::seconds(5),
                /*trackMetadata=*/true,
                "fsdb",
                /*convertToIDPaths=*/true)) {
    const auto& path1 = root_.stringToStruct()["test1"].max();
    const auto& path2 = root_.stringToStruct()["test2"].max();
    tokens1_ = path1.tokens();
    tokens2_ = path2.tokens();
    idTokens2_ = path2.idTokens();
    storage_.registerPublisher(
        tokens1_.begin(),
        tokens1_.end(),
        /*skipThriftStreamLivenessCheck=*/true);
  }

  std::optional<std::string> publisherRoot() {
    return storage_.publisherRootOf(tokens2_);
  }

  std::optional<FsdbErrorCode> addRawKey2(const SubscriptionIdentifier& id) {
    return storage_.addPatchPaths(
        id, {{2, makeRawPath(idTokens2_)}}, publisherRoot());
  }

  void publishBothAndServe() {
    EXPECT_EQ(
        storage_.set_encoded(
            tokens1_.begin(), tokens1_.end(), makeConfirmedI32(111)),
        std::nullopt);
    EXPECT_EQ(
        storage_.set_encoded(
            tokens2_.begin(), tokens2_.end(), makeConfirmedI32(222)),
        std::nullopt);
    storage_.serveOnce();
  }

  SubscriberChunk consumeChunk(
      SubscriptionStreamReader<
          SubscriptionServeQueueElement<SubscriberMessage>>& reader) {
    auto element = folly::coro::blockingWait(
        folly::coro::timeout(
            consumeOne(reader.generator_), std::chrono::seconds(5)));
    return element.val.move_chunk();
  }

  thriftpath::RootThriftPath<TestStruct> root_;
  DoubleResolveStorage storage_;
  SubscriptionIdentifier id_{SubscriberId(kSubscriber), /*uid=*/42};
  std::vector<std::string> tokens1_;
  std::vector<std::string> tokens2_;
  std::vector<std::string> idTokens2_;
};
} // namespace

// Paths added right after subscribe, before the first serve cycle, are
// accepted and delivered in the initial sync.
TEST_F(CowSubscriptionManagerPendingAddTest, AddPathsBeforeRegistration) {
  auto reader = storage_.subscribe_patch(
      SubscriptionIdentifier(id_), {{1, makeRawPath(tokens1_)}});

  ASSERT_EQ(addRawKey2(id_), std::nullopt);

  publishBothAndServe();
  auto patchGroups = *consumeChunk(reader).patchGroups();
  EXPECT_EQ(patchGroups.size(), 2);
  EXPECT_EQ(patchGroups.at(1).size(), 1);
  EXPECT_EQ(patchGroups.at(2).size(), 1);
  EXPECT_EQ(storage_.numSubscriptions(), 2);
}

// Extended variant: the stream revision passed with a pending add is echoed in
// the served patch metadata.
TEST_F(CowSubscriptionManagerPendingAddTest, AddExtPathsBeforeRegistration) {
  constexpr StreamRevision kStreamRevision = 2;
  auto reader = storage_.subscribe_patch_extended(
      SubscriptionIdentifier(id_),
      {{1,
        ext_path_builder::raw("stringToStruct")
            .raw("test1")
            .raw("max")
            .get()}});

  ASSERT_EQ(
      storage_.add_extended_patch_subscription_path(
          SubscriptionIdentifier(id_),
          2,
          ext_path_builder::raw("stringToStruct").raw("test2").raw("max").get(),
          kStreamRevision),
      std::nullopt);

  publishBothAndServe();
  auto patchGroups = *consumeChunk(reader).patchGroups();
  EXPECT_EQ(patchGroups.size(), 2);
  for (const auto& [key, patches] : patchGroups) {
    ASSERT_EQ(patches.size(), 1) << "key " << key;
    EXPECT_EQ(patches.front().metadata()->streamRevision(), kStreamRevision)
        << "key " << key;
  }
  EXPECT_EQ(storage_.numSubscriptions(), 2);
}

// Pending subscriptions get the same validation as live ones.
TEST_F(CowSubscriptionManagerPendingAddTest, PendingAddRejections) {
  auto reader = storage_.subscribe_patch(
      SubscriptionIdentifier(id_), {{1, makeRawPath(tokens1_)}});

  // Key 1 already belongs to the subscription.
  EXPECT_EQ(
      storage_.addPatchPaths(
          id_, {{1, makeRawPath(idTokens2_)}}, publisherRoot()),
      FsdbErrorCode::ID_ALREADY_EXISTS);
  EXPECT_EQ(
      storage_.addPatchPaths(id_, {{2, makeRawPath(idTokens2_)}}, "wrongRoot"),
      FsdbErrorCode::INVALID_REQUEST);
  EXPECT_EQ(
      addRawKey2(
          SubscriptionIdentifier(SubscriberId(kSubscriber), /*uid=*/999)),
      FsdbErrorCode::ID_NOT_FOUND);
}

TYPED_TEST(SubscribableStorageTests, AddPatchSubscriptionPath) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path1 = this->root.stringToStruct()["test1"].max();
  const auto& path2 = this->root.stringToStruct()["test2"].max();
  RawOperPath p1;
  p1.path() = path1.tokens();
  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {{1, std::move(p1)}});
  auto generator = std::move(streamReader.generator_);

  // initial sync for path1
  EXPECT_EQ(storage.set(path1, 123), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto msg = std::move(element.val);
  auto patchGroups = *msg.get_chunk().patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  EXPECT_EQ(patchGroups.begin()->first, 1);

  // add path2 with a new SubscriptionKey on the live subscription
  RawOperPath p2;
  p2.path() = path2.tokens();
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)), 2, std::move(p2)),
      std::nullopt);

  // newly added path should get a full-state initial sync on the same stream
  EXPECT_EQ(storage.set(path2, 200), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  msg = std::move(element.val);
  patchGroups = *msg.get_chunk().patchGroups();
  ASSERT_TRUE(patchGroups.contains(2));
  auto patches = patchGroups.at(2);
  EXPECT_EQ(patches.size(), 1);
  auto patch = patches.front();
  EXPECT_EQ(patch.basePath()[1], "test2");
  // initial sync is delivered as a whole blob
  auto rootPatch = patch.patch()->move_val();
  auto deserialized = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, int32_t>(
          *patch.protocol(), std::move(rootPatch));
  EXPECT_EQ(deserialized, 200);

  // subsequent update on path2 should arrive as an incremental patch
  EXPECT_EQ(storage.set(path2, 201), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  msg = std::move(element.val);
  patchGroups = *msg.get_chunk().patchGroups();
  ASSERT_TRUE(patchGroups.contains(2));
  patch = patchGroups.at(2).front();
  auto newVal = *patch.patch()->val_ref();
  deserialized = facebook::fboss::thrift_cow::
      deserializeBuf<apache::thrift::type_class::integral, int32_t>(
          OperProtocol::COMPACT, std::move(newVal));
  EXPECT_EQ(deserialized, 201);

  // negative: unknown identifier
  RawOperPath pUnknown;
  pUnknown.path() = path1.tokens();
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId("unknownSubscriber")),
          3,
          std::move(pUnknown)),
      FsdbErrorCode::ID_NOT_FOUND);

  // negative: colliding SubscriptionKey (key 1 already in the subscription)
  RawOperPath pDup;
  pDup.path() = path1.tokens();
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          1,
          std::move(pDup)),
      FsdbErrorCode::ID_ALREADY_EXISTS);
}

TYPED_TEST(SubscribableStorageTests, AddPatchSubscriptionPathNonPatch) {
  using namespace facebook::fboss::fsdb;

  if (this->isHybridStorage()) {
    GTEST_SKIP() << "extended subscription under HybridNode is unsupported";
  }

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // A non-patch extended subscription is indexed by identifier but must be
  // rejected by add_patch_subscription_path.
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  auto streamReader = storage.subscribe_delta_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);

  // drive a serve cycle so the subscription is registered in the store
  EXPECT_EQ(
      storage.set(this->root.mapOfStringToI32()["test1"], 1), std::nullopt);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  RawOperPath p;
  p.path() = this->root.stringToStruct()["test2"].max().tokens();
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)), 1, std::move(p)),
      FsdbErrorCode::INVALID_REQUEST);
}

TYPED_TEST(SubscribableStorageTests, AddPatchSubscriptionPathEmptyMap) {
  using namespace facebook::fboss::fsdb;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path1 = this->root.stringToStruct()["test1"].max();
  RawOperPath p1;
  p1.path() = path1.tokens();
  auto streamReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, std::move(p1)}});
  auto generator = std::move(streamReader.generator_);
  EXPECT_EQ(storage.set(path1, 1), std::nullopt);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  // Adding an empty path map is rejected rather than silently succeeding.
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::map<SubscriptionKey, RawOperPath>{}),
      FsdbErrorCode::INVALID_REQUEST);
}

TYPED_TEST(SubscribableStorageTests, AddPatchSubscriptionPathMultipleKeys) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path1 = this->root.stringToStruct()["test1"].max();
  const auto& path2 = this->root.stringToStruct()["test2"].max();
  const auto& path3 = this->root.stringToStruct()["test3"].max();
  RawOperPath p1;
  p1.path() = path1.tokens();
  auto streamReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, std::move(p1)}});
  auto generator = std::move(streamReader.generator_);
  EXPECT_EQ(storage.set(path1, 1), std::nullopt);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  // Two separate adds with distinct keys on the live subscription.
  RawOperPath p2, p3;
  p2.path() = path2.tokens();
  p3.path() = path3.tokens();
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)), 2, std::move(p2)),
      std::nullopt);
  EXPECT_EQ(
      storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)), 3, std::move(p3)),
      std::nullopt);

  // Publish both new paths; each added key should receive exactly one
  // initial-sync patch (no duplicate resolution).
  EXPECT_EQ(storage.set(path2, 200), std::nullopt);
  EXPECT_EQ(storage.set(path3, 300), std::nullopt);

  std::map<SubscriptionKey, int> patchCountByKey;
  WITH_RETRIES({
    auto element = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
    auto msg = std::move(element.val);
    if (msg.getType() == SubscriberMessage::Type::chunk) {
      for (auto& [key, patches] : *msg.get_chunk().patchGroups()) {
        patchCountByKey[key] += patches.size();
      }
    }
    ASSERT_EVENTUALLY_TRUE(
        patchCountByKey.count(2) && patchCountByKey.count(3));
  });
  EXPECT_EQ(patchCountByKey[2], 1);
  EXPECT_EQ(patchCountByKey[3], 1);
}

TYPED_TEST(
    SubscribableStorageTests,
    AddPatchSubscriptionPathBeforeInitialSync) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  // trackMetadata=true keeps the subscription pending initial sync until the
  // publisher root is confirmed. Appending a path in that window must NOT
  // resolve it twice (original initial sync + deferred add-path) -- the
  // double-resolution gap.
  auto storage = this->initStorageTrackMetadata(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& path1 = this->root.stringToStruct()["test1"].max();
  const auto& path2 = this->root.stringToStruct()["test2"].max();
  RawOperPath p1;
  p1.path() = path1.tokens();
  auto streamReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, std::move(p1)}});
  auto generator = std::move(streamReader.generator_);

  // Register the publisher root but leave it unconfirmed so the subscription
  // registers in the store yet stays pending its initial sync.
  auto rootPath = path1.idTokens();
  storage.registerPublisher(rootPath.begin(), rootPath.end(), true);

  // Append path2 while the subscription is registered but not yet initial
  // synced. Poll until registration completes (add stops returning
  // ID_NOT_FOUND).
  std::optional<FsdbErrorCode> addRet = FsdbErrorCode::ID_NOT_FOUND;
  WITH_RETRIES({
    if (addRet == FsdbErrorCode::ID_NOT_FOUND) {
      RawOperPath p2;
      p2.path() = path2.tokens();
      addRet = storage.add_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)), 2, std::move(p2));
    }
    ASSERT_EVENTUALLY_EQ(addRet, std::nullopt);
  });

  EXPECT_EQ(storage.set(path2, 200), std::nullopt);

  // Confirm the publisher root (lastConfirmedAt > 0) by publishing path1 with
  // confirmed metadata; set_encoded confirms even before the root is ready.
  // (get_encoded would throw PUBLISHER_NOT_READY here.)
  OperState operState;
  operState.contents() = serialize<apache::thrift::type_class::integral>(
                             OperProtocol::COMPACT, 123)
                             .toStdString();
  operState.protocol() = OperProtocol::COMPACT;
  operState.metadata() = OperMetadata();
  operState.metadata()->lastConfirmedAt() = 1;
  EXPECT_EQ(storage.set_encoded(path1, operState), std::nullopt);

  // Initial sync for both keys arrives on the same stream. Each key must carry
  // exactly one patch -- a duplicate would indicate double-resolution.
  std::map<SubscriptionKey, int> patchCountByKey;
  WITH_RETRIES({
    auto element = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
    auto msg = std::move(element.val);
    if (msg.getType() == SubscriberMessage::Type::chunk) {
      for (auto& [key, patches] : *msg.get_chunk().patchGroups()) {
        patchCountByKey[key] += patches.size();
      }
    }
    ASSERT_EVENTUALLY_TRUE(
        patchCountByKey.count(1) && patchCountByKey.count(2));
  });
  EXPECT_EQ(patchCountByKey[1], 1);
  EXPECT_EQ(patchCountByKey[2], 1);
}

TYPED_TEST(SubscribableStorageTests, SubscribePatchHeartbeat) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  auto streamReader = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))), this->root);
  auto generator = std::move(streamReader.generator_);

  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  auto msg = std::move(element.val);
  // first message is initial sync
  EXPECT_EQ(msg.getType(), SubscriberMessage::Type::chunk);
  // Should eventually recv heartbeat
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  msg = std::move(element.val);
  EXPECT_EQ(msg.getType(), SubscriberMessage::Type::heartbeat);
}

TYPED_TEST(SubscribableStorageTests, SubscribeHeartbeatConfigured) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  SubscriptionStorageParams params1(std::chrono::seconds(2));
  SubscriptionStorageParams params2(std::chrono::seconds(10));

  auto streamReader1 = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root,
      params1);
  auto generator1 = std::move(streamReader1.generator_);
  auto streamReader2 = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))), this->root);
  auto generator2 = std::move(streamReader2.generator_);

  auto element1 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator1), std::chrono::seconds(20)));
  auto msg1 = std::move(element1.val);
  // first message is initial sync
  EXPECT_EQ(msg1.getType(), SubscriberMessage::Type::chunk);
  // Should recv heartbeat every 2 seconds. Allow leeway incase previous
  // heartbeat was just sent
  element1 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator1), std::chrono::seconds(3)));
  msg1 = std::move(element1.val);
  EXPECT_EQ(msg1.getType(), SubscriberMessage::Type::heartbeat);

  // First sync post subscription setup
  auto element2 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator2), std::chrono::seconds(5)));
  auto msg2 = std::move(element2.val);

  // Heartbeat configured for 10 seconds, so we should not see one within 8
  try {
    element2 = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator2), std::chrono::seconds(3)));
  } catch (const std::exception& ex) {
    EXPECT_EQ(typeid(ex), typeid(folly::FutureTimeout));
  }
}

TYPED_TEST(SubscribableStorageTests, SubscribeHeartbeatNotReceived) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  SubscriptionStorageParams params(std::chrono::seconds(30));

  auto streamReader1 = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      this->root,
      params);
  auto generator1 = std::move(streamReader1.generator_);
  // Keep default subscription hearbeat interval of 5 seconds
  auto streamReader2 = storage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))), this->root);
  auto generator2 = std::move(streamReader2.generator_);

  auto element1 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator1), std::chrono::seconds(20)));
  auto msg1 = std::move(element1.val);
  // first message is initial sync
  EXPECT_EQ(msg1.getType(), SubscriberMessage::Type::chunk);
  // Should not receive any heartbeat
  try {
    element1 = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator1), std::chrono::seconds(20)));
  } catch (const std::exception& ex) {
    EXPECT_EQ(typeid(ex), typeid(folly::FutureTimeout));
  }
  // First sync post subscription setup
  auto element2 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator2), std::chrono::seconds(5)));
  auto msg2 = std::move(element2.val);

  // Should recv heartbeat every 5 seconds(default interval). Allow leeway
  // incase previous heartbeat was just sent
  element2 = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator2), std::chrono::seconds(6)));
  msg2 = std::move(element2.val);
  EXPECT_EQ(msg2.getType(), SubscriberMessage::Type::heartbeat);
}

TYPED_TEST(SubscribableStorageTests, SubscribeDeltaUpdate) {
  auto storage = this->initStorage(this->testStruct);
  storage.start();

  const auto& path = this->root.stringToStruct()["test"].max();
  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      path,
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);

  // set value and subscribe
  EXPECT_EQ(storage.set(path, 1), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  auto deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 1);
  auto first = deltaVal.changes()->at(0);
  EXPECT_THAT(
      *first.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({})));
  EXPECT_FALSE(first.oldState());
  EXPECT_TRUE(first.newState());

  // update value
  EXPECT_EQ(storage.set(path, 10), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 1);
  auto second = deltaVal.changes()->at(0);
  EXPECT_THAT(
      *second.path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({})));
  EXPECT_TRUE(second.oldState());
  EXPECT_TRUE(second.newState());
}

TYPED_TEST(SubscribableStorageTests, SubscribeDeltaAddRemoveParent) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  auto path = this->root.structMap()[99].min();
  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      std::move(path),
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto deltaVal = std::move(element.val);

  EXPECT_EQ(deltaVal.changes()->size(), 1);
  EXPECT_TRUE(deltaVal.metadata());
  auto first = deltaVal.changes()->at(0);

  // in this case, the delta has the relative path, which should be empty
  EXPECT_EQ(first.path()->raw()->size(), 0);

  EXPECT_FALSE(first.oldState());

  EXPECT_EQ(folly::to<int>(*first.newState()), 999);

  // now delete the parent and verify we see the deletion delta too
  storage.remove(this->root.structMap()[99]);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  deltaVal = std::move(element.val);

  EXPECT_EQ(deltaVal.changes()->size(), 1);
  EXPECT_TRUE(deltaVal.metadata());
  first = deltaVal.changes()->at(0);

  // in this case, the delta has the relative path, which should be empty
  EXPECT_EQ(first.path()->raw()->size(), 0);

  EXPECT_FALSE(first.newState());
  EXPECT_EQ(folly::to<int>(*first.oldState()), 999);
}

TYPED_TEST(SubscribableStorageTests, SubscribeEncodedPathSimple) {
  FLAGS_serveHeartbeats = true;
  auto storage = this->initStorage(this->testStruct);

  const auto& path = this->root.stringToStruct()["test"].max();
  auto generator = storage.subscribe_encoded(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      path,
      OperProtocol::SIMPLE_JSON);
  storage.start();

  EXPECT_EQ(
      storage.set(this->root.stringToStruct()["test"].max(), 123),
      std::nullopt);
  auto deltaState = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  ASSERT_TRUE(deltaState.newVal.has_value());

  auto operState = deltaState.newVal;

  auto deserialized = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::integral, int>(
          OperProtocol::SIMPLE_JSON, *operState->contents());

  EXPECT_EQ(deserialized, 123);

  // Should eventually recv heartbeat
  deltaState = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  ASSERT_TRUE(deltaState.newVal.has_value());
  EXPECT_EQ(deltaState.newVal->isHeartbeat(), true);
}

TYPED_TEST(SubscribableStorageTests, SubscribeExtendedPathSimple) {
  // add subscription for a path that doesn't exist yet, then add parent
  FLAGS_serveHeartbeats = true;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);

  auto path = ext_path_builder::raw(
                  apache::thrift::op::get_field_id_v<
                      TestStruct,
                      apache::thrift::ident::mapOfStringToI32>)
                  .regex("test1.*")
                  .get();
  auto generator = storage.subscribe_encoded_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  EXPECT_EQ(
      storage.set(this->root.mapOfStringToI32()["test1"], 998), std::nullopt);
  auto taggedVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  EXPECT_EQ(taggedVal.size(), 1);
  auto oldVal = taggedVal.at(0).oldVal;
  auto newVal = taggedVal.at(0).newVal;
  ASSERT_TRUE(oldVal);
  ASSERT_TRUE(newVal);

  // Old state should not be null, but should have empty contents in
  // the TaggedOperState object
  ASSERT_FALSE(oldVal->state()->contents());

  EXPECT_THAT(
      *newVal->path()->path(),
      ::testing::ElementsAre(
          folly::to<std::string>(kTestStructMapOfStringToI32FieldId), "test1"));

  auto deserialized = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::integral, int>(
          OperProtocol::SIMPLE_JSON, *newVal->state()->contents());

  EXPECT_EQ(deserialized, 998);

  // Should eventually recv heartbeat
  taggedVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(20)));
  EXPECT_EQ(taggedVal.size(), 0);
}

TYPED_TEST(SubscribableStorageTests, SubscribeExtendedPathMultipleChanges) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  auto generator = storage.subscribe_encoded_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  EXPECT_EQ(
      storage.set(this->root, createTestStructForExtendedTests()),
      std::nullopt);
  auto streamedVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  std::map<std::vector<std::string>, int> expected = {
      {{"mapOfStringToI32", "test1"}, 1},
      {{"mapOfStringToI32", "test10"}, 10},
      {{"mapOfStringToI32", "test11"}, 11},
      {{"mapOfStringToI32", "test12"}, 12},
      {{"mapOfStringToI32", "test13"}, 13},
      {{"mapOfStringToI32", "test14"}, 14},
      {{"mapOfStringToI32", "test15"}, 15},
      {{"mapOfStringToI32", "test16"}, 16},
      {{"mapOfStringToI32", "test17"}, 17},
      {{"mapOfStringToI32", "test18"}, 18},
      {{"mapOfStringToI32", "test19"}, 19},
  };

  EXPECT_EQ(streamedVal.size(), expected.size());
  for (const auto& deltaVal : streamedVal) {
    auto oldVal = deltaVal.oldVal;
    auto newVal = deltaVal.newVal;
    ASSERT_TRUE(oldVal);
    ASSERT_TRUE(newVal);

    // Old state should not be null, but should have empty contents in
    // the TaggedOperState object
    ASSERT_FALSE(oldVal->state()->contents());

    const auto& elemPath = *newVal->path()->path();
    const auto& contents = *newVal->state()->contents();
    auto deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::integral, int>(
            OperProtocol::SIMPLE_JSON, contents);
    EXPECT_EQ(expected[elemPath], deserialized)
        << "Mismatch at /" + folly::join('/', elemPath);
  }
}

TYPED_TEST(SubscribableStorageTests, SubscribeExtendedDeltaSimple) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  auto streamReader = storage.subscribe_delta_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  EXPECT_EQ(
      storage.set(this->root.mapOfStringToI32()["test1"], 998), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto streamedVal = std::move(element.val);

  EXPECT_EQ(streamedVal.size(), 1);
  EXPECT_EQ(streamedVal.at(0).delta()->changes()->size(), 1);

  EXPECT_THAT(
      *streamedVal.at(0).path()->path(),
      ::testing::ElementsAre("mapOfStringToI32", "test1"));

  auto deltaUnit = streamedVal.at(0).delta()->changes()->at(0);
  ASSERT_FALSE(deltaUnit.oldState());
  ASSERT_TRUE(deltaUnit.newState());

  // deltas are relative to the subscribed root, so we expect this to be
  // empty;
  EXPECT_TRUE(deltaUnit.path()->raw()->empty());

  auto deserialized = facebook::fboss::thrift_cow::
      deserialize<apache::thrift::type_class::integral, int>(
          OperProtocol::SIMPLE_JSON, *deltaUnit.newState());

  EXPECT_EQ(deserialized, 998);
}

CO_TYPED_TEST(SubscribableStorageTests, SubscribeExtendedDeltaUpdate) {
  auto storage = this->initStorage(this->testStruct);
  storage.start();

  const auto& path =
      ext_path_builder::raw("stringToStruct").regex("test1.*").raw("max").get();
  auto streamReader = storage.subscribe_delta_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
  } else {
    const auto& setPath = this->root.stringToStruct()["test1"].max();
    EXPECT_EQ(storage.set(setPath, 1), std::nullopt);
    auto ret = co_await co_awaitTry(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
    EXPECT_FALSE(ret.hasException());

    // update value
    EXPECT_EQ(storage.set(setPath, 10), std::nullopt);
    ret = co_await co_awaitTry(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
    EXPECT_FALSE(ret.hasException());
  }
}

TYPED_TEST(SubscribableStorageTests, SubscribeExtendedDeltaMultipleChanges) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  auto streamReader = storage.subscribe_delta_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {path},
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
    return;
  }

  storage.start();

  EXPECT_EQ(
      storage.set(this->root, createTestStructForExtendedTests()),
      std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto streamedVal = std::move(element.val);

  std::map<std::vector<std::string>, int> expected = {
      {{"mapOfStringToI32", "test1"}, 1},
      {{"mapOfStringToI32", "test10"}, 10},
      {{"mapOfStringToI32", "test11"}, 11},
      {{"mapOfStringToI32", "test12"}, 12},
      {{"mapOfStringToI32", "test13"}, 13},
      {{"mapOfStringToI32", "test14"}, 14},
      {{"mapOfStringToI32", "test15"}, 15},
      {{"mapOfStringToI32", "test16"}, 16},
      {{"mapOfStringToI32", "test17"}, 17},
      {{"mapOfStringToI32", "test18"}, 18},
      {{"mapOfStringToI32", "test19"}, 19},
  };

  EXPECT_EQ(streamedVal.size(), expected.size());
  for (const auto& taggedDelta : streamedVal) {
    auto rawPath = *taggedDelta.path()->path();
    ASSERT_TRUE(expected.contains(rawPath));

    auto expectedValue = expected[rawPath];
    ASSERT_EQ(taggedDelta.delta()->changes()->size(), 1);
    auto deltaUnit = taggedDelta.delta()->changes()->at(0);
    ASSERT_FALSE(deltaUnit.oldState());
    ASSERT_TRUE(deltaUnit.newState());

    // deltas are relative to the subscribed root, so we expect this to be
    // empty;
    EXPECT_TRUE(deltaUnit.path()->raw()->empty());

    auto deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::integral, int>(
            OperProtocol::SIMPLE_JSON, *deltaUnit.newState());

    EXPECT_EQ(deserialized, expectedValue);
  }
}

TYPED_TEST(SubscribableStorageTests, SetPatchWithPathSpec) {
  // test set/patch on different path spec
  auto storage = this->initStorage(this->testStruct);
  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;

  // verify set API works as expected
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);
  storage.refreshPublishedStateForReads();

  auto struct99 = storage.get(this->root.structMap()[99]);
  EXPECT_EQ(*struct99->min(), 999);
  EXPECT_EQ(*struct99->max(), 1001);

  // test invalid patching logic
  {
    // create oper delta
    OperDelta delta;
    delta.protocol() = OperProtocol::BINARY;

    // bring unit
    OperDeltaUnit unit;
    // intentionally set path as root aka empty path->raw list
    unit.path()->raw() = {};
    unit.newState() = facebook::fboss::thrift_cow::serialize<
        apache::thrift::type_class::structure>(OperProtocol::BINARY, newStruct);

    // add unit to delta
    delta.changes()->push_back(std::move(unit));

    // patch which suppose to fail, instead, it wipes out root struct
    // oper delta has the type(TestStructSimple) of root->structMap->entry
    // the patch is applied on root, which has TestStruct type.
    // Due to the generous nature of thrift deserialization,
    // when it deserializes the TestStructSimple binary, none of the fields
    // match TestStruct, it returns an empty TestStruct instead of error.
    // Therefore the end result is basically patching an empty but valid
    // root struct, and wipes out any existing field with default value.
    EXPECT_EQ(storage.patch(delta), std::nullopt);
    storage.refreshPublishedStateForReads();

    // confirm the new value didn't get in
    // instead it wiped entire content previously set
    auto updatedMap = storage.get(this->root.structMap());
    EXPECT_EQ(updatedMap->size(), 0);
  }

  // test correct patching logic
  {
    // confirm the structMap is empty at beginning
    auto currentMap = storage.get(this->root.structMap());
    EXPECT_EQ(currentMap->size(), 0);

    // update min,max values
    newStruct.min() = 99;
    newStruct.max() = 101;

    // create oper delta
    OperDelta delta;
    delta.protocol() = OperProtocol::BINARY;

    // bring unit
    OperDeltaUnit unit;
    auto correctPath = this->root.structMap()[99].tokens();
    unit.path()->raw() = std::move(correctPath);
    unit.newState() = facebook::fboss::thrift_cow::serialize<
        apache::thrift::type_class::structure>(OperProtocol::BINARY, newStruct);

    // add unit to delta
    delta.changes()->push_back(std::move(unit));

    // patch which suppose to succeed
    // oper delta has the type of root->structMap->struct{min, max}
    // the patch is applied on root->structMap[99], which has the same type
    EXPECT_EQ(storage.patch(delta), std::nullopt);
    storage.refreshPublishedStateForReads();

    // confirm the new value has been stamped to root->structMap[99]
    auto updatedMapEntry = storage.get(this->root.structMap()[99]);
    EXPECT_EQ(*updatedMapEntry->min(), *newStruct.min());
    EXPECT_EQ(*updatedMapEntry->max(), *newStruct.max());
  }
}

// Similar test to SetPatchWithPathSpec except we are testing patching
// TaggedOperState
TYPED_TEST(SubscribableStorageTests, SetPatchWithPathSpecOnTaggedState) {
  // test set/patch on different path spec
  auto storage = this->initStorage(this->testStruct);
  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;

  // verify set API works as expected
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);
  storage.refreshPublishedStateForReads();

  auto struct99 = storage.get(this->root.structMap()[99]);
  EXPECT_EQ(*struct99->min(), 999);
  EXPECT_EQ(*struct99->max(), 1001);

  // test invalid patching logic
  {
    // create tagged oper delta
    TaggedOperState operState;

    operState.path()->path() = {};
    operState.state()->protocol() = OperProtocol::BINARY;
    operState.state()->contents() = facebook::fboss::thrift_cow::serialize<
        apache::thrift::type_class::structure>(OperProtocol::BINARY, newStruct);

    // patch which suppose to fail, instead, it wipes out root struct
    // oper delta has the type(TestStructSimple) of root->structMap->entry
    // the patch is applied on root, which has TestStruct type.
    // Due to the generous nature of thrift deserialization,
    // when it deserializes the TestStructSimple binary, none of the fields
    // match TestStruct, it returns an empty TestStruct instead of error.
    // Therefore the end result is basically patching an empty but valid
    // root struct, and wipes out any existing field with default value.
    EXPECT_EQ(storage.patch(operState), std::nullopt);
    storage.refreshPublishedStateForReads();

    // confirm the new value didn't get in
    // instead it wiped entire content previously set
    auto updatedMap = storage.get(this->root.structMap());
    EXPECT_EQ(updatedMap->size(), 0);
  }

  // test correct patching logic
  {
    // confirm the structMap is empty at beginning
    auto currentMap = storage.get(this->root.structMap());
    EXPECT_EQ(currentMap->size(), 0);

    // update min,max values
    newStruct.min() = 99;
    newStruct.max() = 101;

    // create tagged oper delta
    TaggedOperState operState;
    operState.state()->protocol() = OperProtocol::BINARY;

    auto correctPath = this->root.structMap()[99].tokens();
    operState.path()->path() = std::move(correctPath);
    operState.state()->contents() = facebook::fboss::thrift_cow::serialize<
        apache::thrift::type_class::structure>(OperProtocol::BINARY, newStruct);

    // patch which suppose to succeed
    // tagged oper delta has the type of root->structMap->struct{min, max}
    // the patch is applied on root->structMap[99], which has the same type
    EXPECT_EQ(storage.patch(operState), std::nullopt);
    storage.refreshPublishedStateForReads();

    // confirm the new value has been stamped to root->structMap[99]
    auto updatedMapEntry = storage.get(this->root.structMap()[99]);
    EXPECT_EQ(*updatedMapEntry->min(), *newStruct.min());
    EXPECT_EQ(*updatedMapEntry->max(), *newStruct.max());
  }
}

TYPED_TEST(SubscribableStorageTests, PruneSubscriptionPathStores) {
  // add and remove paths, and verify PathStores are pruned
  auto storage = this->initStorage(this->testStruct);
  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;
  EXPECT_EQ(storage.set(this->root.structMap()[3], newStruct), std::nullopt);
  storage.start();

  // create subscriber
  const auto& path = this->root.stringToStruct()["test"].max();
  auto streamReader = storage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      path,
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);

  // add path and wait for it to be served (publishAndAddPaths)
  EXPECT_EQ(storage.set(path, 1), std::nullopt);
  auto deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  EXPECT_EQ(deltaVal.val.changes()->size(), 1);

  auto initialNumPathStores = storage.numPathStoresRecursive_Expensive();
  XLOG(DBG2) << "initialNumPathStores: " << initialNumPathStores;

  // add another path and check PathStores count
  EXPECT_EQ(storage.set(this->root.structMap()[99], newStruct), std::nullopt);
  EXPECT_EQ(storage.set(path, 2), std::nullopt);
  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  EXPECT_EQ(deltaVal.val.changes()->size(), 1);

  auto maxNumPathStores = storage.numPathStoresRecursive_Expensive();
  // with lazy PathStore creation, numPathStores should not change
  // for exact subscriptions when adding/deleting paths.
  EXPECT_EQ(maxNumPathStores, initialNumPathStores);

  // now delete the added path
  storage.remove(this->root.structMap()[99]);
  EXPECT_EQ(storage.set(path, 3), std::nullopt);
  deltaVal = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
  EXPECT_EQ(deltaVal.val.changes()->size(), 1);

  // after path deletion, numPathStores should drop
  auto finalNumPathStores = storage.numPathStoresRecursive_Expensive();
  XLOG(DBG2) << "finalNumPathStores : " << finalNumPathStores;
  EXPECT_EQ(finalNumPathStores, maxNumPathStores);
  EXPECT_EQ(finalNumPathStores, initialNumPathStores);

  // stronger check: allocation stats and numPathStores must match
  auto numAllocatedPathStore = storage.numPathStores();
  EXPECT_EQ(numAllocatedPathStore, finalNumPathStores);
}

TYPED_TEST(SubscribableStorageTests, ApplyPatch) {
  using namespace facebook::fboss::thrift_cow;
  auto storage = this->initStorage(this->testStruct);

  TestStructSimple curStruct = *this->testStruct.member();
  TestStructSimple newStruct;
  newStruct.min() = 999;
  newStruct.max() = 1001;

  // make some nodes so we can use PatchBuilder
  auto nodeA = std::make_shared<ThriftStructNode<TestStructSimple>>(curStruct);
  auto nodeB = std::make_shared<ThriftStructNode<TestStructSimple>>(
      std::move(newStruct));

  std::vector<std::string> path = {folly::to<std::string>(folly::to_underlying(
      apache::thrift::op::
          get_field_id_v<TestStruct, apache::thrift::ident::member>))};
  auto patch = PatchBuilder::build(nodeA, nodeB, std::move(path));

  auto memberStruct = storage.get(this->root.member());
  EXPECT_EQ(*memberStruct->min(), 10);
  EXPECT_EQ(*memberStruct->max(), 20);

  auto ret = storage.patch(std::move(patch));
  EXPECT_EQ(ret.has_value(), false);
  storage.refreshPublishedStateForReads();

  memberStruct = storage.get(this->root.member());
  EXPECT_EQ(*memberStruct->min(), 999);
  EXPECT_EQ(*memberStruct->max(), 1001);
}

TYPED_TEST(SubscribableStorageTests, PatchInvalidDeltaPath) {
  auto storage = this->initStorage(this->testStruct);
  TestStructSimple newStruct;

  OperDelta delta;
  OperDeltaUnit unit;
  unit.path()->raw() = {"invalid", "path"};
  unit.newState() = facebook::fboss::thrift_cow::serialize<
      apache::thrift::type_class::structure>(OperProtocol::BINARY, newStruct);

  // should fail gracefully
  delta.changes() = {unit};
  EXPECT_EQ(
      storage.patch(delta).value().code(), StorageError::Code::INVALID_PATH);

  // partially valid path should still fail
  unit.path()->raw() = {"inlineStruct", "invalid", "path"};
  delta.changes() = {unit};
}

CO_TYPED_TEST(SubscribableStorageTests, SubscribeExtendedPatchSimple) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  int key = 0;
  auto generator = storage.subscribe_patch_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {{key, path}});
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
  } else {
    storage.start();

    EXPECT_EQ(
        storage.set(this->root.mapOfStringToI32()["test1"], 998), std::nullopt);
    auto element = co_await folly::coro::timeout(
        consumeOne(generator), std::chrono::seconds(5));
    auto msg = std::move(element.val);
    auto chunk = msg.get_chunk();

    EXPECT_EQ(
        chunk.patchGroups()->size(), 1); // single path -> single patchGroup
    EXPECT_EQ(chunk.patchGroups()->at(key).size(), 1);
    EXPECT_THAT(
        *chunk.patchGroups()->at(key).front().basePath(),
        ::testing::ElementsAre(
            "13", "test1")); // 13 is the id of mapOfStringToI32

    auto testStorage = this->createCowStorage(this->testStruct);
    EXPECT_EQ(
        testStorage.patch(std::move(chunk.patchGroups()->at(key).front())),
        std::nullopt);
    EXPECT_EQ(testStorage.root()->toThrift().mapOfStringToI32()["test1"], 998);
  }
}

CO_TYPED_TEST(SubscribableStorageTests, SubscribeExtendedPatchUpdate) {
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  auto tgtStorage = this->createCowStorage(this->testStruct);

  const auto& path =
      ext_path_builder::raw("stringToStruct").regex("test1.*").raw("max").get();
  auto generator = storage.subscribe_patch_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {{0, path}});
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
  } else {
    const auto& setPath = this->root.stringToStruct()["test1"].max();
    EXPECT_EQ(storage.set(setPath, 1), std::nullopt);
    auto ret = co_await co_awaitTry(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
    EXPECT_FALSE(ret.hasException());

    for (auto& patch : ret.value().val.chunk_ref()->patchGroups()->at(0)) {
      EXPECT_EQ(tgtStorage.patch(std::move(patch)), std::nullopt);
    }

    // update value
    EXPECT_EQ(storage.set(setPath, 10), std::nullopt);
    ret = co_await co_awaitTry(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
    EXPECT_FALSE(ret.hasException());

    for (auto& patch : ret.value().val.chunk_ref()->patchGroups()->at(0)) {
      EXPECT_EQ(tgtStorage.patch(std::move(patch)), std::nullopt);
    }

    // remove value
    storage.remove(this->root.stringToStruct()["test1"]);
    ret = co_await co_awaitTry(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(1)));
    EXPECT_FALSE(ret.hasException());
    SubscriberChunk subChunk = *ret.value().val.chunk_ref();
    EXPECT_EQ(subChunk.patchGroups()->size(), 1);
    EXPECT_EQ(subChunk.patchGroups()->at(0).size(), 1);
    auto patch = subChunk.patchGroups()->at(0).front();
    EXPECT_EQ(patch.basePath()->size(), 3);
    EXPECT_EQ(
        patch.patch()->getType(),
        facebook::fboss::thrift_cow::PatchNode::Type::del);
    for (auto& subPatch : ret.value().val.chunk_ref()->patchGroups()->at(0)) {
      EXPECT_EQ(tgtStorage.patch(std::move(subPatch)), std::nullopt);
    }
  }
}

CO_TYPED_TEST(
    SubscribableStorageTests,
    SubscribeExtendedPatchDeleteAnnotatedStruct) {
  // Source (FSDB server role): always non-hybrid so it reliably emits the
  // withdrawal patch, exactly like the real publisher does.
  auto storage =
      NaivePeriodicSubscribableCowStorage<TestStruct, false>(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // Target (subscriber role): hybrid in the hybrid type param.
  auto tgtStorage = this->createCowStorage(this->testStruct);

  const auto& path = ext_path_builder::raw("mapOfStructs")
                         .regex("key1.*")
                         .raw("optionalStruct")
                         .get();
  auto generator = storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{0, path}});

  // Populate mapOfStructs["key1"].optionalStruct and deliver it to the
  // subscriber.
  TestStructSimple optionalStruct;
  optionalStruct.min() = 1;
  optionalStruct.max() = 2;
  OtherStruct entry;
  entry.optionalStruct() = optionalStruct;
  EXPECT_EQ(
      storage.set(this->root.mapOfStructs()["key1"], entry), std::nullopt);

  auto ret = co_await co_awaitTry(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_FALSE(ret.hasException());
  for (auto& patch : ret.value().val.chunk_ref()->patchGroups()->at(0)) {
    EXPECT_EQ(tgtStorage.patch(std::move(patch)), std::nullopt);
  }
  // Subscriber now holds the populated optionalStruct (add path works for
  // hybrid).
  EXPECT_TRUE(tgtStorage.root()
                  ->toThrift()
                  .mapOfStructs()
                  ->at("key1")
                  .optionalStruct());

  // Withdraw the entry -> del patch whose base path ends at optionalStruct.
  storage.remove(this->root.mapOfStructs()["key1"]);
  ret = co_await co_awaitTry(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_FALSE(ret.hasException());
  SubscriberChunk subChunk = *ret.value().val.chunk_ref();
  EXPECT_EQ(subChunk.patchGroups()->size(), 1);
  EXPECT_EQ(subChunk.patchGroups()->at(0).size(), 1);
  EXPECT_EQ(
      subChunk.patchGroups()->at(0).front().patch()->getType(),
      facebook::fboss::thrift_cow::PatchNode::Type::del);

  for (auto& subPatch : subChunk.patchGroups()->at(0)) {
    EXPECT_EQ(tgtStorage.patch(std::move(subPatch)), std::nullopt);
  }

  // The withdrawn entry (and its optionalStruct) must be gone from the
  // subscriber.
  EXPECT_EQ(tgtStorage.root()->toThrift().mapOfStructs()->count("key1"), 0);
}

CO_TYPED_TEST(SubscribableStorageTests, SubscribeExtendedPatchMultipleChanges) {
  // add subscription for a path that doesn't exist yet, then add parent
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  auto path = ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  int key = 0;
  auto generator = storage.subscribe_patch_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {{key, path}});
  if (this->isHybridStorage()) {
    // extended subscription under HybridNode is unsupported
  } else {
    storage.start();

    EXPECT_EQ(
        storage.set(this->root, createTestStructForExtendedTests()),
        std::nullopt);
    auto element = co_await folly::coro::timeout(
        consumeOne(generator), std::chrono::seconds(5));
    auto msg = std::move(element.val);
    auto chunk = msg.get_chunk();

    EXPECT_EQ(
        chunk.patchGroups()->size(), 1); // single path -> single patchGroup
    EXPECT_EQ(
        chunk.patchGroups()->at(key).size(),
        11); // 11 changes from createTestStructForExtendedTests

    std::map<std::string, int> expected = {
        {"test1", 1},
        {"test10", 10},
        {"test11", 11},
        {"test12", 12},
        {"test13", 13},
        {"test14", 14},
        {"test15", 15},
        {"test16", 16},
        {"test17", 17},
        {"test18", 18},
        {"test19", 19},
    };

    auto testStorage = this->createCowStorage(this->testStruct);

    for (auto& patch : chunk.patchGroups()->at(key)) {
      // this will be testXY
      auto lastPathElem = patch.basePath()->at(patch.basePath()->size() - 1);
      EXPECT_EQ(testStorage.patch(std::move(patch)), std::nullopt);
      EXPECT_EQ(
          testStorage.root()->toThrift().mapOfStringToI32()->at(lastPathElem),
          expected.at(lastPathElem));
    }
  }
}

CO_TYPED_TEST(SubscribableStorageTests, SubscribePatchExtendedRecursiveStruct) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  // Pre-populate the recursive tree so the (list-based) recursive paths exist
  // in both source and target; patch apply does not auto-vivify list indices.
  this->testStruct.recursiveMember() = makeRecursiveMember();
  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // Extended subscription whose regex/wildcard match traverses the recursive
  // structure: recursiveMember/*/children/1/children/*. The first wildcard is
  // on the recursiveMember list index and the trailing wildcard is on the
  // (recursive) children list of children[1], so the match ends at a grandchild
  // recursive struct (recursiveMember[0].children[1].children[0]). NOTE: the
  // extended path visitor only supports wildcards over container (list/map/set)
  // elements, not struct fields, hence the trailing "/*" is over the children
  // list rather than the struct's fields.
  auto path = ext_path_builder::raw("recursiveMember")
                  .regex(".*")
                  .raw("children")
                  .raw("1")
                  .raw("children")
                  .regex(".*")
                  .get();
  auto generator = storage.subscribe_patch_extended(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      {{0, path}});

  auto tgtStorage = this->createCowStorage(this->testStruct);
  // A leaf under the matched grandchild recursive struct.
  const std::vector<std::string> minPath = {
      "recursiveMember",
      "0",
      "children",
      "1",
      "children",
      "0",
      "simpleMember",
      "min"};

  // set: change the matched recursive leaf
  EXPECT_EQ(storage.set(minPath, 777), std::nullopt);
  co_await applyPatchesUntilMin(generator, tgtStorage, minPath, 777);
  EXPECT_EQ(tgtStorage.template get<int32_t>(minPath).value(), 777);

  // update: change the same recursive leaf again
  EXPECT_EQ(storage.set(minPath, 888), std::nullopt);
  co_await applyPatchesUntilMin(generator, tgtStorage, minPath, 888);
  EXPECT_EQ(tgtStorage.template get<int32_t>(minPath).value(), 888);
}

class SubscribableStorageTestsPathDelta
    : public Test,
      public WithParamInterface<std::tuple<bool>> {
 public:
  void SetUp() override {
    const std::tuple<bool> params = GetParam();
    isPath = get<0>(params);
    testStruct = initializeTestStruct();
  }

 protected:
  thriftpath::RootThriftPath<TestStruct> root;
  TestStruct testStruct;
  bool isPath{false};
};

INSTANTIATE_TEST_SUITE_P(
    SubscribableStorageSubTypeTests,
    SubscribableStorageTestsPathDelta,
    Combine(Bool()));

using TestSubscribableStorage = NaivePeriodicSubscribableCowStorage<TestStruct>;

CO_TEST_P(SubscribableStorageTestsPathDelta, UnregisterSubscriber) {
  auto storage = TestSubscribableStorage(testStruct);
  storage.start();

  const auto path =
      ext_path_builder::raw("mapOfStringToI32").regex("test1.*").get();
  EXPECT_EQ(storage.set(root.mapOfStringToI32()["test1"], 1), std::nullopt);

  auto subId = SubscriptionIdentifier(SubscriberId(kSubscriber));
  auto subscribeOneAndUnregister = [&](bool isPath) -> folly::coro::Task<void> {
    if (isPath) {
      auto generator = storage.subscribe_encoded_extended(
          std::move(subId), {path}, OperProtocol::SIMPLE_JSON);
      auto ret = co_await co_awaitTry(
          folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
      EXPECT_FALSE(ret.hasException());
    } else {
      auto streamReader = storage.subscribe_delta_extended(
          std::move(subId), {path}, OperProtocol::SIMPLE_JSON);
      auto generator = std::move(streamReader.generator_);
      auto ret = co_await co_awaitTry(
          folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
      EXPECT_FALSE(ret.hasException());
    }
  };

  // register sub1, get a update and unregister it
  co_await subscribeOneAndUnregister(isPath);

  // update cow-thrift value
  EXPECT_EQ(storage.set(root.mapOfStringToI32()["test1"], 2), std::nullopt);

  // register a new sub2 and wait for next serveSubscription cycle.
  // previous stale fully-resolved-subscription states from sub1 should be
  // cleaned up
  co_await subscribeOneAndUnregister(isPath);
}

CO_TEST_P(SubscribableStorageTestsPathDelta, UnregisterSubscriberMulti) {
  auto storage = TestSubscribableStorage(testStruct);
  storage.start();

  const auto path =
      ext_path_builder::raw("mapOfStringToI32").regex("test.*").get();
  EXPECT_EQ(storage.set(root.mapOfStringToI32()["test1"], 1), std::nullopt);

  auto subId = SubscriptionIdentifier(SubscriberId(kSubscriber));
  auto subscribeOneAndUnregister = [&](bool isPath) -> folly::coro::Task<void> {
    if (isPath) {
      auto generator = storage.subscribe_encoded_extended(
          std::move(subId), {path}, OperProtocol::SIMPLE_JSON);
      co_await folly::coro::sleep(
          std::chrono::milliseconds(folly::Random::rand32(0, 5000)));
    } else {
      auto streamReader = storage.subscribe_delta_extended(
          std::move(subId), {path}, OperProtocol::SIMPLE_JSON);
      auto generator = std::move(streamReader.generator_);
      co_await folly::coro::sleep(
          std::chrono::milliseconds(folly::Random::rand32(0, 5000)));
    }
  };

  auto subscribeManyAndUnregister = [&](bool isPath,
                                        int count) -> folly::coro::Task<void> {
    std::vector<folly::coro::Task<void>> tasks;
    tasks.reserve(count);
    for (int i = 0; i < count; ++i) {
      tasks.emplace_back(subscribeOneAndUnregister(isPath));
    }
    co_await folly::coro::collectAllRange(std::move(tasks));
  };

  folly::coro::AsyncScope backgroundScope;
  backgroundScope.add(co_withExecutor(
      folly::getGlobalCPUExecutor(), subscribeManyAndUnregister(isPath, 50)));

  for (int j = 0; j < 5; ++j) {
    for (int i = 0; i < 10; ++i) {
      // update cow-thrift value
      EXPECT_EQ(
          storage.set(root.mapOfStringToI32()[fmt::format("test{}", i)], j),
          std::nullopt);
    }
    co_await folly::coro::sleep(std::chrono::seconds(1));
  }

  co_await backgroundScope.joinAsync();
  co_return;
}

TYPED_TEST(SubscribableStorageTests, AddExtendedPatchSubscriptionPath) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // Subscribe with an extended (wildcard) path: stringToStruct/test1.*/max
  const auto& extPath =
      ext_path_builder::raw("stringToStruct").regex("test1.*").raw("max").get();
  auto streamReader = storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, extPath}});
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    GTEST_SKIP() << "extended subscription under HybridNode is unsupported";
  }

  // initial sync for the wildcard-expanded "test1" key
  const auto& setPath = this->root.stringToStruct()["test1"].max();
  EXPECT_EQ(storage.set(setPath, 100), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  auto msg = std::move(element.val);
  EXPECT_GE(msg.get_chunk().patchGroups()->size(), 1);

  // Add a second extended path to the live subscription
  const auto& extPath2 =
      ext_path_builder::raw("mapOfStringToI32").regex("test.*").get();
  std::map<SubscriptionKey, ExtendedOperPath> addPaths;
  addPaths[2] = extPath2;
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::move(addPaths)),
      std::nullopt);

  // The added wildcard path should get initial sync
  EXPECT_EQ(
      storage.set(this->root.mapOfStringToI32()["test1"], 999), std::nullopt);
  element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  msg = std::move(element.val);
  auto& patchGroups = *msg.get_chunk().patchGroups();
  ASSERT_TRUE(patchGroups.contains(2));

  // negative: unknown identifier
  std::map<SubscriptionKey, ExtendedOperPath> unknownPaths;
  unknownPaths[3] = extPath2;
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId("unknownSubscriber")),
          std::move(unknownPaths)),
      FsdbErrorCode::ID_NOT_FOUND);

  // negative: colliding SubscriptionKey (key 1 already exists)
  std::map<SubscriptionKey, ExtendedOperPath> dupPaths;
  dupPaths[1] = extPath;
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::move(dupPaths)),
      FsdbErrorCode::ID_ALREADY_EXISTS);
}

TYPED_TEST(SubscribableStorageTests, AddExtendedPatchSubscriptionPathEmptyMap) {
  using namespace facebook::fboss::fsdb;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  const auto& extPath =
      ext_path_builder::raw("stringToStruct").regex("test1.*").raw("max").get();
  auto streamReader = storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, extPath}});
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    GTEST_SKIP() << "extended subscription under HybridNode is unsupported";
  }
  const auto& setPath = this->root.stringToStruct()["test1"].max();
  EXPECT_EQ(storage.set(setPath, 1), std::nullopt);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  // Empty map is rejected.
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::map<SubscriptionKey, ExtendedOperPath>{}),
      FsdbErrorCode::INVALID_REQUEST);
}

// A rejected extended add (colliding key) must leave no partial state: a retry
// with a fresh key must succeed and deliver, proving the rejection didn't
// poison the subscription's path set or leave a dangling deferred-resolution
// entry.
TYPED_TEST(
    SubscribableStorageTests,
    AddExtendedPatchSubscriptionPathRetryAfterError) {
  using namespace facebook::fboss::fsdb;
  using namespace facebook::fboss::thrift_cow;

  auto storage = this->initStorage(this->testStruct);
  storage.setConvertToIDPaths(true);
  storage.start();

  // Subscribe with an extended (wildcard) path: stringToStruct/test1.*/max
  const auto& extPath =
      ext_path_builder::raw("stringToStruct").regex("test1.*").raw("max").get();
  auto streamReader = storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId(kSubscriber)), {{1, extPath}});
  auto generator = std::move(streamReader.generator_);
  if (this->isHybridStorage()) {
    GTEST_SKIP() << "extended subscription under HybridNode is unsupported";
  }

  // initial sync for the wildcard-expanded "test1" key
  const auto& setPath = this->root.stringToStruct()["test1"].max();
  EXPECT_EQ(storage.set(setPath, 100), std::nullopt);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  const auto& extPath2 =
      ext_path_builder::raw("mapOfStringToI32").regex("test.*").get();

  // Rejected add: SubscriptionKey 1 already exists -> ID_ALREADY_EXISTS.
  std::map<SubscriptionKey, ExtendedOperPath> collidingPaths;
  collidingPaths[1] = extPath2;
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::move(collidingPaths)),
      FsdbErrorCode::ID_ALREADY_EXISTS);

  // Retry with a fresh key: must succeed despite the earlier rejection.
  std::map<SubscriptionKey, ExtendedOperPath> retryPaths;
  retryPaths[2] = extPath2;
  EXPECT_EQ(
      storage.add_extended_patch_subscription_path(
          SubscriptionIdentifier(SubscriberId(kSubscriber)),
          std::move(retryPaths)),
      std::nullopt);

  // The retried path must be fully functional: publishing under it produces a
  // served chunk for key 2, proving the rejected add left clean state.
  EXPECT_EQ(
      storage.set(this->root.mapOfStringToI32()["test1"], 999), std::nullopt);
  WITH_RETRIES({
    auto element = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
    auto msg = std::move(element.val);
    bool hasKey2 = msg.getType() == SubscriberMessage::Type::chunk &&
        msg.get_chunk().patchGroups()->count(2);
    ASSERT_EVENTUALLY_TRUE(hasKey2);
  });
}

namespace {

// metricPrefix_ is held by const-reference; anchor a long-lived owner.
const std::string& fsdbPrefix() {
  static const std::string p = "fsdb";
  return p;
}

using StorageParams = NaivePeriodicSubscribableStorageBase::StorageParams;

// tick=20ms, default=100ms => 5 buckets (index 0 = 20ms ... index 4 = 100ms).
StorageParams bucketParams() {
  StorageParams p(
      std::chrono::milliseconds(100),
      std::chrono::seconds(5),
      false,
      fsdbPrefix());
  p.setServeTickInterval(std::chrono::milliseconds(20));
  return p;
}

// tick == default interval => a single bucket, i.e. legacy behavior.
StorageParams singleBucketParams() {
  return StorageParams(
      std::chrono::milliseconds(50),
      std::chrono::seconds(5),
      false,
      fsdbPrefix());
}

// Bucket baselines are protected implementation detail; a test-only subclass
// keeps these off the production class.
class BucketTestStorage : public TestSubscribableStorage {
 public:
  using TestSubscribableStorage::TestSubscribableStorage;

  bool bucketBaselineHeld(size_t bucket) const {
    return this->bucketPublishedState_[bucket].rlock()->has_value();
  }

  // One serve cycle for a single bucket, so a test controls exactly which
  // buckets are served instead of waiting on the periodic loop.
  void serveBucketOnce(size_t bucket) {
    auto [oldRoot, newRoot, metadataServer] = this->publishCurrentState(bucket);
    this->subscriptions_.serveSubscriptions(
        oldRoot, newRoot, metadataServer, bucket);
  }
};

} // namespace

TEST(SubscribableStorageServeInterval, RejectsTooManyBuckets) {
  StorageParams p(
      std::chrono::milliseconds(10000),
      std::chrono::seconds(5),
      false,
      fsdbPrefix());
  EXPECT_DEATH(
      p.setServeTickInterval(std::chrono::milliseconds(100)), "too many");
}

const SubscriptionIdentifier& intervalTestSub() {
  static const SubscriptionIdentifier kSub{SubscriberId("interval_test")};
  return kSub;
}

TEST(SubscribableStorageServeInterval, RoundsUpAndClampsToBuckets) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  EXPECT_EQ(storage.numServeBuckets(), 5u);

  // Rounded up to a whole tick.
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(std::nullopt, static_cast<uint32_t>(25)),
          intervalTestSub()),
      40u);
  // Exact multiple is preserved.
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(std::nullopt, static_cast<uint32_t>(40)),
          intervalTestSub()),
      40u);
  // Below the tick clamps up to the floor.
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(std::nullopt, static_cast<uint32_t>(1)),
          intervalTestSub()),
      20u);
  // Above the default clamps down to it.
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(
              std::nullopt, static_cast<uint32_t>(100000)),
          intervalTestSub()),
      100u);
}

TEST(SubscribableStorageServeInterval, AbsentIntervalMeansDefault) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  EXPECT_EQ(
      storage.resolveServeIntervalMs(std::nullopt, intervalTestSub()),
      std::nullopt);
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(std::nullopt, std::nullopt),
          intervalTestSub()),
      std::nullopt);
}

TEST(SubscribableStorageServeInterval, TickDisabledGrantsDefault) {
  auto storage =
      BucketTestStorage(initializeTestStruct(), singleBucketParams());
  EXPECT_EQ(storage.numServeBuckets(), 1u);
  EXPECT_EQ(
      storage.resolveServeIntervalMs(
          SubscriptionStorageParams(std::nullopt, static_cast<uint32_t>(20)),
          intervalTestSub()),
      50u);
}

TEST(SubscribableStorageServeInterval, ClampReasonNamesTheRuleThatFired) {
  EXPECT_EQ(
      describeServeIntervalClamp(20, 100, 100), "serve tick not configured");
  EXPECT_EQ(
      describeServeIntervalClamp(100000, 20, 100),
      "clamped to the default interval");
  EXPECT_EQ(
      describeServeIntervalClamp(1, 20, 100), "raised to the serve tick floor");
  EXPECT_EQ(
      describeServeIntervalClamp(25, 20, 100), "rounded up to a whole tick");
  // Single-bucket is the shipped configuration, and a request above the
  // default is clamped there for a reason the tick has nothing to do with.
  EXPECT_EQ(
      describeServeIntervalClamp(20000, 10000, 10000),
      "clamped to the default interval");
  EXPECT_EQ(
      describeServeIntervalClamp(2000, 10000, 10000),
      "serve tick not configured");
}

// The serve loop bills each bucket against tick * (bucket + 1); if that drifts
// from serveBucketIndex, slow buckets get reported as slow serves.
TEST(SubscribableStorageServeInterval, BucketCadenceRoundTrips) {
  constexpr uint32_t kTickMs = 20;
  for (size_t bucket = 0; bucket < kMaxServeBuckets; ++bucket) {
    const auto intervalMs = kTickMs * static_cast<uint32_t>(bucket + 1);
    EXPECT_EQ(serveBucketIndex(intervalMs, kTickMs), bucket);
  }
}

// An interval is only honored for stats path subscriptions, but requesting one
// elsewhere must never fail the subscribe -- it is ignored.
TEST(SubscribableStorageServeInterval, IgnoredRatherThanRejected) {
  auto storage =
      BucketTestStorage(initializeTestStruct(), singleBucketParams());
  storage.setConvertToIDPaths(true);
  thriftpath::RootThriftPath<TestStruct> root;
  SubscriptionStorageParams fast(std::nullopt, static_cast<uint32_t>(20));

  EXPECT_NO_THROW({
    (void)storage.subscribe_patch(
        SubscriptionIdentifier(SubscriberId("patch")), root, fast);
  });
  EXPECT_NO_THROW({
    (void)storage.subscribe_delta(
        SubscriptionIdentifier(SubscriberId("delta")),
        root,
        OperProtocol::COMPACT,
        fast);
  });
  EXPECT_NO_THROW({
    auto subId = SubscriptionIdentifier(SubscriberId("encoded"));
    (void)storage.subscribe_encoded(
        std::move(subId), root, OperProtocol::COMPACT, fast);
  });
}

TEST(SubscribableStorageServeInterval, FastBucketServesUpdates) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(20));
  auto streamReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
  auto generator = std::move(streamReader.generator_);

  auto initial = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_EQ(initial.val.getType(), SubscriberMessage::Type::chunk);

  EXPECT_EQ(storage.set(root.member().min(), 1), std::nullopt);
  auto first = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_EQ(first.val.getType(), SubscriberMessage::Type::chunk);
}

TEST(SubscribableStorageServeInterval, DefaultSubscriberStillServed) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  auto streamReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("default")), root);
  auto generator = std::move(streamReader.generator_);

  auto initial = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_EQ(initial.val.getType(), SubscriberMessage::Type::chunk);

  EXPECT_EQ(storage.set(root.tx(), false), std::nullopt);
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
  EXPECT_EQ(element.val.getType(), SubscriberMessage::Type::chunk);
}

// The shipped stats configuration: one subscriber at the tick cadence and one
// at the default, coexisting on the same storage.
TEST(SubscribableStorageServeInterval, FastAndDefaultSubscribersCoexist) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(20));
  auto fastReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
  auto slowReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("slow")), root);
  auto fastGen = std::move(fastReader.generator_);
  auto slowGen = std::move(slowReader.generator_);

  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(fastGen), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(slowGen), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);

  // They landed in different buckets, and only those two are occupied.
  const size_t defaultBucket = storage.numServeBuckets() - 1;
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(storage.bucketBaselineHeld(0));
    EXPECT_EVENTUALLY_TRUE(storage.bucketBaselineHeld(defaultBucket));
  });
  for (size_t bucket = 1; bucket < defaultBucket; ++bucket) {
    EXPECT_FALSE(storage.bucketBaselineHeld(bucket));
  }

  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(storage.numSubscriptions(), 2);
    EXPECT_EVENTUALLY_EQ(storage.numIntervalSubscriptions(), 1);
  });

  EXPECT_EQ(storage.set(root.member().min(), 11), std::nullopt);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(fastGen), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(slowGen), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
}

TEST(SubscribableStorageServeInterval, MultipleSubscribersShareABucket) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(20));
  auto readerA = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fastA")), root, fastParams);
  auto readerB = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fastB")), root, fastParams);
  auto genA = std::move(readerA.generator_);
  auto genB = std::move(readerB.generator_);

  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(genA), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(genB), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);

  // Same interval means one bucket, so a single publish feeds both.
  WITH_RETRIES({ EXPECT_EVENTUALLY_TRUE(storage.bucketBaselineHeld(0)); });
  for (size_t bucket = 1; bucket < storage.numServeBuckets(); ++bucket) {
    EXPECT_FALSE(storage.bucketBaselineHeld(bucket));
  }

  EXPECT_EQ(storage.set(root.member().min(), 22), std::nullopt);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(genA), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
  EXPECT_EQ(
      folly::coro::blockingWait(
          folly::coro::timeout(consumeOne(genB), std::chrono::seconds(5)))
          .val.getType(),
      SubscriberMessage::Type::chunk);
}

// Every bucket occupied, two subscribers each: exercises the (bucket + 1) tick
// divisor for every bucket, the tick where all of them coincide, and the
// peak-memory shape of one retained baseline per bucket.
TEST(SubscribableStorageServeInterval, AllBucketsOccupiedWithTwoSubscribers) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  const size_t numBuckets = storage.numServeBuckets();
  ASSERT_EQ(numBuckets, 5u);

  using Reader = SubscriptionStreamReader<
      SubscriptionServeQueueElement<SubscriberMessage>>;
  std::vector<Reader> readers;
  readers.reserve(numBuckets * 2);
  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    // bucketParams() ticks every 20ms, so bucket b wants (b + 1) * 20ms.
    SubscriptionStorageParams params(
        std::nullopt, static_cast<uint32_t>((bucket + 1) * 20));
    for (int replica = 0; replica < 2; ++replica) {
      readers.emplace_back(storage.subscribe_patch(
          SubscriptionIdentifier(
              SubscriberId(fmt::format("b{}s{}", bucket, replica))),
          root,
          params));
    }
  }

  for (auto& reader : readers) {
    EXPECT_EQ(
        folly::coro::blockingWait(
            folly::coro::timeout(consumeOne(reader), std::chrono::seconds(10)))
            .val.getType(),
        SubscriberMessage::Type::chunk);
  }

  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_TRUE(storage.bucketBaselineHeld(bucket)); });
  }

  EXPECT_EQ(storage.set(root.member().min(), 33), std::nullopt);
  for (auto& reader : readers) {
    EXPECT_EQ(
        folly::coro::blockingWait(
            folly::coro::timeout(consumeOne(reader), std::chrono::seconds(10)))
            .val.getType(),
        SubscriberMessage::Type::chunk);
  }
}

// The guarantee the whole feature rests on: serving a fast bucket must not
// deliver to a slow bucket's subscriber. Asserts on bytes enqueued rather than
// draining the streams, so there is no timing dependence either way.
TEST(SubscribableStorageServeInterval, SlowBucketNotServedOnFastTick) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);

  thriftpath::RootThriftPath<TestStruct> root;
  const size_t fastBucket = 0;
  const size_t slowBucket = storage.numServeBuckets() - 1;

  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(20));
  auto fastReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
  auto slowReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("slow")), root);

  // Each bucket's first serve registers its pending subscription and enqueues
  // initial sync.
  storage.serveBucketOnce(fastBucket);
  storage.serveBucketOnce(slowBucket);
  const auto fastAfterSync = fastReader.streamInfo_->enqueuedDataSize.load();
  const auto slowAfterSync = slowReader.streamInfo_->enqueuedDataSize.load();
  EXPECT_GT(fastAfterSync, 0u);
  EXPECT_GT(slowAfterSync, 0u);

  EXPECT_EQ(storage.set(root.member().min(), 77), std::nullopt);
  storage.serveBucketOnce(fastBucket);

  EXPECT_GT(fastReader.streamInfo_->enqueuedDataSize.load(), fastAfterSync);
  // The slow bucket was not served, so nothing may have reached its subscriber.
  EXPECT_EQ(slowReader.streamInfo_->enqueuedDataSize.load(), slowAfterSync);

  // ...and it does get the update once its own bucket is served.
  storage.serveBucketOnce(slowBucket);
  EXPECT_GT(slowReader.streamInfo_->enqueuedDataSize.load(), slowAfterSync);
}

TEST(SubscribableStorageServeInterval, GetSlotRefreshesWithNoSubscribers) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  EXPECT_EQ(storage.set(root.tx(), false), std::nullopt);
  WITH_RETRIES({ EXPECT_EVENTUALLY_TRUE(storage.publishedStateCurrent()); });
  EXPECT_FALSE(storage.get(root.tx()).value());
}

// Regression: publish() is a global one-shot, so a single UNPUBLISHED walk used
// to register newly-added paths with only ONE store, leaving every other
// bucket's extended subscriptions unable to resolve keys that appeared after
// they subscribed. One walk must feed all buckets.
TEST(SubscribableStorageServeInterval, AddedPathsReachEveryBucket) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  const auto& extPath =
      ext_path_builder::raw("mapOfStringToI32").regex("test.*").get();

  // A fast-bucket subscriber publishes first on most ticks; a default-bucket
  // extended subscriber must still see keys that appear afterwards.
  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(20));
  auto fastReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
  auto fastGen = std::move(fastReader.generator_);

  auto extReader = storage.subscribe_patch_extended(
      SubscriptionIdentifier(SubscriberId("slow_ext")), {{1, extPath}});
  auto extGen = std::move(extReader.generator_);

  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(fastGen), std::chrono::seconds(5)));

  // A key that did not exist at subscribe time must still be delivered. The
  // wildcard has nothing to serve until it resolves, so this is also its
  // initial sync.
  EXPECT_EQ(storage.set(root.mapOfStringToI32()["test1"], 42), std::nullopt);
  WITH_RETRIES({
    auto element = folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(extGen), std::chrono::seconds(5)));
    auto msg = std::move(element.val);
    const bool hasKey = msg.getType() == SubscriberMessage::Type::chunk &&
        msg.chunk().as_const().value().patchGroups()->count(1);
    ASSERT_EVENTUALLY_TRUE(hasKey);
  });
}

// The freeze walk mutates published_ on shared COW nodes, so it must stay
// mutually exclusive with concurrent publishes and rlock GET readers. Publish
// rapidly across several buckets and assert we neither trip the
// !isPublished() CHECK nor lose the final value.
TEST(SubscribableStorageServeInterval, ConcurrentPublishDoesNotRace) {
  StorageParams params(
      std::chrono::milliseconds(20),
      std::chrono::seconds(5),
      false,
      fsdbPrefix());
  params.setServeTickInterval(std::chrono::milliseconds(10));
  auto storage = BucketTestStorage(initializeTestStruct(), params);
  storage.setConvertToIDPaths(true);

  thriftpath::RootThriftPath<TestStruct> root;
  SubscriptionStorageParams fastParams(std::nullopt, static_cast<uint32_t>(10));
  auto fastReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
  auto defaultReader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("default")), root);
  auto fastGen = std::move(fastReader.generator_);
  auto defaultGen = std::move(defaultReader.generator_);
  storage.start();
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(fastGen), std::chrono::seconds(5)));
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(defaultGen), std::chrono::seconds(5)));

  for (int i = 1; i <= 200; ++i) {
    EXPECT_EQ(storage.set(root.member().min(), i), std::nullopt);
    folly::coro::blockingWait(folly::coro::sleep(std::chrono::milliseconds(2)));
  }
  WITH_RETRIES(
      { EXPECT_EVENTUALLY_EQ(storage.get(root.member().min()).value(), 200); });
}

TEST(SubscribableStorageServeInterval, IdleBucketsDoNotPinStaleTrees) {
  auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  // Only the default bucket is occupied; every faster bucket stays idle.
  auto reader = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("default")), root);
  auto generator = std::move(reader.generator_);
  folly::coro::blockingWait(
      folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));

  EXPECT_EQ(storage.set(root.member().min(), 42), std::nullopt);

  const size_t defaultBucket = storage.numServeBuckets() - 1;
  WITH_RETRIES(
      { EXPECT_EVENTUALLY_TRUE(storage.bucketBaselineHeld(defaultBucket)); });
  for (size_t bucket = 0; bucket < defaultBucket; ++bucket) {
    EXPECT_FALSE(storage.bucketBaselineHeld(bucket));
  }
}

// A client that re-subscribes in a loop must not accelerate the bucket it
// lands in. Before this was fixed, a pending registration force-served the
// whole bucket, so on a live switch one re-subscribing client pulled every
// other subscriber in the default bucket off its cadence.
TEST(SubscribableStorageServeInterval, PendingDoesNotAccelerateBucket) {
  // Slower than bucketParams() so the window between cadence serves is wide
  // enough to observe without racing it: tick 200ms, default bucket 1s.
  StorageParams params(
      std::chrono::milliseconds(1000),
      std::chrono::seconds(5),
      false,
      fsdbPrefix());
  params.setServeTickInterval(std::chrono::milliseconds(200));
  auto storage = BucketTestStorage(initializeTestStruct(), params);
  storage.setConvertToIDPaths(true);
  storage.start();

  thriftpath::RootThriftPath<TestStruct> root;
  auto established = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("established")), root);

  // Settle: wait for initial sync, then for the serve after it, so the next
  // cadence tick is a full bucket interval away and the probe window below
  // cannot collide with it.
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(established.streamInfo_->enqueuedDataSize.load(), 0u);
  });
  EXPECT_EQ(storage.set(root.member().min(), 1), std::nullopt);
  auto afterSync = established.streamInfo_->enqueuedDataSize.load();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(
        established.streamInfo_->enqueuedDataSize.load(), afterSync);
  });
  const auto baseline = established.streamInfo_->enqueuedDataSize.load();

  // Register a newcomer into the same bucket and give it several ticks. There
  // is pending work and there is data to serve, so the only thing keeping the
  // established subscriber quiet is that its bucket is not due.
  EXPECT_EQ(storage.set(root.member().min(), 2), std::nullopt);
  auto newcomer = storage.subscribe_patch(
      SubscriptionIdentifier(SubscriberId("newcomer")), root);
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  EXPECT_EQ(established.streamInfo_->enqueuedDataSize.load(), baseline);

  // ...and both are served once the bucket's own tick comes round.
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(
        established.streamInfo_->enqueuedDataSize.load(), baseline);
    EXPECT_EVENTUALLY_GT(newcomer.streamInfo_->enqueuedDataSize.load(), 0u);
  });
}

TEST(SubscribableStorageServeInterval, StopDoesNotCrash) {
  {
    auto storage = BucketTestStorage(initializeTestStruct(), bucketParams());
    storage.setConvertToIDPaths(true);
    storage.start();

    thriftpath::RootThriftPath<TestStruct> root;
    SubscriptionStorageParams fastParams(
        std::nullopt, static_cast<uint32_t>(20));
    auto reader = storage.subscribe_patch(
        SubscriptionIdentifier(SubscriberId("fast")), root, fastParams);
    auto generator = std::move(reader.generator_);
    folly::coro::blockingWait(
        folly::coro::timeout(consumeOne(generator), std::chrono::seconds(5)));
    EXPECT_EQ(storage.set(root.member().min(), 1), std::nullopt);
  }
  SUCCEED();
}
