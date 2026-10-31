/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"

#include <folly/json/json.h>
#include <algorithm>
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class NextHopGroupStoreTest : public SaiStoreTest {
 public:
  NextHopGroupSaiId createNextHopGroup() {
    auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
    return nextHopGroupApi.create<SaiNextHopGroupTraits>(
        {SAI_NEXT_HOP_GROUP_TYPE_ECMP,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt},
        0);
  }

  NextHopGroupSaiId createArsNextHopGroup(ArsSaiId arsSaiId) {
    auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
    std::optional<SaiNextHopGroupTraits::Attributes::ArsObjectId> arsObjectId{
        arsSaiId};
    return nextHopGroupApi.create<SaiNextHopGroupTraits>(
        {SAI_NEXT_HOP_GROUP_TYPE_ECMP,
         arsObjectId,
         std::nullopt,
         std::nullopt,
         std::nullopt},
        0);
  }

  ArsSaiId createArs() {
    return saiApiTable->arsApi().create<SaiArsTraits>(
        {SAI_ARS_MODE_PER_PACKET_QUALITY,
         0,
         0,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt},
        0);
  }

  NextHopGroupMemberSaiId createNextHopGroupMember(
      sai_object_id_t groupId,
      sai_object_id_t nextHopId,
      std::optional<sai_uint32_t> weight) {
    auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
    return nextHopGroupApi.create<SaiNextHopGroupMemberTraits>(
        {groupId,
         nextHopId,
         weight
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
         ,
         std::nullopt,
         std::nullopt
#endif
        },
        0);
  }

  NextHopSaiId createNextHop(const folly::IPAddress& ip) {
    auto& nextHopApi = saiApiTable->nextHopApi();
    return nextHopApi.create<SaiIpNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_IP, 42, ip, std::nullopt}, 0);
  }

  NextHopSaiId createMplsNextHop(
      const folly::IPAddress& ip,
      std::vector<sai_uint32_t> labels) {
    auto& nextHopApi = saiApiTable->nextHopApi();
    return nextHopApi.create<SaiMplsNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_MPLS,
         42,
         ip,
         labels,
         SAI_OUTSEG_TYPE_SWAP,
         std::nullopt},
        0);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  NextHopSaiId createSrv6SidlistNextHop(
      sai_object_id_t tunnelId,
      sai_object_id_t srv6SidlistId) {
    auto& nextHopApi = saiApiTable->nextHopApi();
    return nextHopApi.create<SaiSrv6SidlistNextHopTraits>(
        {SAI_NEXT_HOP_TYPE_SRV6_SIDLIST, tunnelId, srv6SidlistId, std::nullopt},
        0);
  }
#endif
};

TEST_F(NextHopGroupStoreTest, loadEmptyNextHopGroup) {
  auto nextHopGroupId = createNextHopGroup();
  auto nextHopGroupId2 = createArsNextHopGroup(createArs());

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiNextHopGroupTraits>();

  SaiNextHopGroupTraits::AdapterHostKey k;
  SaiNextHopGroupTraits::AdapterHostKey k2 = {
      {}, SAI_ARS_MODE_PER_PACKET_QUALITY};
  auto got = store.get(k);
  EXPECT_EQ(got->adapterKey(), nextHopGroupId);
  auto got2 = store.get(k2);
  EXPECT_EQ(got2->adapterKey(), nextHopGroupId2);
}

