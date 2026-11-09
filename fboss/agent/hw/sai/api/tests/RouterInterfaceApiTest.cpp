/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class RouterInterfaceApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    routerInterfaceApi = std::make_unique<RouterInterfaceApi>();
  }
  RouterInterfaceSaiId createRouterInterface(
      sai_object_id_t vr = 42,
      sai_object_id_t vlan = 43) {
    SaiVlanRouterInterfaceTraits::Attributes::Type typeAttribute(
        SAI_ROUTER_INTERFACE_TYPE_VLAN);
    SaiVlanRouterInterfaceTraits::Attributes::VirtualRouterId
        virtualRouterIdAttribute(vr);
    SaiVlanRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute(vlan);
    auto rifId = routerInterfaceApi->create<SaiVlanRouterInterfaceTraits>(
        {virtualRouterIdAttribute,
         typeAttribute,
         vlanIdAttribute,
         std::nullopt,
         std::nullopt
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
         ,
         std::nullopt
#endif
        },
        0);
    EXPECT_EQ(rifId, fs->routeInterfaceManager.get(rifId).id);
    EXPECT_EQ(vr, fs->routeInterfaceManager.get(rifId).virtualRouterId);
    EXPECT_EQ(vlan, fs->routeInterfaceManager.get(rifId).portOrVlanId);
    return rifId;
  }

  RouterInterfaceSaiId createMplsRouterInterface(sai_object_id_t vr = 42) {
    SaiMplsRouterInterfaceTraits::Attributes::Type typeAttribute(
        SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER);
    SaiMplsRouterInterfaceTraits::Attributes::VirtualRouterId
        virtualRouterIdAttribute(vr);
    auto rifId = routerInterfaceApi->create<SaiMplsRouterInterfaceTraits>(
        {virtualRouterIdAttribute, typeAttribute}, 0);
    EXPECT_EQ(rifId, fs->routeInterfaceManager.get(rifId).id);
    EXPECT_EQ(vr, fs->routeInterfaceManager.get(rifId).virtualRouterId);
    return rifId;
  }
  RouterInterfaceSaiId createPortRouterInterface(
      sai_object_id_t vr = 42,
      sai_object_id_t port = 43) {
    SaiPortRouterInterfaceTraits::Attributes::Type typeAttribute(
        SAI_ROUTER_INTERFACE_TYPE_PORT);
    SaiPortRouterInterfaceTraits::Attributes::VirtualRouterId
        virtualRouterIdAttribute(vr);
    SaiPortRouterInterfaceTraits::Attributes::PortId portIdAttribute(port);
    auto rifId = routerInterfaceApi->create<SaiPortRouterInterfaceTraits>(
        {virtualRouterIdAttribute,
         typeAttribute,
         portIdAttribute,
         std::nullopt,
         std::nullopt
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
         ,
         std::nullopt
#endif
        },
        0);
    EXPECT_EQ(rifId, fs->routeInterfaceManager.get(rifId).id);
    EXPECT_EQ(vr, fs->routeInterfaceManager.get(rifId).virtualRouterId);
    EXPECT_EQ(port, fs->routeInterfaceManager.get(rifId).portOrVlanId);
    return rifId;
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<RouterInterfaceApi> routerInterfaceApi;
  sai_object_id_t switchId;
};

TEST_F(RouterInterfaceApiTest, create) {
  createRouterInterface();
}

TEST_F(RouterInterfaceApiTest, setSrcMac) {
  auto rifId = createRouterInterface();
  folly::MacAddress mac("42:42:42:42:42:42");
  SaiVlanRouterInterfaceTraits::Attributes::SrcMac ma(mac);
  SaiVlanRouterInterfaceTraits::Attributes::SrcMac ma2;
  EXPECT_NE(mac, routerInterfaceApi->getAttribute(rifId, ma2));
  routerInterfaceApi->setAttribute(rifId, ma);
  EXPECT_EQ(mac, routerInterfaceApi->getAttribute(rifId, ma2));
}

