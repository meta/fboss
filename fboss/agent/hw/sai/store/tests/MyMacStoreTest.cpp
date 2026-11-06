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
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {
const folly::MacAddress kMac("02:fb:00:00:00:01");
const folly::MacAddress kMask("ff:ff:ff:ff:ff:ff");
constexpr uint16_t kVlan = 2000;
} // namespace

class MyMacStoreTest : public SaiStoreTest {
 public:
  MyMacSaiId createMyMac() {
    SaiMyMacTraits::CreateAttributes c{kMac, kMask, kVlan};
    return saiApiTable->myMacApi().create<SaiMyMacTraits>(c, 0);
  }
};

TEST_F(MyMacStoreTest, loadMyMac) {
  auto id = createMyMac();
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiMyMacTraits>();
  SaiMyMacTraits::AdapterHostKey k{kMac, kMask, kVlan};
  EXPECT_EQ(store.get(k)->adapterKey(), id);
}

TEST_F(MyMacStoreTest, myMacLoadCtor) {
  auto id = createMyMac();
  auto obj = createObj<SaiMyMacTraits>(id);
  EXPECT_EQ(obj.adapterKey(), id);
  EXPECT_EQ(GET_ATTR(MyMac, MacAddress, obj.attributes()), kMac);
  EXPECT_EQ(GET_ATTR(MyMac, VlanId, obj.attributes()), kVlan);
}

TEST_F(MyMacStoreTest, myMacCreateCtor) {
  SaiMyMacTraits::AdapterHostKey k{kMac, kMask, kVlan};
  SaiMyMacTraits::CreateAttributes c{kMac, kMask, kVlan};
  auto obj = createObj<SaiMyMacTraits>(k, c, 0);
  EXPECT_EQ(GET_ATTR(MyMac, MacAddress, obj.attributes()), kMac);
  EXPECT_EQ(GET_ATTR(MyMac, VlanId, obj.attributes()), kVlan);
}

TEST_F(MyMacStoreTest, serDeser) {
  auto id = createMyMac();
  verifyAdapterKeySerDeser<SaiMyMacTraits>({id});
}

TEST_F(MyMacStoreTest, toStr) {
  std::ignore = createMyMac();
  verifyToStr<SaiMyMacTraits>();
}
