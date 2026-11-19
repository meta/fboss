/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <optional>
#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/types.h"

using namespace facebook::fboss;
class MirrorManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    ManagerTestBase::SetUp();
    p0 = testInterfaces[0].remoteHosts[0].port;
    p1 = testInterfaces[1].remoteHosts[0].port;
  }
  TestInterface intf0;
  TestRemoteHost h0;
  TestPort p0;
  TestPort p1;

  void createLocalMirror(
      std::string mirrorId = "mirror1",
      PortID portId = PortID(1)) {
    auto mirror = std::make_shared<Mirror>(
        mirrorId,
        std::make_optional<PortDescriptor>(PortDescriptor(portId)),
        std::optional<folly::IPAddress>());
    saiManagerTable->mirrorManager().addNode(mirror);
  }

  void createTunnelMirror(
      sai_mirror_session_type_t type,
      std::string mirrorId = "mirror1",
      PortID portId = PortID(1),
      folly::IPAddress srcIp = folly::IPAddress{"42.42.42.42"},
      folly::IPAddress dstIp = folly::IPAddress{"43.43.43.43"},
      folly::MacAddress srcMac = folly::MacAddress{"00:11:11:11:11:11"},
      folly::MacAddress dstMac = folly::MacAddress{"00:22:22:22:22:22"},
      uint8_t ttl = 20,
      uint8_t tos = 8,
      uint32_t udpSrcPort = 1246,
      uint32_t udpDstPort = 7962) {
    auto mirror = std::make_shared<Mirror>(
        mirrorId,
        std::make_optional<PortDescriptor>(PortDescriptor(portId)),
        std::make_optional<folly::IPAddress>(dstIp),
        std::make_optional<folly::IPAddress>(srcIp),
        std::optional<TunnelUdpPorts>(),
        tos);
    mirror->setEgressPortDesc(PortDescriptor(portId));
    auto mirrorTunnel = type == SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE
        ? MirrorTunnel{srcIp, dstIp, srcMac, dstMac, ttl}
        : MirrorTunnel{
              srcIp,
              dstIp,
              srcMac,
              dstMac,
              TunnelUdpPorts{udpSrcPort, udpDstPort},
              ttl};
    mirror->setMirrorTunnel(mirrorTunnel);
    saiManagerTable->mirrorManager().addNode(mirror);
  }

  void checkLocalMirror(
      std::string mirrorId = "mirror1",
      PortID portId = PortID(1)) {
    auto portHandle = saiManagerTable->portManager().getPortHandle(portId);
    auto mirrorHandle =
        saiManagerTable->mirrorManager().getMirrorHandle(mirrorId);
    auto mirrorSaiId = mirrorHandle->adapterKey();
    auto& mirrorApi = saiApiTable->mirrorApi();
    auto gotMonitorPort = mirrorApi.getAttribute(
        mirrorSaiId, SaiLocalMirrorTraits::Attributes::MonitorPort{});
    EXPECT_EQ(gotMonitorPort, portHandle->port->adapterKey());
    auto gotMirrorType = mirrorApi.getAttribute(
        mirrorSaiId, SaiLocalMirrorTraits::Attributes::Type{});
    EXPECT_EQ(gotMirrorType, SAI_MIRROR_SESSION_TYPE_LOCAL);
  }

  void checkErspanMirror(
      std::string mirrorId = "mirror1",
      PortID portId = PortID(1),
      folly::IPAddress srcIp = folly::IPAddress{"42.42.42.42"},
      folly::IPAddress dstIp = folly::IPAddress{"43.43.43.43"},
      folly::MacAddress srcMac = folly::MacAddress{"00:11:11:11:11:11"},
      folly::MacAddress dstMac = folly::MacAddress{"00:22:22:22:22:22"},
      uint8_t ttl = 20,
      uint8_t tos = 8) {
    auto portHandle = saiManagerTable->portManager().getPortHandle(portId);
    auto mirrorHandle =
        saiManagerTable->mirrorManager().getMirrorHandle(mirrorId);
    auto mirrorSaiId = mirrorHandle->adapterKey();
    auto& mirrorApi = saiApiTable->mirrorApi();
    auto gotMonitorPort = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::MonitorPort{});
    EXPECT_EQ(gotMonitorPort, portHandle->port->adapterKey());
    auto gotMirrorType = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::Type{});
    EXPECT_EQ(gotMirrorType, SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE);
    auto gotSrcIp = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::SrcIpAddress{});
    EXPECT_EQ(gotSrcIp, srcIp);
    auto gotDstIp = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::DstIpAddress{});
    EXPECT_EQ(gotDstIp, dstIp);
    auto gotSrcMac = mirrorApi.getAttribute(
        mirrorSaiId,
        SaiEnhancedRemoteMirrorTraits::Attributes::SrcMacAddress{});
    EXPECT_EQ(gotSrcMac, srcMac);
    auto gotDstMac = mirrorApi.getAttribute(
        mirrorSaiId,
        SaiEnhancedRemoteMirrorTraits::Attributes::DstMacAddress{});
    EXPECT_EQ(gotDstMac, dstMac);
    auto gotTos = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::Tos{});
    EXPECT_EQ(gotTos, tos);
    auto gotTtl = mirrorApi.getAttribute(
        mirrorSaiId, SaiEnhancedRemoteMirrorTraits::Attributes::Ttl{});
    EXPECT_EQ(gotTtl, ttl);
  }

  void checkSflowMirror(
      std::string mirrorId = "mirror1",
      PortID portId = PortID(1),
      folly::IPAddress srcIp = folly::IPAddress{"42.42.42.42"},
      folly::IPAddress dstIp = folly::IPAddress{"43.43.43.43"},
      folly::MacAddress srcMac = folly::MacAddress{"00:11:11:11:11:11"},
      folly::MacAddress dstMac = folly::MacAddress{"00:22:22:22:22:22"},
      uint8_t ttl = 20,
      uint8_t tos = 8,
      uint32_t udpSrcPort = 1246,
      uint32_t udpDstPort = 7962) {
    auto portHandle = saiManagerTable->portManager().getPortHandle(portId);
    auto mirrorHandle =
        saiManagerTable->mirrorManager().getMirrorHandle(mirrorId);
    auto mirrorSaiId = mirrorHandle->adapterKey();
    auto& mirrorApi = saiApiTable->mirrorApi();
    auto gotMonitorPort = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::MonitorPort{});
    EXPECT_EQ(gotMonitorPort, portHandle->port->adapterKey());
    auto gotMirrorType = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::Type{});
    EXPECT_EQ(gotMirrorType, SAI_MIRROR_SESSION_TYPE_SFLOW);
    auto gotSrcIp = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::SrcIpAddress{});
    EXPECT_EQ(gotSrcIp, srcIp);
    auto gotDstIp = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::DstIpAddress{});
    EXPECT_EQ(gotDstIp, dstIp);
    auto gotSrcMac = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::SrcMacAddress{});
    EXPECT_EQ(gotSrcMac, srcMac);
    auto gotDstMac = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::DstMacAddress{});
    EXPECT_EQ(gotDstMac, dstMac);
    auto gotTos = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::Tos{});
    EXPECT_EQ(gotTos, tos);
    auto gotTtl = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::Ttl{});
    EXPECT_EQ(gotTtl, ttl);
    auto gotUdpSrcPort = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::UdpSrcPort{});
    EXPECT_EQ(gotUdpSrcPort, udpSrcPort);
    auto gotUdpDstPort = mirrorApi.getAttribute(
        mirrorSaiId, SaiSflowMirrorTraits::Attributes::UdpDstPort{});
    EXPECT_EQ(gotUdpDstPort, udpDstPort);
  }
};

