/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/IsolationGroupApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class IsolationGroupStoreTest : public SaiStoreTest {
 public:
  SaiIsolationGroupTraits::CreateAttributes createAttributes(
      sai_isolation_group_type_t type = SAI_ISOLATION_GROUP_TYPE_PORT) const {
    return SaiIsolationGroupTraits::CreateAttributes{type};
  }

  IsolationGroupSaiId createIsolationGroup(
      sai_isolation_group_type_t type = SAI_ISOLATION_GROUP_TYPE_PORT) const {
    return saiApiTable->isolationGroupApi().create<SaiIsolationGroupTraits>(
        {type}, 0);
  }

  IsolationGroupMemberSaiId createMember(
      sai_object_id_t isolationGroupId,
      sai_object_id_t isolationObject) const {
    return saiApiTable->isolationGroupApi()
        .create<SaiIsolationGroupMemberTraits>(
            {isolationGroupId, isolationObject}, 0);
  }
};

/*
 * Members are keyed by (group oid, isolated object oid), both of which the
 * adapter knows, so a member survives reload on its own. The reload still has
 * to carry the key map because the group store shares the same reload and its
 * key is not adapter-recoverable.
 */
TEST_F(IsolationGroupStoreTest, loadIsolationGroupMember) {
  SaiIsolationGroupTraits::AdapterHostKey groupKey{"isolationGroup"};

  SaiStore s(0);
  auto group =
      s.get<SaiIsolationGroupTraits>().setObject(groupKey, createAttributes());
  auto groupId = group->adapterKey();
  SaiIsolationGroupMemberTraits::AdapterHostKey memberKey{groupId, 10};
  auto member = s.get<SaiIsolationGroupMemberTraits>().setObject(
      memberKey, SaiIsolationGroupMemberTraits::CreateAttributes{groupId, 10});
  auto memberId = member->adapterKey();

  auto adapterKeys = s.adapterKeysFollyDynamic();
  auto adapterKeys2AdapterHostKeys =
      s.adapterKeys2AdapterHostKeysFollyDynamic();
  SaiStore s1(0);
  s1.reload(&adapterKeys, &adapterKeys2AdapterHostKeys);

  auto got = s1.get<SaiIsolationGroupMemberTraits>().get(memberKey);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), memberId);
}

/*
 * The group's adapter host key is the FBOSS group name, which the adapter does
 * not know, so warm boot has to go through the serialized key map rather than
 * a bare reload().
 */
TEST_F(IsolationGroupStoreTest, isolationGroupWarmBootRoundTrip) {
  SaiIsolationGroupTraits::AdapterHostKey k0{"isolationGroup0"};
  SaiIsolationGroupTraits::AdapterHostKey k1{"isolationGroup1"};

  SaiStore s(0);
  auto& store = s.get<SaiIsolationGroupTraits>();
  auto obj0 = store.setObject(k0, createAttributes());
  auto obj1 = store.setObject(
      k1, createAttributes(SAI_ISOLATION_GROUP_TYPE_BRIDGE_PORT));
  EXPECT_NE(obj0->adapterKey(), obj1->adapterKey());

  auto adapterKeys = s.adapterKeysFollyDynamic();
  auto adapterKeys2AdapterHostKeys =
      s.adapterKeys2AdapterHostKeysFollyDynamic();
  SaiStore s1(0);
  s1.reload(&adapterKeys, &adapterKeys2AdapterHostKeys);
  auto& store1 = s1.get<SaiIsolationGroupTraits>();

  EXPECT_EQ(store1.get(k0)->adapterKey(), obj0->adapterKey());
  EXPECT_EQ(store1.get(k1)->adapterKey(), obj1->adapterKey());
}

// Two ports isolating the same members reference one group by name, so the
// store must hand back the same object rather than creating a second group.
TEST_F(IsolationGroupStoreTest, isolationGroupSharedByName) {
  SaiIsolationGroupTraits::AdapterHostKey k{"isolationGroup"};

  SaiStore s(0);
  auto& store = s.get<SaiIsolationGroupTraits>();
  auto obj0 = store.setObject(k, createAttributes());
  auto obj1 = store.setObject(k, createAttributes());

  EXPECT_EQ(obj0->adapterKey(), obj1->adapterKey());
}

