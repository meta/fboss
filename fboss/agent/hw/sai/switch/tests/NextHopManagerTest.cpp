/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/switch/SaiFdbManager.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6TunnelManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/Srv6Tunnel.h"

#include <unordered_set>

using namespace facebook::fboss;

class NextHopManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
    intf1 = testInterfaces[1];
    intf2 = testInterfaces[2];
    resolveArp(intf0.id, intf0.remoteHosts[0]);
    resolveArp(intf1.id, intf1.remoteHosts[0]);
    resolveArp(intf2.id, intf2.remoteHosts[0]);
  }

  void checkNextHop(
      NextHopSaiId nextHopId,
      RouterInterfaceSaiId expectedRifId,
      const folly::IPAddress& expectedIp) {
    auto rifIdGot = saiApiTable->nextHopApi().getAttribute(
        nextHopId, SaiIpNextHopTraits::Attributes::RouterInterfaceId());
    EXPECT_EQ(rifIdGot, expectedRifId);
    auto ipGot = saiApiTable->nextHopApi().getAttribute(
        nextHopId, SaiIpNextHopTraits::Attributes::Ip());
    EXPECT_EQ(ipGot, expectedIp);
    auto typeGot = saiApiTable->nextHopApi().getAttribute(
        nextHopId, SaiIpNextHopTraits::Attributes::Type());
    EXPECT_EQ(typeGot, SAI_NEXT_HOP_TYPE_IP);
  }

  void checkNextHopGroup(
      NextHopGroupSaiId nextHopGroupId,
      sai_next_hop_group_type_t expectedType,
      const std::unordered_set<folly::IPAddress>& expectedNextHopIps) {
    auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
    auto type = nextHopGroupApi.getAttribute(
        nextHopGroupId, SaiNextHopGroupTraits::Attributes::Type{});
    EXPECT_EQ(type, expectedType);

    auto members = nextHopGroupApi.getAttribute(
        nextHopGroupId, SaiNextHopGroupTraits::Attributes::NextHopMemberList{});
    std::unordered_set<folly::IPAddress> nextHopIps;
    for (auto member : members) {
      auto nextHopId = nextHopGroupApi.getAttribute(
          NextHopGroupMemberSaiId(member),
          SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
      nextHopIps.insert(saiApiTable->nextHopApi().getAttribute(
          NextHopSaiId(nextHopId), SaiIpNextHopTraits::Attributes::Ip{}));
    }
    EXPECT_EQ(nextHopIps, expectedNextHopIps);
  }

  ResolvedNextHop makeResolvedNextHop(
      const TestInterface& intf,
      NextHopRole role) const {
    return ResolvedNextHop{
        intf.remoteHosts[0].ip,
        InterfaceID(intf.id),
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        role};
  }

  TestInterface intf0;
  TestInterface intf1;
  TestInterface intf2;
};

