/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/LagApi.h"
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/LoggingUtil.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class RouterInterfaceStoreTest : public SaiStoreTest {
 public:
  RouterInterfaceSaiId createRouterInterface(
      sai_object_id_t vlanId,
      const folly::MacAddress& mac,
      sai_uint32_t mtu) {
    return saiApiTable->routerInterfaceApi()
        .create<SaiVlanRouterInterfaceTraits>(
            {0,
             SAI_ROUTER_INTERFACE_TYPE_VLAN,
             vlanId,
             mac,
             mtu
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
             ,
             std::nullopt
#endif
            },
            0);
  }
  RouterInterfaceSaiId createPortRouterInterface(
      sai_object_id_t portId,
      const folly::MacAddress& mac,
      sai_uint32_t mtu) {
    return saiApiTable->routerInterfaceApi()
        .create<SaiPortRouterInterfaceTraits>(
            {0,
             SAI_ROUTER_INTERFACE_TYPE_PORT,
             portId,
             mac,
             mtu
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
             ,
             std::nullopt
#endif
            },
            0);
  }

  LagSaiId createLag(const std::string& label) {
    std::array<char, 32> labelValue{};
    std::copy(label.begin(), label.end(), labelValue.data());
    return saiApiTable->lagApi().create<SaiLagTraits>(
        SaiLagTraits::CreateAttributes{labelValue, std::nullopt}, 0);
  }

  RouterInterfaceSaiId createMplsRouterInterface() {
    return saiApiTable->routerInterfaceApi()
        .create<SaiMplsRouterInterfaceTraits>(
            {0, SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER}, 0);
  }
};

TEST_F(RouterInterfaceStoreTest, loadRouterInterfaces) {
  auto srcMac1 = folly::MacAddress{"41:41:41:41:41:41"};
  auto srcMac2 = folly::MacAddress{"42:42:42:42:42:42"};
  auto routerInterfaceSaiId1 = createRouterInterface(41, srcMac1, 1514);
  auto routerInterfaceSaiId2 = createRouterInterface(42, srcMac2, 9000);
  auto portRouterInterfaceSaiId1 = createPortRouterInterface(41, srcMac1, 1514);
  auto portRouterInterfaceSaiId2 = createPortRouterInterface(42, srcMac2, 9000);
  auto mplsRouterInterfaceSaiId = createMplsRouterInterface();

  SaiStore s(0);
  s.reload();
  {
    auto& store = s.get<SaiVlanRouterInterfaceTraits>();

    SaiVlanRouterInterfaceTraits::AdapterHostKey k1{0, 41};
    SaiVlanRouterInterfaceTraits::AdapterHostKey k2{0, 42};
    auto got = store.get(k1);
    EXPECT_EQ(got->adapterKey(), routerInterfaceSaiId1);
    got = store.get(k2);
    EXPECT_EQ(got->adapterKey(), routerInterfaceSaiId2);
  }
  {
    auto& store = s.get<SaiPortRouterInterfaceTraits>();

    SaiPortRouterInterfaceTraits::AdapterHostKey k1{0, 41};
    SaiPortRouterInterfaceTraits::AdapterHostKey k2{0, 42};
    auto got = store.get(k1);
    EXPECT_EQ(got->adapterKey(), portRouterInterfaceSaiId1);
    got = store.get(k2);
    EXPECT_EQ(got->adapterKey(), portRouterInterfaceSaiId2);
  }
  {
    auto& store = s.get<SaiMplsRouterInterfaceTraits>();
    auto got1 = store.get(SaiMplsRouterInterfaceTraits::AdapterHostKey{0});
    EXPECT_EQ(got1->adapterKey(), mplsRouterInterfaceSaiId);
  }
}

