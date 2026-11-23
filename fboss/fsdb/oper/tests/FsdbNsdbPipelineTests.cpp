// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/json/dynamic.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <utility>

#include <fboss/fsdb/oper/NaivePeriodicSubscribableStorage.h>
#include <fboss/lib/CommonUtils.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/coro/Timeout.h>
#include "fboss/fsdb/oper/tests/TestHelpers.h"
#include "fboss/fsdb/tests/gen-cpp2-thriftpath/thriftpath_test.h" // @manual=//fboss/fsdb/tests:thriftpath_test_thrift-cpp2-thriftpath
#include "fboss/fsdb/tests/gen-cpp2/thriftpath_test_types.h"

using folly::dynamic;
using namespace testing;

namespace {

using namespace facebook::fboss::fsdb;

constexpr auto kSubscriber = "testSubscriber";

template <typename Gen>
folly::coro::Task<typename Gen::value_type> consumeOne(Gen& generator) {
  auto item = co_await generator.next();
  auto&& value = *item;
  co_return std::move(value);
}

using PatchGenerator = folly::coro::AsyncGenerator<
    SubscriptionServeQueueElement<SubscriberMessage>&&>;
using DeltaGenerator =
    folly::coro::AsyncGenerator<SubscriptionServeQueueElement<OperDelta>&&>;

OperDelta awaitDelta(DeltaGenerator& deltaSubStream) {
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(
          consumeOne(deltaSubStream), std::chrono::seconds(5)));
  OperDelta deltaVal = std::move(element.val);
  EXPECT_EQ(deltaVal.changes()->size(), 1);
  return deltaVal;
}

Patch awaitPatch(PatchGenerator& patchSubStream, bool isInitial = false) {
  auto element = folly::coro::blockingWait(
      folly::coro::timeout(
          consumeOne(patchSubStream), std::chrono::seconds(5)));
  SubscriberMessage patchMsg = std::move(element.val);
  auto patchGroups = *patchMsg.chunk().as_const()->patchGroups();
  EXPECT_EQ(patchGroups.size(), 1);
  auto patches = patchGroups.begin()->second;
  EXPECT_EQ(patches.size(), 1);
  Patch patch = patches.front();
  if (isInitial) {
    // make sure Patch.PatchNode has a value
    auto rootPatch = patch.patch()->val();
    EXPECT_TRUE(rootPatch);
  }
  return patch;
}

void applyPatchAndVerify(
    Patch& patch,
    auto& subscriptionPath,
    const auto& srcStorage,
    auto& tgtStorage) {
  auto srcState = srcStorage.get(subscriptionPath).value();
  std::optional<StorageError> result = tgtStorage.patch(std::move(patch));
  EXPECT_EQ(result.has_value(), false);
  tgtStorage.refreshPublishedStateForReads();
  auto tgtState = tgtStorage.get(subscriptionPath).value();
  EXPECT_EQ(srcState, tgtState);
}

void applyDeltaAndVerify(
    OperDelta& patch,
    auto& subscriptionPath,
    const auto& srcStorage,
    auto& tgtStorage) {
  for (auto& change : *patch.changes()) {
    auto& pathVec = *change.path()->raw();
    auto tokens = subscriptionPath.tokens();
    pathVec.insert(pathVec.begin(), tokens.begin(), tokens.end());
  }
  std::optional<StorageError> result = tgtStorage.patch(std::move(patch));
  EXPECT_EQ(result.has_value(), false);
  tgtStorage.refreshPublishedStateForReads();
  auto tgtState = tgtStorage.get(subscriptionPath).value();
  auto srcState = srcStorage.get(subscriptionPath).value();
  EXPECT_EQ(srcState, tgtState);
}

} // namespace

enum class TestDataType {
  kHybridMapOfStruct, // test data covering hybrid map of struct
  kMapOfHybridStruct // test data covering map with hybrid struct values
};