TEST_F(NextHopManagerTest, testAddNextHop) {
  RouterInterfaceSaiId rifId{42};
  folly::IPAddress ip4{"42.42.42.42"};
  std::shared_ptr<SaiIpNextHop> nextHop =
      saiManagerTable->nextHopManager().addNextHop(rifId, ip4);
  checkNextHop(nextHop->adapterKey(), rifId, ip4);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(NextHopManagerTest, testProtectionNextHopGroup) {
  FLAGS_flowletSwitchingEnable = true;
  saiManagerTable->nextHopGroupManager().setPrimaryArsSwitchingMode(
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  RouteNextHopEntry::NextHopSet backupNhops{
      makeResolvedNextHop(intf1, NextHopRole::BACKUP),
      makeResolvedNextHop(intf2, NextHopRole::BACKUP),
  };
  auto swNextHops = backupNhops;
  swNextHops.insert(makeResolvedNextHop(intf0, NextHopRole::PRIMARY));

  auto nextHopGroupHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              swNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  ASSERT_NE(nextHopGroupHandle->nextHopGroup, nullptr);
  EXPECT_EQ(nextHopGroupHandle->desiredEcmpSwitchingMode_, std::nullopt);
  auto type = saiApiTable->nextHopGroupApi().getAttribute(
      nextHopGroupHandle->nextHopGroup->adapterKey(),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(type, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);
  ASSERT_EQ(nextHopGroupHandle->members_.size(), 1);
  ASSERT_NE(nextHopGroupHandle->childGroupMember_, nullptr);
  auto childGroupMember =
      nextHopGroupHandle->childGroupMember_->getNhopGroupMemberObject();
  ASSERT_NE(childGroupMember, nullptr);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  EXPECT_EQ(
      std::get<3>(childGroupMember->attributes()),
      SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
          SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_STANDBY});
#endif
  auto childNextHopGroupId = saiApiTable->nextHopGroupApi().getAttribute(
      childGroupMember->adapterKey(),
      SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  checkNextHopGroup(
      NextHopGroupSaiId(childNextHopGroupId),
      SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION,
      {intf1.remoteHosts[0].ip, intf2.remoteHosts[0].ip});
  auto backupNextHopGroupHandle =
      saiManagerTable->nextHopGroupManager().getNextHopGroup(SaiNextHopGroupKey(
          backupNhops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION));
  ASSERT_NE(backupNextHopGroupHandle, nullptr);
  EXPECT_EQ(
      backupNextHopGroupHandle->desiredEcmpSwitchingMode_,
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  EXPECT_EQ(backupNextHopGroupHandle->childGroupMember_, nullptr);
  for (const auto& member : backupNextHopGroupHandle->members_) {
    ASSERT_NE(member->getObject(), nullptr);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    EXPECT_EQ(std::get<3>(member->getObject()->attributes()), std::nullopt);
#endif
  }
  for (const auto& member : nextHopGroupHandle->members_) {
    ASSERT_NE(member->getObject(), nullptr);
    EXPECT_EQ(std::get<2>(member->getObject()->attributes()), std::nullopt);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    EXPECT_EQ(
        std::get<3>(member->getObject()->attributes()),
        SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
            SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_PRIMARY});
#endif
  }
}

TEST_F(NextHopManagerTest, testProtectionNextHopGroupMemberResolution) {
  const auto& primaryHost = intf0.remoteHosts[0];
  const auto& backupHost = intf1.remoteHosts[0];
  auto primaryArpEntry = makeArpEntry(intf0.id, primaryHost);
  auto backupArpEntry = makeArpEntry(intf1.id, backupHost);
  saiManagerTable->neighborManager().removeNeighbor(primaryArpEntry);
  saiManagerTable->neighborManager().removeNeighbor(backupArpEntry);

  auto backup = makeResolvedNextHop(intf1, NextHopRole::BACKUP);
  RouteNextHopEntry::NextHopSet backupNhops{backup};
  RouteNextHopEntry::NextHopSet swNextHops{
      makeResolvedNextHop(intf0, NextHopRole::PRIMARY),
      backup,
  };
  auto parentGroupHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              swNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  ASSERT_NE(parentGroupHandle->nextHopGroup, nullptr);
  auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
  auto parentGroupType = nextHopGroupApi.getAttribute(
      parentGroupHandle->nextHopGroup->adapterKey(),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(parentGroupType, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);
  ASSERT_NE(parentGroupHandle->childGroupMember_, nullptr);
  auto childGroupMember =
      parentGroupHandle->childGroupMember_->getNhopGroupMemberObject();
  ASSERT_NE(childGroupMember, nullptr);
  auto childGroupId = nextHopGroupApi.getAttribute(
      childGroupMember->adapterKey(),
      SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  auto childGroupType = nextHopGroupApi.getAttribute(
      NextHopGroupSaiId(childGroupId),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(childGroupType, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION);

  auto childGroupHandle =
      saiManagerTable->nextHopGroupManager().getNextHopGroup(SaiNextHopGroupKey(
          backupNhops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION));
  ASSERT_NE(childGroupHandle, nullptr);
  ASSERT_EQ(parentGroupHandle->members_.size(), 1);
  ASSERT_EQ(childGroupHandle->members_.size(), 1);
  EXPECT_FALSE(parentGroupHandle->members_[0]->isProgrammed());
  EXPECT_FALSE(childGroupHandle->members_[0]->isProgrammed());

  primaryArpEntry = resolveArp(intf0.id, primaryHost);
  EXPECT_TRUE(parentGroupHandle->members_[0]->isProgrammed());
  EXPECT_FALSE(childGroupHandle->members_[0]->isProgrammed());

  backupArpEntry = resolveArp(intf1.id, backupHost);
  EXPECT_TRUE(parentGroupHandle->members_[0]->isProgrammed());
  EXPECT_TRUE(childGroupHandle->members_[0]->isProgrammed());

  saiManagerTable->neighborManager().removeNeighbor(primaryArpEntry);
  saiManagerTable->neighborManager().removeNeighbor(backupArpEntry);
  EXPECT_FALSE(parentGroupHandle->members_[0]->isProgrammed());
  EXPECT_FALSE(childGroupHandle->members_[0]->isProgrammed());

  resolveArp(intf0.id, primaryHost);
  resolveArp(intf1.id, backupHost);
  EXPECT_TRUE(parentGroupHandle->members_[0]->isProgrammed());
  EXPECT_TRUE(childGroupHandle->members_[0]->isProgrammed());
}

TEST_F(NextHopManagerTest, testProtectionGroupsWithDifferentBackupsDoNotAlias) {
  auto primary = makeResolvedNextHop(intf0, NextHopRole::PRIMARY);
  RouteNextHopEntry::NextHopSet firstNextHops{
      primary,
      makeResolvedNextHop(intf1, NextHopRole::BACKUP),
  };
  RouteNextHopEntry::NextHopSet secondNextHops{
      primary,
      makeResolvedNextHop(intf2, NextHopRole::BACKUP),
  };

  auto firstHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              firstNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  auto secondHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              secondNextHops,
              std::nullopt,
              SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));

  ASSERT_NE(firstHandle->nextHopGroup, nullptr);
  ASSERT_NE(secondHandle->nextHopGroup, nullptr);
  EXPECT_NE(
      firstHandle->nextHopGroup->adapterHostKey(),
      secondHandle->nextHopGroup->adapterHostKey());
  EXPECT_NE(
      firstHandle->nextHopGroup->adapterKey(),
      secondHandle->nextHopGroup->adapterKey());
}

TEST_F(NextHopManagerTest, testProtectionGroupsWithSameBackupShareChildGroup) {
  auto backup = makeResolvedNextHop(intf2, NextHopRole::BACKUP);
  RouteNextHopEntry::NextHopSet backupNhops{backup};
  RouteNextHopEntry::NextHopSet firstNextHops{
      makeResolvedNextHop(intf0, NextHopRole::PRIMARY),
      backup,
  };
  RouteNextHopEntry::NextHopSet secondNextHops{
      makeResolvedNextHop(intf1, NextHopRole::PRIMARY),
      backup,
  };

  auto firstHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              firstNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  auto secondHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              secondNextHops,
              std::nullopt,
              SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));

  ASSERT_NE(firstHandle->childGroupMember_, nullptr);
  ASSERT_NE(secondHandle->childGroupMember_, nullptr);
  auto firstChildMember =
      firstHandle->childGroupMember_->getNhopGroupMemberObject();
  auto secondChildMember =
      secondHandle->childGroupMember_->getNhopGroupMemberObject();
  ASSERT_NE(firstChildMember, nullptr);
  ASSERT_NE(secondChildMember, nullptr);
  auto firstChildGroupId = saiApiTable->nextHopGroupApi().getAttribute(
      firstChildMember->adapterKey(),
      SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  auto secondChildGroupId = saiApiTable->nextHopGroupApi().getAttribute(
      secondChildMember->adapterKey(),
      SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  EXPECT_EQ(firstChildGroupId, secondChildGroupId);

  auto backupHandle =
      saiManagerTable->nextHopGroupManager().getNextHopGroup(SaiNextHopGroupKey(
          backupNhops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION));
  ASSERT_NE(backupHandle, nullptr);
  ASSERT_NE(backupHandle->nextHopGroup, nullptr);
  EXPECT_EQ(backupHandle->nextHopGroup->adapterKey(), firstChildGroupId);
}
#endif

TEST_F(NextHopManagerTest, testPrimaryOnlyNextHopGroup) {
  RouteNextHopEntry::NextHopSet swNextHops{
      makeResolvedNextHop(intf0, NextHopRole::PRIMARY),
      makeResolvedNextHop(intf1, NextHopRole::PRIMARY),
  };

  auto nextHopGroupHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(swNextHops, std::nullopt));
  ASSERT_NE(nextHopGroupHandle->nextHopGroup, nullptr);
  EXPECT_EQ(nextHopGroupHandle->childGroupMember_, nullptr);
  for (const auto& member : nextHopGroupHandle->members_) {
    ASSERT_NE(member->getObject(), nullptr);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    EXPECT_EQ(std::get<3>(member->getObject()->attributes()), std::nullopt);
#endif
  }
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(NextHopManagerTest, testBackupOnlyNextHopGroup) {
  RouteNextHopEntry::NextHopSet swNextHops{
      makeResolvedNextHop(intf1, NextHopRole::BACKUP),
      makeResolvedNextHop(intf2, NextHopRole::BACKUP),
  };

  auto parentGroupHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(
              swNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  ASSERT_NE(parentGroupHandle->nextHopGroup, nullptr);
  auto parentType = saiApiTable->nextHopGroupApi().getAttribute(
      parentGroupHandle->nextHopGroup->adapterKey(),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(parentType, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);
  EXPECT_TRUE(parentGroupHandle->members_.empty());
  ASSERT_NE(parentGroupHandle->childGroupMember_, nullptr);
  auto childGroupMember =
      parentGroupHandle->childGroupMember_->getNhopGroupMemberObject();
  ASSERT_NE(childGroupMember, nullptr);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  EXPECT_EQ(
      std::get<3>(childGroupMember->attributes()),
      SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
          SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_STANDBY});
#endif
  auto childGroupId = saiApiTable->nextHopGroupApi().getAttribute(
      childGroupMember->adapterKey(),
      SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  checkNextHopGroup(
      NextHopGroupSaiId(childGroupId),
      SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION,
      {intf1.remoteHosts[0].ip, intf2.remoteHosts[0].ip});

  auto childGroupHandle =
      saiManagerTable->nextHopGroupManager().getNextHopGroup(SaiNextHopGroupKey(
          swNextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION));
  ASSERT_NE(childGroupHandle, nullptr);
  ASSERT_EQ(childGroupHandle->members_.size(), 2);
  EXPECT_EQ(childGroupHandle->childGroupMember_, nullptr);
  for (const auto& member : childGroupHandle->members_) {
    ASSERT_NE(member->getObject(), nullptr);
    EXPECT_EQ(std::get<2>(member->getObject()->attributes()), std::nullopt);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    EXPECT_EQ(std::get<3>(member->getObject()->attributes()), std::nullopt);
#endif
  }
}
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
class Srv6NextHopManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
    intf0 = testInterfaces[0];
    resolveArp(intf0.id, intf0.remoteHosts[0]);
  }

  std::shared_ptr<Srv6Tunnel> makeSrv6Tunnel(
      const std::string& tunnelId,
      uint32_t intfID) {
    auto tunnel = std::make_shared<Srv6Tunnel>(tunnelId);
    tunnel->setType(TunnelType::SRV6_ENCAP);
    tunnel->setUnderlayIntfId(InterfaceID(intfID));
    tunnel->setSrcIP(folly::IPAddressV6("2001:db8::1"));
    tunnel->setTTLMode(cfg::TunnelMode::PIPE);
    tunnel->setDscpMode(cfg::TunnelMode::UNIFORM);
    tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
    return tunnel;
  }

  ResolvedNextHop makeSrv6NextHop(
      const TestInterface& testInterface,
      const std::string& tunnelId) const {
    const auto& remote = testInterface.remoteHosts.at(0);
    std::vector<folly::IPAddressV6> segmentList{
        folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
    return ResolvedNextHop{
        remote.ip,
        InterfaceID(testInterface.id),
        ECMP_WEIGHT,
        std::nullopt, // labelForwardingAction
        std::nullopt, // disableTTLDecrement
        std::nullopt, // topologyInfo
        std::nullopt, // adjustedWeight
        segmentList,
        TunnelType::SRV6_ENCAP,
        tunnelId};
  }

  std::shared_ptr<SaiSrv6SidListHandle> makeSrv6SidListHandle(
      const ResolvedNextHop& swNextHop) {
    auto rifHandle =
        saiManagerTable->routerInterfaceManager().getRouterInterfaceHandle(
            swNextHop.intfID().value());
    auto [sidListKey, sidListAttrs] =
        makeSrv6SidListKeyAndAttributes(rifHandle->adapterKey(), swNextHop);
    return saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(
        sidListKey, sidListAttrs);
  }

  TestInterface intf0;
};

TEST_F(Srv6NextHopManagerTest, getAdapterHostKeySrv6) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto sidListId =
      srv6SidListHandle->managedSidList->getSidList()->adapterKey();
  auto adapterHostKey =
      saiManagerTable->nextHopManager().getAdapterHostKey(swNextHop, sidListId);

  auto* srv6Key =
      std::get_if<SaiSrv6SidlistNextHopTraits::AdapterHostKey>(&adapterHostKey);
  ASSERT_NE(srv6Key, nullptr);
}

TEST_F(Srv6NextHopManagerTest, getAdapterHostKeyMissingTunnel) {
  auto swNextHop = makeSrv6NextHop(intf0, "nonexistent_tunnel");
  EXPECT_THROW(
      saiManagerTable->nextHopManager().getAdapterHostKey(swNextHop),
      FbossError);
}

TEST_F(Srv6NextHopManagerTest, addManagedSrv6NextHop) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  auto* srv6NextHop =
      std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(&managedNextHop);
  ASSERT_NE(srv6NextHop, nullptr);
  EXPECT_NE(*srv6NextHop, nullptr);
}

TEST_F(Srv6NextHopManagerTest, addManagedSrv6NextHopCreatesSidList) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  auto* srv6NextHop =
      std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(&managedNextHop);
  ASSERT_NE(srv6NextHop, nullptr);
  ASSERT_NE(*srv6NextHop, nullptr);

  // Verify the SID list handle was cached on the managed next hop
  auto& sidListHandle = (*srv6NextHop)->getSrv6SidListHandle();
  ASSERT_NE(sidListHandle, nullptr);
  ASSERT_NE(sidListHandle->managedSidList->getSidList(), nullptr);

  // Verify SID list attributes
  auto sidListId = sidListHandle->managedSidList->getSidList()->adapterKey();
  auto gotType = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::Type{});
  EXPECT_EQ(gotType, SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);

  auto gotSegments = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::SegmentList{});
  auto expectedSegments = toSaiIp6List(
      {folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")});
  EXPECT_EQ(gotSegments, expectedSegments);

  // Verify NextHopId was set on the SID list to the underlay IP nhop
  ASSERT_NE((*srv6NextHop)->getSaiObject(), nullptr);
  auto& underlayNhOpt = (*srv6NextHop)->getUnderlayNextHop();
  ASSERT_TRUE(underlayNhOpt.has_value());
  auto underlayIpNhop =
      std::get<std::shared_ptr<ManagedIpNextHop>>(*underlayNhOpt);
  ASSERT_NE(underlayIpNhop->getSaiObject(), nullptr);
  auto gotNextHopId = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::NextHopId{});
  EXPECT_EQ(gotNextHopId, underlayIpNhop->getSaiObject()->adapterKey());
}

TEST_F(Srv6NextHopManagerTest, addManagedSrv6NextHopSidListInSrv6Manager) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  // Get the SID list's AdapterHostKey from the managed next hop
  auto* srv6NextHop =
      std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(&managedNextHop);
  ASSERT_NE(srv6NextHop, nullptr);
  ASSERT_NE(*srv6NextHop, nullptr);
  auto& sidListHandle = (*srv6NextHop)->getSrv6SidListHandle();
  ASSERT_NE(sidListHandle, nullptr);
  ASSERT_NE(sidListHandle->managedSidList->getSidList(), nullptr);
  auto sidListKey =
      sidListHandle->managedSidList->getSidList()->adapterHostKey();

  // Verify the SID list was inserted into SaiSrv6SidListManager
  auto* handle =
      saiManagerTable->srv6SidListManager().getSrv6SidListHandle(sidListKey);
  EXPECT_NE(handle, nullptr);
}

