// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/api/SaiVersion.h"

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"
#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/switch/SaiFdbManager.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6MySidManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6TunnelManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/Srv6Tunnel.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"

using namespace facebook::fboss;

namespace {
std::shared_ptr<MySid> makeMySid(
    const std::string& addr = "fc00:100::1",
    uint8_t len = 48,
    MySidType type = MySidType::DECAPSULATE_AND_LOOKUP) {
  state::MySidFields fields;
  fields.type() = type;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress(addr));
  thriftPrefix.prefixLength() = len;
  fields.mySid() = thriftPrefix;
  return std::make_shared<MySid>(fields);
}

} // namespace

class MySidManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE |
        SetupStage::NEIGHBOR;
    ManagerTestBase::SetUp();
  }
};

TEST_F(MySidManagerTest, addDecapsulateAndLookup) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto state = getProgrammedState();
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  EXPECT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UDT46);

  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_FORWARD);
}

TEST_F(MySidManagerTest, addDuplicateThrows) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto state = getProgrammedState();
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);
  EXPECT_THROW(
      saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state),
      FbossError);
}

TEST_F(MySidManagerTest, addUnresolvedUASidProgramsWithDropAction) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  auto state = getProgrammedState();
  // uA / uN unresolved entries are programmed in SAI with PacketAction=DROP
  // so a midpoint packet hits the SRv6 lookup and increments the discard
  // counter rather than silently falling through to regular IP routing.
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  EXPECT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_DROP);

  auto gotNextHopId =
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{});
  EXPECT_EQ(gotNextHopId, SAI_NULL_OBJECT_ID);
}

TEST_F(MySidManagerTest, removeDecapsulateAndLookup) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto state = getProgrammedState();
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);

  saiManagerTable->srv6MySidManager().removeMySidEntry(mySid, state);
  EXPECT_EQ(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
}

TEST_F(MySidManagerTest, removeNonexistentThrows) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto state = getProgrammedState();
  EXPECT_THROW(
      saiManagerTable->srv6MySidManager().removeMySidEntry(mySid, state),
      FbossError);
}

TEST_F(MySidManagerTest, removeUnresolvedUASidErasesHandle) {
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  auto state = getProgrammedState();
  // uA / uN unresolved entries ARE programmed (with PacketAction=DROP), so
  // removeMySidEntry must erase the SAI handle.
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);

  saiManagerTable->srv6MySidManager().removeMySidEntry(mySid, state);
  EXPECT_EQ(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
}

TEST_F(MySidManagerTest, changeType) {
  auto oldMySid =
      makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto state = getProgrammedState();
  saiManagerTable->srv6MySidManager().addMySidEntry(oldMySid, state);

  auto key = getMySidAdapterHostKey(*oldMySid, saiManagerTable);
  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);

  // Change to a different DECAPSULATE_AND_LOOKUP (re-add)
  auto newMySid =
      makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, state);

  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
}

class MySidManagerWithNextHopIdTest : public MySidManagerTest {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    MySidManagerTest::SetUp();
  }
  void TearDown() override {
    MySidManagerTest::TearDown();
    FLAGS_enable_nexthop_id_manager = false;
  }

  std::shared_ptr<MySid> makeResolvedAdjacencySid(const TestInterface& intf) {
    RouteNextHopSet nextHops{makeNextHop(intf)};
    auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
    mySid->setResolvedNextHopsId(
        nextHopIDManager_->getOrAllocRouteNextHopSetID(nextHops)
            .nextHopIdSetIter->second.id);
    return mySid;
  }

  // The returned weak_ptr expires iff the SAI entry is removed.
  std::weak_ptr<SaiMySid> addAndWatchMySid(
      const std::shared_ptr<MySid>& mySid,
      const std::shared_ptr<SwitchState>& state) {
    auto& mySidManager = saiManagerTable->srv6MySidManager();
    mySidManager.addMySidEntry(mySid, state);
    return mySidManager.getMySidObject(
        getMySidAdapterHostKey(*mySid, saiManagerTable));
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
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

  RouteNextHopSet makeProtectionNextHops(
      const TestInterface& primaryInterface,
      const RouteNextHopSet& backupNextHops) const {
    auto nextHops = backupNextHops;
    nextHops.insert(
        makeResolvedNextHop(primaryInterface, NextHopRole::PRIMARY));
    return nextHops;
  }

  std::shared_ptr<MySid> makeAdjacencySid(
      const std::string& address,
      const TestInterface& primaryInterface,
      const RouteNextHopSet& backupNextHops) {
    RouteNextHopSet primaryNextHops{
        makeResolvedNextHop(primaryInterface, NextHopRole::PRIMARY),
    };
    auto primaryAllocResult =
        nextHopIDManager_->getOrAllocRouteNextHopSetID(primaryNextHops);
    auto backupAllocResult =
        nextHopIDManager_->getOrAllocRouteNextHopSetID(backupNextHops);
    auto mySid = makeMySid(address, 48, MySidType::ADJACENCY_MICRO_SID);
    mySid->setResolvedNextHopsId(
        primaryAllocResult.nextHopIdSetIter->second.id);
    mySid->setBackupResolvedNextHopsId(
        backupAllocResult.nextHopIdSetIter->second.id);
    return mySid;
  }

  // Protected uA MySID whose adjacency neighbor has not resolved yet, so it
  // carries backup next hops only.
  std::shared_ptr<MySid> makeBackupOnlyAdjacencySid(
      const std::string& address,
      const RouteNextHopSet& backupNextHops) {
    auto backupAllocResult =
        nextHopIDManager_->getOrAllocRouteNextHopSetID(backupNextHops);
    auto mySid = makeMySid(address, 48, MySidType::ADJACENCY_MICRO_SID);
    mySid->setBackupResolvedNextHopsId(
        backupAllocResult.nextHopIdSetIter->second.id);
    return mySid;
  }

  std::shared_ptr<MySid> addAdjacencySid(
      const std::string& address,
      const TestInterface& primaryInterface,
      const RouteNextHopSet& backupNextHops) {
    auto mySid = makeAdjacencySid(address, primaryInterface, backupNextHops);
    saiManagerTable->srv6MySidManager().addMySidEntry(
        mySid, getProgrammedState());
    return mySid;
  }

  const SaiNextHopGroupHandle* getProtectionNextHopGroup(
      const RouteNextHopSet& nextHops) const {
    return saiManagerTable->nextHopGroupManager().getNextHopGroup(
        SaiNextHopGroupKey(
            nextHops, std::nullopt, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION));
  }

  std::optional<sai_object_id_t> getChildGroupId(
      const SaiNextHopGroupHandle* groupHandle) const {
    if (!groupHandle || !groupHandle->childGroupMember_) {
      return std::nullopt;
    }
    auto childMember =
        groupHandle->childGroupMember_->getNhopGroupMemberObject();
    if (!childMember) {
      return std::nullopt;
    }
    return saiApiTable->nextHopGroupApi().getAttribute(
        childMember->adapterKey(),
        SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
  }
#endif
};

TEST_F(MySidManagerWithNextHopIdTest, addWithSingleResolvedNextHop) {
  // Allocate next hop IDs for a single next hop
  RouteNextHopSet nhopSet;
  nhopSet.insert(makeNextHop(testInterfaces[0]));
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = getProgrammedState();

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  EXPECT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA);

  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_FORWARD);

  auto gotNextHopId =
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{});
  EXPECT_NE(gotNextHopId, SAI_NULL_OBJECT_ID);
}

