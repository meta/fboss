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
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

static constexpr folly::StringPiece srcIpStr = "42.42.12.34";
static constexpr folly::StringPiece dstIpStr = "22.22.12.24";
static constexpr folly::StringPiece srcMacStr = "42:42:42:12:34:56";
static constexpr folly::StringPiece dstMacStr = "21:21:22:12:34:56";

class MirrorApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    mirrorApi = std::make_unique<MirrorApi>();
  }

  folly::IPAddress srcIp{srcIpStr};
  folly::IPAddress dstIp{dstIpStr};
  folly::MacAddress srcMac{srcMacStr};
  folly::MacAddress dstMac{dstMacStr};

  MirrorSaiId createLocalMirror(const sai_object_id_t portId) const {
    return mirrorApi->create<SaiLocalMirrorTraits>(
        {SAI_MIRROR_SESSION_TYPE_LOCAL, portId, std::nullopt}, 0);
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
      uint32_t samplingRate) {
    return mirrorApi->create<SaiEnhancedRemoteMirrorTraits>(
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
         std::nullopt,
         std::nullopt},
        0);
  }

  void checkLocalMirror(MirrorSaiId mirrorSaiId) const {
    auto fms = fs->mirrorManager.get(mirrorSaiId);
    EXPECT_EQ(fms.id, mirrorSaiId);
    SaiLocalMirrorTraits::Attributes::MonitorPort monitorPort;
    auto gotMonitorPort = mirrorApi->getAttribute(mirrorSaiId, monitorPort);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).monitorPort, gotMonitorPort);
  }

  void checkEnhancedRemoteMirror(MirrorSaiId mirrorSaiId) const {
    auto fms = fs->mirrorManager.get(mirrorSaiId);
    EXPECT_EQ(fms.id, mirrorSaiId);
    SaiEnhancedRemoteMirrorTraits::Attributes::MonitorPort monitorPort;
    SaiEnhancedRemoteMirrorTraits::Attributes::Tos tos;
    SaiEnhancedRemoteMirrorTraits::Attributes::Ttl ttl;
    SaiEnhancedRemoteMirrorTraits::Attributes::TruncateSize truncateSize;
    SaiEnhancedRemoteMirrorTraits::Attributes::SrcIpAddress srcIpAttr;
    SaiEnhancedRemoteMirrorTraits::Attributes::DstIpAddress dstIpAttr;
    SaiEnhancedRemoteMirrorTraits::Attributes::SrcMacAddress srcMacAttr;
    SaiEnhancedRemoteMirrorTraits::Attributes::DstMacAddress dstMacAttr;
    SaiEnhancedRemoteMirrorTraits::Attributes::GreProtocolType greProtocolType;
    SaiEnhancedRemoteMirrorTraits::Attributes::IpHeaderVersion ipHeaderVersion;
    auto gotMonitorPort = mirrorApi->getAttribute(mirrorSaiId, monitorPort);
    auto gotTos = mirrorApi->getAttribute(mirrorSaiId, tos);
    auto gotTtl = mirrorApi->getAttribute(mirrorSaiId, ttl);
    auto gotTruncateSize = mirrorApi->getAttribute(mirrorSaiId, truncateSize);
    auto gotSrcIp = mirrorApi->getAttribute(mirrorSaiId, srcIpAttr);
    auto gotDstIp = mirrorApi->getAttribute(mirrorSaiId, dstIpAttr);
    auto gotSrcMac = mirrorApi->getAttribute(mirrorSaiId, srcMacAttr);
    auto gotDstMac = mirrorApi->getAttribute(mirrorSaiId, dstMacAttr);
    auto gotGreProtocolType =
        mirrorApi->getAttribute(mirrorSaiId, greProtocolType);
    auto gotIpHeaderVersion =
        mirrorApi->getAttribute(mirrorSaiId, ipHeaderVersion);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).monitorPort, gotMonitorPort);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).tos, gotTos);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).ttl, gotTtl);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).truncateSize, gotTruncateSize);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).srcIp, gotSrcIp);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).dstIp, gotDstIp);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).srcMac, gotSrcMac);
    EXPECT_EQ(fs->mirrorManager.get(mirrorSaiId).dstMac, gotDstMac);
    EXPECT_EQ(
        fs->mirrorManager.get(mirrorSaiId).greProtocolType, gotGreProtocolType);
    EXPECT_EQ(
        fs->mirrorManager.get(mirrorSaiId).ipHeaderVersion, gotIpHeaderVersion);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<MirrorApi> mirrorApi;
};

TEST_F(MirrorApiTest, createLocalMirror) {
  auto mirrorSaiId = createLocalMirror(0);
  checkLocalMirror(mirrorSaiId);
}

TEST_F(MirrorApiTest, removeLocalMirror) {
  auto mirrorSaiId = createLocalMirror(0);
  mirrorApi->remove(mirrorSaiId);
}

TEST_F(MirrorApiTest, createEnhancedRemoteMirror) {
  auto mirrorSaiId = createEnhancedRemoteMirror(
      1, 16, 240, 255, srcIp, dstIp, srcMac, dstMac, 4, 2148, 0);
  checkEnhancedRemoteMirror(mirrorSaiId);
}

TEST_F(MirrorApiTest, removeEnhancedRemoteMirror) {
  auto mirrorSaiId = createEnhancedRemoteMirror(
      2, 42, 238, 255, srcIp, dstIp, srcMac, dstMac, 4, 220, 0);
  mirrorApi->remove(mirrorSaiId);
}

TEST_F(MirrorApiTest, setMirrorAttributes) {
  auto mirrorSaiId = createEnhancedRemoteMirror(
      1, 16, 240, 255, srcIp, dstIp, srcMac, dstMac, 4, 2148, 0);
  checkEnhancedRemoteMirror(mirrorSaiId);
  SaiEnhancedRemoteMirrorTraits::Attributes::MonitorPort monitorPort{2};
  mirrorApi->setAttribute(mirrorSaiId, monitorPort);
  SaiEnhancedRemoteMirrorTraits::Attributes::SrcIpAddress srcIp{
      folly::IPAddress{"41.41.41.1"}};
  mirrorApi->setAttribute(mirrorSaiId, srcIp);
  EXPECT_EQ(mirrorApi->getAttribute(mirrorSaiId, monitorPort), 2);
  EXPECT_EQ(
      mirrorApi->getAttribute(mirrorSaiId, srcIp),
      folly::IPAddress{"41.41.41.1"});
}