TEST_F(NextHopGroupStoreTest, loadNextHopGroup) {
  // Create a next hop group
  auto nextHopGroupId = createNextHopGroup();
  auto nextHopGroupId2 = createArsNextHopGroup(createArs());

  // Create two next hops and two next hop group members
  folly::IPAddress ip1{"10.10.10.1"};
  folly::IPAddress ip2{"10.10.10.2"};
  folly::IPAddress ip3{"10.10.10.3"};
  folly::IPAddress ip4{"10.10.10.4"};
  sai_uint32_t weight1 = 8;
  sai_uint32_t weight2 = 9;
  sai_uint32_t weight3 = 10;
  sai_uint32_t weight4 = 11;

  auto nextHopId1 = createNextHop(ip1);
  auto nextHopId2 = createNextHop(ip2);
  auto nextHopId3 = createMplsNextHop(ip3, {102, 103});
  auto nextHopId4 = createMplsNextHop(ip4, {201, 203});

  auto nextHopGroupMemberId1 =
      createNextHopGroupMember(nextHopGroupId, nextHopId1, weight1);
  auto nextHopGroupMemberId2 =
      createNextHopGroupMember(nextHopGroupId, nextHopId2, weight2);
  auto nextHopGroupMemberId3 =
      createNextHopGroupMember(nextHopGroupId, nextHopId3, weight3);
  auto nextHopGroupMemberId4 =
      createNextHopGroupMember(nextHopGroupId, nextHopId4, weight4);

  auto nextHopGroupMemberId5 =
      createNextHopGroupMember(nextHopGroupId2, nextHopId1, weight1);
  auto nextHopGroupMemberId6 =
      createNextHopGroupMember(nextHopGroupId2, nextHopId2, weight2);
  auto nextHopGroupMemberId7 =
      createNextHopGroupMember(nextHopGroupId2, nextHopId3, weight3);
  auto nextHopGroupMemberId8 =
      createNextHopGroupMember(nextHopGroupId2, nextHopId4, weight4);

  // perform a warm boot load
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiNextHopGroupTraits>();

  SaiNextHopGroupTraits::AdapterHostKey k;
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, weight1));
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip2}, weight2));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiMplsNextHopTraits::AdapterHostKey{
              42,
              ip3,
              std::vector<sai_uint32_t>{102, 103},
              SAI_OUTSEG_TYPE_SWAP},
          weight3));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiMplsNextHopTraits::AdapterHostKey{
              42,
              ip4,
              std::vector<sai_uint32_t>{201, 203},
              SAI_OUTSEG_TYPE_SWAP},
          weight4));

  SaiNextHopGroupTraits::AdapterHostKey k0{k};
  k0.mode = SAI_ARS_MODE_PER_PACKET_QUALITY;

  auto got = store.get(k);
  EXPECT_TRUE(got);
  auto got0 = store.get(k0);
  EXPECT_TRUE(got0);
  EXPECT_EQ(got->adapterKey(), nextHopGroupId);
  EXPECT_EQ(got0->adapterKey(), nextHopGroupId2);

  auto& memberStore = s.get<SaiNextHopGroupMemberTraits>();
  SaiNextHopGroupMemberTraits::AdapterHostKey k1{nextHopGroupId, nextHopId1};
  SaiNextHopGroupMemberTraits::AdapterHostKey k2{nextHopGroupId, nextHopId2};
  SaiNextHopGroupMemberTraits::AdapterHostKey k3{nextHopGroupId, nextHopId3};
  SaiNextHopGroupMemberTraits::AdapterHostKey k4{nextHopGroupId, nextHopId4};
  SaiNextHopGroupMemberTraits::AdapterHostKey k5{nextHopGroupId2, nextHopId1};
  SaiNextHopGroupMemberTraits::AdapterHostKey k6{nextHopGroupId2, nextHopId2};
  SaiNextHopGroupMemberTraits::AdapterHostKey k7{nextHopGroupId2, nextHopId3};
  SaiNextHopGroupMemberTraits::AdapterHostKey k8{nextHopGroupId2, nextHopId4};

  auto got1 = memberStore.get(k1);
  auto got2 = memberStore.get(k2);
  auto got3 = memberStore.get(k3);
  auto got4 = memberStore.get(k4);
  auto got5 = memberStore.get(k5);
  auto got6 = memberStore.get(k6);
  auto got7 = memberStore.get(k7);
  auto got8 = memberStore.get(k8);
  EXPECT_TRUE(got1);
  EXPECT_TRUE(got2);
  EXPECT_TRUE(got3);
  EXPECT_TRUE(got4);
  EXPECT_TRUE(got5);
  EXPECT_TRUE(got6);
  EXPECT_TRUE(got7);
  EXPECT_TRUE(got8);
  EXPECT_EQ(got1->adapterKey(), nextHopGroupMemberId1);
  EXPECT_EQ(got2->adapterKey(), nextHopGroupMemberId2);
  EXPECT_EQ(got3->adapterKey(), nextHopGroupMemberId3);
  EXPECT_EQ(got4->adapterKey(), nextHopGroupMemberId4);
  EXPECT_EQ(got5->adapterKey(), nextHopGroupMemberId5);
  EXPECT_EQ(got6->adapterKey(), nextHopGroupMemberId6);
  EXPECT_EQ(got7->adapterKey(), nextHopGroupMemberId7);
  EXPECT_EQ(got8->adapterKey(), nextHopGroupMemberId8);
}