template <typename StorageT, TestDataType dataType>
struct TestDataFactory;

template <typename StorageT>
struct TestDataFactory<StorageT, TestDataType::kHybridMapOfStruct> {
 private:
  StorageT& storage_;
  const thriftpath::RootThriftPath<TestStruct> root;
  decltype(root) subscriptionPath_ = root;
  const int oldKey = 3;
  const int newKey = 99;
  int newIntVal = 12345;

  TestStructSimple buildNewStruct() {
    TestStructSimple newStruct;
    newStruct.min() = 999;
    newStruct.max() = 1001;
    return newStruct;
  }

  auto getUpdatedEntry() {
    return storage_.get(subscriptionPath().structMap()[oldKey]).value();
  }

 public:
  explicit TestDataFactory(StorageT& storage) : storage_(storage) {}

  auto& subscriptionPath() {
    return subscriptionPath_;
  }

  std::optional<StorageError> addNewEntryToMap() {
    auto newStruct = buildNewStruct();
    return storage_.set(this->root.structMap()[newKey], newStruct);
  }

  void verifyOperDeltaForNewEntry(OperDelta& deltaMsg) {
    auto first = deltaMsg.changes()->at(0);
    // expect only new field
    EXPECT_FALSE(first.oldState());
    EXPECT_TRUE(first.newState());
    // expect newly added entry
    EXPECT_THAT(
        *first.path()->raw(),
        ::testing::ContainerEq(std::vector<std::string>({"structMap", "99"})));
    TestStructSimple deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::structure, TestStructSimple>(
            OperProtocol::SIMPLE_JSON, *first.newState());
    auto expected = buildNewStruct();
    EXPECT_EQ(deserialized, expected);
  }

  std::optional<StorageError> deepUpdateExistingEntryInMap() {
    return storage_.set(
        this->root.structMap()[oldKey].optionalIntegral(), newIntVal);
  }

  void verifyOperDeltaForDeepUpdate(OperDelta& deltaMsg, bool isHybridStorage) {
    storage_.refreshPublishedStateForReads();
    auto first = deltaMsg.changes()->at(0);
    if (isHybridStorage) {
      // expect both old and new state
      EXPECT_TRUE(first.oldState());
      EXPECT_TRUE(first.newState());
      // expect full struct
      EXPECT_EQ(first.path()->raw()->size(), 2);
      EXPECT_THAT(
          *first.path()->raw(),
          ::testing::ContainerEq(std::vector<std::string>({"structMap", "3"})));
      TestStructSimple deserialized = facebook::fboss::thrift_cow::
          deserialize<apache::thrift::type_class::structure, TestStructSimple>(
              OperProtocol::SIMPLE_JSON, *first.newState());
      auto expected = getUpdatedEntry();
      EXPECT_EQ(deserialized, expected);
      EXPECT_EQ(deserialized.min(), 100);
      EXPECT_EQ(deserialized.max(), 200);
      EXPECT_EQ(deserialized.optionalIntegral(), 12345);
    } else {
      EXPECT_FALSE(first.oldState());
      EXPECT_TRUE(first.newState());
      EXPECT_EQ(first.path()->raw()->size(), 3);
      EXPECT_THAT(
          *first.path()->raw(),
          ::testing::ContainerEq(
              std::vector<std::string>(
                  {"structMap", "3", "optionalIntegral"})));
      auto deserializedInt32 = facebook::fboss::thrift_cow::
          deserialize<apache::thrift::type_class::integral, int>(
              OperProtocol::SIMPLE_JSON, *first.newState());
      EXPECT_EQ(deserializedInt32, 12345);
    }
  }

  std::optional<StorageError> deleteExistingEntryFromMap() {
    storage_.remove(subscriptionPath().structMap()[newKey]);
    return std::nullopt;
  }

