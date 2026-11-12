// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/api/TunnelMapApi.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

class TunnelMapApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    tunnelApi = std::make_unique<TunnelApi>();
  }

  TunnelMapSaiId createTunnelMap(sai_tunnel_map_type_t _type) {
    SaiTunnelMapTraits::Attributes::Type type{_type};
    SaiTunnelMapTraits::CreateAttributes a{type};
    return tunnelApi->create<SaiTunnelMapTraits>(a, 0);
  }

  TunnelMapEntrySaiId
  createVniToVrEntry(TunnelMapSaiId map, sai_uint32_t vni, sai_object_id_t vr) {
    using Attributes = SaiVniToVrTunnelMapEntryTraits::Attributes;
    Attributes::TunnelMapType type{
        SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID};
    Attributes::TunnelMap tunnelMap{map};
    Attributes::VniIdKey vniKey{vni};
    Attributes::VirtualRouterIdValue vrValue{vr};
    SaiVniToVrTunnelMapEntryTraits::CreateAttributes a{
        type, tunnelMap, vniKey, vrValue};
    return tunnelApi->create<SaiVniToVrTunnelMapEntryTraits>(a, 0);
  }

  TunnelMapEntrySaiId
  createVrToVniEntry(TunnelMapSaiId map, sai_object_id_t vr, sai_uint32_t vni) {
    using Attributes = SaiVrToVniTunnelMapEntryTraits::Attributes;
    Attributes::TunnelMapType type{
        SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI};
    Attributes::TunnelMap tunnelMap{map};
    Attributes::VirtualRouterIdKey vrKey{vr};
    Attributes::VniIdValue vniValue{vni};
    SaiVrToVniTunnelMapEntryTraits::CreateAttributes a{
        type, tunnelMap, vrKey, vniValue};
    return tunnelApi->create<SaiVrToVniTunnelMapEntryTraits>(a, 0);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<TunnelApi> tunnelApi;
};

TEST_F(TunnelMapApiTest, createTunnelMap) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  EXPECT_EQ(
      tunnelApi->getAttribute(mapId, SaiTunnelMapTraits::Attributes::Type{}),
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  EXPECT_EQ(
      fs->tunnelMapManager.get(mapId).type,
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
}

TEST_F(TunnelMapApiTest, removeTunnelMap) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  tunnelApi->remove(mapId);
  EXPECT_THROW(
      tunnelApi->getAttribute(mapId, SaiTunnelMapTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TunnelMapApiTest, createVniToVrEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  using Attributes = SaiVniToVrTunnelMapEntryTraits::Attributes;
  EXPECT_EQ(
      tunnelApi->getAttribute(entryId, Attributes::TunnelMapType{}),
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  EXPECT_EQ(tunnelApi->getAttribute(entryId, Attributes::TunnelMap{}), mapId);
  EXPECT_EQ(tunnelApi->getAttribute(entryId, Attributes::VniIdKey{}), 100);
  EXPECT_EQ(
      tunnelApi->getAttribute(entryId, Attributes::VirtualRouterIdValue{}), 42);
}

TEST_F(TunnelMapApiTest, createVrToVniEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  auto entryId = createVrToVniEntry(mapId, 42, 100);
  using Attributes = SaiVrToVniTunnelMapEntryTraits::Attributes;
  EXPECT_EQ(
      tunnelApi->getAttribute(entryId, Attributes::TunnelMapType{}),
      SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI);
  EXPECT_EQ(tunnelApi->getAttribute(entryId, Attributes::TunnelMap{}), mapId);
  EXPECT_EQ(
      tunnelApi->getAttribute(entryId, Attributes::VirtualRouterIdKey{}), 42);
  EXPECT_EQ(tunnelApi->getAttribute(entryId, Attributes::VniIdValue{}), 100);
}

TEST_F(TunnelMapApiTest, removeTunnelMapEntry) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  tunnelApi->remove(entryId);
  EXPECT_THROW(
      tunnelApi->getAttribute(
          entryId, SaiVniToVrTunnelMapEntryTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TunnelMapApiTest, setTunnelMapEntryAttributes) {
  auto mapId = createTunnelMap(SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID);
  auto entryId = createVniToVrEntry(mapId, 100, 42);
  using Attributes = SaiVniToVrTunnelMapEntryTraits::Attributes;
  tunnelApi->setAttribute(entryId, Attributes::VniIdKey{200});
  EXPECT_EQ(tunnelApi->getAttribute(entryId, Attributes::VniIdKey{}), 200);
  tunnelApi->setAttribute(entryId, Attributes::VirtualRouterIdValue{43});
  EXPECT_EQ(
      tunnelApi->getAttribute(entryId, Attributes::VirtualRouterIdValue{}), 43);
}