TEST_F(MirrorManagerTest, createLocalMirror) {
  std::string mirrorId = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort1);
  createLocalMirror(mirrorId, PortID(p1.id));
  checkLocalMirror(mirrorId, PortID(p1.id));
}

TEST_F(MirrorManagerTest, createDuplicateLocalMirror) {
  std::string mirrorId = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort1);
  createLocalMirror(mirrorId, PortID(p1.id));
  EXPECT_THROW(createLocalMirror(mirrorId, PortID(p1.id)), FbossError);
}

TEST_F(MirrorManagerTest, removeMirror) {
  std::string mirrorId = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort1);
  auto mirror = std::make_shared<Mirror>(
      mirrorId,
      std::make_optional<PortDescriptor>(PortDescriptor(swPort1->getID())),
      std::optional<folly::IPAddress>());
  saiManagerTable->mirrorManager().addNode(mirror);
  saiManagerTable->mirrorManager().removeMirror(mirror);
  EXPECT_THROW(
      saiManagerTable->mirrorManager().removeMirror(mirror), FbossError);
}

TEST_F(MirrorManagerTest, createErspanMirror) {
  std::string mirrorId = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort1);
  createTunnelMirror(
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE, mirrorId, PortID(p1.id));
  checkErspanMirror(mirrorId, PortID(p1.id));
}

TEST_F(MirrorManagerTest, createSflowMirror) {
  std::string mirrorId = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort1);
  createTunnelMirror(SAI_MIRROR_SESSION_TYPE_SFLOW, mirrorId, PortID(p1.id));
  checkSflowMirror(mirrorId, PortID(p1.id));
}