  void verifyOperDeltaForDelete(OperDelta& deltaMsg) {
    // expect deleted entry
    auto first = deltaMsg.changes()->at(0);
    EXPECT_TRUE(first.oldState());
    EXPECT_FALSE(first.newState());
    EXPECT_THAT(
        *first.path()->raw(),
        ::testing::ContainerEq(std::vector<std::string>({"structMap", "99"})));
    TestStructSimple deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::structure, TestStructSimple>(
            OperProtocol::SIMPLE_JSON, *first.oldState());
    auto newStruct = buildNewStruct();
    EXPECT_EQ(deserialized, newStruct);
  }
};

template <typename StorageT>
struct TestDataFactory<StorageT, TestDataType::kMapOfHybridStruct> {
 private:
  StorageT& storage_;
  const thriftpath::RootThriftPath<TestStruct> root;
  decltype(root.mapOfHybridStruct()) subscriptionPath_ =
      root.mapOfHybridStruct();
  const int oldKey = 301;
  const int newKey = 5301;
  int newIntVal = 777;

  TestHybridStruct buildNewStruct() {
    TestHybridStruct newStruct;
    newStruct.optionalIntegral() = 999;
    return newStruct;
  }

  auto getUpdatedEntry() {
    return storage_.get(subscriptionPath()[oldKey]).value();
  }

 public:
  explicit TestDataFactory(StorageT& storage) : storage_(storage) {}

  auto& subscriptionPath() {
    return subscriptionPath_;
  }

  std::optional<StorageError> addNewEntryToMap() {
    auto newStruct = buildNewStruct();
    return storage_.set(subscriptionPath()[newKey], newStruct);
  }

  void verifyOperDeltaForNewEntry(OperDelta& deltaMsg) {
    auto first = deltaMsg.changes()->at(0);
    // expect only new field
    EXPECT_FALSE(first.oldState());
    EXPECT_TRUE(first.newState());
    // expect newly added entry
    EXPECT_THAT(
        *first.path()->raw(),
        ::testing::ContainerEq(std::vector<std::string>({"5301"})));
    EXPECT_FALSE(first.oldState());
    TestHybridStruct deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::structure, TestHybridStruct>(
            OperProtocol::SIMPLE_JSON, *first.newState());
    auto expected = buildNewStruct();
    EXPECT_EQ(deserialized, expected);
  }

  std::optional<StorageError> deepUpdateExistingEntryInMap() {
    return storage_.set(
        subscriptionPath()[oldKey].optionalIntegral(), newIntVal);
  }

  void verifyOperDeltaForDeepUpdate(OperDelta& deltaMsg, bool isHybridStorage) {
    storage_.refreshPublishedStateForReads();
    auto first = deltaMsg.changes()->at(0);
    // expect both old and new states
    EXPECT_TRUE(first.oldState());
    EXPECT_TRUE(first.newState());
    if (isHybridStorage) {
      // expect full struct
      EXPECT_EQ(first.path()->raw()->size(), 1);
      EXPECT_THAT(
          *first.path()->raw(),
          ::testing::ContainerEq(std::vector<std::string>({"301"})));
      TestHybridStruct deserialized = facebook::fboss::thrift_cow::
          deserialize<apache::thrift::type_class::structure, TestHybridStruct>(
              OperProtocol::SIMPLE_JSON, *first.newState());
      auto expected = getUpdatedEntry();
      EXPECT_EQ(deserialized, expected);
      EXPECT_EQ(deserialized.optionalIntegral(), newIntVal);
    } else {
      // expect only changed field
      EXPECT_EQ(first.path()->raw()->size(), 2);
      EXPECT_THAT(
          *first.path()->raw(),
          ::testing::ContainerEq(
              std::vector<std::string>({"301", "optionalIntegral"})));
      auto deserializedInt32 = facebook::fboss::thrift_cow::
          deserialize<apache::thrift::type_class::integral, int>(
              OperProtocol::SIMPLE_JSON, *first.newState());
      EXPECT_EQ(deserializedInt32, newIntVal);
    }
  }

