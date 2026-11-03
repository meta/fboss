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

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

#include <memory>

using namespace facebook::fboss;

static constexpr folly::StringPiece str4 = "42.42.12.34";

class NextHopApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    nextHopApi = std::make_unique<NextHopApi>();
  }
  NextHopSaiId createNextHop(folly::IPAddress ip) {
    SaiIpNextHopTraits::Attributes::Type typeAttribute(SAI_NEXT_HOP_TYPE_IP);
    SaiIpNextHopTraits::Attributes::RouterInterfaceId
        routerInterfaceIdAttribute(0);
    SaiIpNextHopTraits::Attributes::Ip ipAttribute(ip4);
    auto nextHopId = nextHopApi->create<SaiIpNextHopTraits>(
        {typeAttribute, routerInterfaceIdAttribute, ipAttribute, std::nullopt},
        0);
    auto fnh = fs->nextHopManager.get(nextHopId);
    EXPECT_EQ(SAI_NEXT_HOP_TYPE_IP, fnh.type);
    EXPECT_EQ(ip, fnh.ip);
    EXPECT_EQ(0, fnh.routerInterfaceId);
    return nextHopId;
  }

  NextHopSaiId createMplsNextHop(
      folly::IPAddress ip,
      std::vector<sai_uint32_t> stack,
      sai_int32_t outsegType = SAI_OUTSEG_TYPE_SWAP) {
    SaiMplsNextHopTraits::Attributes::Type typeAttribute(
        SAI_NEXT_HOP_TYPE_MPLS);
    SaiMplsNextHopTraits::Attributes::RouterInterfaceId
        routerInterfaceIdAttribute(0);
    SaiMplsNextHopTraits::Attributes::Ip ipAttribute(ip4);
    SaiMplsNextHopTraits::Attributes::LabelStack labelStack{stack};
    SaiMplsNextHopTraits::Attributes::OutsegType outsegTypeAttribute{
        outsegType};
    auto nextHopId = nextHopApi->create<SaiMplsNextHopTraits>(
        {typeAttribute,
         routerInterfaceIdAttribute,
         ipAttribute,
         labelStack,
         outsegTypeAttribute,
         std::nullopt},
        0);
    auto fnh = fs->nextHopManager.get(nextHopId);
    EXPECT_EQ(SAI_NEXT_HOP_TYPE_MPLS, fnh.type);
    EXPECT_EQ(ip, fnh.ip);
    EXPECT_EQ(0, fnh.routerInterfaceId);
    EXPECT_EQ(stack, fnh.labelStack);
    EXPECT_EQ(outsegType, fnh.outsegType);

    return nextHopId;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  NextHopSaiId createSrv6SidlistNextHop(
      sai_object_id_t tunnelId,
      sai_object_id_t srv6SidlistId) {
    SaiSrv6SidlistNextHopTraits::Attributes::Type typeAttribute(
        SAI_NEXT_HOP_TYPE_SRV6_SIDLIST);
    SaiSrv6SidlistNextHopTraits::Attributes::TunnelId tunnelIdAttribute(
        tunnelId);
    SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId
        srv6SidlistIdAttribute(srv6SidlistId);
    auto nextHopId = nextHopApi->create<SaiSrv6SidlistNextHopTraits>(
        {typeAttribute,
         tunnelIdAttribute,
         srv6SidlistIdAttribute,
         std::nullopt},
        0);
    auto fnh = fs->nextHopManager.get(nextHopId);
    EXPECT_EQ(SAI_NEXT_HOP_TYPE_SRV6_SIDLIST, fnh.type);
    EXPECT_EQ(tunnelId, fnh.tunnelId);
    EXPECT_EQ(srv6SidlistId, fnh.srv6SidlistId);
    return nextHopId;
  }
#endif
  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<NextHopApi> nextHopApi;
  folly::IPAddress ip4{str4};
};

TEST_F(NextHopApiTest, createNextHop) {
  createNextHop(ip4);
}

TEST_F(NextHopApiTest, createMplsNextHop) {
  createMplsNextHop(ip4, {1001, 2001, 3001});
}

TEST_F(NextHopApiTest, removeNextHop) {
  auto nextHopId = createNextHop(ip4);
  EXPECT_EQ(fs->nextHopManager.map().size(), 1);
  nextHopApi->remove(nextHopId);
  EXPECT_EQ(fs->nextHopManager.map().size(), 0);
}