TEST_F(IsolationGroupStoreTest, isolationGroupCtor) {
  auto groupId = createIsolationGroup();
  SaiIsolationGroupTraits::AdapterHostKey k{"isolationGroup"};
  auto obj = createObj<SaiIsolationGroupTraits>(groupId, k);
  EXPECT_EQ(obj.adapterKey(), groupId);
}

TEST_F(IsolationGroupStoreTest, isolationGroupCreateCtor) {
  SaiIsolationGroupTraits::CreateAttributes c{SAI_ISOLATION_GROUP_TYPE_PORT};
  SaiIsolationGroupTraits::AdapterHostKey k{"isolationGroup"};
  auto obj = createObj<SaiIsolationGroupTraits>(k, c, 0);
  EXPECT_EQ(
      GET_OPT_ATTR(IsolationGroup, Type, obj.attributes()),
      SAI_ISOLATION_GROUP_TYPE_PORT);
}

TEST_F(IsolationGroupStoreTest, isolationGroupMemberCtor) {
  auto groupId = createIsolationGroup();
  auto memberId = createMember(groupId, 10);
  auto obj = createObj<SaiIsolationGroupMemberTraits>(memberId);
  EXPECT_EQ(obj.adapterKey(), memberId);
  EXPECT_EQ(
      GET_ATTR(IsolationGroupMember, IsolationObject, obj.attributes()), 10);
}

TEST_F(IsolationGroupStoreTest, isolationGroupMemberCreateCtor) {
  auto groupId = createIsolationGroup();
  SaiIsolationGroupMemberTraits::CreateAttributes c{groupId, 10};
  SaiIsolationGroupMemberTraits::AdapterHostKey k{groupId, 10};
  auto obj = createObj<SaiIsolationGroupMemberTraits>(k, c, 0);
  EXPECT_EQ(
      GET_ATTR(IsolationGroupMember, IsolationGroupId, obj.attributes()),
      groupId);
  EXPECT_EQ(
      GET_ATTR(IsolationGroupMember, IsolationObject, obj.attributes()), 10);
}

/*
 * verifyAdapterKeySerDeser/verifyToStr both bare-reload every store, which the
 * group's non-recoverable key cannot satisfy, so check serialization and
 * formatting against a store built the way the agent builds it.
 */
TEST_F(IsolationGroupStoreTest, serDesIsolationGroupMemberStore) {
  SaiIsolationGroupTraits::AdapterHostKey groupKey{"isolationGroup"};

  SaiStore s(0);
  auto group =
      s.get<SaiIsolationGroupTraits>().setObject(groupKey, createAttributes());
  auto groupId = group->adapterKey();
  auto member = s.get<SaiIsolationGroupMemberTraits>().setObject(
      SaiIsolationGroupMemberTraits::AdapterHostKey{groupId, 10},
      SaiIsolationGroupMemberTraits::CreateAttributes{groupId, 10});
  auto memberId = member->adapterKey();

  auto json = s.adapterKeysFollyDynamic();
  auto gotAdapterKeys =
      keysForSaiObjStoreFromStoreJson<SaiIsolationGroupMemberTraits>(json);
  EXPECT_EQ(gotAdapterKeys.size(), 1);
  EXPECT_EQ(gotAdapterKeys[0], memberId);
}

TEST_F(IsolationGroupStoreTest, toStrIsolationGroupStore) {
  SaiStore s(0);
  auto& store = s.get<SaiIsolationGroupTraits>();
  store.setObject(
      SaiIsolationGroupTraits::AdapterHostKey{"isolationGroup"},
      createAttributes());
  auto str = fmt::format("{}", store);
  EXPECT_EQ(std::count(str.begin(), str.end(), '\n'), store.size() + 1);
}

TEST_F(IsolationGroupStoreTest, isolationGroupAdapterHostKeyToAndFromDynamic) {
  SaiIsolationGroupTraits::AdapterHostKey k{"isolationGroup0"};

  SaiStore s(0);
  auto& store = s.get<SaiIsolationGroupTraits>();
  auto obj = store.setObject(k, createAttributes());

  auto json = obj->adapterHostKeyToFollyDynamic();
  auto recovered =
      SaiObject<SaiIsolationGroupTraits>::follyDynamicToAdapterHostKey(json);
  EXPECT_EQ(recovered, k);
}