TEST_F(MySidManagerWithNextHopIdTest, addWithMultipleResolvedNextHops) {
  // Allocate next hop IDs for multiple next hops
  RouteNextHopSet nhopSet;
  nhopSet.insert(makeNextHop(testInterfaces[0]));
  nhopSet.insert(makeNextHop(testInterfaces[1]));
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = getProgrammedState();

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  EXPECT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA);

  // In fake SAI, NHG members are managed/deferred so the NHG adapter key
  // is SAI_NULL_OBJECT_ID, which correctly triggers DROP packet action
  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_DROP);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(
    MySidManagerWithNextHopIdTest,
    singleNhopProtectionAdjacencySidCreatesTopLevelAndChildGroups) {
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto nextHops = makeProtectionNextHops(testInterfaces.at(0), backupNextHops);
  auto mySid =
      addAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);

  const auto* groupHandle = getProtectionNextHopGroup(nextHops);
  ASSERT_NE(groupHandle, nullptr);
  ASSERT_NE(groupHandle->nextHopGroup, nullptr);

  auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
  auto topLevelGroupType = nextHopGroupApi.getAttribute(
      groupHandle->nextHopGroup->adapterKey(),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(topLevelGroupType, SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);

  auto childGroupId = getChildGroupId(groupHandle);
  ASSERT_TRUE(childGroupId.has_value());
  auto childGroupType = nextHopGroupApi.getAttribute(
      NextHopGroupSaiId(childGroupId.value()),
      SaiNextHopGroupTraits::Attributes::Type{});
  EXPECT_EQ(childGroupType, SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
  auto& srv6Api = saiApiTable->srv6Api();
  auto gotNextHopId =
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{});
  EXPECT_EQ(gotNextHopId, groupHandle->nextHopGroup->adapterKey());
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    backupOnlyAdjacencySidCreatesProtectionGroup) {
  // Before the adjacency neighbor resolves, a protected uA MySID carries only
  // its backup next hops. A single one of those must still be programmed as a
  // protection group: as a plain next hop the standby semantics are lost and
  // traffic forwards over the backup as though it were the primary path.
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto mySid = makeBackupOnlyAdjacencySid("fc00:100::1", backupNextHops);
  saiManagerTable->srv6MySidManager().addMySidEntry(
      mySid, getProgrammedState());

  const auto* groupHandle = getProtectionNextHopGroup(backupNextHops);
  ASSERT_NE(groupHandle, nullptr);
  ASSERT_NE(groupHandle->nextHopGroup, nullptr);

  auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
  EXPECT_EQ(
      nextHopGroupApi.getAttribute(
          groupHandle->nextHopGroup->adapterKey(),
          SaiNextHopGroupTraits::Attributes::Type{}),
      SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);

  auto childGroupId = getChildGroupId(groupHandle);
  ASSERT_TRUE(childGroupId.has_value());
  EXPECT_EQ(
      nextHopGroupApi.getAttribute(
          NextHopGroupSaiId(childGroupId.value()),
          SaiNextHopGroupTraits::Attributes::Type{}),
      SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
  EXPECT_EQ(
      saiApiTable->srv6Api().getAttribute(
          key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      groupHandle->nextHopGroup->adapterKey());
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    backupOnlyAdjacencySidGainsPrimaryOnChange) {
  // When the adjacency neighbor resolves, the entry moves from a backup-only
  // protection group to one covering both the primary and backup paths.
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto backupOnlyMySid =
      makeBackupOnlyAdjacencySid("fc00:100::1", backupNextHops);
  saiManagerTable->srv6MySidManager().addMySidEntry(
      backupOnlyMySid, getProgrammedState());
  ASSERT_NE(getProtectionNextHopGroup(backupNextHops), nullptr);

  auto protectedMySid =
      makeAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);
  saiManagerTable->srv6MySidManager().changeMySidEntry(
      backupOnlyMySid, protectedMySid, getProgrammedState());

  auto nextHops = makeProtectionNextHops(testInterfaces.at(0), backupNextHops);
  const auto* groupHandle = getProtectionNextHopGroup(nextHops);
  ASSERT_NE(groupHandle, nullptr);
  ASSERT_NE(groupHandle->nextHopGroup, nullptr);
  EXPECT_EQ(
      saiApiTable->nextHopGroupApi().getAttribute(
          groupHandle->nextHopGroup->adapterKey(),
          SaiNextHopGroupTraits::Attributes::Type{}),
      SAI_NEXT_HOP_GROUP_TYPE_PROTECTION);

  auto key = getMySidAdapterHostKey(*protectedMySid, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
  EXPECT_EQ(
      saiApiTable->srv6Api().getAttribute(
          key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      groupHandle->nextHopGroup->adapterKey());
}

TEST_F(MySidManagerWithNextHopIdTest, protectedAdjacencySidLosesPrimary) {
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto protectedMySid =
      makeAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);
  saiManagerTable->srv6MySidManager().addMySidEntry(
      protectedMySid, getProgrammedState());
  const auto protectedNextHops =
      makeProtectionNextHops(testInterfaces.at(0), backupNextHops);
  ASSERT_NE(getProtectionNextHopGroup(protectedNextHops), nullptr);

  auto backupOnlyMySid =
      makeBackupOnlyAdjacencySid("fc00:100::1", backupNextHops);
  saiManagerTable->srv6MySidManager().changeMySidEntry(
      protectedMySid, backupOnlyMySid, getProgrammedState());

  EXPECT_EQ(getProtectionNextHopGroup(protectedNextHops), nullptr);
  const auto* backupOnlyGroup = getProtectionNextHopGroup(backupNextHops);
  ASSERT_NE(backupOnlyGroup, nullptr);
  ASSERT_NE(backupOnlyGroup->nextHopGroup, nullptr);
  EXPECT_EQ(
      saiApiTable->srv6Api().getAttribute(
          getMySidAdapterHostKey(*backupOnlyMySid, saiManagerTable),
          SaiMySidEntryTraits::Attributes::NextHopId{}),
      backupOnlyGroup->nextHopGroup->adapterKey());
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    protectionAdjacencySidsWithSameBackupsShareChildGroup) {
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(2), NextHopRole::BACKUP),
      makeResolvedNextHop(testInterfaces.at(3), NextHopRole::BACKUP),
  };
  auto firstNextHops =
      makeProtectionNextHops(testInterfaces.at(0), backupNextHops);
  auto secondNextHops =
      makeProtectionNextHops(testInterfaces.at(1), backupNextHops);

  addAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);
  addAdjacencySid("fc00:100::2", testInterfaces.at(1), backupNextHops);

  const auto* firstGroupHandle = getProtectionNextHopGroup(firstNextHops);
  const auto* secondGroupHandle = getProtectionNextHopGroup(secondNextHops);
  ASSERT_NE(firstGroupHandle, nullptr);
  ASSERT_NE(secondGroupHandle, nullptr);
  ASSERT_NE(firstGroupHandle->nextHopGroup, nullptr);
  ASSERT_NE(secondGroupHandle->nextHopGroup, nullptr);
  EXPECT_NE(
      firstGroupHandle->nextHopGroup->adapterKey(),
      secondGroupHandle->nextHopGroup->adapterKey());

  auto firstChildGroupId = getChildGroupId(firstGroupHandle);
  auto secondChildGroupId = getChildGroupId(secondGroupHandle);
  ASSERT_TRUE(firstChildGroupId.has_value());
  ASSERT_TRUE(secondChildGroupId.has_value());
  EXPECT_EQ(firstChildGroupId, secondChildGroupId);
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    updatingAdjacencySidBackupsStopsChildGroupSharing) {
  RouteNextHopSet sharedBackupNextHops{
      makeResolvedNextHop(testInterfaces.at(2), NextHopRole::BACKUP),
      makeResolvedNextHop(testInterfaces.at(3), NextHopRole::BACKUP),
  };
  auto firstNextHops =
      makeProtectionNextHops(testInterfaces.at(0), sharedBackupNextHops);
  auto secondNextHops =
      makeProtectionNextHops(testInterfaces.at(1), sharedBackupNextHops);

  addAdjacencySid("fc00:100::1", testInterfaces.at(0), sharedBackupNextHops);
  auto secondMySid = addAdjacencySid(
      "fc00:100::2", testInterfaces.at(1), sharedBackupNextHops);

  const auto* firstGroupHandle = getProtectionNextHopGroup(firstNextHops);
  const auto* secondGroupHandle = getProtectionNextHopGroup(secondNextHops);
  ASSERT_NE(firstGroupHandle, nullptr);
  ASSERT_NE(secondGroupHandle, nullptr);
  auto firstChildGroupId = getChildGroupId(firstGroupHandle);
  auto secondChildGroupId = getChildGroupId(secondGroupHandle);
  ASSERT_TRUE(firstChildGroupId.has_value());
  ASSERT_TRUE(secondChildGroupId.has_value());
  EXPECT_EQ(firstChildGroupId, secondChildGroupId);

  RouteNextHopSet updatedBackupNextHops{
      makeResolvedNextHop(testInterfaces.at(4), NextHopRole::BACKUP),
      makeResolvedNextHop(testInterfaces.at(5), NextHopRole::BACKUP),
  };
  auto updatedSecondNextHops =
      makeProtectionNextHops(testInterfaces.at(1), updatedBackupNextHops);
  auto updatedSecondMySid = makeAdjacencySid(
      "fc00:100::2", testInterfaces.at(1), updatedBackupNextHops);
  saiManagerTable->srv6MySidManager().changeMySidEntry(
      secondMySid, updatedSecondMySid, getProgrammedState());

  firstGroupHandle = getProtectionNextHopGroup(firstNextHops);
  secondGroupHandle = getProtectionNextHopGroup(updatedSecondNextHops);
  ASSERT_NE(firstGroupHandle, nullptr);
  ASSERT_NE(secondGroupHandle, nullptr);
  ASSERT_NE(firstGroupHandle->nextHopGroup, nullptr);
  ASSERT_NE(secondGroupHandle->nextHopGroup, nullptr);
  EXPECT_NE(
      firstGroupHandle->nextHopGroup->adapterKey(),
      secondGroupHandle->nextHopGroup->adapterKey());
  firstChildGroupId = getChildGroupId(firstGroupHandle);
  secondChildGroupId = getChildGroupId(secondGroupHandle);
  ASSERT_TRUE(firstChildGroupId.has_value());
  ASSERT_TRUE(secondChildGroupId.has_value());
  EXPECT_NE(firstChildGroupId, secondChildGroupId);
}
#endif