TEST_F(NextHopApiTest, getIpTypeAttribute) {
  auto nextHopId = createNextHop(ip4);

  auto nextHopIpGot =
      nextHopApi->getAttribute(nextHopId, SaiIpNextHopTraits::Attributes::Ip());
  auto nextHopRouterInterfaceIdGot = nextHopApi->getAttribute(
      nextHopId, SaiIpNextHopTraits::Attributes::RouterInterfaceId());
  auto nextHopTypeGot = nextHopApi->getAttribute(
      nextHopId, SaiIpNextHopTraits::Attributes::Type());

  EXPECT_EQ(nextHopIpGot, ip4);
  EXPECT_EQ(nextHopRouterInterfaceIdGot, 0);
  EXPECT_EQ(nextHopTypeGot, SAI_NEXT_HOP_TYPE_IP);

  // IP does not support get LabelStack for Mpls
  EXPECT_THROW(
      nextHopApi->getAttribute(
          nextHopId, SaiMplsNextHopTraits::Attributes::LabelStack()),
      SaiApiError);
}

TEST_F(NextHopApiTest, getMplsTypeAttribute) {
  std::vector<sai_uint32_t> stack{1001, 2001, 3001};
  auto nextHopId = createMplsNextHop(ip4, stack);

  auto nextHopIpGot =
      nextHopApi->getAttribute(nextHopId, SaiIpNextHopTraits::Attributes::Ip());
  auto nextHopRouterInterfaceIdGot = nextHopApi->getAttribute(
      nextHopId, SaiIpNextHopTraits::Attributes::RouterInterfaceId());
  auto nextHopLabelStackGot = nextHopApi->getAttribute(
      nextHopId, SaiMplsNextHopTraits::Attributes::LabelStack());
  auto nextHopTypeGot = nextHopApi->getAttribute(
      nextHopId, SaiIpNextHopTraits::Attributes::Type());

  EXPECT_EQ(nextHopIpGot, ip4);
  EXPECT_EQ(nextHopRouterInterfaceIdGot, 0);
  EXPECT_EQ(nextHopLabelStackGot, stack);
  EXPECT_EQ(nextHopTypeGot, SAI_NEXT_HOP_TYPE_MPLS);
}

TEST_F(NextHopApiTest, getMplsOutsegTypeAttribute) {
  auto pushNextHopId =
      createMplsNextHop(ip4, {1001, 2001}, SAI_OUTSEG_TYPE_PUSH);
  auto swapNextHopId = createMplsNextHop(ip4, {3001}, SAI_OUTSEG_TYPE_SWAP);

  EXPECT_EQ(
      nextHopApi->getAttribute(
          pushNextHopId, SaiMplsNextHopTraits::Attributes::OutsegType()),
      SAI_OUTSEG_TYPE_PUSH);
  EXPECT_EQ(
      nextHopApi->getAttribute(
          swapNextHopId, SaiMplsNextHopTraits::Attributes::OutsegType()),
      SAI_OUTSEG_TYPE_SWAP);
}

// IP is create only, so if we try to set it, we expect to fail
TEST_F(NextHopApiTest, setIpTypeAttribute) {
  auto nextHopId = createNextHop(ip4);

  SaiIpNextHopTraits::Attributes::Ip ipAttribute(ip4);
  SaiIpNextHopTraits::Attributes::RouterInterfaceId routerInterfaceIdAttribute{
      1};
  SaiIpNextHopTraits::Attributes::Type typeAttribute{SAI_NEXT_HOP_TYPE_IP};

  EXPECT_THROW(nextHopApi->setAttribute(nextHopId, ipAttribute);, SaiApiError);
  EXPECT_THROW(
      nextHopApi->setAttribute(nextHopId, routerInterfaceIdAttribute),
      SaiApiError);
  EXPECT_THROW(nextHopApi->setAttribute(nextHopId, typeAttribute), SaiApiError);
}

// MPLS is create only as well, so if we try to set it, we expect to fail
TEST_F(NextHopApiTest, setMplsTypeAttribute) {
  std::vector<sai_uint32_t> stack{1001, 2001, 3001};
  auto nextHopId = createMplsNextHop(ip4, stack);

  SaiIpNextHopTraits::Attributes::Ip ipAttribute(ip4);
  SaiIpNextHopTraits::Attributes::RouterInterfaceId routerInterfaceIdAttribute{
      1};
  SaiMplsNextHopTraits::Attributes::LabelStack labelStackAttribute{
      std::vector<sai_uint32_t>{2001, 3001, 4001}};
  SaiIpNextHopTraits::Attributes::Type typeAttribute{SAI_NEXT_HOP_TYPE_IP};

  EXPECT_THROW(nextHopApi->setAttribute(nextHopId, ipAttribute);, SaiApiError);
  EXPECT_THROW(
      nextHopApi->setAttribute(nextHopId, routerInterfaceIdAttribute),
      SaiApiError);
  EXPECT_THROW(
      nextHopApi->setAttribute(nextHopId, labelStackAttribute), SaiApiError);
  EXPECT_THROW(nextHopApi->setAttribute(nextHopId, typeAttribute), SaiApiError);
}