  std::optional<StorageError> deleteExistingEntryFromMap() {
    storage_.remove(subscriptionPath()[newKey]);
    return std::nullopt;
  }

  void verifyOperDeltaForDelete(OperDelta& deltaMsg) {
    // expect deleted entry
    auto first = deltaMsg.changes()->at(0);
    EXPECT_TRUE(first.oldState());
    EXPECT_FALSE(first.newState());
    EXPECT_THAT(
        *first.path()->raw(),
        ::testing::ContainerEq(std::vector<std::string>({"5301"})));
    TestHybridStruct deserialized = facebook::fboss::thrift_cow::
        deserialize<apache::thrift::type_class::structure, TestHybridStruct>(
            OperProtocol::SIMPLE_JSON, *first.oldState());
    auto newStruct = buildNewStruct();
    EXPECT_EQ(deserialized, newStruct);
  }
};

template <bool EnableHybridStorage, TestDataType DataType>
struct TestParams {
  static constexpr auto hybridStorage = EnableHybridStorage;
  static constexpr auto dataType = DataType;
};

using FsdbNsdbPipelineTestTypes = ::testing::Types<
    TestParams<false, TestDataType::kHybridMapOfStruct>,
    TestParams<true, TestDataType::kHybridMapOfStruct>,
    TestParams<false, TestDataType::kMapOfHybridStruct>,
    TestParams<true, TestDataType::kMapOfHybridStruct>>;

// helper class to test data propagation through FSDB-NSDB pipeline
template <typename TestParams>
class FsdbNsdbPipelineTests : public Test {
 public:
  void SetUp() override {
    testStruct = initializeTestStruct();
  }

  auto initStorage(auto& val) {
    auto constexpr isHybridStorage = TestParams::hybridStorage;
    using RootType = std::remove_cvref_t<decltype(val)>;
    return NaivePeriodicSubscribableCowStorage<RootType, isHybridStorage>(val);
  }

  auto initHybridStorage(auto& val) {
    using RootType = std::remove_cvref_t<decltype(val)>;
    return NaivePeriodicSubscribableCowStorage<RootType, true>(val);
  }

  auto initPureCowStorage(auto& val) {
    using RootType = std::remove_cvref_t<decltype(val)>;
    return NaivePeriodicSubscribableCowStorage<RootType, false>(val);
  }

  constexpr bool isHybridStorage() {
    return TestParams::hybridStorage;
  }

  constexpr TestDataType getTestDataType() {
    return TestParams::dataType;
  }

 protected:
  TestStruct testStruct;
};

TYPED_TEST_SUITE(FsdbNsdbPipelineTests, FsdbNsdbPipelineTestTypes);

