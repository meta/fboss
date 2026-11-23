// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/api/TunnelMapApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

class TunnelMapStoreTest : public SaiStoreTest {
 public:
  SaiTunnelMapTraits::CreateAttributes createTunnelMapAttrs(
      sai_tunnel_map_type_t type) const {
    SaiTunnelMapTraits::Attributes::Type t{type};
    return {t};
  }
  TunnelMapSaiId createTunnelMap(sai_tunnel_map_type_t type) {
    return saiApiTable->tunnelApi().create<SaiTunnelMapTraits>(
        createTunnelMapAttrs(type), 0);
  }

  SaiVniToVrTunnelMapEntryTraits::CreateAttributes createVniToVrAttrs(
      TunnelMapSaiId map,
      sai_uint32_t vni,
      sai_object_id_t vr) const {
    using Attributes = SaiVniToVrTunnelMapEntryTraits::Attributes;
    Attributes::TunnelMapType type{
        SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID};
    Attributes::TunnelMap tunnelMap{map};
    Attributes::VniIdKey vniKey{vni};
    Attributes::VirtualRouterIdValue vrValue{vr};
    return {type, tunnelMap, vniKey, vrValue};
  }
  TunnelMapEntrySaiId
  createVniToVrEntry(TunnelMapSaiId map, sai_uint32_t vni, sai_object_id_t vr) {
    return saiApiTable->tunnelApi().create<SaiVniToVrTunnelMapEntryTraits>(
        createVniToVrAttrs(map, vni, vr), 0);
  }

  SaiVrToVniTunnelMapEntryTraits::CreateAttributes createVrToVniAttrs(
      TunnelMapSaiId map,
      sai_object_id_t vr,
      sai_uint32_t vni) const {
    using Attributes = SaiVrToVniTunnelMapEntryTraits::Attributes;
    Attributes::TunnelMapType type{
        SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI};
    Attributes::TunnelMap tunnelMap{map};
    Attributes::VirtualRouterIdKey vrKey{vr};
    Attributes::VniIdValue vniValue{vni};
    return {type, tunnelMap, vrKey, vniValue};
  }
  TunnelMapEntrySaiId
  createVrToVniEntry(TunnelMapSaiId map, sai_object_id_t vr, sai_uint32_t vni) {
    return saiApiTable->tunnelApi().create<SaiVrToVniTunnelMapEntryTraits>(
        createVrToVniAttrs(map, vr, vni), 0);
  }
};

TEST_F(TunnelMapStoreTest, loadTunnelMap) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiTunnelMapTraits>();
  auto got = store.get(
      createTunnelMapAttrs(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID));
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), mapId);
  EXPECT_EQ(
      GET_ATTR(TunnelMap, Type, got->attributes()),
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
}

TEST_F(TunnelMapStoreTest, loadVniToVrEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVniToVrTunnelMapEntryTraits>();
  auto got = store.get(createVniToVrAttrs(mapId, 100, 42));
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), entryId);
  EXPECT_EQ(GET_ATTR(VniToVrTunnelMapEntry, VniIdKey, got->attributes()), 100);
  EXPECT_EQ(
      GET_ATTR(VniToVrTunnelMapEntry, VirtualRouterIdValue, got->attributes()),
      42);
  EXPECT_EQ(
      GET_ATTR(VniToVrTunnelMapEntry, TunnelMap, got->attributes()), mapId);
}

TEST_F(TunnelMapStoreTest, loadVrToVniEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  auto entryId = createVrToVniEntry(mapId, 42, 100);
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiVrToVniTunnelMapEntryTraits>();
  auto got = store.get(createVrToVniAttrs(mapId, 42, 100));
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), entryId);
  EXPECT_EQ(
      GET_ATTR(VrToVniTunnelMapEntry, VirtualRouterIdKey, got->attributes()),
      42);
  EXPECT_EQ(
      GET_ATTR(VrToVniTunnelMapEntry, VniIdValue, got->attributes()), 100);
  EXPECT_EQ(
      GET_ATTR(VrToVniTunnelMapEntry, TunnelMap, got->attributes()), mapId);
}

TEST_F(TunnelMapStoreTest, entriesArePartitionedByDirection) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  std::ignore = createVniToVrEntry(mapId, 100, 42);
  SaiStore s(0);
  s.reload();
  // The VNI->VR entry must not show up in the VR->VNI store and vice versa.
  auto& vrToVniStore = s.get<SaiVrToVniTunnelMapEntryTraits>();
  EXPECT_EQ(vrToVniStore.get(createVrToVniAttrs(mapId, 42, 100)), nullptr);
  auto& vniToVrStore = s.get<SaiVniToVrTunnelMapEntryTraits>();
  EXPECT_NE(vniToVrStore.get(createVniToVrAttrs(mapId, 100, 42)), nullptr);
}