TEST_F(MySidManagerWithNextHopIdTest, removeWithResolvedNextHop) {
  RouteNextHopSet nhopSet;
  nhopSet.insert(makeNextHop(testInterfaces[0]));
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = getProgrammedState();

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);

  saiManagerTable->srv6MySidManager().removeMySidEntry(mySid, state);
  EXPECT_EQ(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
}

TEST_F(MySidManagerWithNextHopIdTest, changeUnresolvedToResolved) {
  auto state = getProgrammedState();

  // Start with an unresolved MySid (no resolvedNextHopsId)
  auto oldMySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  saiManagerTable->srv6MySidManager().addMySidEntry(oldMySid, state);

  auto key = getMySidAdapterHostKey(*oldMySid, saiManagerTable);
  // Unresolved uA entry is programmed with PacketAction=DROP.
  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);
  {
    auto& srv6Api = saiApiTable->srv6Api();
    auto gotAction = srv6Api.getAttribute(
        key, SaiMySidEntryTraits::Attributes::PacketAction{});
    EXPECT_EQ(gotAction, SAI_PACKET_ACTION_DROP);
  }

  // Now resolve it with a next hop
  RouteNextHopSet nhopSet;
  nhopSet.insert(makeNextHop(testInterfaces[0]));
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  // Re-fetch state with populated FibInfo ID maps
  state = getProgrammedState();

  auto newMySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  newMySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, state);

  EXPECT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key), nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA);

  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_FORWARD);
}

