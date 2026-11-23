// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiTunnelManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/IpTunnel.h"
#include "fboss/agent/types.h"
#include "folly/IPAddressV6.h"
#include "folly/logging/xlog.h"

#include <string>

using namespace facebook::fboss;

std::shared_ptr<IpTunnel> makeTunnel(
    const std::string& tunnelId = "tunnel0",
    uint32_t intfID = 0,
    cfg::TunnelMode mode = cfg::TunnelMode::PIPE) {
  auto tunnel = std::make_shared<IpTunnel>(tunnelId);
  tunnel->setType(TunnelType::IP_IN_IP_DECAP);
  tunnel->setUnderlayIntfId(InterfaceID(intfID));
  tunnel->setTTLMode(mode);
  tunnel->setDscpMode(mode);
  tunnel->setEcnMode(mode);
  tunnel->setTunnelTermType(cfg::TunnelTerminationType::P2MP);
  tunnel->setSrcIP(folly::IPAddressV6("2401:db00:11c:8202:0:0:0:100"));
  tunnel->setDstIP(folly::IPAddressV6("::"));
  tunnel->setDstIPMask(
      folly::IPAddressV6("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"));
  tunnel->setSrcIPMask(
      folly::IPAddressV6("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"));
  return tunnel;
}

class TunnelManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
  }

  void checkTunnel(
      TunnelSaiId saiId,
      std::string id,
      int32_t expType,
      int32_t expTermType,
      int32_t expTtl,
      int32_t expDscp,
      int32_t expEcn,
      const folly::IPAddress& expDstIp,
      const folly::IPAddress& expSrcIp) {
    auto type = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::Type());
    EXPECT_EQ(type, expType);
    SaiRouterInterfaceHandle* intfHandle =
        saiManagerTable->routerInterfaceManager().getRouterInterfaceHandle(
            InterfaceID(intf0.id));
    RouterInterfaceSaiId saiIntfId{intfHandle->adapterKey()};
    auto underlay = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::UnderlayInterface());
    EXPECT_EQ(underlay, saiIntfId);
    auto overlay = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::UnderlayInterface());
    EXPECT_EQ(overlay, saiIntfId);
    auto ttl = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::DecapTtlMode());
    EXPECT_EQ(ttl, expTtl);
    auto dscp = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::DecapDscpMode());
    EXPECT_EQ(dscp, expDscp);
    auto ecn = saiApiTable->tunnelApi().getAttribute(
        saiId, SaiIpInIpTunnelTraits::Attributes::DecapEcnMode());
    EXPECT_EQ(ecn, expEcn);
    auto handle = saiManagerTable->tunnelManager().getTunnelHandle(id);
    SaiVirtualRouterHandle* virtualRouterHandle =
        saiManagerTable->virtualRouterManager().getVirtualRouterHandle(
            RouterID(0));
    VirtualRouterSaiId saiVirtualRouterId{
        virtualRouterHandle->virtualRouter->adapterKey()};
    auto tunnelTerm = handle->getP2MPTunnelTermHandle();
    EXPECT_EQ(
        GET_ATTR(P2MPTunnelTerm, VrId, tunnelTerm->attributes()),
        saiVirtualRouterId);
    EXPECT_EQ(
        GET_ATTR(P2MPTunnelTerm, Type, tunnelTerm->attributes()), expTermType);
    EXPECT_EQ(
        GET_ATTR(P2MPTunnelTerm, TunnelType, tunnelTerm->attributes()),
        expType);
    EXPECT_EQ(
        GET_ATTR(P2MPTunnelTerm, DstIp, tunnelTerm->attributes()), expSrcIp);
    EXPECT_EQ(
        GET_ATTR(P2MPTunnelTerm, ActionTunnelId, tunnelTerm->attributes()),
        saiId);
  }

  TestInterface intf0;
};

std::shared_ptr<IpTunnel> makeEncapTunnel(
    const std::string& tunnelId = "encapTunnel0",
    uint32_t intfID = 0) {
  auto tunnel = std::make_shared<IpTunnel>(tunnelId);
  tunnel->setType(TunnelType::IP_IN_IP_ENCAP);
  tunnel->setUnderlayIntfId(InterfaceID(intfID));
  tunnel->setTTLMode(cfg::TunnelMode::PIPE);
  tunnel->setDscpMode(cfg::TunnelMode::PIPE);
  tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
  tunnel->setTunnelTermType(cfg::TunnelTerminationType::P2P);
  tunnel->setSrcIP(folly::IPAddressV6("2401:db00::1"));
  tunnel->setDstIP(folly::IPAddressV6("2401:db00:ffff::1"));
  return tunnel;
}

