// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/String.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/fsdb/tests/gen-cpp2/thriftpath_test_types.h"
#include "fboss/thrift_cow/storage/CowStateUpdate.h"
#include "fboss/thrift_cow/storage/CowStorage.h"
#include "fboss/thrift_cow/storage/CowStorageMgr.h"

using folly::dynamic;
using namespace facebook::fboss::fsdb;
namespace k = apache::thrift::ident;

namespace {

TestStruct createTestStruct() {
  dynamic testDyn = dynamic::object("tx", true)("rx", false)(
      "name", "testname")("optionalString", "bla")(
      "member", dynamic::object("min", 10)("max", 20))(
      "variantMember", dynamic::object("integral", 99))(
      "structMap",
      dynamic::object("3", dynamic::object("min", 100)("max", 200)))(
      "enumSet", dynamic::array(1))("integralSet", dynamic::array(5));

  return facebook::thrift::from_dynamic<TestStruct>(
      testDyn, facebook::thrift::dynamic_format::JSON_1);
}

CowStorage<TestStruct> createTestStorage() {
  auto storage = CowStorage<TestStruct>{createTestStruct()};
  storage.publish();
  return storage;
}

} // namespace

namespace facebook::fboss::fsdb {

std::ostream& operator<<(std::ostream& os, const TestStruct& testStruct) {
  return os << apache::thrift::debugString(testStruct);
}

} // namespace facebook::fboss::fsdb

TEST(CowStorageTests, create) {
  auto storage = createTestStorage();
  EXPECT_EQ(storage.root()->getFields()->toThrift(), createTestStruct());
}

class CowStorageUpdateTests : public ::testing::Test {
 public:
  void SetUp() override {
    mgr_ = std::make_unique<CowStorageMgr<TestStruct>>(createTestStorage());
  }
  void TearDown() override {
    mgr_.reset();
  }

  using CowStateUpdateFn =
      typename CowStateUpdate<TestStruct>::CowStateUpdateFn;
  using CowState = typename CowStateUpdate<TestStruct>::CowState;

 protected:
  CowStateUpdateFn setTx(bool val) {
    return [val](const std::shared_ptr<CowState>& in) {
      if (!in->get<k::tx>()) {
        return std::shared_ptr<CowState>();
      }
      auto out = in->clone();
      out->ref<k::tx>() = val;
      return out;
    };
  }
  void waitForCowStateUpdates() {
    auto noopUpdate =
        [](const std::shared_ptr<CowState>&) -> std::shared_ptr<CowState> {
      return nullptr;
    };
    mgr_->updateStateBlocking("waitForStateUpdates", noopUpdate);
  }
  std::unique_ptr<CowStorageMgr<TestStruct>> mgr_;
};

TEST_F(CowStorageUpdateTests, updateBlocking) {
  EXPECT_TRUE(mgr_->getState()->get<k::tx>()->ref());
  mgr_->updateStateBlocking("SetTxFalse", setTx(false));
  EXPECT_TRUE(mgr_->getState()->isPublished());
  EXPECT_FALSE(mgr_->getState()->get<k::tx>()->ref());
}

TEST_F(CowStorageUpdateTests, update) {
  EXPECT_TRUE(mgr_->getState()->get<k::tx>()->ref());
  mgr_->updateState("SetTxFalse", setTx(false));
  waitForCowStateUpdates();
  EXPECT_TRUE(mgr_->getState()->isPublished());
  EXPECT_FALSE(mgr_->getState()->get<k::tx>()->ref());
}

TEST_F(CowStorageUpdateTests, updateNonCoalescing) {
  int numCallbacks = 0;
  mgr_ = std::make_unique<CowStorageMgr<TestStruct>>(
      createTestStorage(),
      [&numCallbacks](const auto&, const auto&) { ++numCallbacks; });

  mgr_->updateStateNoCoalescing("SetTxFalse", setTx(false));
  mgr_->updateStateNoCoalescing("SetTxTrue", setTx(true));
  waitForCowStateUpdates();

  EXPECT_TRUE(mgr_->getState()->isPublished());
  EXPECT_TRUE(mgr_->getState()->get<k::tx>()->ref());
  EXPECT_EQ(numCallbacks, 2);
}

/// Check that modify() actually clones children
///
/// The behavior is controlled by the published flag. This test checks if we
/// forgot to call publish().
TEST_F(CowStorageUpdateTests, updateIsCloned) {
  mgr_ = std::make_unique<CowStorageMgr<TestStruct>>(
      createTestStorage(), [](const auto& oldState, const auto& newState) {
        // If we fail to clone() the child node in modify(), updates will be
        // applied to both new and old state and they will be equal. Assert that
        // we actually get something different in old and new.
        EXPECT_NE(oldState->toThrift(), newState->toThrift());
      });
  {
    // The first update depends on the initial struct being passed in being
    // marked as published on our side.
    SCOPED_TRACE("Initial Update");
    mgr_->updateStateBlocking(
        "InitMemberMax", [](const std::shared_ptr<CowState>& in) {
          auto out = in->clone();
          out->modify("member");
          out->ref<k::member>()->ref<k::max>() = 30;
          return out;
        });
  }
  {
    // The second update depends on the first update being marked as published
    // after callbacks are called.
    SCOPED_TRACE("Second Update");
    mgr_->updateStateBlocking(
        "SetMemberMin", [](const std::shared_ptr<CowState>& in) {
          auto out = in->clone();
          out->modify("member");
          out->ref<k::member>()->ref<k::max>() = 40;
          return out;
        });
  }
}