TEST_F(MySidManagerWithNextHopIdTest, changeResolvedNextHopUpdatesInPlace) {
  auto oldMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto newMySid = makeResolvedAdjacencySid(testInterfaces.at(1));
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_FALSE(entry.expired());
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  auto& srv6Api = saiApiTable->srv6Api();
  auto nextHopId =
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{});
  ASSERT_NE(nextHopId, SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      saiApiTable->nextHopApi().getAttribute(
          NextHopSaiId(nextHopId), SaiIpNextHopTraits::Attributes::Ip{}),
      testInterfaces.at(1).remoteHosts.at(0).ip);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    changeResolvedToUnresolvedUpdatesInPlace) {
  auto oldMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto newMySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_FALSE(entry.expired());
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  auto& srv6Api = saiApiTable->srv6Api();
  EXPECT_EQ(
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_DROP);
}

TEST_F(
    MySidManagerWithNextHopIdTest,
    changeUnresolvedToResolvedUpdatesInPlace) {
  auto oldMySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  auto newMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_FALSE(entry.expired());
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  auto& srv6Api = saiApiTable->srv6Api();
  EXPECT_NE(
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

TEST_F(MySidManagerWithNextHopIdTest, changeTypeRecreatesEntry) {
  auto oldMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto newMySid =
      makeMySid("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP);
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  ASSERT_EQ(getMySidAdapterHostKey(*oldMySid, saiManagerTable), key);

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_TRUE(entry.expired());
  EXPECT_EQ(
      saiApiTable->srv6Api().getAttribute(
          key, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UDT46);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(MySidManagerWithNextHopIdTest, changeAddingFrrBackupUpdatesInPlace) {
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto oldMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto newMySid =
      makeAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_FALSE(entry.expired());
  const auto* groupHandle = getProtectionNextHopGroup(
      makeProtectionNextHops(testInterfaces.at(0), backupNextHops));
  ASSERT_NE(groupHandle, nullptr);
  ASSERT_NE(groupHandle->nextHopGroup, nullptr);
  EXPECT_EQ(
      saiApiTable->srv6Api().getAttribute(
          getMySidAdapterHostKey(*newMySid, saiManagerTable),
          SaiMySidEntryTraits::Attributes::NextHopId{}),
      groupHandle->nextHopGroup->adapterKey());
}

TEST_F(MySidManagerWithNextHopIdTest, changeRemovingFrrBackupUpdatesInPlace) {
  RouteNextHopSet backupNextHops{
      makeResolvedNextHop(testInterfaces.at(1), NextHopRole::BACKUP),
  };
  auto oldMySid =
      makeAdjacencySid("fc00:100::1", testInterfaces.at(0), backupNextHops);
  auto newMySid = makeResolvedAdjacencySid(testInterfaces.at(0));
  auto entry = addAndWatchMySid(oldMySid, getProgrammedState());
  const auto protectionNextHops =
      makeProtectionNextHops(testInterfaces.at(0), backupNextHops);
  ASSERT_NE(getProtectionNextHopGroup(protectionNextHops), nullptr);

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, getProgrammedState());

  EXPECT_FALSE(entry.expired());
  EXPECT_EQ(getProtectionNextHopGroup(protectionNextHops), nullptr);
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  auto nextHopId = saiApiTable->srv6Api().getAttribute(
      key, SaiMySidEntryTraits::Attributes::NextHopId{});
  ASSERT_NE(nextHopId, SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      saiApiTable->nextHopApi().getAttribute(
          NextHopSaiId(nextHopId), SaiIpNextHopTraits::Attributes::Ip{}),
      testInterfaces.at(0).remoteHosts.at(0).ip);
}
#endif

TEST_F(MySidManagerWithNextHopIdTest, addNodeMicroSidWithResolvedNextHop) {
  RouteNextHopSet nhopSet;
  nhopSet.insert(makeNextHop(testInterfaces[0]));
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = getProgrammedState();

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::NODE_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  EXPECT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UN);

  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_FORWARD);
}

class MySidBindingSidTest : public MySidManagerWithNextHopIdTest {
 public:
  static inline const std::string kSrv6TunnelId{"srv6Tunnel0"};

  void addSrv6Tunnel() const {
    auto tunnel = std::make_shared<Srv6Tunnel>(kSrv6TunnelId);
    tunnel->setType(TunnelType::SRV6_ENCAP);
    tunnel->setUnderlayIntfId(InterfaceID(testInterfaces[0].id));
    tunnel->setSrcIP(folly::IPAddressV6("2001:db8::1"));
    tunnel->setTTLMode(cfg::TunnelMode::PIPE);
    tunnel->setDscpMode(cfg::TunnelMode::UNIFORM);
    tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
    saiManagerTable->srv6TunnelManager().addSrv6Tunnel(tunnel);
  }

  ResolvedNextHop makeSrv6NextHop(
      const TestInterface& testInterface,
      const folly::IPAddressV6& nextHopIp,
      const std::vector<folly::IPAddressV6>& sidList) const {
    return ResolvedNextHop{
        nextHopIp,
        InterfaceID(testInterface.id),
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        sidList,
        TunnelType::SRV6_ENCAP,
        kSrv6TunnelId};
  }

  std::shared_ptr<NdpEntry> makeNdpEntry(
      const TestInterface& testInterface,
      const folly::IPAddressV6& ip,
      const folly::MacAddress& mac) const {
    const auto& remote = testInterface.remoteHosts.at(0);
    return std::make_shared<NdpEntry>(
        ip,
        mac,
        PortDescriptor(PortID(remote.port.id)),
        InterfaceID(testInterface.id),
        state::NeighborEntryType::DYNAMIC_ENTRY);
  }

  std::shared_ptr<NdpEntry> resolveNdp(
      const TestInterface& testInterface,
      const folly::IPAddressV6& ip,
      const folly::MacAddress& mac) const {
    const auto& remote = testInterface.remoteHosts.at(0);
    auto ndpEntry = makeNdpEntry(testInterface, ip, mac);
    saiManagerTable->neighborManager().addNeighbor(ndpEntry);
    saiManagerTable->fdbManager().addFdbEntry(
        SaiPortDescriptor(PortID(remote.port.id)),
        InterfaceID(testInterface.id),
        mac,
        SAI_FDB_ENTRY_TYPE_STATIC,
        std::nullopt);
    return ndpEntry;
  }

  void unresolveNdp(const std::shared_ptr<NdpEntry>& ndpEntry) const {
    saiManagerTable->neighborManager().removeNeighbor(ndpEntry);
  }

  void bringLinkDown(const TestInterface& testInterface) const {
    const auto& remote = testInterface.remoteHosts.at(0);
    saiManagerTable->fdbManager().handleLinkDown(
        SaiPortDescriptor(PortID(remote.port.id)));
  }

  std::shared_ptr<SwitchState> makeStateWithNextHopIdMaps() {
    auto state = getProgrammedState()->clone();
    populateFibInfoIdMaps(nextHopIDManager_.get(), state);
    state->publish();
    return state;
  }

  sai_object_id_t verifySrv6SidListAndNextHop(
      const ResolvedNextHop& swNextHop) const {
    auto* rifHandle =
        saiManagerTable->routerInterfaceManager().getRouterInterfaceHandle(
            swNextHop.intfID().value());
    EXPECT_NE(rifHandle, nullptr);
    if (!rifHandle) {
      return SAI_NULL_OBJECT_ID;
    }

    const auto expectedSegments = toSaiIp6List(swNextHop.srv6SegmentList());
    SaiSrv6SidListTraits::AdapterHostKey sidListKey{
        SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED,
        expectedSegments,
        rifHandle->adapterKey(),
        swNextHop.addr()};
    auto* sidListHandle =
        saiManagerTable->srv6SidListManager().getSrv6SidListHandle(sidListKey);
    EXPECT_NE(sidListHandle, nullptr);
    if (!sidListHandle || !sidListHandle->managedSidList->getSidList()) {
      return SAI_NULL_OBJECT_ID;
    }

    const auto sidListId =
        sidListHandle->managedSidList->getSidList()->adapterKey();
    auto gotSegments = saiApiTable->srv6Api().getAttribute(
        sidListId, SaiSrv6SidListTraits::Attributes::SegmentList{});
    EXPECT_EQ(gotSegments, expectedSegments);

    auto adapterHostKey = saiManagerTable->nextHopManager().getAdapterHostKey(
        swNextHop, sidListId);
    auto* srv6Key = std::get_if<SaiSrv6SidlistNextHopTraits::AdapterHostKey>(
        &adapterHostKey);
    EXPECT_NE(srv6Key, nullptr);
    if (!srv6Key) {
      return SAI_NULL_OBJECT_ID;
    }

    auto* managedNh =
        saiManagerTable->nextHopManager().getManagedNextHop(*srv6Key);
    EXPECT_NE(managedNh, nullptr);
    if (!managedNh || !managedNh->getSaiObject()) {
      return SAI_NULL_OBJECT_ID;
    }
    return managedNh->getSaiObject()->adapterKey();
  }
};

TEST_F(MySidBindingSidTest, addWithResolvedSrv6NextHopGroup) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList0{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("2001:db8::30"), folly::IPAddressV6("2001:db8::40")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList0);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::11"), sidList1);
  resolveNdp(
      testInterfaces[0],
      srv6NextHop0.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});

  RouteNextHopSet nhopSet{srv6NextHop0, srv6NextHop1};
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  ASSERT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);

  auto gotAction = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::PacketAction{});
  // In fake SAI, NHG members are managed/deferred so the NHG adapter key
  // is SAI_NULL_OBJECT_ID, which correctly triggers DROP packet action.
  EXPECT_EQ(gotAction, SAI_PACKET_ACTION_DROP);

  auto gotNextHopId =
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{});
  EXPECT_EQ(gotNextHopId, SAI_NULL_OBJECT_ID);

  auto nhgHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(nhopSet, std::nullopt));
  ASSERT_NE(nhgHandle, nullptr);
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 2);

  std::set<sai_object_id_t> nextHopIds;
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop0));
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop1));
  EXPECT_EQ(nextHopIds.size(), 2);
}