TEST_F(RouterInterfaceStoreTest, routerInterfaceLoadCtor) {
  folly::MacAddress srcMac{"41:41:41:41:41:41"};
  {
    auto routerInterfaceSaiId = createRouterInterface(41, srcMac, 9000);

    auto obj = createObj<SaiVlanRouterInterfaceTraits>(routerInterfaceSaiId);
    EXPECT_EQ(obj.adapterKey(), routerInterfaceSaiId);
    EXPECT_EQ(GET_ATTR(VlanRouterInterface, VlanId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(VlanRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(VlanRouterInterface, Mtu, obj.attributes()), 9000);
  }
  {
    auto portRouterInterfaceSaiId = createPortRouterInterface(41, srcMac, 9000);

    auto obj =
        createObj<SaiPortRouterInterfaceTraits>(portRouterInterfaceSaiId);
    EXPECT_EQ(obj.adapterKey(), portRouterInterfaceSaiId);
    EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(PortRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, obj.attributes()), 9000);
  }
  {
    auto mplsRouterInterfaceSaiId = createMplsRouterInterface();
    auto mplsObj =
        createObj<SaiMplsRouterInterfaceTraits>(mplsRouterInterfaceSaiId);
    EXPECT_EQ(mplsObj.adapterKey(), mplsRouterInterfaceSaiId);
    EXPECT_EQ(
        GET_ATTR(MplsRouterInterface, Type, mplsObj.attributes()),
        SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER);
    EXPECT_EQ(
        GET_ATTR(MplsRouterInterface, VirtualRouterId, mplsObj.attributes()),
        0);
  }
}

TEST_F(RouterInterfaceStoreTest, routerInterfaceCreateCtor) {
  folly::MacAddress srcMac{"41:41:41:41:41:41"};
  {
    SaiVlanRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiVlanRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_VLAN,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };

    auto obj = createObj<SaiVlanRouterInterfaceTraits>(k, c, 0);
    EXPECT_EQ(GET_ATTR(VlanRouterInterface, VlanId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(VlanRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(VlanRouterInterface, Mtu, obj.attributes()), 9000);
  }
  {
    SaiPortRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiPortRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_PORT,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };

    auto obj = createObj<SaiPortRouterInterfaceTraits>(k, c, 0);
    EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(PortRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, obj.attributes()), 9000);
  }
  {
    SaiMplsRouterInterfaceTraits::AdapterHostKey mplsIntfAhk{0};
    SaiMplsRouterInterfaceTraits::CreateAttributes mplsIntfCreateAttr{
        0, SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER};
    auto mplsObj = createObj<SaiMplsRouterInterfaceTraits>(
        mplsIntfAhk, mplsIntfCreateAttr, 0);
    EXPECT_EQ(
        GET_ATTR(MplsRouterInterface, Type, mplsObj.attributes()),
        SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER);
    EXPECT_EQ(
        GET_ATTR(MplsRouterInterface, VirtualRouterId, mplsObj.attributes()),
        0);
  }
}

TEST_F(RouterInterfaceStoreTest, routerInterfaceSetSrcMac) {
  folly::MacAddress srcMac{"41:41:41:41:41:41"};
  {
    SaiVlanRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiVlanRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_VLAN,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };

    auto obj = createObj<SaiVlanRouterInterfaceTraits>(k, c, 0);
    EXPECT_EQ(GET_ATTR(VlanRouterInterface, VlanId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(VlanRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(VlanRouterInterface, Mtu, obj.attributes()), 9000);

    folly::MacAddress srcMac2{"42:42:42:42:42:42"};
    SaiVlanRouterInterfaceTraits::CreateAttributes newAttrs{
        0,
        SAI_ROUTER_INTERFACE_TYPE_VLAN,
        41,
        srcMac2,
        1514
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };
    obj.setAttributes(newAttrs);
    EXPECT_EQ(GET_ATTR(VlanRouterInterface, VlanId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(VlanRouterInterface, SrcMac, obj.attributes()), srcMac2);
    EXPECT_EQ(GET_OPT_ATTR(VlanRouterInterface, Mtu, obj.attributes()), 1514);
  }
  {
    SaiPortRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiPortRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_PORT,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };

    auto obj = createObj<SaiPortRouterInterfaceTraits>(k, c, 0);
    EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(PortRouterInterface, SrcMac, obj.attributes()), srcMac);
    EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, obj.attributes()), 9000);

    folly::MacAddress srcMac2{"42:42:42:42:42:42"};
    SaiPortRouterInterfaceTraits::CreateAttributes newAttrs{
        0,
        SAI_ROUTER_INTERFACE_TYPE_PORT,
        41,
        srcMac2,
        1514
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };
    obj.setAttributes(newAttrs);
    EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, obj.attributes()), 41);
    EXPECT_EQ(
        GET_OPT_ATTR(PortRouterInterface, SrcMac, obj.attributes()), srcMac2);
    EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, obj.attributes()), 1514);
  }
}

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
TEST_F(RouterInterfaceStoreTest, routerInterfaceAdminMplsState) {
  folly::MacAddress srcMac{"41:41:41:41:41:41"};
  SaiVlanRouterInterfaceTraits::AdapterHostKey k{0, 41};
  SaiVlanRouterInterfaceTraits::CreateAttributes c{
      0, SAI_ROUTER_INTERFACE_TYPE_VLAN, 41, srcMac, 9000, true};

  auto obj = createObj<SaiVlanRouterInterfaceTraits>(k, c, 0);
  EXPECT_EQ(
      GET_OPT_ATTR(VlanRouterInterface, AdminMplsState, obj.attributes()),
      true);
  auto apiAdminMplsState = saiApiTable->routerInterfaceApi().getAttribute(
      obj.adapterKey(),
      SaiVlanRouterInterfaceTraits::Attributes::AdminMplsState{});
  EXPECT_TRUE(apiAdminMplsState);
}
#endif