TEST_F(MirrorManagerTest, portMirroring) {
  std::string mirrorId1 = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p0);
  saiManagerTable->portManager().addPort(swPort1);
  createLocalMirror(mirrorId1, swPort1->getID());
  std::shared_ptr<Port> swPort2 = makePort(p1);
  swPort2->setIngressMirror(mirrorId1);
  saiManagerTable->portManager().addPort(swPort2);
  SaiPortHandle* portHandle =
      saiManagerTable->portManager().getPortHandle(swPort2->getID());
  SaiMirrorHandle* mirrorHandle =
      saiManagerTable->mirrorManager().getMirrorHandle(mirrorId1);
  auto& portApi = saiApiTable->portApi();
  auto gotMirrorSaiIdList = portApi.getAttribute(
      portHandle->port->adapterKey(),
      SaiPortTraits::Attributes::IngressMirrorSession{});
  EXPECT_EQ(gotMirrorSaiIdList[0], mirrorHandle->adapterKey());
}

TEST_F(MirrorManagerTest, portMirroringAndSampling) {
  std::string mirrorId1 = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p0);
  saiManagerTable->portManager().addPort(swPort1);
  createTunnelMirror(
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE, mirrorId1, swPort1->getID());
  std::shared_ptr<Port> swPort2 = makePort(p1);
  swPort2->setIngressMirror(mirrorId1);
  swPort2->setSflowIngressRate(100);
  swPort2->setSampleDestination(cfg::SampleDestination::MIRROR);
  saiManagerTable->portManager().addPort(swPort2);
  SaiPortHandle* portHandle =
      saiManagerTable->portManager().getPortHandle(swPort2->getID());
  SaiMirrorHandle* mirrorHandle =
      saiManagerTable->mirrorManager().getMirrorHandle(mirrorId1);
  auto& portApi = saiApiTable->portApi();
  auto gotMirrorSaiIdList = portApi.getAttribute(
      portHandle->port->adapterKey(),
      SaiPortTraits::Attributes::IngressMirrorSession{});
  EXPECT_EQ(gotMirrorSaiIdList.size(), 0);
  auto gotSamplePacketSaiId = portApi.getAttribute(
      portHandle->port->adapterKey(),
      SaiPortTraits::Attributes::IngressSamplePacketEnable{});
  EXPECT_EQ(
      gotSamplePacketSaiId, portHandle->ingressSamplePacket->adapterKey());
  auto gotSampleMirrorSaiIdList = portApi.getAttribute(
      portHandle->port->adapterKey(),
      SaiPortTraits::Attributes::IngressSampleMirrorSession{});
  EXPECT_EQ(gotSampleMirrorSaiIdList[0], mirrorHandle->adapterKey());
}

// A live config change that turns on sampling to a mirror on an existing port
// must program the sample packet object before the sample mirror session,
// and detach the sample mirror session before removing the sample packet.
TEST_F(MirrorManagerTest, changePortSamplingMirror) {
  std::string mirrorId1 = "mirror1";
  std::shared_ptr<Port> swPort1 = makePort(p0);
  saiManagerTable->portManager().addPort(swPort1);
  createTunnelMirror(
      SAI_MIRROR_SESSION_TYPE_SFLOW, mirrorId1, swPort1->getID());
  std::shared_ptr<Port> swPort2 = makePort(p1);
  saiManagerTable->portManager().addPort(swPort2);

  auto sampledPort = swPort2->clone();
  sampledPort->setIngressMirror(mirrorId1);
  sampledPort->setSflowIngressRate(100);
  sampledPort->setSampleDestination(cfg::SampleDestination::MIRROR);
  EXPECT_NO_THROW(
      saiManagerTable->portManager().changePort(swPort2, sampledPort));

  SaiPortHandle* portHandle =
      saiManagerTable->portManager().getPortHandle(sampledPort->getID());
  SaiMirrorHandle* mirrorHandle =
      saiManagerTable->mirrorManager().getMirrorHandle(mirrorId1);
  auto& portApi = saiApiTable->portApi();
  ASSERT_NE(portHandle->ingressSamplePacket, nullptr);
  EXPECT_EQ(
      portApi.getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressSamplePacketEnable{}),
      portHandle->ingressSamplePacket->adapterKey());
  auto gotSampleMirrorSaiIdList = portApi.getAttribute(
      portHandle->port->adapterKey(),
      SaiPortTraits::Attributes::IngressSampleMirrorSession{});
  ASSERT_EQ(gotSampleMirrorSaiIdList.size(), 1);
  EXPECT_EQ(gotSampleMirrorSaiIdList[0], mirrorHandle->adapterKey());
  EXPECT_TRUE(portHandle->mirrorInfo.isMirrorSampled());

  auto unsampledPort = sampledPort->clone();
  unsampledPort->setIngressMirror(std::nullopt);
  unsampledPort->setSflowIngressRate(0);
  unsampledPort->setSampleDestination(std::nullopt);
  EXPECT_NO_THROW(
      saiManagerTable->portManager().changePort(sampledPort, unsampledPort));
  EXPECT_EQ(
      portApi
          .getAttribute(
              portHandle->port->adapterKey(),
              SaiPortTraits::Attributes::IngressSampleMirrorSession{})
          .size(),
      0);
  EXPECT_EQ(
      portApi.getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressSamplePacketEnable{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(portHandle->ingressSamplePacket, nullptr);
}