TEST_F(MySidBindingSidTest, resolvedSrv6NextHopGroupTracksResolutionAndLink) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList0{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("2001:db8::30"), folly::IPAddressV6("2001:db8::40")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList0);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::11"), sidList1);

  RouteNextHopSet nhopSet{srv6NextHop0, srv6NextHop1};
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto nhgHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(nhopSet, std::nullopt));
  ASSERT_NE(nhgHandle, nullptr);
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 0);

  resolveNdp(
      testInterfaces[0],
      srv6NextHop0.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 1);

  auto ndpEntry1 = resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 2);

  bringLinkDown(testInterfaces[1]);
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 1);

  unresolveNdp(ndpEntry1);
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 1);

  resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 2);
}

TEST_F(MySidBindingSidTest, addWithDistinctResolvedSrv6NextHopsAndSameSidList) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::11"), sidList);
  resolveNdp(
      testInterfaces[0],
      srv6NextHop0.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});

  RouteNextHopSet nhopSet{srv6NextHop0, srv6NextHop1};
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto saiEntry = saiManagerTable->srv6MySidManager().getMySidObject(key);
  ASSERT_NE(saiEntry, nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotBehavior = srv6Api.getAttribute(
      key, SaiMySidEntryTraits::Attributes::EndpointBehavior{});
  EXPECT_EQ(gotBehavior, SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);

  auto nhgHandle =
      saiManagerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
          SaiNextHopGroupKey(nhopSet, std::nullopt));
  ASSERT_NE(nhgHandle, nullptr);
  EXPECT_EQ(nhgHandle->nextHopGroupSize(), 2);

  std::set<sai_object_id_t> nextHopIds;
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop0));
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop1));
  EXPECT_EQ(nextHopIds.size(), 2);
}