TEST_F(Srv6NextHopManagerTest, getManagedSrv6NextHop) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto sidListId =
      srv6SidListHandle->managedSidList->getSidList()->adapterKey();
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  auto adapterHostKey =
      saiManagerTable->nextHopManager().getAdapterHostKey(swNextHop, sidListId);
  auto* srv6Key =
      std::get_if<SaiSrv6SidlistNextHopTraits::AdapterHostKey>(&adapterHostKey);
  ASSERT_NE(srv6Key, nullptr);

  auto* got = saiManagerTable->nextHopManager().getManagedNextHop(*srv6Key);
  EXPECT_NE(got, nullptr);
}

TEST_F(Srv6NextHopManagerTest, sidListFreedWhenManagedNextHopDestroyed) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  SaiSrv6SidListTraits::AdapterHostKey sidListKey;

  {
    auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
    auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
    auto managedNextHop =
        saiManagerTable->nextHopManager().addManagedSaiNextHop(
            swNextHop, std::move(srv6SidListHandle));

    // Get the SID list key from the managed next hop
    auto* srv6NextHop =
        std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(&managedNextHop);
    ASSERT_NE(srv6NextHop, nullptr);
    ASSERT_NE(*srv6NextHop, nullptr);
    auto& sidListHandle = (*srv6NextHop)->getSrv6SidListHandle();
    ASSERT_NE(sidListHandle, nullptr);
    sidListKey = sidListHandle->managedSidList->getSidList()->adapterHostKey();

    // Verify SID list exists in SaiSrv6SidListManager while managed next hop is
    // alive
    auto* handle =
        saiManagerTable->srv6SidListManager().getSrv6SidListHandle(sidListKey);
    ASSERT_NE(handle, nullptr);
  }
  // managedNextHop destroyed here — SID list should be freed

  auto* handle =
      saiManagerTable->srv6SidListManager().getSrv6SidListHandle(sidListKey);
  EXPECT_EQ(handle, nullptr);
}