TEST_F(NextHopGroupStoreTest, nextHopGroupLoadCtor) {
  auto id = createNextHopGroup();
  auto obj = createObj<SaiNextHopGroupTraits>(id);
  EXPECT_EQ(obj.adapterKey(), id);
}

TEST_F(NextHopGroupStoreTest, nextHopGroupMemberLoadCtor) {
  auto nhgId = createNextHopGroup();
  auto id = createNextHopGroupMember(nhgId, NextHopSaiId{10}, std::nullopt);
  auto obj = createObj<SaiNextHopGroupMemberTraits>(id);
  EXPECT_EQ(obj.adapterKey(), id);
}

TEST_F(NextHopGroupStoreTest, nextHopGroupCreateCtor) {
  SaiNextHopGroupTraits::AdapterHostKey k;
  SaiNextHopGroupTraits::CreateAttributes c{
      SAI_NEXT_HOP_GROUP_TYPE_ECMP,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt};
  auto obj = createObj<SaiNextHopGroupTraits>(k, c, 0);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
// HierarchicalNextHop is create-only and brcm-sai has no getter for it, so a
// warm-boot reload stores nullopt. FakeSai does implement the getter, so the
// nullopt state is set up directly here rather than via SaiStore::reload().
// Applying a value on top of nullopt must not issue a set (FakeSai rejects
// one), but the store must adopt the value.
TEST_F(NextHopGroupStoreTest, createOnlyAttrNotReadBackSkipsHwWrite) {
  auto& store = saiStore->get<SaiNextHopGroupTraits>();
  SaiNextHopGroupTraits::AdapterHostKey k;
  SaiNextHopGroupTraits::CreateAttributes created{
      SAI_NEXT_HOP_GROUP_TYPE_ECMP, std::nullopt, std::nullopt, std::nullopt};
  auto obj = store.setObject(k, created);
  auto id = obj->adapterKey();
  auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
  auto hwBefore = nextHopGroupApi.getAttribute(
      id, SaiNextHopGroupTraits::Attributes::HierarchicalNextHop{});

  SaiNextHopGroupTraits::CreateAttributes desired{
      SAI_NEXT_HOP_GROUP_TYPE_ECMP,
      std::nullopt,
      std::nullopt,
      SaiNextHopGroupTraits::Attributes::HierarchicalNextHop{!hwBefore}};
  EXPECT_NO_THROW(store.setObject(k, desired));
  EXPECT_EQ(
      GET_OPT_ATTR(NextHopGroup, HierarchicalNextHop, obj->attributes()),
      !hwBefore);
  EXPECT_EQ(
      nextHopGroupApi.getAttribute(
          id, SaiNextHopGroupTraits::Attributes::HierarchicalNextHop{}),
      hwBefore);
}
#endif

TEST_F(NextHopGroupStoreTest, nextHopGroupMemberCreateCtor) {
  auto nhgId = createNextHopGroup();
  SaiNextHopGroupMemberTraits::AdapterHostKey k{nhgId, 42};
  SaiNextHopGroupMemberTraits::CreateAttributes c{
      nhgId,
      42,
      2
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::nullopt,
      std::nullopt
#endif
  };
  auto obj = createObj<SaiNextHopGroupMemberTraits>(k, c, 0);
  EXPECT_EQ(GET_ATTR(NextHopGroupMember, NextHopId, obj.attributes()), 42);
  EXPECT_EQ(GET_OPT_ATTR(NextHopGroupMember, Weight, obj.attributes()), 2);
}

TEST_F(NextHopGroupStoreTest, nhopGroupSerDeser) {
  auto nextHopGroupId = createNextHopGroup();
  verifyAdapterKeySerDeser<SaiNextHopGroupTraits>({nextHopGroupId});
}

TEST_F(NextHopGroupStoreTest, nhopGroupToStr) {
  std::ignore = createNextHopGroup();
  verifyToStr<SaiNextHopGroupTraits>();
}

TEST_F(NextHopGroupStoreTest, nhopGroupMemberSerDeser) {
  auto nextHopGroupId = createNextHopGroup();
  auto nextHopId = createNextHop(folly::IPAddress{"10.10.10.1"});
  auto nextHopGroupMemberId =
      createNextHopGroupMember(nextHopGroupId, nextHopId, std::nullopt);
  verifyAdapterKeySerDeser<SaiNextHopGroupMemberTraits>({nextHopGroupMemberId});
}

TEST_F(NextHopGroupStoreTest, nhopGroupMemberToStr) {
  auto nextHopGroupId = createNextHopGroup();
  auto nextHopId = createNextHop(folly::IPAddress{"10.10.10.1"});
  std::ignore =
      createNextHopGroupMember(nextHopGroupId, nextHopId, std::nullopt);
  verifyToStr<SaiNextHopGroupMemberTraits>();
}

TEST_F(NextHopGroupStoreTest, nextHopGroupJson) {
  // Create a next hop group
  auto nextHopGroupId = createNextHopGroup();
  auto nextHopGroupId2 = createArsNextHopGroup(createArs());

  // Create four next hops and four next hop group members
  folly::IPAddress ip1{"10.10.10.1"};
  folly::IPAddress ip2{"10.10.10.2"};
  folly::IPAddress ip3{"10.10.10.3"};
  folly::IPAddress ip4{"10.10.10.4"};
  sai_uint32_t weight1 = 8;
  sai_uint32_t weight2 = 9;
  sai_uint32_t weight3 = 10;
  sai_uint32_t weight4 = 11;

  auto nextHopId1 = createNextHop(ip1);
  auto nextHopId2 = createNextHop(ip2);
  auto nextHopId3 = createMplsNextHop(ip3, {102, 103});
  auto nextHopId4 = createMplsNextHop(ip4, {201, 203});

  createNextHopGroupMember(nextHopGroupId, nextHopId1, weight1);
  createNextHopGroupMember(nextHopGroupId, nextHopId2, weight2);
  createNextHopGroupMember(nextHopGroupId, nextHopId3, weight3);
  createNextHopGroupMember(nextHopGroupId, nextHopId4, weight4);
  createNextHopGroupMember(nextHopGroupId2, nextHopId1, weight1);
  createNextHopGroupMember(nextHopGroupId2, nextHopId2, weight2);
  createNextHopGroupMember(nextHopGroupId2, nextHopId3, weight3);
  createNextHopGroupMember(nextHopGroupId2, nextHopId4, weight4);

  // perform a warm boot load
  SaiStore s(0);
  s.reload();
  auto& store0 = s.get<SaiNextHopGroupTraits>();

  SaiNextHopGroupTraits::AdapterHostKey k;
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, weight1));
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip2}, weight2));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiMplsNextHopTraits::AdapterHostKey{
              42,
              ip3,
              std::vector<sai_uint32_t>{102, 103},
              SAI_OUTSEG_TYPE_SWAP},
          weight3));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiMplsNextHopTraits::AdapterHostKey{
              42,
              ip4,
              std::vector<sai_uint32_t>{201, 203},
              SAI_OUTSEG_TYPE_SWAP},
          weight4));
  auto got = store0.get(k);
  EXPECT_TRUE(got);
  auto json = got->adapterHostKeyToFollyDynamic();
  auto k1 =
      SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(json);
  EXPECT_EQ(k1, k);

  SaiNextHopGroupTraits::AdapterHostKey k0{k};
  k0.mode = SAI_ARS_MODE_PER_PACKET_QUALITY;
  auto got0 = store0.get(k0);
  EXPECT_TRUE(got0);
  auto json0 = got0->adapterHostKeyToFollyDynamic();
  auto k2 =
      SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(json0);
  EXPECT_EQ(k2, k0);

  auto ak2AhkJson = s.adapterKeys2AdapterHostKeysFollyDynamic();
  EXPECT_TRUE(!ak2AhkJson.empty());
  auto& nhgAk2AhkJson =
      ak2AhkJson[saiObjectTypeToString(SAI_OBJECT_TYPE_NEXT_HOP_GROUP)];
  EXPECT_TRUE(!nhgAk2AhkJson.empty());
  EXPECT_EQ(nhgAk2AhkJson.size(), 2);

  auto iter = nhgAk2AhkJson.find(folly::to<std::string>(got->adapterKey()));
  EXPECT_FALSE(nhgAk2AhkJson.items().end() == iter);
  EXPECT_EQ(iter->second, json);

  auto iter0 = nhgAk2AhkJson.find(folly::to<std::string>(got0->adapterKey()));
  EXPECT_FALSE(nhgAk2AhkJson.items().end() == iter0);
  EXPECT_EQ(iter0->second, json0);
}