TEST_F(TunnelManagerTest, addTunnel) {
  std::shared_ptr<IpTunnel> swTunnel = makeTunnel("tunnel0", intf0.id);
  TunnelSaiId saiId = saiManagerTable->tunnelManager().addTunnel(swTunnel);
  checkTunnel(
      saiId,
      "tunnel0",
      SAI_TUNNEL_TYPE_IPINIP,
      SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP,
      SAI_TUNNEL_TTL_MODE_PIPE_MODEL,
      SAI_TUNNEL_DSCP_MODE_PIPE_MODEL,
      SAI_TUNNEL_DECAP_ECN_MODE_COPY_FROM_OUTER,
      folly::IPAddressV6("::"),
      folly::IPAddressV6("2401:db00:11c:8202:0:0:0:100"));
}

TEST_F(TunnelManagerTest, addTwoTunnels) {
  TunnelSaiId saiId0 =
      saiManagerTable->tunnelManager().addTunnel(makeTunnel("tunn0", intf0.id));
  checkTunnel(
      saiId0,
      "tunn0",
      SAI_TUNNEL_TYPE_IPINIP,
      SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP,
      SAI_TUNNEL_TTL_MODE_PIPE_MODEL,
      SAI_TUNNEL_DSCP_MODE_PIPE_MODEL,
      SAI_TUNNEL_DECAP_ECN_MODE_COPY_FROM_OUTER,
      folly::IPAddressV6("::"),
      folly::IPAddressV6("2401:db00:11c:8202:0:0:0:100"));
  TunnelSaiId saiId1 =
      saiManagerTable->tunnelManager().addTunnel(makeTunnel("tunn1", intf0.id));
  checkTunnel(
      saiId1,
      "tunn1",
      SAI_TUNNEL_TYPE_IPINIP,
      SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP,
      SAI_TUNNEL_TTL_MODE_PIPE_MODEL,
      SAI_TUNNEL_DSCP_MODE_PIPE_MODEL,
      SAI_TUNNEL_DECAP_ECN_MODE_COPY_FROM_OUTER,
      folly::IPAddressV6("::"),
      folly::IPAddressV6("2401:db00:11c:8202:0:0:0:100"));
}

TEST_F(TunnelManagerTest, addDupTunnel) {
  std::shared_ptr<IpTunnel> swTunnel = makeTunnel();
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  EXPECT_THROW(
      saiManagerTable->tunnelManager().addTunnel(swTunnel), FbossError);
}

