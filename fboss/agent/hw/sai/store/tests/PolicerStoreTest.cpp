// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/api/PolicerApi.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class PolicerStoreTest : public SaiStoreTest {
 public:
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
};

TEST_F(PolicerStoreTest, createLoadAndUpdateByName) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto policer = store.setObject("mirror0", attributes());
  auto id = policer->adapterKey();
  EXPECT_EQ(policer->adapterHostKey(), "mirror0");
  EXPECT_EQ(policer->attributes(), attributes());
  auto loaded = createObj<SaiPolicerTraits>(id, "mirror0");
  EXPECT_EQ(loaded.adapterHostKey(), "mirror0");
  EXPECT_EQ(loaded.attributes(), attributes());
  loaded.release();

  auto updated = store.setObject("mirror0", attributes(2000, 200));
  EXPECT_EQ(updated->adapterKey(), id);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cir{}),
      2000);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          id, SaiPolicerTraits::Attributes::Cbs{}),
      200);
}

TEST_F(PolicerStoreTest, independentSameRateNamesSurviveSerializedReload) {
  auto& store = saiStore->get<SaiPolicerTraits>();
  auto first = store.setObject("mirror0", attributes());
  auto second = store.setObject("mirror1", attributes());
  auto firstId = first->adapterKey();
  auto secondId = second->adapterKey();
  EXPECT_NE(firstId, secondId);
  EXPECT_EQ(first->adapterHostKeyToFollyDynamic().asString(), "mirror0");
  EXPECT_EQ(
      SaiObject<SaiPolicerTraits>::follyDynamicToAdapterHostKey(
          second->adapterHostKeyToFollyDynamic()),
      "mirror1");
  auto keys = saiStore->adapterKeysFollyDynamic();
  auto names = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  saiStore->exitForWarmBoot();
  SaiStore reloaded(0);
  reloaded.reload(&keys, &names);
  auto& recovered = reloaded.get<SaiPolicerTraits>();
  ASSERT_NE(recovered.get("mirror0"), nullptr);
  ASSERT_NE(recovered.get("mirror1"), nullptr);
  EXPECT_EQ(recovered.get("mirror0")->adapterKey(), firstId);
  EXPECT_EQ(recovered.get("mirror1")->adapterKey(), secondId);
  auto updated = recovered.setObject("mirror0", attributes(2000, 200));
  EXPECT_EQ(updated->adapterKey(), firstId);
  EXPECT_EQ(
      saiApiTable->policerApi().getAttribute(
          secondId, SaiPolicerTraits::Attributes::Cir{}),
      1000);
}