// The protection discriminator and the hierarchical-ECMP fields
// (childNextHopGroups + level) of the adapter host key must round-trip
// through warm boot state, otherwise a protection/hierarchical group would
// reload with an identity that collides with a plain ECMP group having the same
// member set.
TEST_F(NextHopGroupStoreTest, protectionAndHierarchicalAhkSerDeser) {
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiNextHopGroupTraits>();

  folly::IPAddress ip1{"10.10.10.1"};
  folly::IPAddress ip2{"10.10.10.2"};
  folly::IPAddress primaryIp{"10.10.10.9"};

  // Protection group: the primary is a direct member, the backup/standby group
  // is a child, and the group is tagged HW_PROTECTION.
  SaiNextHopGroupTraits::AdapterHostKey backupKey;
  backupKey.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, 1));
  backupKey.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip2}, 1));
  SaiNextHopGroupTraits::AdapterHostKey protKey;
  protKey.groupType = SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION;
  protKey.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, primaryIp}, 1));
  protKey.childNextHopGroups = {backupKey};
  auto protObj = store.setObject(
      protKey,
      {SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       std::nullopt});
  auto protJson = protObj->adapterHostKeyToFollyDynamic();
  EXPECT_EQ(
      SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(protJson),
      protKey);
  // A plain ECMP group over the same primary next hop must have a distinct key:
  // it differs by type (ECMP vs HW_PROTECTION) and has no child group.
  SaiNextHopGroupTraits::AdapterHostKey ecmpKey;
  ecmpKey.nhopMemberSet = protKey.nhopMemberSet;
  EXPECT_NE(protKey, ecmpKey);

  // Hierarchical ECMP: parent group whose members are child groups' identities.
  SaiNextHopGroupTraits::AdapterHostKey childKey;
  childKey.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, 3));
  SaiNextHopGroupTraits::AdapterHostKey childKey2;
  childKey2.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip2}, 4));

  // A std::set built from an initializer list is sorted AND duplicate-free, so
  // the repeated childKey collapses to a single entry.
  SaiNextHopGroupTraits::AdapterHostKey parentKey;
  parentKey.level = 1;
  parentKey.childNextHopGroups = {childKey, childKey2, childKey};
  EXPECT_EQ(parentKey.childNextHopGroups.size(), 2);

  // The same two children in the opposite order must produce an identical key
  // and hash (order-independent identity).
  SaiNextHopGroupTraits::AdapterHostKey parentKeyReordered;
  parentKeyReordered.level = 1;
  parentKeyReordered.childNextHopGroups = {childKey2, childKey};
  EXPECT_EQ(parentKey, parentKeyReordered);
  EXPECT_EQ(
      std::hash<SaiNextHopGroupTraits::AdapterHostKey>{}(parentKey),
      std::hash<SaiNextHopGroupTraits::AdapterHostKey>{}(parentKeyReordered));

  auto parentObj = store.setObject(
      parentKey,
      {SAI_NEXT_HOP_GROUP_TYPE_ECMP,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       std::nullopt});
  auto parentJson = parentObj->adapterHostKeyToFollyDynamic();
  EXPECT_EQ(
      SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(
          parentJson),
      parentKey);
  // A flat group with an empty member set and level 0 must differ from the
  // level-1 hierarchical parent.
  EXPECT_NE(parentKey, SaiNextHopGroupTraits::AdapterHostKey{});
}

