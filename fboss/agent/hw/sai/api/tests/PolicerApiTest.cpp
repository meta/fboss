// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/api/PolicerApi.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace facebook::fboss;

class PolicerApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    sai_api_initialize(0, nullptr);
    policerApi = std::make_unique<PolicerApi>();
  }

  SaiPolicerTraits::CreateAttributes attributes(
      uint64_t cir = 1000,
      uint64_t cbs = 100) const {
    return {
        SAI_METER_TYPE_PACKETS,
        SAI_POLICER_MODE_SR_TCM,
        SAI_POLICER_COLOR_SOURCE_BLIND,
        cir,
        cbs,
        0,
        SAI_PACKET_ACTION_FORWARD,
        SAI_PACKET_ACTION_DROP,
        SAI_PACKET_ACTION_DROP};
  }

  std::unique_ptr<PolicerApi> policerApi;
};

TEST_F(PolicerApiTest, createAndGetAttributes) {
  auto expected = attributes(uint64_t{1} << 40, uint64_t{1} << 36);
  auto id = policerApi->create<SaiPolicerTraits>(expected, 0);
  EXPECT_NE(id, PolicerSaiId{SAI_NULL_OBJECT_ID});
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::CreateAttributes{}),
      expected);
  EXPECT_TRUE(FakeSai::getInstance()->policerManager.exists(id));
  policerApi->remove(id);
  EXPECT_FALSE(FakeSai::getInstance()->policerManager.exists(id));
  EXPECT_THROW(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cir{}),
      SaiApiError);
}

TEST_F(PolicerApiTest, updateRateAndBurst) {
  auto id = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  policerApi->setAttribute(id, SaiPolicerTraits::Attributes::Cir{2000});
  policerApi->setAttribute(id, SaiPolicerTraits::Attributes::Cbs{200});
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cir{}), 2000);
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Cbs{}), 200);
  EXPECT_EQ(
      policerApi->getAttribute(id, SaiPolicerTraits::Attributes::Pbs{}), 0);
  EXPECT_THROW(
      policerApi->setAttribute(
          id, SaiPolicerTraits::Attributes::Type{SAI_METER_TYPE_BYTES}),
      SaiApiError);
  EXPECT_THROW(
      policerApi->setAttribute(
          id, SaiPolicerTraits::Attributes::Mode{SAI_POLICER_MODE_TR_TCM}),
      SaiApiError);
  policerApi->remove(id);
}

TEST_F(PolicerApiTest, enumeratePolicers) {
  auto first = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  auto second = policerApi->create<SaiPolicerTraits>(attributes(), 0);
  auto keys = getObjectKeys<SaiPolicerTraits>(0);
  EXPECT_NE(std::find(keys.begin(), keys.end(), first), keys.end());
  EXPECT_NE(std::find(keys.begin(), keys.end(), second), keys.end());
  policerApi->remove(first);
  policerApi->remove(second);
}