TEST_F(MySidBindingSidTest, addWithSameLinkLocalNextHopDistinctSidLists) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList0{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("2001:db8::30"), folly::IPAddressV6("2001:db8::40")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList0);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList1);
  resolveNdp(
      testInterfaces[0],
      folly::IPAddressV6("fe80::10"),
      folly::MacAddress{"10:10:10:10:10:a0"});

  RouteNextHopSet nhopSet0{srv6NextHop0};
  auto allocResult0 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet0);
  auto nextHopSetId0 = allocResult0.nextHopIdSetIter->second.id;

  RouteNextHopSet nhopSet1{srv6NextHop1};
  auto allocResult1 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet1);
  auto nextHopSetId1 = allocResult1.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();

  auto mySid0 = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid0->setResolvedNextHopsId(nextHopSetId0);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid0, state);

  auto mySid1 = makeMySid("fc00:100::2", 48, MySidType::BINDING_MICRO_SID);
  mySid1->setResolvedNextHopsId(nextHopSetId1);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid1, state);

  auto key0 = getMySidAdapterHostKey(*mySid0, saiManagerTable);
  auto key1 = getMySidAdapterHostKey(*mySid1, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key0), nullptr);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key1), nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  EXPECT_EQ(
      srv6Api.getAttribute(
          key0, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key1, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);

  std::set<sai_object_id_t> nextHopIds;
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop0));
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop1));
  EXPECT_EQ(nextHopIds.size(), 2);
}