TEST_F(NextHopGroupStoreTest, bulkSetNextHopGroup) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  // Create a next hop group
  auto nextHopGroupId = createNextHopGroup();

  // Create two next hops and two next hop group members
  folly::IPAddress ip1{"10.10.10.1"};
  folly::IPAddress ip2{"10.10.10.2"};
  sai_uint32_t weight1 = 1;
  sai_uint32_t weight2 = 2;
  sai_uint32_t weight3 = 3;
  sai_uint32_t weight4 = 4;

  auto nextHopId1 = createNextHop(ip1);
  auto nextHopId2 = createNextHop(ip2);

  createNextHopGroupMember(nextHopGroupId, nextHopId1, weight1);
  createNextHopGroupMember(nextHopGroupId, nextHopId2, weight2);

  // perform a warm boot load
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiNextHopGroupTraits>();

  SaiNextHopGroupTraits::AdapterHostKey k;
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, weight1));
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip2}, weight2));

  auto got = store.get(k);
  EXPECT_TRUE(got);

  auto& memberStore = s.get<SaiNextHopGroupMemberTraits>();
  SaiNextHopGroupMemberTraits::AdapterHostKey k1{nextHopGroupId, nextHopId1};
  SaiNextHopGroupMemberTraits::AdapterHostKey k2{nextHopGroupId, nextHopId2};

  // Do bulk set of weights
  std::vector<SaiNextHopGroupMemberTraits::Attributes::Weight> weights = {
      weight3, weight4};
  std::vector<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKeys = {
      k1, k2};
  memberStore.setObjects(adapterHostKeys, weights);
  auto got1 = memberStore.get(k1);
  auto got2 = memberStore.get(k2);
  EXPECT_TRUE(got1);
  EXPECT_TRUE(got2);
  EXPECT_EQ(
      got1->attributes(),
      (SaiNextHopGroupMemberTraits::CreateAttributes{
          nextHopGroupId,
          nextHopId1,
          weight3
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
          ,
          // Reload materializes these optional protection-member attributes
          // with their SAI defaults (PRIMARY role, null monitored object).
          SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
              SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_PRIMARY},
          SaiNextHopGroupMemberTraits::Attributes::MonitoredObject{
              SAI_NULL_OBJECT_ID}
#endif
      }));
  EXPECT_EQ(
      got2->attributes(),
      (SaiNextHopGroupMemberTraits::CreateAttributes{
          nextHopGroupId,
          nextHopId2,
          weight4
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
          ,
          // Reload materializes these optional protection-member attributes
          // with their SAI defaults (PRIMARY role, null monitored object).
          SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
              SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_PRIMARY},
          SaiNextHopGroupMemberTraits::Attributes::MonitoredObject{
              SAI_NULL_OBJECT_ID}
#endif
      }));