TEST_F(RouterInterfaceStoreTest, serDeserTest) {
  auto routerInterfaceSaiId =
      createRouterInterface(41, folly::MacAddress{"41:41:41:41:41:41"}, 1514);

  verifyAdapterKeySerDeser<SaiVlanRouterInterfaceTraits>(
      {routerInterfaceSaiId});

  auto portRouterInterfaceSaiId = createPortRouterInterface(
      41, folly::MacAddress{"41:41:41:41:41:41"}, 1514);

  verifyAdapterKeySerDeser<SaiPortRouterInterfaceTraits>(
      {portRouterInterfaceSaiId}, true);

  auto mplsRouterInterfaceSaiId = createMplsRouterInterface();
  verifyAdapterKeySerDeser<SaiMplsRouterInterfaceTraits>(
      {mplsRouterInterfaceSaiId}, true);
}

TEST_F(RouterInterfaceStoreTest, routerFormatTest) {
  folly::MacAddress srcMac{"41:41:41:41:41:41"};
  {
    SaiVlanRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiVlanRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_VLAN,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };
    auto obj = createObj<SaiVlanRouterInterfaceTraits>(k, c, 0);
    std::string expected =
        "RouterInterfaceSaiId(0): "
        "(VirtualRouterId: 0, Type: 1, VlanId: 41, "
        "SrcMac: 41:41:41:41:41:41, Mtu: 9000"
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ", nullopt"
#endif
        ")";
    EXPECT_EQ(expected, fmt::format("{}", obj));
  }
  {
    SaiPortRouterInterfaceTraits::AdapterHostKey k{0, 41};
    SaiPortRouterInterfaceTraits::CreateAttributes c{
        0,
        SAI_ROUTER_INTERFACE_TYPE_PORT,
        41,
        srcMac,
        9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ,
        std::nullopt
#endif
    };
    auto obj = createObj<SaiPortRouterInterfaceTraits>(k, c, 0);
    auto expected =
        "RouterInterfaceSaiId(1): "
        "(VirtualRouterId: 0, Type: 0, PortId: 41, "
        "SrcMac: 41:41:41:41:41:41, Mtu: 9000"
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
        ", nullopt"
#endif
        ")";
    EXPECT_EQ(expected, fmt::format("{}", obj));
  }
  {
    SaiMplsRouterInterfaceTraits::AdapterHostKey mplsIntfAhk{0};
    SaiMplsRouterInterfaceTraits::CreateAttributes mplsIntfCreateAttr{
        0, SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER};
    auto mplsObj = createObj<SaiMplsRouterInterfaceTraits>(
        mplsIntfAhk, mplsIntfCreateAttr, 0);

    auto expected1 =
        "RouterInterfaceSaiId(2): "
        "(VirtualRouterId: 0, Type: 3)";
    EXPECT_EQ(expected1, fmt::format("{}", mplsObj));
  }
}

TEST_F(RouterInterfaceStoreTest, toStrTest) {
  std::ignore =
      createRouterInterface(41, folly::MacAddress{"41:41:41:41:41:41"}, 1514);
  verifyToStr<SaiVlanRouterInterfaceTraits>();
  std::ignore = createPortRouterInterface(
      41, folly::MacAddress{"41:41:41:41:41:41"}, 1514);
  verifyToStr<SaiPortRouterInterfaceTraits>();
  std::ignore = createMplsRouterInterface();
  verifyToStr<SaiMplsRouterInterfaceTraits>();
}