TEST_F(NextHopApiTest, formatNextHopAttributes) {
  SaiIpNextHopTraits::Attributes::Type t{SAI_NEXT_HOP_TYPE_IP};
  EXPECT_EQ("Type: 0", fmt::format("{}", t));
  SaiIpNextHopTraits::Attributes::RouterInterfaceId rifid{42};
  EXPECT_EQ("RouterInterfaceId: 42", fmt::format("{}", rifid));
  SaiIpNextHopTraits::Attributes::Ip ip{ip4};
  EXPECT_EQ(fmt::format("Ip: {}", str4), fmt::format("{}", ip));
  SaiMplsNextHopTraits::Attributes::LabelStack ls{{42, 100}};
  EXPECT_EQ("LabelStack: [42, 100]", fmt::format("{}", ls));
  SaiMplsNextHopTraits::Attributes::OutsegType ot{SAI_OUTSEG_TYPE_PUSH};
  EXPECT_EQ("OutsegType: 0", fmt::format("{}", ot));
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
TEST_F(NextHopApiTest, createSrv6SidlistNextHop) {
  createSrv6SidlistNextHop(10, 20);
}

TEST_F(NextHopApiTest, getSrv6SidlistTypeAttribute) {
  sai_object_id_t tunnelId = 10;
  sai_object_id_t srv6SidlistId = 20;
  auto nextHopId = createSrv6SidlistNextHop(tunnelId, srv6SidlistId);

  auto nextHopTypeGot = nextHopApi->getAttribute(
      nextHopId, SaiIpNextHopTraits::Attributes::Type());
  auto nextHopTunnelIdGot = nextHopApi->getAttribute(
      nextHopId, SaiSrv6SidlistNextHopTraits::Attributes::TunnelId());
  auto nextHopSrv6SidlistIdGot = nextHopApi->getAttribute(
      nextHopId, SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId());

  EXPECT_EQ(nextHopTypeGot, SAI_NEXT_HOP_TYPE_SRV6_SIDLIST);
  EXPECT_EQ(nextHopTunnelIdGot, tunnelId);
  EXPECT_EQ(nextHopSrv6SidlistIdGot, srv6SidlistId);
}

TEST_F(NextHopApiTest, removeSrv6SidlistNextHop) {
  auto nextHopId = createSrv6SidlistNextHop(10, 20);
  EXPECT_EQ(fs->nextHopManager.map().size(), 1);
  nextHopApi->remove(nextHopId);
  EXPECT_EQ(fs->nextHopManager.map().size(), 0);
}

TEST_F(NextHopApiTest, formatSrv6SidlistNextHopAttributes) {
  SaiSrv6SidlistNextHopTraits::Attributes::TunnelId tid{42};
  EXPECT_EQ("TunnelId: 42", fmt::format("{}", tid));
  SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId sid{100};
  EXPECT_EQ("Srv6SidlistId: 100", fmt::format("{}", sid));
}
#endif

TEST_F(NextHopApiTest, createTunnelEncapNextHopWithOverlayDstMac) {
  SaiTunnelEncapNextHopTraits::Attributes::Type type{
      SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP};
  SaiTunnelEncapNextHopTraits::Attributes::TunnelId tunnelId{42};
  SaiTunnelEncapNextHopTraits::Attributes::Ip ip{ip4};
  SaiTunnelEncapNextHopTraits::Attributes::OverlayDstMac overlayDstMac{
      folly::MacAddress("02:00:00:00:00:01")};
  SaiTunnelEncapNextHopTraits::CreateAttributes attrs{
      type, tunnelId, ip, overlayDstMac, std::nullopt};
  auto nextHopId = nextHopApi->create<SaiTunnelEncapNextHopTraits>(attrs, 0);
  EXPECT_EQ(
      nextHopApi->getAttribute(
          nextHopId, SaiTunnelEncapNextHopTraits::Attributes::Type{}),
      SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP);
  EXPECT_EQ(
      nextHopApi->getAttribute(
          nextHopId, SaiTunnelEncapNextHopTraits::Attributes::OverlayDstMac{}),
      folly::MacAddress("02:00:00:00:00:01"));
  const auto& fnh = fs->nextHopManager.get(nextHopId);
  EXPECT_EQ(fnh.tunnelId, 42);
  EXPECT_EQ(fnh.overlayDstMac, folly::MacAddress("02:00:00:00:00:01"));

  // An unset MAC must not alias a set one in the host key.
  SaiTunnelEncapNextHopTraits::CreateAttributes noMacAttrs{
      type, tunnelId, ip, std::nullopt, std::nullopt};
  auto noMacId = nextHopApi->create<SaiTunnelEncapNextHopTraits>(noMacAttrs, 0);
  EXPECT_NE(noMacId, nextHopId);
  EXPECT_EQ(
      nextHopApi->getAttribute(
          noMacId, SaiTunnelEncapNextHopTraits::Attributes::OverlayDstMac{}),
      folly::MacAddress("00:00:00:00:00:00"));
}
