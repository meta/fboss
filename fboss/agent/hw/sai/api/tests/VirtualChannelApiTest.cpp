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
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

#include <algorithm>

#if defined(SAI_CBFC_SUPPORTED)

using namespace facebook::fboss;

namespace {
constexpr sai_object_id_t kPort = 42;
constexpr sai_uint8_t kRdmaVc = 2;
constexpr sai_uint8_t kMonitoringVc = 6;
constexpr sai_uint64_t kReservedCreditSize = 36;
} // namespace

class VirtualChannelApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    virtualChannelApi = std::make_unique<VirtualChannelApi>();
  }

  CbfcCreditProfileSaiId createCreditProfile(
      sai_uint64_t reservedCreditSize) const {
    return virtualChannelApi->create<SaiCbfcCreditProfileTraits>(
        {SAI_NULL_OBJECT_ID, reservedCreditSize}, 0);
  }

  VirtualChannelSaiId createVirtualChannel(
      sai_uint8_t index,
      std::optional<CbfcCreditProfileSaiId> profile = std::nullopt) const {
    using Attributes = SaiVirtualChannelTraits::Attributes;
    std::optional<Attributes::CbfcSenderCreditProfile> profileAttribute;
    if (profile) {
      profileAttribute = Attributes::CbfcSenderCreditProfile{*profile};
    }
    return virtualChannelApi->create<SaiVirtualChannelTraits>(
        {kPort, index, profileAttribute, true, true}, 0);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<VirtualChannelApi> virtualChannelApi;
};

TEST_F(VirtualChannelApiTest, createVirtualChannel) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto id = createVirtualChannel(kRdmaVc);

  EXPECT_EQ(id, fs->virtualChannelManager.get(id).id);
  EXPECT_EQ(virtualChannelApi->getAttribute(id, Attributes::Port{}), kPort);
  EXPECT_EQ(virtualChannelApi->getAttribute(id, Attributes::Index{}), kRdmaVc);
  EXPECT_TRUE(
      virtualChannelApi->getAttribute(id, Attributes::CbfcReceiverEnable{}));
  EXPECT_TRUE(
      virtualChannelApi->getAttribute(id, Attributes::CbfcSenderEnable{}));
}

TEST_F(VirtualChannelApiTest, createVirtualChannelWithoutCreditProfile) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto id = createVirtualChannel(kRdmaVc);

  // reservedCreditSize is optional in config, so a VC may reference no profile.
  EXPECT_EQ(
      virtualChannelApi->getAttribute(
          id, Attributes::CbfcSenderCreditProfile{}),
      SAI_NULL_OBJECT_ID);
}

TEST_F(VirtualChannelApiTest, virtualChannelsAreKeyedOnPortAndIndex) {
  auto rdma = createVirtualChannel(kRdmaVc);
  auto monitoring = createVirtualChannel(kMonitoringVc);

  EXPECT_NE(rdma, monitoring);
  EXPECT_EQ(fs->virtualChannelManager.map().size(), 2);
}

TEST_F(VirtualChannelApiTest, setVirtualChannelEnables) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto id = createVirtualChannel(kRdmaVc);

  virtualChannelApi->setAttribute(id, Attributes::CbfcSenderEnable{false});
  EXPECT_FALSE(
      virtualChannelApi->getAttribute(id, Attributes::CbfcSenderEnable{}));
  EXPECT_TRUE(
      virtualChannelApi->getAttribute(id, Attributes::CbfcReceiverEnable{}));
}

TEST_F(VirtualChannelApiTest, removeVirtualChannel) {
  auto id = createVirtualChannel(kRdmaVc);
  EXPECT_EQ(fs->virtualChannelManager.map().size(), 1);

  virtualChannelApi->remove(id);
  EXPECT_EQ(fs->virtualChannelManager.map().size(), 0);
}

TEST_F(VirtualChannelApiTest, virtualChannelStatsReadZero) {
  auto id = createVirtualChannel(kRdmaVc);

  // Fake has no dataplane, so no credit is ever consumed. This only asserts
  // that the four counters FBOSS reads are wired up and return a value.
  auto stats = virtualChannelApi->getStats<SaiVirtualChannelTraits>(
      id, SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), SaiVirtualChannelTraits::CounterIdsToRead.size());
  for (auto value : stats) {
    EXPECT_EQ(value, 0);
  }
}