TEST_F(RouterInterfaceApiTest, setVrId) {
  auto rifId = createRouterInterface();
  SaiVlanRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute(10);
  SaiVlanRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute2;
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, virtualRouterIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setVlanId) {
  auto rifId = createRouterInterface();
  SaiVlanRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute(10);
  SaiVlanRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute2;
  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, vlanIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, vlanIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);

  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, vlanIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setMtu) {
  auto rifId = createRouterInterface();
  sai_uint32_t mtu{9000};
  SaiVlanRouterInterfaceTraits::Attributes::Mtu mtu1{mtu};
  SaiVlanRouterInterfaceTraits::Attributes::Mtu mtu2;
  EXPECT_NE(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
  EXPECT_EQ(1514 /*default*/, routerInterfaceApi->getAttribute(rifId, mtu2));
  routerInterfaceApi->setAttribute(rifId, mtu1);
  EXPECT_EQ(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
}

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
TEST_F(RouterInterfaceApiTest, setAdminMplsState) {
  auto rifId = createRouterInterface();
  SaiVlanRouterInterfaceTraits::Attributes::AdminMplsState getMplsState;
  EXPECT_FALSE(routerInterfaceApi->getAttribute(rifId, getMplsState));
  routerInterfaceApi->setAttribute(
      rifId, SaiVlanRouterInterfaceTraits::Attributes::AdminMplsState{true});
  EXPECT_TRUE(routerInterfaceApi->getAttribute(rifId, getMplsState));
}
#endif

TEST_F(RouterInterfaceApiTest, formatRouterInterfaceAttributes) {
  std::string macStr{"42:42:42:42:42:42"};
  SaiVlanRouterInterfaceTraits::Attributes::SrcMac sm{
      folly::MacAddress{macStr}};
  EXPECT_EQ(fmt::format("SrcMac: {}", macStr), fmt::format("{}", sm));
  SaiVlanRouterInterfaceTraits::Attributes::Type t{
      SAI_ROUTER_INTERFACE_TYPE_VLAN};
  EXPECT_EQ("Type: 1", fmt::format("{}", t));
  SaiVlanRouterInterfaceTraits::Attributes::VirtualRouterId vrid{0};
  EXPECT_EQ("VirtualRouterId: 0", fmt::format("{}", vrid));
  SaiVlanRouterInterfaceTraits::Attributes::VlanId vid{42};
  EXPECT_EQ("VlanId: 42", fmt::format("{}", vid));
  SaiVlanRouterInterfaceTraits::Attributes::Mtu m{9000};
  EXPECT_EQ("Mtu: 9000", fmt::format("{}", m));
}

TEST_F(RouterInterfaceApiTest, mplsRouterInterface) {
  auto rifId = createMplsRouterInterface();
  auto intf = fs->routeInterfaceManager.get(rifId);
  EXPECT_EQ(intf.type, SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER);
}

TEST_F(RouterInterfaceApiTest, createPortRif) {
  createPortRouterInterface();
}

TEST_F(RouterInterfaceApiTest, setSrcMacPortRif) {
  auto rifId = createPortRouterInterface();
  folly::MacAddress mac("42:42:42:42:42:42");
  SaiPortRouterInterfaceTraits::Attributes::SrcMac ma(mac);
  SaiPortRouterInterfaceTraits::Attributes::SrcMac ma2;
  EXPECT_NE(mac, routerInterfaceApi->getAttribute(rifId, ma2));
  routerInterfaceApi->setAttribute(rifId, ma);
  EXPECT_EQ(mac, routerInterfaceApi->getAttribute(rifId, ma2));
}

TEST_F(RouterInterfaceApiTest, setVrIdPortRif) {
  auto rifId = createPortRouterInterface();
  SaiPortRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute(10);
  SaiPortRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute2;
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, virtualRouterIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);
  EXPECT_EQ(
      42, routerInterfaceApi->getAttribute(rifId, virtualRouterIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setPortId) {
  auto rifId = createPortRouterInterface();
  SaiPortRouterInterfaceTraits::Attributes::PortId portIdAttribute(10);
  SaiPortRouterInterfaceTraits::Attributes::PortId portIdAttribute2;
  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, portIdAttribute2));
  EXPECT_THROW(
      try {
        routerInterfaceApi->setAttribute(rifId, portIdAttribute);
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);

  EXPECT_EQ(43, routerInterfaceApi->getAttribute(rifId, portIdAttribute2));
}

TEST_F(RouterInterfaceApiTest, setMtuPortRif) {
  auto rifId = createPortRouterInterface();
  sai_uint32_t mtu{9000};
  SaiPortRouterInterfaceTraits::Attributes::Mtu mtu1{mtu};
  SaiPortRouterInterfaceTraits::Attributes::Mtu mtu2;
  EXPECT_NE(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
  EXPECT_EQ(1514 /*default*/, routerInterfaceApi->getAttribute(rifId, mtu2));
  routerInterfaceApi->setAttribute(rifId, mtu1);
  EXPECT_EQ(mtu, routerInterfaceApi->getAttribute(rifId, mtu2));
}

TEST_F(RouterInterfaceApiTest, formatPortRouterInterfaceAttributes) {
  std::string macStr{"42:42:42:42:42:42"};
  SaiPortRouterInterfaceTraits::Attributes::SrcMac sm{
      folly::MacAddress{macStr}};
  EXPECT_EQ(fmt::format("SrcMac: {}", macStr), fmt::format("{}", sm));
  SaiPortRouterInterfaceTraits::Attributes::Type t{
      SAI_ROUTER_INTERFACE_TYPE_VLAN};
  EXPECT_EQ("Type: 1", fmt::format("{}", t));
  SaiPortRouterInterfaceTraits::Attributes::VirtualRouterId vrid{0};
  EXPECT_EQ("VirtualRouterId: 0", fmt::format("{}", vrid));
  SaiPortRouterInterfaceTraits::Attributes::PortId pid{42};
  EXPECT_EQ("PortId: 42", fmt::format("{}", pid));
  SaiPortRouterInterfaceTraits::Attributes::Mtu m{9000};
  EXPECT_EQ("Mtu: 9000", fmt::format("{}", m));
}
