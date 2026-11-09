// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class Srv6StoreTest : public SaiStoreTest {
 public:
  SaiSrv6SidListTraits::CreateAttributes createSrv6SidListAttrs() const {
    SaiSrv6SidListTraits::Attributes::Type type{
        SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED};
    return {type, std::nullopt, std::nullopt};
  }

  SaiSrv6SidListTraits::AdapterHostKey makeAdapterHostKey() const {
    return SaiSrv6SidListTraits::AdapterHostKey{
        SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED,
        std::nullopt,
        RouterInterfaceSaiId{kRifId},
        kNextHopIp};
  }

  static constexpr sai_object_id_t kRifId{42};
  static inline const folly::IPAddress kNextHopIp{"10.0.0.1"};
};

TEST_F(Srv6StoreTest, setObjectPreservesAdapterHostKey) {
  auto key = makeAdapterHostKey();
  auto attrs = createSrv6SidListAttrs();
  auto& store = saiStore->get<SaiSrv6SidListTraits>();
  auto obj = store.setObject(key, attrs);
  EXPECT_EQ(obj->adapterHostKey(), key);
}

TEST_F(Srv6StoreTest, setObjectTwiceReturnsSameObject) {
  auto key = makeAdapterHostKey();
  auto attrs = createSrv6SidListAttrs();
  auto& store = saiStore->get<SaiSrv6SidListTraits>();
  auto obj1 = store.setObject(key, attrs);
  auto obj2 = store.setObject(key, attrs);
  EXPECT_EQ(obj1->adapterKey(), obj2->adapterKey());
  EXPECT_EQ(obj1->adapterHostKey(), obj2->adapterHostKey());
}

TEST_F(Srv6StoreTest, srv6SidListCreateCtor) {
  auto key = makeAdapterHostKey();
  auto attrs = createSrv6SidListAttrs();
  SaiObject<SaiSrv6SidListTraits> obj =
      createObj<SaiSrv6SidListTraits>(key, attrs, 0);
  EXPECT_EQ(obj.adapterHostKey(), key);
  EXPECT_EQ(
      GET_ATTR(Srv6SidList, Type, obj.attributes()),
      SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
}

TEST_F(Srv6StoreTest, serDeserSrv6SidList) {
  auto key = makeAdapterHostKey();
  auto attrs = createSrv6SidListAttrs();
  auto& store = saiStore->get<SaiSrv6SidListTraits>();
  auto obj = store.setObject(key, attrs);
  auto sidListId = obj->adapterKey();
  // Verify adapter key appears in the serialized JSON
  auto json = saiStore->adapterKeysFollyDynamic();
  auto gotKeys = keysForSaiObjStoreFromStoreJson<SaiSrv6SidListTraits>(json);
  EXPECT_EQ(gotKeys.size(), 1);
  EXPECT_EQ(gotKeys[0], sidListId);
}

TEST_F(Srv6StoreTest, toStrSrv6SidList) {
  auto key = makeAdapterHostKey();
  auto attrs = createSrv6SidListAttrs();
  auto& store = saiStore->get<SaiSrv6SidListTraits>();
  store.setObject(key, attrs);
  auto str = fmt::format("{}", store);
  EXPECT_EQ(std::count(str.begin(), str.end(), '\n'), store.size() + 1);
}

// MySidEntry store tests

class MySidEntryStoreTest : public SaiStoreTest {
 public:
  SaiMySidEntryTraits::MySidEntry makeMySidEntryKey(
      const folly::IPAddressV6& sid = folly::IPAddressV6("2001:db8::1"),
      sai_object_id_t switchId = 0,
      sai_object_id_t vrId = 0,
      uint8_t locatorBlockLen = 48,
      uint8_t locatorNodeLen = 16,
      uint8_t functionLen = 20,
      uint8_t argsLen = 0) const {
    return SaiMySidEntryTraits::MySidEntry(
        switchId,
        vrId,
        locatorBlockLen,
        locatorNodeLen,
        functionLen,
        argsLen,
        sid);
  }

  SaiMySidEntryTraits::CreateAttributes makeMySidEntryAttrs(
      sai_int32_t behavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E) const {
    return SaiMySidEntryTraits::CreateAttributes{
        behavior,
        SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USP,
        SAI_NULL_OBJECT_ID,
        0,
        SAI_PACKET_ACTION_FORWARD};
  }
};

TEST_F(MySidEntryStoreTest, reloadMySidWithoutTunnelId) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs(SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA);
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);

  saiStore->reload();

  EXPECT_NE(saiStore->get<SaiMySidEntryTraits>().get(entry), nullptr);
}

