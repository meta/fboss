/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/MyMacApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {
const folly::MacAddress kMac("02:fb:00:00:00:01");
const folly::MacAddress kMask("ff:ff:ff:ff:ff:ff");
constexpr uint16_t kVlan = 2000;
} // namespace

class MyMacApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    myMacApi = std::make_unique<MyMacApi>();
  }

  MyMacSaiId createMyMac() const {
    return myMacApi->create<SaiMyMacTraits>({kMac, kMask, kVlan}, 0);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<MyMacApi> myMacApi;
};

TEST_F(MyMacApiTest, createMyMac) {
  auto id = createMyMac();
  EXPECT_EQ(
      myMacApi->getAttribute(id, SaiMyMacTraits::Attributes::MacAddress{}),
      kMac);
  EXPECT_EQ(
      myMacApi->getAttribute(id, SaiMyMacTraits::Attributes::MacAddressMask{}),
      kMask);
  EXPECT_EQ(
      myMacApi->getAttribute(id, SaiMyMacTraits::Attributes::VlanId{}), kVlan);
}

TEST_F(MyMacApiTest, removeMyMac) {
  auto id = createMyMac();
  EXPECT_EQ(fs->myMacManager.map().size(), 1);
  myMacApi->remove(id);
  EXPECT_EQ(fs->myMacManager.map().size(), 0);
}

TEST_F(MyMacApiTest, defaults) {
  EXPECT_EQ(SaiMyMacTraits::Attributes::VlanId::defaultValue(), 0);
  EXPECT_EQ(
      SaiMyMacTraits::Attributes::MacAddressMask::defaultValue(),
      folly::MacAddress("ff:ff:ff:ff:ff:ff"));
}
