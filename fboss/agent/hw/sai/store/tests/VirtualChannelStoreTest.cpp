/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/VirtualChannelApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

#if defined(SAI_CBFC_SUPPORTED)

using namespace facebook::fboss;

namespace {
constexpr sai_object_id_t kPort = 42;
constexpr sai_uint8_t kRdmaVc = 2;
constexpr sai_uint8_t kMonitoringVc = 6;
} // namespace

class VirtualChannelStoreTest : public SaiStoreTest {
 public:
  CbfcCreditProfileSaiId createCreditProfile(
      sai_uint64_t reservedCreditSize) const {
    return saiApiTable->virtualChannelApi().create<SaiCbfcCreditProfileTraits>(
        {SAI_NULL_OBJECT_ID, reservedCreditSize}, 0);
  }

  VirtualChannelSaiId createVirtualChannel(sai_uint8_t index) const {
    return saiApiTable->virtualChannelApi().create<SaiVirtualChannelTraits>(
        {kPort, index, std::nullopt, true, true}, 0);
  }
};

TEST_F(VirtualChannelStoreTest, loadVirtualChannels) {
  auto rdma = createVirtualChannel(kRdmaVc);
  auto monitoring = createVirtualChannel(kMonitoringVc);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualChannelTraits>();

  // The key is port plus index, the two CREATE_ONLY attributes.
  auto got = store.get(SaiVirtualChannelTraits::AdapterHostKey{kPort, kRdmaVc});
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), rdma);

  got =
      store.get(SaiVirtualChannelTraits::AdapterHostKey{kPort, kMonitoringVc});
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), monitoring);
}

TEST_F(VirtualChannelStoreTest, loadCreditProfiles) {
  auto profile = createCreditProfile(36);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiCbfcCreditProfileTraits>();

  auto got = store.get(
      SaiCbfcCreditProfileTraits::AdapterHostKey{SAI_NULL_OBJECT_ID, 36});
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), profile);
}

TEST_F(VirtualChannelStoreTest, virtualChannelSetObject) {
  auto& store = saiStore->get<SaiVirtualChannelTraits>();
  SaiVirtualChannelTraits::AdapterHostKey k{kPort, kRdmaVc};
  SaiVirtualChannelTraits::CreateAttributes c{
      kPort, kRdmaVc, std::nullopt, true, true};

  auto obj = store.setObject(k, c);
  EXPECT_EQ(
      GET_ATTR(VirtualChannel, Index, obj->attributes()),
      static_cast<sai_uint8_t>(kRdmaVc));
  EXPECT_EQ(store.get(k), obj);
}

TEST_F(VirtualChannelStoreTest, virtualChannelSetEnableIsNotRecreate) {
  auto& store = saiStore->get<SaiVirtualChannelTraits>();
  SaiVirtualChannelTraits::AdapterHostKey k{kPort, kRdmaVc};

  auto obj = store.setObject(k, {kPort, kRdmaVc, std::nullopt, true, true});
  auto adapterKey = obj->adapterKey();

  // The enables are CREATE_AND_SET, so flipping one keeps the same key and
  // must not destroy a live virtual channel.
  auto same = store.setObject(k, {kPort, kRdmaVc, std::nullopt, true, false});
  EXPECT_EQ(same->adapterKey(), adapterKey);
  EXPECT_FALSE(
      GET_OPT_ATTR(VirtualChannel, CbfcSenderEnable, same->attributes()));
}

TEST_F(VirtualChannelStoreTest, creditProfilesDedupeOnReservedSize) {
  auto& store = saiStore->get<SaiCbfcCreditProfileTraits>();
  SaiCbfcCreditProfileTraits::CreateAttributes c{SAI_NULL_OBJECT_ID, 36};

  // AdapterHostKey is the whole attribute set, so two virtual channels asking
  // for the same reservation share one profile object. 216 ports x 2 lossless
  // classes is 2 objects, not 432.
  auto first = store.setObject(c, c);
  auto second = store.setObject(c, c);

  EXPECT_EQ(first, second);
  EXPECT_EQ(first->adapterKey(), second->adapterKey());
}

TEST_F(VirtualChannelStoreTest, creditProfilesDifferOnReservedSize) {
  auto& store = saiStore->get<SaiCbfcCreditProfileTraits>();
  SaiCbfcCreditProfileTraits::CreateAttributes rdma{SAI_NULL_OBJECT_ID, 36};
  SaiCbfcCreditProfileTraits::CreateAttributes monitoring{
      SAI_NULL_OBJECT_ID, 12};

  // RESERVED_CREDIT_SIZE is CREATE_ONLY, so a changed value has to become a
  // different object rather than a set on the existing one.
  auto first = store.setObject(rdma, rdma);
  auto second = store.setObject(monitoring, monitoring);

  EXPECT_NE(first->adapterKey(), second->adapterKey());
}

TEST_F(VirtualChannelStoreTest, virtualChannelWarmBootReclaim) {
  auto rdma = createVirtualChannel(kRdmaVc);
  SaiVirtualChannelTraits::AdapterHostKey k{kPort, kRdmaVc};

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualChannelTraits>();

  // A warm boot loads what survived in hardware. Claiming it with setObject is
  // what stops the unreferenced sweep from tearing down a live CBFC session.
  auto obj = store.setObject(k, {kPort, kRdmaVc, std::nullopt, true, true});
  EXPECT_EQ(obj->adapterKey(), rdma);
}

#endif