TEST_F(MySidEntryStoreTest, createMySidEntry) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);

  saiStore->reload();
  auto& store = saiStore->get<SaiMySidEntryTraits>();

  auto got = store.get(entry);
  EXPECT_EQ(got->adapterKey(), entry);
  EXPECT_EQ(
      GET_ATTR(MySidEntry, EndpointBehavior, got->attributes()),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
}

TEST_F(MySidEntryStoreTest, modifyMySidEntry) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);
  srv6Api.setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::EndpointBehavior{
          SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X});

  saiStore->reload();
  auto& store = saiStore->get<SaiMySidEntryTraits>();

  auto got = store.get(entry);
  EXPECT_EQ(got->adapterKey(), entry);
  EXPECT_EQ(
      GET_ATTR(MySidEntry, EndpointBehavior, got->attributes()),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_X);
}

TEST_F(MySidEntryStoreTest, setMySidEntryPacketAction) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);
  srv6Api.setAttribute(
      entry,
      SaiMySidEntryTraits::Attributes::PacketAction{SAI_PACKET_ACTION_TRAP});

  saiStore->reload();
  auto& store = saiStore->get<SaiMySidEntryTraits>();

  auto got = store.get(entry);
  EXPECT_EQ(got->adapterKey(), entry);
  EXPECT_EQ(
      GET_ATTR(MySidEntry, PacketAction, got->attributes()),
      SAI_PACKET_ACTION_TRAP);
}

TEST_F(MySidEntryStoreTest, mySidEntrySerDeser) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);
  verifyAdapterKeySerDeser<SaiMySidEntryTraits>({entry});
}

TEST_F(MySidEntryStoreTest, mySidEntryToStr) {
  auto& srv6Api = saiApiTable->srv6Api();
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  srv6Api.create<SaiMySidEntryTraits>(entry, attrs);
  verifyToStr<SaiMySidEntryTraits>();
}

TEST_F(MySidEntryStoreTest, setObjectMySidEntry) {
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  auto& store = saiStore->get<SaiMySidEntryTraits>();
  auto obj = store.setObject(entry, attrs);
  EXPECT_EQ(obj->adapterHostKey(), entry);
  EXPECT_EQ(
      GET_ATTR(MySidEntry, EndpointBehavior, obj->attributes()),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
}

TEST_F(MySidEntryStoreTest, setObjectTwiceReturnsSameObject) {
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  auto& store = saiStore->get<SaiMySidEntryTraits>();
  auto obj1 = store.setObject(entry, attrs);
  auto obj2 = store.setObject(entry, attrs);
  EXPECT_EQ(obj1->adapterKey(), obj2->adapterKey());
  EXPECT_EQ(obj1->adapterHostKey(), obj2->adapterHostKey());
}

TEST_F(MySidEntryStoreTest, mySidEntryCreateCtor) {
  auto entry = makeMySidEntryKey();
  auto attrs = makeMySidEntryAttrs();
  SaiObject<SaiMySidEntryTraits> obj =
      createObj<SaiMySidEntryTraits>(entry, attrs, 0);
  EXPECT_EQ(obj.adapterHostKey(), entry);
  EXPECT_EQ(
      GET_ATTR(MySidEntry, EndpointBehavior, obj.attributes()),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_E);
}