// The three counters left out of cbfcVcStats() are Tomahawk Ultra 1 B0
// stepping only. get_virtual_channel_stats is all-or-nothing, so one of them
// creeping into the list would lose every virtual channel counter on an A0
// port, not just that one.
TEST_F(VirtualChannelApiTest, cbfcVcStatsOmitsB0OnlyCounters) {
  const auto& ids = SaiVirtualChannelTraits::cbfcVcStats();
  for (auto b0Only :
       {SAI_VIRTUAL_CHANNEL_STAT_SENDER_CREDITS_CONSUMED,
        SAI_VIRTUAL_CHANNEL_STAT_SENDER_CREDITS_FREED,
        SAI_VIRTUAL_CHANNEL_STAT_RECEIVER_CREDITS_FREED}) {
    EXPECT_EQ(std::find(ids.begin(), ids.end(), b0Only), ids.end());
  }
}

TEST_F(VirtualChannelApiTest, getCbfcVcStats) {
  auto id = createVirtualChannel(kRdmaVc);
  auto stats = virtualChannelApi->getStats<SaiVirtualChannelTraits>(
      id, SaiVirtualChannelTraits::cbfcVcStats(), SAI_STATS_MODE_READ);
  EXPECT_EQ(stats.size(), SaiVirtualChannelTraits::cbfcVcStats().size());
}

TEST_F(VirtualChannelApiTest, readNativeCreditLimit) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto id = createVirtualChannel(kRdmaVc);

  // READ_ONLY, derived by hardware from the MMU carving. Fake cans it, so this
  // asserts the type and union member round-trip, not the value's meaning.
  EXPECT_EQ(
      virtualChannelApi->getAttribute(
          id, Attributes::CbfcReceiverNativeCreditLimit{}),
      500);
}

TEST_F(VirtualChannelApiTest, createCreditProfile) {
  using Attributes = SaiCbfcCreditProfileTraits::Attributes;
  auto id = createCreditProfile(kReservedCreditSize);

  EXPECT_EQ(id, fs->cbfcCreditProfileManager.get(id).id);
  EXPECT_EQ(
      virtualChannelApi->getAttribute(id, Attributes::ReservedCreditSize{}),
      kReservedCreditSize);
  // CBFC_CREDIT_POOL is not modelled, so a profile always has a null pool.
  EXPECT_EQ(
      virtualChannelApi->getAttribute(id, Attributes::PoolId{}),
      SAI_NULL_OBJECT_ID);
}

TEST_F(VirtualChannelApiTest, bindCreditProfileToVirtualChannel) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto profile = createCreditProfile(kReservedCreditSize);
  auto id = createVirtualChannel(kRdmaVc, profile);

  EXPECT_EQ(
      virtualChannelApi->getAttribute(
          id, Attributes::CbfcSenderCreditProfile{}),
      profile);
}

TEST_F(VirtualChannelApiTest, rebindCreditProfile) {
  using Attributes = SaiVirtualChannelTraits::Attributes;
  auto id = createVirtualChannel(kRdmaVc, createCreditProfile(36));
  auto newProfile = createCreditProfile(48);

  // A changed reservedCreditSize yields a new profile object that the manager
  // rebinds, rather than mutating the old one -- RESERVED_CREDIT_SIZE is
  // CREATE_ONLY.
  virtualChannelApi->setAttribute(
      id, Attributes::CbfcSenderCreditProfile{newProfile});

  EXPECT_EQ(
      virtualChannelApi->getAttribute(
          id, Attributes::CbfcSenderCreditProfile{}),
      newProfile);
}

TEST_F(VirtualChannelApiTest, removeCreditProfile) {
  auto id = createCreditProfile(kReservedCreditSize);
  EXPECT_EQ(fs->cbfcCreditProfileManager.map().size(), 1);

  virtualChannelApi->remove(id);
  EXPECT_EQ(fs->cbfcCreditProfileManager.map().size(), 0);
}

#endif
