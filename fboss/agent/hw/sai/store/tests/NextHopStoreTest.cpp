/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class NextHopStoreTest : public SaiStoreTest {
 public:
  NextHopSaiId createNextHop(const folly::IPAddress& ip) {
    return saiApiTable->nextHopApi().create<SaiIpNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_IP, 42, ip, std::nullopt}, 0);
  }
  NextHopSaiId createMplsNextHop(
      const folly::IPAddress& ip,
      std::vector<sai_uint32_t> stack,
      std::optional<SaiMplsNextHopTraits::Attributes::OutsegType> outsegType =
          SaiMplsNextHopTraits::Attributes::OutsegType{SAI_OUTSEG_TYPE_SWAP}) {
    return saiApiTable->nextHopApi().create<SaiMplsNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_MPLS,
         42,
         ip,
         std::move(stack),
         outsegType,
         std::nullopt},
        0);
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  NextHopSaiId createSrv6SidlistNextHop(
      sai_object_id_t tunnelId,
      sai_object_id_t srv6SidlistId) {
    return saiApiTable->nextHopApi().create<SaiSrv6SidlistNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_SRV6_SIDLIST, tunnelId, srv6SidlistId, std::nullopt},
        0);
  }
#endif
};

TEST_F(NextHopStoreTest, loadNextHops) {
  folly::IPAddress ip1{"4200::41"};
  folly::IPAddress ip2{"4200::42"};
  auto nextHopSaiId1 = createNextHop(ip1);
  auto nextHopSaiId2 = createNextHop(ip2);
  auto nextHopSaiId3 =
      createMplsNextHop(ip1, std::vector<sai_uint32_t>{1001, 1002});
  auto nextHopSaiId4 =
      createMplsNextHop(ip2, std::vector<sai_uint32_t>{2001, 2002});

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiIpNextHopTraits>();

  SaiIpNextHopTraits::AdapterHostKey k1{42, ip1};
  SaiIpNextHopTraits::AdapterHostKey k2{42, ip2};
  auto got = store.get(k1);
  EXPECT_EQ(got->adapterKey(), nextHopSaiId1);
  got = store.get(k2);
  EXPECT_EQ(got->adapterKey(), nextHopSaiId2);

  auto& mplsNextHopStore = s.get<SaiMplsNextHopTraits>();
  SaiMplsNextHopTraits::AdapterHostKey k3{
      42, ip1, std::vector<sai_uint32_t>{1001, 1002}, SAI_OUTSEG_TYPE_SWAP};
  SaiMplsNextHopTraits::AdapterHostKey k4{
      42, ip2, std::vector<sai_uint32_t>{2001, 2002}, SAI_OUTSEG_TYPE_SWAP};
  auto mplsNhop = mplsNextHopStore.get(k3);
  ASSERT_NE(mplsNhop, nullptr);
  EXPECT_EQ(mplsNhop->adapterKey(), nextHopSaiId3);
  mplsNhop = mplsNextHopStore.get(k4);
  ASSERT_NE(mplsNhop, nullptr);
  EXPECT_EQ(mplsNhop->adapterKey(), nextHopSaiId4);
}

TEST_F(NextHopStoreTest, loadMplsNextHopsByOutsegType) {
  folly::IPAddress ip{"4200::41"};
  std::vector<sai_uint32_t> stack{1001};
  auto pushSaiId = createMplsNextHop(ip, stack, SAI_OUTSEG_TYPE_PUSH);
  auto swapSaiId = createMplsNextHop(ip, stack, SAI_OUTSEG_TYPE_SWAP);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiMplsNextHopTraits>();

  SaiMplsNextHopTraits::AdapterHostKey pushKey{
      42, ip, stack, SAI_OUTSEG_TYPE_PUSH};
  SaiMplsNextHopTraits::AdapterHostKey swapKey{
      42, ip, stack, SAI_OUTSEG_TYPE_SWAP};
  ASSERT_NE(store.get(pushKey), nullptr);
  ASSERT_NE(store.get(swapKey), nullptr);
  EXPECT_EQ(store.get(pushKey)->adapterKey(), pushSaiId);
  EXPECT_EQ(store.get(swapKey)->adapterKey(), swapSaiId);
}

TEST_F(NextHopStoreTest, nextHopLoadCtor) {
  auto ip = folly::IPAddress("::");
  auto nextHopSaiId = createNextHop(ip);
  auto obj = createObj<SaiIpNextHopTraits>(nextHopSaiId);
  EXPECT_EQ(obj.adapterKey(), nextHopSaiId);
  EXPECT_EQ(GET_ATTR(IpNextHop, Ip, obj.attributes()), ip);
}

TEST_F(NextHopStoreTest, nextHopCreateCtor) {
  auto ip = folly::IPAddress("::");
  SaiIpNextHopTraits::CreateAttributes c{
      SAI_NEXT_HOP_TYPE_IP, 42, ip, std::nullopt};
  SaiIpNextHopTraits::AdapterHostKey k{42, ip};
  auto obj = createObj<SaiIpNextHopTraits>(k, c, 0);
  EXPECT_EQ(GET_ATTR(IpNextHop, Ip, obj.attributes()), ip);
}

TEST_F(NextHopStoreTest, ipNextHopSerDeser) {
  auto ip = folly::IPAddress("::");
  auto nextHopSaiId = createNextHop(ip);
  verifyAdapterKeySerDeser<SaiIpNextHopTraits>({nextHopSaiId});
}

