// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/api/VirtualRouterApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class VirtualRouterStoreTest : public SaiStoreTest {};

TEST_F(VirtualRouterStoreTest, defaultVirtualRouterReloadedAsRouterIdZero) {
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualRouterTraits>();
  auto defaultVr = store.get(RouterID(0));
  ASSERT_NE(defaultVr, nullptr);

  auto loaded =
      store.loadObjectOwnedByAdapter(defaultVr->adapterKey(), RouterID(0));
  EXPECT_EQ(loaded, defaultVr);
  EXPECT_TRUE(loaded->isOwnedByAdapter());
}

TEST_F(VirtualRouterStoreTest, warmBootRoundTrip) {
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualRouterTraits>();
  auto defaultVrId = store.get(RouterID(0))->adapterKey();
  auto vr = store.setObject(RouterID(1001), {});
  EXPECT_NE(vr->adapterKey(), defaultVrId);

  auto adapterKeys = s.adapterKeysFollyDynamic();
  auto adapterKeys2AdapterHostKeys =
      s.adapterKeys2AdapterHostKeysFollyDynamic();
  SaiStore s1(0);
  s1.reload(&adapterKeys, &adapterKeys2AdapterHostKeys);
  auto& store1 = s1.get<SaiVirtualRouterTraits>();

  EXPECT_EQ(store1.get(RouterID(0))->adapterKey(), defaultVrId);
  EXPECT_EQ(store1.get(RouterID(1001))->adapterKey(), vr->adapterKey());
}

TEST_F(VirtualRouterStoreTest, warmBootFromStateWithoutAdapterHostKeys) {
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualRouterTraits>();
  auto defaultVrId = store.get(RouterID(0))->adapterKey();

  auto adapterKeys = s.adapterKeysFollyDynamic();
  auto adapterKeys2AdapterHostKeys =
      s.adapterKeys2AdapterHostKeysFollyDynamic();
  adapterKeys2AdapterHostKeys.erase(store.objectTypeName());
  SaiStore s1(0);
  s1.reload(&adapterKeys, &adapterKeys2AdapterHostKeys);

  EXPECT_EQ(
      s1.get<SaiVirtualRouterTraits>().get(RouterID(0))->adapterKey(),
      defaultVrId);
}

TEST_F(VirtualRouterStoreTest, adapterHostKeyToAndFromDynamic) {
  SaiStore s(0);
  auto obj = s.get<SaiVirtualRouterTraits>().setObject(RouterID(1001), {});

  auto json = obj->adapterHostKeyToFollyDynamic();
  EXPECT_EQ(
      SaiObject<SaiVirtualRouterTraits>::follyDynamicToAdapterHostKey(json),
      RouterID(1001));
}

TEST_F(VirtualRouterStoreTest, serDeserVirtualRouterStore) {
  SaiStore s(0);
  s.reload();
  auto vr = s.get<SaiVirtualRouterTraits>().setObject(RouterID(1001), {});

  auto keys = keysForSaiObjStoreFromStoreJson<SaiVirtualRouterTraits>(
      s.adapterKeysFollyDynamic());
  EXPECT_EQ(keys.size(), 2);
  EXPECT_NE(std::find(keys.begin(), keys.end(), vr->adapterKey()), keys.end());
}

TEST_F(VirtualRouterStoreTest, toStrVirtualRouterStore) {
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVirtualRouterTraits>();
  store.setObject(RouterID(1001), {});
  auto str = fmt::format("{}", store);
  EXPECT_EQ(std::count(str.begin(), str.end(), '\n'), store.size() + 1);
}