TEST_F(TunnelMapStoreTest, mapLoadCtor) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  SaiObject<SaiTunnelMapTraits> obj = createObj<SaiTunnelMapTraits>(mapId);
  EXPECT_EQ(obj.adapterKey(), mapId);
  EXPECT_EQ(
      GET_ATTR(TunnelMap, Type, obj.attributes()),
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
}

TEST_F(TunnelMapStoreTest, vniToVrLoadCtor) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  SaiObject<SaiVniToVrTunnelMapEntryTraits> obj =
      createObj<SaiVniToVrTunnelMapEntryTraits>(entryId);
  EXPECT_EQ(obj.adapterKey(), entryId);
  EXPECT_EQ(GET_ATTR(VniToVrTunnelMapEntry, VniIdKey, obj.attributes()), 100);
}

TEST_F(TunnelMapStoreTest, vrToVniLoadCtor) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  auto entryId = createVrToVniEntry(mapId, 42, 100);
  SaiObject<SaiVrToVniTunnelMapEntryTraits> obj =
      createObj<SaiVrToVniTunnelMapEntryTraits>(entryId);
  EXPECT_EQ(obj.adapterKey(), entryId);
  EXPECT_EQ(GET_ATTR(VrToVniTunnelMapEntry, VniIdValue, obj.attributes()), 100);
}

TEST_F(TunnelMapStoreTest, mapCreateCtor) {
  SaiTunnelMapTraits::AdapterHostKey k{
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID};
  SaiObject<SaiTunnelMapTraits> obj = createObj<SaiTunnelMapTraits>(k, k, 0);
  EXPECT_EQ(
      GET_ATTR(TunnelMap, Type, obj.attributes()),
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
}

TEST_F(TunnelMapStoreTest, vniToVrCreateCtor) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  SaiVniToVrTunnelMapEntryTraits::AdapterHostKey k{
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID, mapId, 100, 42};
  SaiObject<SaiVniToVrTunnelMapEntryTraits> obj =
      createObj<SaiVniToVrTunnelMapEntryTraits>(k, k, 0);
  EXPECT_EQ(GET_ATTR(VniToVrTunnelMapEntry, VniIdKey, obj.attributes()), 100);
  EXPECT_EQ(
      GET_ATTR(VniToVrTunnelMapEntry, VirtualRouterIdValue, obj.attributes()),
      42);
}

TEST_F(TunnelMapStoreTest, vrToVniCreateCtor) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  SaiVrToVniTunnelMapEntryTraits::AdapterHostKey k{
      SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI, mapId, 42, 100};
  SaiObject<SaiVrToVniTunnelMapEntryTraits> obj =
      createObj<SaiVrToVniTunnelMapEntryTraits>(k, k, 0);
  EXPECT_EQ(
      GET_ATTR(VrToVniTunnelMapEntry, VirtualRouterIdKey, obj.attributes()),
      42);
  EXPECT_EQ(GET_ATTR(VrToVniTunnelMapEntry, VniIdValue, obj.attributes()), 100);
}

TEST_F(TunnelMapStoreTest, serDeserTunnelMap) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  verifyAdapterKeySerDeser<SaiTunnelMapTraits>({mapId});
}

TEST_F(TunnelMapStoreTest, serDeserVniToVrEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  verifyAdapterKeySerDeser<SaiVniToVrTunnelMapEntryTraits>({entryId});
}

TEST_F(TunnelMapStoreTest, serDeserVrToVniEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  auto entryId = createVrToVniEntry(mapId, 42, 100);
  verifyAdapterKeySerDeser<SaiVrToVniTunnelMapEntryTraits>({entryId});
}

TEST_F(TunnelMapStoreTest, toStrTunnelMap) {
  std::ignore = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  verifyToStr<SaiTunnelMapTraits>();
}

TEST_F(TunnelMapStoreTest, toStrVniToVrEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  std::ignore = createVniToVrEntry(mapId, 100, 42);
  verifyToStr<SaiVniToVrTunnelMapEntryTraits>();
}

TEST_F(TunnelMapStoreTest, toStrVrToVniEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  std::ignore = createVrToVniEntry(mapId, 42, 100);
  verifyToStr<SaiVrToVniTunnelMapEntryTraits>();
}
