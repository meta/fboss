/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/HwTestMirrorUtils.h"
#include "fboss/agent/state/Mirror.h"

#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include <gtest/gtest.h>

namespace facebook::fboss::utility {

#define MIRROR_PORT_INGRESS 0x00000001
#define MIRROR_PORT_EGRESS 0x00000002
#define MIRROR_PORT_SFLOW 0x00000004

void getAllMirrorDestinations(
    facebook::fboss::HwSwitch* hwSwitch,
    std::vector<uint64_t>& destinations) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch);
  auto mirrorSaiOids =
      saiSwitch->managerTable()->mirrorManager().getAllMirrorSessionOids();
  std::transform(
      mirrorSaiOids.begin(),
      mirrorSaiOids.end(),
      std::back_inserter(destinations),
      [](MirrorSaiId mirrorOid) -> uint64_t { return mirrorOid; });
}

uint32_t getMirrorPortIngressAndSflowFlags() {
  return MIRROR_PORT_INGRESS | MIRROR_PORT_SFLOW;
}

uint32_t getMirrorPortIngressFlags() {
  return MIRROR_PORT_INGRESS;
}

uint32_t getMirrorPortEgressFlags() {
  return MIRROR_PORT_EGRESS;
}

uint32_t getMirrorPortSflowFlags() {
  return MIRROR_PORT_SFLOW;
}

static void verifyResolvedLocalMirror(
    const SaiSwitch* saiSwitch,
    const std::shared_ptr<facebook::fboss::Mirror>& mirror,
    SaiMirrorHandle* mirrorHandle) {
  auto portHandle = saiSwitch->managerTable()->portManager().getPortHandle(
      mirror->getEgressPortDesc().value().phyPortID());
  ASSERT_NE(portHandle, nullptr);
  auto monitorPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiLocalMirrorTraits::Attributes::MonitorPort());
  EXPECT_EQ(portHandle->port->adapterKey(), monitorPort);

  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiLocalMirrorTraits::Attributes::Type());
  EXPECT_EQ(type, SAI_MIRROR_SESSION_TYPE_LOCAL);
}

static void verifyResolvedErspanMirror(
    const SaiSwitch* saiSwitch,
    const std::shared_ptr<facebook::fboss::Mirror>& mirror,
    SaiMirrorHandle* mirrorHandle) {
  auto portHandle = saiSwitch->managerTable()->portManager().getPortHandle(
      mirror->getEgressPortDesc().value().phyPortID());
  ASSERT_NE(portHandle, nullptr);
  auto monitorPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::MonitorPort());
  EXPECT_EQ(portHandle->port->adapterKey(), monitorPort);

  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiLocalMirrorTraits::Attributes::Type());
  EXPECT_EQ(type, SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE);

  // TODO: add truncate check

  // TOS
  auto tos = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::Tos());
  EXPECT_EQ(tos, mirror->getDscp() << 2);

  const auto& tunnel = mirror->getMirrorTunnel();

  // src and dst IP
  auto srcIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::SrcIpAddress());
  EXPECT_EQ(srcIp, tunnel->srcIp);
  auto dstIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::DstIpAddress());
  EXPECT_EQ(dstIp, tunnel->dstIp);

  // src and dst MAC
  auto srcMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::SrcMacAddress());
  EXPECT_EQ(srcMac, tunnel->srcMac);
  auto dstMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::DstMacAddress());
  EXPECT_EQ(dstMac, tunnel->dstMac);

  // TTL
  auto ttl = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::Ttl());
  EXPECT_EQ(ttl, tunnel->ttl);
}

static void verifyResolvedSflowMirror(
    const SaiSwitch* saiSwitch,
    const std::shared_ptr<facebook::fboss::Mirror>& mirror,
    SaiMirrorHandle* mirrorHandle) {
  auto portHandle = saiSwitch->managerTable()->portManager().getPortHandle(
      mirror->getEgressPortDesc().value().phyPortID());
  ASSERT_NE(portHandle, nullptr);
  auto monitorPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::MonitorPort());
  EXPECT_EQ(portHandle->port->adapterKey(), monitorPort);

  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiLocalMirrorTraits::Attributes::Type());
  EXPECT_EQ(type, SAI_MIRROR_SESSION_TYPE_SFLOW);

  // TODO: add truncate check

  // TOS
  auto tos = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiSflowMirrorTraits::Attributes::Tos());
  EXPECT_EQ(tos, mirror->getDscp() << 2);

  const auto& tunnel = mirror->getMirrorTunnel();

  // src and dst IP
  auto srcIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::SrcIpAddress());
  EXPECT_EQ(srcIp, tunnel->srcIp);
  auto dstIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::DstIpAddress());
  EXPECT_EQ(dstIp, tunnel->dstIp);

  // src and dst MAC
  auto srcMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::SrcMacAddress());
  EXPECT_EQ(srcMac, tunnel->srcMac);
  auto dstMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::DstMacAddress());
  EXPECT_EQ(dstMac, tunnel->dstMac);

  // src and dst UDP port
  auto udpPorts = mirror->getTunnelUdpPorts();
  auto udpSrcPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::UdpSrcPort());
  EXPECT_EQ(udpPorts.value().udpSrcPort, udpSrcPort);
  auto udpDstPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::UdpDstPort());
  EXPECT_EQ(udpPorts.value().udpDstPort, udpDstPort);

  // TTL
  auto ttl = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiSflowMirrorTraits::Attributes::Ttl());
  EXPECT_EQ(ttl, tunnel->ttl);
}