TEST_F(
    MySidBindingSidTest,
    addWithDistinctLinkLocalNextHopsAndDistinctSidLists) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList0{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("2001:db8::30"), folly::IPAddressV6("2001:db8::40")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList0);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::11"), sidList1);
  resolveNdp(
      testInterfaces[0],
      srv6NextHop0.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});

  RouteNextHopSet nhopSet0{srv6NextHop0};
  auto allocResult0 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet0);
  auto nextHopSetId0 = allocResult0.nextHopIdSetIter->second.id;

  RouteNextHopSet nhopSet1{srv6NextHop1};
  auto allocResult1 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet1);
  auto nextHopSetId1 = allocResult1.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();

  auto mySid0 = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid0->setResolvedNextHopsId(nextHopSetId0);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid0, state);

  auto mySid1 = makeMySid("fc00:100::2", 48, MySidType::BINDING_MICRO_SID);
  mySid1->setResolvedNextHopsId(nextHopSetId1);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid1, state);

  auto key0 = getMySidAdapterHostKey(*mySid0, saiManagerTable);
  auto key1 = getMySidAdapterHostKey(*mySid1, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key0), nullptr);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key1), nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  EXPECT_EQ(
      srv6Api.getAttribute(
          key0, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key1, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);

  std::set<sai_object_id_t> nextHopIds;
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop0));
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop1));
  EXPECT_EQ(nextHopIds.size(), 2);
}

TEST_F(MySidBindingSidTest, addWithDistinctLinkLocalNextHopsAndSameSidList) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::11"), sidList);
  resolveNdp(
      testInterfaces[0],
      srv6NextHop0.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  resolveNdp(
      testInterfaces[1],
      srv6NextHop1.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});

  RouteNextHopSet nhopSet0{srv6NextHop0};
  auto allocResult0 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet0);
  auto nextHopSetId0 = allocResult0.nextHopIdSetIter->second.id;

  RouteNextHopSet nhopSet1{srv6NextHop1};
  auto allocResult1 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet1);
  auto nextHopSetId1 = allocResult1.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();

  auto mySid0 = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid0->setResolvedNextHopsId(nextHopSetId0);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid0, state);

  auto mySid1 = makeMySid("fc00:100::2", 48, MySidType::BINDING_MICRO_SID);
  mySid1->setResolvedNextHopsId(nextHopSetId1);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid1, state);

  auto key0 = getMySidAdapterHostKey(*mySid0, saiManagerTable);
  auto key1 = getMySidAdapterHostKey(*mySid1, saiManagerTable);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key0), nullptr);
  ASSERT_NE(saiManagerTable->srv6MySidManager().getMySidObject(key1), nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  EXPECT_EQ(
      srv6Api.getAttribute(
          key0, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key1, SaiMySidEntryTraits::Attributes::EndpointBehavior{}),
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED);

  std::set<sai_object_id_t> nextHopIds;
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop0));
  nextHopIds.insert(verifySrv6SidListAndNextHop(srv6NextHop1));
  EXPECT_EQ(nextHopIds.size(), 2);
}

TEST_F(MySidBindingSidTest, sharedNextHopWhenAllFieldsMatch) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  resolveNdp(
      testInterfaces[0],
      folly::IPAddressV6("fe80::10"),
      folly::MacAddress{"10:10:10:10:10:a0"});

  RouteNextHopSet nhopSet0{srv6NextHop0};
  auto allocResult0 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet0);
  auto nextHopSetId0 = allocResult0.nextHopIdSetIter->second.id;

  RouteNextHopSet nhopSet1{srv6NextHop1};
  auto allocResult1 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet1);
  auto nextHopSetId1 = allocResult1.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();

  auto mySid0 = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid0->setResolvedNextHopsId(nextHopSetId0);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid0, state);

  auto mySid1 = makeMySid("fc00:100::2", 48, MySidType::BINDING_MICRO_SID);
  mySid1->setResolvedNextHopsId(nextHopSetId1);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid1, state);

  auto nhId0 = verifySrv6SidListAndNextHop(srv6NextHop0);
  auto nhId1 = verifySrv6SidListAndNextHop(srv6NextHop1);
  EXPECT_EQ(nhId0, nhId1);
}

