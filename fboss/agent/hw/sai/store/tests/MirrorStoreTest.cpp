/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

#include <variant>

using namespace facebook::fboss;

static constexpr folly::StringPiece srcIpStr = "42.42.12.34";
static constexpr folly::StringPiece dstIpStr = "22.22.12.24";
static constexpr folly::StringPiece srcMacStr = "42:42:42:12:34:56";
static constexpr folly::StringPiece dstMacStr = "21:21:22:12:34:56";

class MirrorStoreTest : public SaiStoreTest {
 public:
  folly::IPAddress srcIp{srcIpStr};
  folly::IPAddress dstIp{dstIpStr};
  folly::MacAddress srcMac{srcMacStr};
  folly::MacAddress dstMac{dstMacStr};

  MirrorSaiId createLocalMirror(int portId) {
    SaiLocalMirrorTraits::CreateAttributes c{
        SAI_MIRROR_SESSION_TYPE_LOCAL, portId, std::nullopt};
    return saiApiTable->mirrorApi().create<SaiLocalMirrorTraits>(c, 0);
  }

  MirrorSaiId createEnhancedRemoteMirror(
      sai_object_id_t portId,
      uint8_t tos,
      uint16_t truncateSize,
      uint8_t ttl,
      folly::IPAddress& srcIpAddr,
      folly::IPAddress& dstIpAddr,
      folly::MacAddress& srcMacAddr,
      folly::MacAddress& dstMacAddr,
      uint8_t ipHeaderVersion,
      uint16_t greProtocol,
      uint32_t samplingRate,
      std::optional<uint32_t> tcBufferLimit) {
    return saiApiTable->mirrorApi().create<SaiEnhancedRemoteMirrorTraits>(
        {SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE,
         portId,
         SAI_ERSPAN_ENCAPSULATION_TYPE_MIRROR_L3_GRE_TUNNEL,
         tos,
         srcIpAddr,
         dstIpAddr,
         srcMacAddr,
         dstMacAddr,
         greProtocol,
         ipHeaderVersion,
         ttl,
         truncateSize,
         samplingRate,
         tcBufferLimit,
         std::nullopt},
        0);
  }
};

TEST_F(MirrorStoreTest, loadMirrorSessions) {
  auto mirrorId1 = createLocalMirror(0);
  auto mirrorId2 = createEnhancedRemoteMirror(
      1, 16, 240, 255, srcIp, dstIp, srcMac, dstMac, 4, 2148, 0, std::nullopt);

  SaiStore s(0);
  s.reload();
  auto& store1 = s.get<SaiLocalMirrorTraits>();
  auto& store2 = s.get<SaiEnhancedRemoteMirrorTraits>();

  SaiLocalMirrorTraits::AdapterHostKey k1{
      SAI_MIRROR_SESSION_TYPE_LOCAL, 0, std::nullopt};
  SaiEnhancedRemoteMirrorTraits::AdapterHostKey k2{
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE, 1, srcIp, dstIp};
  auto got1 = store1.get(k1);
  EXPECT_EQ(got1->adapterKey(), mirrorId1);
  auto got2 = store2.get(k2);
  EXPECT_EQ(got2->adapterKey(), mirrorId2);
}

TEST_F(MirrorStoreTest, mirrorLoadCtor) {
  auto mirrorId = createLocalMirror(0);
  auto obj = createObj<SaiLocalMirrorTraits>(mirrorId);
  EXPECT_EQ(obj.adapterKey(), mirrorId);
  EXPECT_EQ(
      GET_ATTR(LocalMirror, Type, obj.attributes()),
      SAI_MIRROR_SESSION_TYPE_LOCAL);
}

TEST_F(MirrorStoreTest, mirrorCreateCtor) {
  SaiLocalMirrorTraits::AdapterHostKey k{
      SAI_MIRROR_SESSION_TYPE_LOCAL, 0, std::nullopt};
  SaiLocalMirrorTraits::CreateAttributes c{
      SAI_MIRROR_SESSION_TYPE_LOCAL, 0, std::nullopt};
  auto obj = createObj<SaiLocalMirrorTraits>(k, c, 0);
  EXPECT_EQ(
      GET_ATTR(LocalMirror, Type, obj.attributes()),
      SAI_MIRROR_SESSION_TYPE_LOCAL);
}

TEST_F(MirrorStoreTest, localSpanSerDeser) {
  auto mirrorId = createLocalMirror(0);
  verifyAdapterKeySerDeser<SaiLocalMirrorTraits>({mirrorId});
}

TEST_F(MirrorStoreTest, erSpanSerDeser) {
  auto mirrorId = createEnhancedRemoteMirror(
      2, 16, 220, 255, srcIp, dstIp, srcMac, dstMac, 4, 2200, 0, std::nullopt);
  verifyAdapterKeySerDeser<SaiEnhancedRemoteMirrorTraits>({mirrorId});
}

TEST_F(MirrorStoreTest, toStr) {
  std::ignore = createLocalMirror(0);
  verifyToStr<SaiLocalMirrorTraits>();
  std::ignore = createEnhancedRemoteMirror(
      20, 16, 220, 180, srcIp, dstIp, srcMac, dstMac, 4, 2200, 0, std::nullopt);
  verifyToStr<SaiEnhancedRemoteMirrorTraits>();
}