void verifyResolvedMirror(
    facebook::fboss::HwSwitch* hwSwitch,
    const std::shared_ptr<facebook::fboss::Mirror>& mirror) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch);
  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(
          mirror->getID());
  ASSERT_NE(mirrorHandle, nullptr);
  if (mirror->getMirrorTunnel().has_value()) {
    auto mirrorTunnel = mirror->getMirrorTunnel().value();
    if (mirrorTunnel.udpPorts.has_value()) {
      verifyResolvedSflowMirror(saiSwitch, mirror, mirrorHandle);
    } else {
      verifyResolvedErspanMirror(saiSwitch, mirror, mirrorHandle);
    }
  } else {
    verifyResolvedLocalMirror(saiSwitch, mirror, mirrorHandle);
  }
}

void verifyUnResolvedMirror(
    facebook::fboss::HwSwitch* hwSwitch,
    const std::shared_ptr<facebook::fboss::Mirror>& mirror) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch);
  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(
          mirror->getID());
  ASSERT_EQ(mirrorHandle, nullptr);
}

static void verifyPortMirrorDestinationImpl(
    facebook::fboss::HwSwitch* hwSwitch,
    PortID port,
    uint32_t flags,
    std::optional<uint64_t> mirror_dest_id) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch);
  auto portHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(port);
  std::vector<sai_object_id_t> mirrorSaiOidList;
  if (flags & MIRROR_PORT_SFLOW) {
    if (flags & MIRROR_PORT_INGRESS) {
      mirrorSaiOidList = SaiApiTable::getInstance()->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressSampleMirrorSession());
    } else {
      mirrorSaiOidList = SaiApiTable::getInstance()->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::EgressSampleMirrorSession());
    }
  } else {
    if (flags & MIRROR_PORT_INGRESS) {
      mirrorSaiOidList = SaiApiTable::getInstance()->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressMirrorSession());
    } else {
      mirrorSaiOidList = SaiApiTable::getInstance()->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::EgressMirrorSession());
    }
  }

  if (mirror_dest_id.has_value()) {
    EXPECT_NE(mirrorSaiOidList.size(), 0);
    EXPECT_EQ(mirrorSaiOidList[0], mirror_dest_id.value());
  } else {
    EXPECT_EQ(mirrorSaiOidList.size(), 0);
  }
}

void verifyPortMirrorDestination(
    facebook::fboss::HwSwitch* hwSwitch,
    PortID port,
    uint32_t flags,
    uint64_t mirror_dest_id) {
  verifyPortMirrorDestinationImpl(hwSwitch, port, flags, mirror_dest_id);
}

void verifyPortNoMirrorDestination(
    facebook::fboss::HwSwitch* hwSwitch,
    PortID port,
    uint32_t flags) {
  verifyPortMirrorDestinationImpl(hwSwitch, port, flags, std::nullopt);
}

void verifyAclMirrorDestination(
    facebook::fboss::HwSwitch* /* hwSwitch */,
    const std::string& /* mirrorId */) {}

void verifyNoAclMirrorDestination(
    facebook::fboss::HwSwitch* /* hwSwitch */,
    const std::string& /* mirrorId */) {}

bool isMirrorSflowTunnelEnabled(
    facebook::fboss::HwSwitch* /* hwSwitch */,
    uint64_t destination) {
  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      MirrorSaiId(destination), SaiSflowMirrorTraits::Attributes::Type());
  return type == SAI_MIRROR_SESSION_TYPE_SFLOW;
}

HwResourceStats getHwTableStats(facebook::fboss::HwSwitch* hwSwitch) {
  return hwSwitch->getResourceStats();
}

} // namespace facebook::fboss::utility