TEST_F(MySidBindingSidTest, distinctNextHopWhenRouterInterfaceDiffers) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const auto srv6NextHop0 = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  const auto srv6NextHop1 = makeSrv6NextHop(
      testInterfaces[1], folly::IPAddressV6("fe80::10"), sidList);
  resolveNdp(
      testInterfaces[0],
      folly::IPAddressV6("fe80::10"),
      folly::MacAddress{"10:10:10:10:10:a0"});
  resolveNdp(
      testInterfaces[1],
      folly::IPAddressV6("fe80::10"),
      folly::MacAddress{"10:10:10:10:10:a1"});

  RouteNextHopSet nhopSet0{srv6NextHop0};
  auto allocResult0 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet0);
  auto nextHopSetId0 = allocResult0.nextHopIdSetIter->second.id;

  RouteNextHopSet nhopSet1{srv6NextHop1};
  auto allocResult1 = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet1);
  auto nextHopSetId1 = allocResult1.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();

  auto mySid0 = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid0->setResolvedNextHopsId(nextHopSetId0);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid0, state);

  auto mySid1 = makeMySid("fc00:100::2", 48, MySidType::BINDING_MICRO_SID);
  mySid1->setResolvedNextHopsId(nextHopSetId1);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid1, state);

  auto nhId0 = verifySrv6SidListAndNextHop(srv6NextHop0);
  auto nhId1 = verifySrv6SidListAndNextHop(srv6NextHop1);
  EXPECT_NE(nhId0, nhId1);
}

TEST_F(MySidBindingSidTest, changeToUnresolvedNextHopUpdatesInPlace) {
  addSrv6Tunnel();
  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10")};
  const auto oldNextHop = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);
  const auto newNextHop = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::20"), sidList);
  auto makeBindingSid = [this](const ResolvedNextHop& nextHop) {
    auto mySid = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
    mySid->setResolvedNextHopsId(
        nextHopIDManager_->getOrAllocRouteNextHopSetID(RouteNextHopSet{nextHop})
            .nextHopIdSetIter->second.id);
    return mySid;
  };
  auto oldMySid = makeBindingSid(oldNextHop);
  auto newMySid = makeBindingSid(newNextHop);
  resolveNdp(
      testInterfaces[0],
      oldNextHop.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  auto entry = addAndWatchMySid(oldMySid, makeStateWithNextHopIdMaps());
  auto key = getMySidAdapterHostKey(*newMySid, saiManagerTable);
  auto& srv6Api = saiApiTable->srv6Api();
  ASSERT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);

  saiManagerTable->srv6MySidManager().changeMySidEntry(
      oldMySid, newMySid, makeStateWithNextHopIdMaps());

  EXPECT_FALSE(entry.expired());
  EXPECT_EQ(
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_DROP);

  resolveNdp(
      testInterfaces[0],
      newNextHop.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a1"});

  EXPECT_FALSE(entry.expired());
  EXPECT_NE(
      srv6Api.getAttribute(key, SaiMySidEntryTraits::Attributes::NextHopId{}),
      SAI_NULL_OBJECT_ID);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

TEST_F(MySidBindingSidTest, singleSrv6NextHopTracksResolutionAndLink) {
  addSrv6Tunnel();

  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  const auto srv6NextHop = makeSrv6NextHop(
      testInterfaces[0], folly::IPAddressV6("fe80::10"), sidList);

  RouteNextHopSet nhopSet{srv6NextHop};
  auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhopSet);
  auto nextHopSetId = allocResult.nextHopIdSetIter->second.id;

  auto state = makeStateWithNextHopIdMaps();
  auto mySid = makeMySid("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  mySid->setResolvedNextHopsId(nextHopSetId);
  saiManagerTable->srv6MySidManager().addMySidEntry(mySid, state);

  auto key = getMySidAdapterHostKey(*mySid, saiManagerTable);
  auto& srv6Api = saiApiTable->srv6Api();

  // Next hop unresolved — packet action should be DROP
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_DROP);

  // Resolve NDP — packet action should become FORWARD
  auto ndpEntry = resolveNdp(
      testInterfaces[0],
      srv6NextHop.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);

  // Link down — packet action should go back to DROP
  bringLinkDown(testInterfaces[0]);
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_DROP);

  // Unresolve and re-resolve NDP — packet action should return to FORWARD
  unresolveNdp(ndpEntry);
  resolveNdp(
      testInterfaces[0],
      srv6NextHop.addr().asV6(),
      folly::MacAddress{"10:10:10:10:10:a0"});
  EXPECT_EQ(
      srv6Api.getAttribute(
          key, SaiMySidEntryTraits::Attributes::PacketAction{}),
      SAI_PACKET_ACTION_FORWARD);
}

#endif