TEST_F(Srv6NextHopManagerTest, listManagedSrv6NextHops) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  auto output = saiManagerTable->nextHopManager().listManagedObjects();
  EXPECT_FALSE(output.empty());
  EXPECT_NE(output.find("srv6"), std::string::npos);
}
TEST_F(Srv6NextHopManagerTest, linkDownAndReResolveUsesCachedSidList) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);

  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto srv6SidListHandle = makeSrv6SidListHandle(swNextHop);
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, std::move(srv6SidListHandle));

  auto* srv6NextHop =
      std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(&managedNextHop);
  ASSERT_NE(srv6NextHop, nullptr);
  ASSERT_NE(*srv6NextHop, nullptr);

  // Record initial SAI object and SID list info
  ASSERT_NE((*srv6NextHop)->getSaiObject(), nullptr);
  auto& sidListHandle = (*srv6NextHop)->getSrv6SidListHandle();
  ASSERT_NE(sidListHandle, nullptr);
  ASSERT_NE(sidListHandle->managedSidList->getSidList(), nullptr);
  auto sidListId = sidListHandle->managedSidList->getSidList()->adapterKey();

  // Get the underlay IP nhop whose adapter key is used as NextHopId
  auto& underlayNhOpt = (*srv6NextHop)->getUnderlayNextHop();
  ASSERT_TRUE(underlayNhOpt.has_value());
  auto underlayIpNhop =
      std::get<std::shared_ptr<ManagedIpNextHop>>(*underlayNhOpt);
  ASSERT_NE(underlayIpNhop->getSaiObject(), nullptr);
  auto initialUnderlayNhopId = underlayIpNhop->getSaiObject()->adapterKey();

  // Verify NextHopId was set on the SID list initially to the underlay nhop
  auto gotNextHopId = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::NextHopId{});
  EXPECT_EQ(gotNextHopId, initialUnderlayNhopId);

  // Simulate link down — cascades through FDB → neighbor → managed next hop
  const auto& remoteHost = intf0.remoteHosts[0];
  saiManagerTable->fdbManager().handleLinkDown(
      SaiPortDescriptor(PortID(remoteHost.port.id)));

  // SAI next hop object should be reset
  EXPECT_EQ((*srv6NextHop)->getSaiObject(), nullptr);

  // NextHopId on the SID list should be cleared to SAI_NULL_OBJECT_ID
  auto clearedNextHopId = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::NextHopId{});
  EXPECT_EQ(clearedNextHopId, SAI_NULL_OBJECT_ID);

  // Remove the neighbor and FDB entry
  auto arpEntry = makeArpEntry(intf0.id, remoteHost);
  saiManagerTable->neighborManager().removeNeighbor(arpEntry);

  // Re-resolve the neighbor — triggers createObject on the managed next hop
  resolveArp(intf0.id, remoteHost);

  // SAI next hop object should be recreated
  ASSERT_NE((*srv6NextHop)->getSaiObject(), nullptr);

  // The cached sidListHandle should still be valid with same sidList object
  auto& cachedHandle = (*srv6NextHop)->getSrv6SidListHandle();
  ASSERT_NE(cachedHandle, nullptr);
  ASSERT_NE(cachedHandle->managedSidList->getSidList(), nullptr);
  EXPECT_EQ(
      cachedHandle->managedSidList->getSidList()->adapterKey(), sidListId);

  // NextHopId on the SID list should be updated to the new underlay nhop
  ASSERT_NE(underlayIpNhop->getSaiObject(), nullptr);
  auto newUnderlayNhopId = underlayIpNhop->getSaiObject()->adapterKey();
  auto updatedNextHopId = saiApiTable->srv6Api().getAttribute(
      sidListId, SaiSrv6SidListTraits::Attributes::NextHopId{});
  EXPECT_EQ(updatedNextHopId, newUnderlayNhopId);
}