TEST_F(TunnelManagerTest, changeTunnel) {
  std::shared_ptr<IpTunnel> swTunnel = makeTunnel("tunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto swTunnel2 = makeTunnel("tunnel1", intf0.id, cfg::TunnelMode::UNIFORM);
  swTunnel2->setSrcIP(
      folly::IPAddressV6("2001:db8:3333:4444:5555:6666:7777:8888"));
  saiManagerTable->tunnelManager().changeTunnel(swTunnel, swTunnel2);
  auto handle = saiManagerTable->tunnelManager().getTunnelHandle("tunnel1");
  EXPECT_NE(handle, nullptr);
  EXPECT_EQ(
      GET_OPT_ATTR(IpInIpTunnel, DecapTtlMode, handle->tunnel->attributes()),
      0);
  // Term obj has reversed semantics of src and dst
  auto tunnelTerm = handle->getP2MPTunnelTermHandle();
  EXPECT_EQ(
      GET_ATTR(P2MPTunnelTerm, DstIp, tunnelTerm->attributes()),
      folly::IPAddressV6("2001:db8:3333:4444:5555:6666:7777:8888"));
  SaiVirtualRouterHandle* virtualRouterHandle =
      saiManagerTable->virtualRouterManager().getVirtualRouterHandle(
          RouterID(0));
  VirtualRouterSaiId saiVirtualRouterId{
      virtualRouterHandle->virtualRouter->adapterKey()};
  EXPECT_EQ(
      GET_ATTR(P2MPTunnelTerm, VrId, tunnelTerm->attributes()),
      saiVirtualRouterId);
}

TEST_F(TunnelManagerTest, getNonexistentTunnel) {
  auto swTunnel = makeTunnel("tunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto handle = saiManagerTable->tunnelManager().getTunnelHandle("tunnel1");
  EXPECT_FALSE(handle);
}

TEST_F(TunnelManagerTest, removeTunnel) {
  auto swTunnel = makeTunnel("tunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto& tunnelManager = saiManagerTable->tunnelManager();
  tunnelManager.removeTunnel(swTunnel);
  auto handle = saiManagerTable->tunnelManager().getTunnelHandle("tunnel0");
  EXPECT_FALSE(handle);
}

TEST_F(TunnelManagerTest, removeNonexistentTunnel) {
  auto swTunnel = makeTunnel("tunnel0");
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto& tunnelManager = saiManagerTable->tunnelManager();
  auto swTunnel2 = makeTunnel("tunnel1");
  EXPECT_THROW(tunnelManager.removeTunnel(swTunnel2), FbossError);
}

TEST_F(TunnelManagerTest, addEncapTunnel) {
  auto swTunnel = makeEncapTunnel("encapTunnel0", intf0.id);
  TunnelSaiId saiId = saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto type = saiApiTable->tunnelApi().getAttribute(
      saiId, SaiIpInIpTunnelTraits::Attributes::Type());
  EXPECT_EQ(type, SAI_TUNNEL_TYPE_IPINIP);
  auto encapSrcIp = saiApiTable->tunnelApi().getAttribute(
      saiId, SaiIpInIpTunnelTraits::Attributes::EncapSrcIp());
  EXPECT_EQ(encapSrcIp, folly::IPAddress("2401:db00::1"));
  auto encapTtlMode = saiApiTable->tunnelApi().getAttribute(
      saiId, SaiIpInIpTunnelTraits::Attributes::EncapTtlMode());
  EXPECT_EQ(encapTtlMode, SAI_TUNNEL_TTL_MODE_PIPE_MODEL);
  auto encapDscpMode = saiApiTable->tunnelApi().getAttribute(
      saiId, SaiIpInIpTunnelTraits::Attributes::EncapDscpMode());
  EXPECT_EQ(encapDscpMode, SAI_TUNNEL_DSCP_MODE_PIPE_MODEL);
}

TEST_F(TunnelManagerTest, encapTunnelNoTerm) {
  auto swTunnel = makeEncapTunnel("encapTunnel0", intf0.id);
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto handle =
      saiManagerTable->tunnelManager().getTunnelHandle("encapTunnel0");
  EXPECT_NE(handle, nullptr);
  EXPECT_FALSE(handle->tunnelTerm.has_value());
  EXPECT_EQ(handle->getP2MPTunnelTermHandle(), nullptr);
}

TEST_F(TunnelManagerTest, removeEncapTunnel) {
  auto swTunnel = makeEncapTunnel("encapTunnel0", intf0.id);
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  saiManagerTable->tunnelManager().removeTunnel(swTunnel);
  auto handle =
      saiManagerTable->tunnelManager().getTunnelHandle("encapTunnel0");
  EXPECT_FALSE(handle);
}

TEST_F(TunnelManagerTest, changeEncapTunnel) {
  auto swTunnel = makeEncapTunnel("encapTunnel0", intf0.id);
  saiManagerTable->tunnelManager().addTunnel(swTunnel);
  auto swTunnel2 = makeEncapTunnel("encapTunnel0", intf0.id);
  swTunnel2->setSrcIP(folly::IPAddressV6("2401:db00::2"));
  saiManagerTable->tunnelManager().changeTunnel(swTunnel, swTunnel2);
  auto handle =
      saiManagerTable->tunnelManager().getTunnelHandle("encapTunnel0");
  EXPECT_NE(handle, nullptr);
  auto encapSrcIp = saiApiTable->tunnelApi().getAttribute(
      handle->tunnel->adapterKey(),
      SaiIpInIpTunnelTraits::Attributes::EncapSrcIp());
  EXPECT_EQ(encapSrcIp, folly::IPAddress("2401:db00::2"));
}

TEST_F(TunnelManagerTest, vxlanTunnelNotImplemented) {
  auto swTunnel = makeTunnel("vxlanTunnel0", intf0.id);
  swTunnel->setType(TunnelType::VXLAN);
  try {
    saiManagerTable->tunnelManager().addTunnel(swTunnel);
    FAIL() << "Expected FbossError for VXLAN tunnel";
  } catch (const FbossError& e) {
    EXPECT_NE(
        std::string(e.what()).find("VXLAN tunnel not implemented yet"),
        std::string::npos);
  }
}