/*
 * SAI has no LAG specific router interface type: a router interface over an
 * aggregate port is a SAI_ROUTER_INTERFACE_TYPE_PORT whose PortId attribute
 * holds a LAG object id rather than a port object id. These tests pin that
 * down, so that reusing SaiPortRouterInterfaceTraits for both stays correct.
 *
 * The alternative, a fourth traits type also keyed on
 * SAI_ROUTER_INTERFACE_TYPE_PORT, would break warm boot: SaiObjectStore::reload
 * picks objects by their type attribute, so both stores would claim the same
 * router interfaces and refOrInsert would fatal on the duplicate.
 */
TEST_F(RouterInterfaceStoreTest, loadLagRouterInterface) {
  auto srcMac = folly::MacAddress{"41:41:41:41:41:41"};
  auto lagSaiId = createLag("lag0");
  auto lagRouterInterfaceSaiId =
      createPortRouterInterface(lagSaiId, srcMac, 9000);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiPortRouterInterfaceTraits>();

  SaiPortRouterInterfaceTraits::AdapterHostKey k{0, lagSaiId};
  auto got = store.get(k);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), lagRouterInterfaceSaiId);
  EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, got->attributes()), lagSaiId);
}

TEST_F(RouterInterfaceStoreTest, portAndLagRouterInterfacesCoexist) {
  auto srcMac1 = folly::MacAddress{"41:41:41:41:41:41"};
  auto srcMac2 = folly::MacAddress{"42:42:42:42:42:42"};
  auto lagSaiId = createLag("lag0");
  // A port object id and a LAG object id are drawn from disjoint spaces, so
  // {VirtualRouterId, PortId} stays unique across the two flavours.
  sai_object_id_t portSaiId = 41;
  ASSERT_NE(static_cast<sai_object_id_t>(lagSaiId), portSaiId);

  auto portRifSaiId = createPortRouterInterface(portSaiId, srcMac1, 1514);
  auto lagRifSaiId = createPortRouterInterface(lagSaiId, srcMac2, 9000);
  EXPECT_NE(portRifSaiId, lagRifSaiId);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiPortRouterInterfaceTraits>();

  // Both are claimed by the same store, as distinct objects.
  auto gotPort =
      store.get(SaiPortRouterInterfaceTraits::AdapterHostKey{0, portSaiId});
  auto gotLag =
      store.get(SaiPortRouterInterfaceTraits::AdapterHostKey{0, lagSaiId});
  ASSERT_NE(gotPort, nullptr);
  ASSERT_NE(gotLag, nullptr);
  EXPECT_EQ(gotPort->adapterKey(), portRifSaiId);
  EXPECT_EQ(gotLag->adapterKey(), lagRifSaiId);
  EXPECT_EQ(
      GET_OPT_ATTR(PortRouterInterface, Mtu, gotPort->attributes()), 1514);
  EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, gotLag->attributes()), 9000);
}

TEST_F(RouterInterfaceStoreTest, lagRouterInterfaceSetMtu) {
  auto srcMac = folly::MacAddress{"41:41:41:41:41:41"};
  auto lagSaiId = createLag("lag0");
  std::ignore = createPortRouterInterface(lagSaiId, srcMac, 1514);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiPortRouterInterfaceTraits>();

  SaiPortRouterInterfaceTraits::AdapterHostKey k{0, lagSaiId};
  SaiPortRouterInterfaceTraits::CreateAttributes newAttrs{
      0,
      SAI_ROUTER_INTERFACE_TYPE_PORT,
      lagSaiId,
      srcMac,
      9000
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      ,
      std::nullopt
#endif
  };
  auto obj = store.setObject(k, newAttrs);
  EXPECT_EQ(GET_OPT_ATTR(PortRouterInterface, Mtu, obj->attributes()), 9000);
  EXPECT_EQ(GET_ATTR(PortRouterInterface, PortId, obj->attributes()), lagSaiId);
}

TEST_F(RouterInterfaceStoreTest, lagRouterInterfaceSerDeser) {
  auto lagSaiId = createLag("lag0");
  auto lagRouterInterfaceSaiId = createPortRouterInterface(
      lagSaiId, folly::MacAddress{"41:41:41:41:41:41"}, 9000);
  verifyAdapterKeySerDeser<SaiPortRouterInterfaceTraits>(
      {lagRouterInterfaceSaiId});
}