TEST_F(Srv6NextHopManagerTest, warmBootKeepsReloadedSidListNextHop) {
  auto swTunnel = makeSrv6Tunnel("srv6tunnel0", intf0.id);
  saiManagerTable->srv6TunnelManager().addSrv6Tunnel(swTunnel);
  auto swNextHop = makeSrv6NextHop(intf0, "srv6tunnel0");
  auto managedNextHop = saiManagerTable->nextHopManager().addManagedSaiNextHop(
      swNextHop, makeSrv6SidListHandle(swNextHop));
  auto srv6NextHop =
      std::get<std::shared_ptr<ManagedSrv6NextHop>>(managedNextHop);
  auto underlayIpNhop = std::get<std::shared_ptr<ManagedIpNextHop>>(
      *srv6NextHop->getUnderlayNextHop());
  auto underlayNhopId = underlayIpNhop->getSaiObject()->adapterKey();

  // 1. Reload the SID list into a second store, as warm boot does
  auto adapterKeys = saiStore->adapterKeysFollyDynamic();
  auto ak2Ahk = saiStore->adapterKeys2AdapterHostKeysFollyDynamic();
  SaiStore wbStore(0);
  wbStore.reload(&adapterKeys, &ak2Ahk, {SAI_OBJECT_TYPE_SRV6_SIDLIST});

  // 2. Re-create the managed SID list with the cold boot create attributes
  auto rifId = saiManagerTable->routerInterfaceManager()
                   .getRouterInterfaceHandle(swNextHop.intfID().value())
                   ->adapterKey();
  auto [sidListKey, sidListAttrs] =
      makeSrv6SidListKeyAndAttributes(rifId, swNextHop);
  ManagedSrv6SidList wbSidList(
      &saiManagerTable->srv6SidListManager(),
      &wbStore,
      SaiIpNextHopTraits::AdapterHostKey{rifId, swNextHop.addr()},
      sidListKey,
      sidListAttrs);

  // 3. The reloaded NextHopId is kept, so re-publishing it is not a write
  EXPECT_EQ(
      std::get<std::optional<SaiSrv6SidListTraits::Attributes::NextHopId>>(
          wbSidList.getSidList()->attributes()),
      SaiSrv6SidListTraits::Attributes::NextHopId{underlayNhopId});
  wbSidList.getSidList()->release();
}
#endif