#endif
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
TEST_F(NextHopGroupStoreTest, nextHopGroupJsonAllNextHopTypes) {
  // Create a next hop group with IP, MPLS, and SRv6 next hops
  auto nextHopGroupId = createNextHopGroup();

  folly::IPAddress ip1{"10.10.10.1"};
  folly::IPAddress ip2{"10.10.10.2"};
  folly::IPAddress ip3{"10.10.10.3"};
  sai_uint32_t weight1 = 5;
  sai_uint32_t weight2 = 10;
  sai_uint32_t weight3 = 15;

  sai_object_id_t tunnelId = 100;
  sai_object_id_t srv6SidlistId = 200;

  auto nextHopId1 = createNextHop(ip1);
  auto nextHopId2 = createMplsNextHop(ip2, {301, 302});
  auto nextHopId3 = createSrv6SidlistNextHop(tunnelId, srv6SidlistId);

  createNextHopGroupMember(nextHopGroupId, nextHopId1, weight1);
  createNextHopGroupMember(nextHopGroupId, nextHopId2, weight2);
  createNextHopGroupMember(nextHopGroupId, nextHopId3, weight3);

  // perform a warm boot load
  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiNextHopGroupTraits>();

  SaiNextHopGroupTraits::AdapterHostKey k;
  k.nhopMemberSet.insert(
      std::make_pair(SaiIpNextHopTraits::AdapterHostKey{42, ip1}, weight1));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiMplsNextHopTraits::AdapterHostKey{
              42,
              ip2,
              std::vector<sai_uint32_t>{301, 302},
              SAI_OUTSEG_TYPE_SWAP},
          weight2));
  k.nhopMemberSet.insert(
      std::make_pair(
          SaiSrv6SidlistNextHopTraits::AdapterHostKey{tunnelId, srv6SidlistId},
          weight3));

  auto got = store.get(k);
  EXPECT_TRUE(got);

  // Verify round-trip serialization
  auto json = got->adapterHostKeyToFollyDynamic();
  auto recoveredKey =
      SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(json);
  EXPECT_EQ(recoveredKey, k);

  // Verify the member list contains all three next hop types
  auto memberList = json[AttributeName<
      SaiNextHopGroupTraits::Attributes::NextHopMemberList>::value];
  EXPECT_EQ(memberList.size(), 3);

  // Verify adapterKeys2AdapterHostKeys map
  auto ak2AhkJson = s.adapterKeys2AdapterHostKeysFollyDynamic();
  auto& nhgAk2AhkJson =
      ak2AhkJson[saiObjectTypeToString(SAI_OBJECT_TYPE_NEXT_HOP_GROUP)];
  EXPECT_TRUE(!nhgAk2AhkJson.empty());
  auto iter = nhgAk2AhkJson.find(folly::to<std::string>(got->adapterKey()));
  EXPECT_FALSE(nhgAk2AhkJson.items().end() == iter);
}
#endif