TEST_F(NextHopStoreTest, mplsNextHopSerDeser) {
  auto nextHopSaiId = createMplsNextHop(
      folly::IPAddress{"4200::41"}, std::vector<sai_uint32_t>{1001, 1002});
  verifyAdapterKeySerDeser<SaiMplsNextHopTraits>({nextHopSaiId});
}

// The existing case above only covers the SAI default of SWAP. Round trip a
// push next hop too, so the serialized key carries the non default value.
TEST_F(NextHopStoreTest, mplsNextHopOutsegTypePushSerDeser) {
  auto nextHopSaiId = createMplsNextHop(
      folly::IPAddress{"4200::41"},
      std::vector<sai_uint32_t>{1001, 1002},
      SaiMplsNextHopTraits::Attributes::OutsegType{SAI_OUTSEG_TYPE_PUSH});
  verifyAdapterKeySerDeser<SaiMplsNextHopTraits>({nextHopSaiId});
}

TEST_F(NextHopStoreTest, ipNextHopToStr) {
  auto ip = folly::IPAddress("::");
  std::ignore = createNextHop(ip);
  verifyToStr<SaiIpNextHopTraits>();
}

TEST_F(NextHopStoreTest, mplsNextHopToStr) {
  std::ignore = createMplsNextHop(
      folly::IPAddress{"4200::41"}, std::vector<sai_uint32_t>{1001, 1002});
  verifyToStr<SaiMplsNextHopTraits>();
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
TEST_F(NextHopStoreTest, loadSrv6SidlistNextHops) {
  auto nextHopSaiId1 = createSrv6SidlistNextHop(10, 20);
  auto nextHopSaiId2 = createSrv6SidlistNextHop(30, 40);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiSrv6SidlistNextHopTraits>();

  SaiSrv6SidlistNextHopTraits::AdapterHostKey k1{10, 20};
  SaiSrv6SidlistNextHopTraits::AdapterHostKey k2{30, 40};
  auto got = store.get(k1);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), nextHopSaiId1);
  got = store.get(k2);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), nextHopSaiId2);
}

TEST_F(NextHopStoreTest, srv6SidlistNextHopLoadCtor) {
  auto nextHopSaiId = createSrv6SidlistNextHop(10, 20);
  auto obj = createObj<SaiSrv6SidlistNextHopTraits>(nextHopSaiId);
  EXPECT_EQ(obj.adapterKey(), nextHopSaiId);
}

TEST_F(NextHopStoreTest, srv6SidlistNextHopCreateCtor) {
  SaiSrv6SidlistNextHopTraits::CreateAttributes c{
      SAI_NEXT_HOP_TYPE_SRV6_SIDLIST, 10, 20, std::nullopt};
  SaiSrv6SidlistNextHopTraits::AdapterHostKey k{10, 20};
  auto obj = createObj<SaiSrv6SidlistNextHopTraits>(k, c, 0);
  EXPECT_EQ(
      GET_ATTR(Srv6SidlistNextHop, TunnelId, obj.attributes()),
      static_cast<sai_object_id_t>(10));
}

TEST_F(NextHopStoreTest, srv6SidlistNextHopSerDeser) {
  auto nextHopSaiId = createSrv6SidlistNextHop(10, 20);
  verifyAdapterKeySerDeser<SaiSrv6SidlistNextHopTraits>({nextHopSaiId});
}

TEST_F(NextHopStoreTest, srv6SidlistNextHopToStr) {
  std::ignore = createSrv6SidlistNextHop(10, 20);
  verifyToStr<SaiSrv6SidlistNextHopTraits>();
}
#endif

TEST_F(NextHopStoreTest, loadTunnelEncapNextHopsWithOverlayDstMac) {
  folly::IPAddress ip{"10.0.0.1"};
  auto& nextHopApi = saiApiTable->nextHopApi();
  SaiTunnelEncapNextHopTraits::CreateAttributes attrs{
      SaiTunnelEncapNextHopTraits::Attributes::Type{
          SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP},
      SaiTunnelEncapNextHopTraits::Attributes::TunnelId{42},
      SaiTunnelEncapNextHopTraits::Attributes::Ip{ip},
      SaiTunnelEncapNextHopTraits::Attributes::OverlayDstMac{
          folly::MacAddress("02:00:00:00:00:01")},
      std::nullopt};
  auto nextHopSaiId = nextHopApi.create<SaiTunnelEncapNextHopTraits>(attrs, 0);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiTunnelEncapNextHopTraits>();
  SaiTunnelEncapNextHopTraits::AdapterHostKey k{42, ip};
  auto got = store.get(k);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), nextHopSaiId);
  EXPECT_EQ(
      GET_OPT_ATTR(TunnelEncapNextHop, OverlayDstMac, got->attributes()),
      folly::MacAddress("02:00:00:00:00:01"));
}

TEST_F(NextHopStoreTest, tunnelEncapNextHopSerDeser) {
  folly::IPAddress ip{"10.0.0.2"};
  auto nextHopSaiId =
      saiApiTable->nextHopApi().create<SaiTunnelEncapNextHopTraits>(
          {SaiTunnelEncapNextHopTraits::Attributes::Type{
               SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP},
           SaiTunnelEncapNextHopTraits::Attributes::TunnelId{43},
           SaiTunnelEncapNextHopTraits::Attributes::Ip{ip},
           SaiTunnelEncapNextHopTraits::Attributes::OverlayDstMac{
               folly::MacAddress("02:00:00:00:00:03")},
           std::nullopt},
          0);
  verifyAdapterKeySerDeser<SaiTunnelEncapNextHopTraits>({nextHopSaiId});
}