TYPED_TEST(FsdbNsdbPipelineTests, FsdbToSwitchAgentPatchSubscription) {
  // srcStorage (FSDB): pure COW storage
  // tgtStorage (SwitchAgent): typed tests will cover hybrid and pure COW
  auto srcStorage = this->initPureCowStorage(this->testStruct);
  srcStorage.setConvertToIDPaths(true);
  srcStorage.start();

  TestDataFactory<decltype(srcStorage), TypeParam::dataType> factory(
      srcStorage);

  TestStruct emptyInitialData;
  auto tgtStorage = this->initStorage(emptyInitialData);

  auto streamReader = srcStorage.subscribe_patch(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      factory.subscriptionPath());
  PatchGenerator genPatch = std::move(streamReader.generator_);

  // 1. initial sync: patch initial sync, and verify target storage state
  Patch patch = awaitPatch(genPatch, true);
  applyPatchAndVerify(
      patch, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 2. add new entry to map
  EXPECT_EQ(factory.addNewEntryToMap(), std::nullopt);
  patch = awaitPatch(genPatch);
  applyPatchAndVerify(
      patch, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 3. deep update existing entry in map
  EXPECT_EQ(factory.deepUpdateExistingEntryInMap(), std::nullopt);
  patch = awaitPatch(genPatch);
  applyPatchAndVerify(
      patch, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 4. delete existing entry from map
  EXPECT_EQ(factory.deleteExistingEntryFromMap(), std::nullopt);
  patch = awaitPatch(genPatch);
  applyPatchAndVerify(
      patch, factory.subscriptionPath(), srcStorage, tgtStorage);
}

TYPED_TEST(FsdbNsdbPipelineTests, SwitchAgentToNsdbDeltaPublish) {
  // srcStorage (SwitchAgent): Hybrid COW storage
  // tgtStorage (NSDB): typed tests will cover hybrid and pure COW
  auto srcStorage = this->initHybridStorage(this->testStruct);
  srcStorage.start();

  TestDataFactory<decltype(srcStorage), TypeParam::dataType> factory(
      srcStorage);

  TestStruct emptyInitialData;
  auto tgtStorage = this->initStorage(emptyInitialData);

  auto streamReader = srcStorage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      factory.subscriptionPath(),
      OperProtocol::SIMPLE_JSON);
  DeltaGenerator generator = std::move(streamReader.generator_);

  // 1. initial sync: patch initial sync, and verify target storage state
  OperDelta deltaVal = awaitDelta(generator);
  applyDeltaAndVerify(
      deltaVal, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 2. add new entry to map
  EXPECT_EQ(factory.addNewEntryToMap(), std::nullopt);
  deltaVal = awaitDelta(generator);
  applyDeltaAndVerify(
      deltaVal, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 3. deep update existing entry in map
  EXPECT_EQ(factory.deepUpdateExistingEntryInMap(), std::nullopt);
  deltaVal = awaitDelta(generator);
  applyDeltaAndVerify(
      deltaVal, factory.subscriptionPath(), srcStorage, tgtStorage);

  // 4. delete existing entry from map
  EXPECT_EQ(factory.deleteExistingEntryFromMap(), std::nullopt);
  deltaVal = awaitDelta(generator);
  applyDeltaAndVerify(
      deltaVal, factory.subscriptionPath(), srcStorage, tgtStorage);
}

TYPED_TEST(FsdbNsdbPipelineTests, verifyNsdbDeltaSubscriptionGranularity) {
  auto srcStorage = this->initStorage(this->testStruct);
  srcStorage.start();

  TestDataFactory<decltype(srcStorage), TypeParam::dataType> factory(
      srcStorage);

  auto streamReader = srcStorage.subscribe_delta(
      std::move(SubscriptionIdentifier(SubscriberId(kSubscriber))),
      factory.subscriptionPath(),
      OperProtocol::SIMPLE_JSON);
  auto generator = std::move(streamReader.generator_);

  // 1. verify initial sync
  auto deltaMsg = awaitDelta(generator);
  // expect full tree
  EXPECT_THAT(
      *deltaMsg.changes()->at(0).path()->raw(),
      ::testing::ContainerEq(std::vector<std::string>({})));

  // 2. add new entry to map
  EXPECT_EQ(factory.addNewEntryToMap(), std::nullopt);
  deltaMsg = awaitDelta(generator);
  // verify that received delta is as expected for new entry
  factory.verifyOperDeltaForNewEntry(deltaMsg);

  // 2. deep update existing entry in map
  EXPECT_EQ(factory.deepUpdateExistingEntryInMap(), std::nullopt);
  deltaMsg = awaitDelta(generator);
  // verify that received delta is as expected for the change
  factory.verifyOperDeltaForDeepUpdate(deltaMsg, this->isHybridStorage());

  // 3. delete existing entry from map
  EXPECT_EQ(factory.deleteExistingEntryFromMap(), std::nullopt);
  deltaMsg = awaitDelta(generator);
  // verify that received delta is as expected for deleted entry
  factory.verifyOperDeltaForDelete(deltaMsg);
}
