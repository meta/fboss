/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/store/tests/SaiStoreTest.h"

using namespace facebook::fboss;

class AclTableGroupStoreTest : public SaiStoreTest {
 public:
  const std::vector<sai_int32_t>& kBindPointTypeList() const {
    static const std::vector<sai_int32_t> bindPointTypeList{
        SAI_ACL_BIND_POINT_TYPE_SWITCH};

    return bindPointTypeList;
  }

  sai_acl_table_group_type_t kTableGroupType() const {
    return SAI_ACL_TABLE_GROUP_TYPE_PARALLEL;
  }

  const std::vector<sai_int32_t>& kActionTypeList() const {
    static const std::vector<sai_int32_t> actionTypeList{
        SAI_ACL_ACTION_TYPE_SET_DSCP};

    return actionTypeList;
  }

  sai_uint32_t kPriority() const {
    return SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY;
  }

  AclTableSaiId createAclTable(sai_int32_t stage) const {
    return saiApiTable->aclApi().create<SaiAclTableTraits>(
        {
            stage,
            kBindPointTypeList(),
            kActionTypeList(),
            true, // srcIpv6
            true, // dstIpv6
            std::nullopt, // dstIpv6Word3
            std::nullopt, // dstIpv6Word2
            true, // srcIpv4
            true, // dstIpv4
            true, // l4SrcPort
            true, // l4DstPort
            true, // ipProtocol
            true, // tcpFlags
            true, // srcPort
            true, // outPort
            true, // ipFrag
            true, // icmpv4Type
            true, // icmpv4Code
            true, // icmpv6Type
            true, // icmpv6Code
            true, // dscp
            true, // tc
            true, // dstMac
            true, // ipType
            true, // ttl
            true, // mpls label0 ttl
            true, // fdb meta
            true, // route meta
            true, // neighbor meta
            true, // ether type
            true, // outer vlan id
            std::nullopt, // aclRangeType
            true, // bth opcode
            true, // ipv6 next header
            0, // udf group 0
            1, // udf group 1
            2, // udf group 2
            3, // udf group 3
            4, // udf group 4
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
            true, // route destination
#endif
            true, // port meta
        },
        0);
  }

  AclTableGroupSaiId createAclTableGroup(sai_int32_t stage) const {
    return saiApiTable->aclApi().create<SaiAclTableGroupTraits>(
        {stage, kBindPointTypeList(), kTableGroupType()}, 0);
  }

  AclTableGroupMemberSaiId createAclTableGroupMember(
      AclTableGroupSaiId aclTableGroupId,
      AclTableSaiId aclTableId) const {
    SaiAclTableGroupMemberTraits::Attributes::TableGroupId
        aclTableGroupTableGroupIdAttribute{aclTableGroupId};
    SaiAclTableGroupMemberTraits::Attributes::TableId
        aclTableGroupTableIdAttribute{aclTableId};
    SaiAclTableGroupMemberTraits::Attributes::Priority
        aclTableGroupPriorityAttribute{kPriority()};

    return saiApiTable->aclApi().create<SaiAclTableGroupMemberTraits>(
        {aclTableGroupTableGroupIdAttribute,
         aclTableGroupTableIdAttribute,
         aclTableGroupPriorityAttribute},
        0);
  }
};

TEST_F(AclTableGroupStoreTest, loadAclTableGroups) {
  auto aclTableGroupId = createAclTableGroup(SAI_ACL_STAGE_INGRESS);
  auto aclTableGroupId2 = createAclTableGroup(SAI_ACL_STAGE_EGRESS);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiAclTableGroupTraits>();

  SaiAclTableGroupTraits::AdapterHostKey k{
      SAI_ACL_STAGE_INGRESS,
      this->kBindPointTypeList(),
      this->kTableGroupType()};
  auto got = store.get(k);
  EXPECT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), aclTableGroupId);

  SaiAclTableGroupTraits::AdapterHostKey k2{
      SAI_ACL_STAGE_EGRESS,
      this->kBindPointTypeList(),
      this->kTableGroupType()};
  auto got2 = store.get(k2);
  EXPECT_NE(got2, nullptr);
  EXPECT_EQ(got2->adapterKey(), aclTableGroupId2);
}

class AclTableGroupStoreParamTest
    : public AclTableGroupStoreTest,
      public testing::WithParamInterface<sai_acl_stage_t> {};

TEST_P(AclTableGroupStoreParamTest, loadAclTableGroupMember) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto aclTableId = createAclTable(GetParam());
  auto aclTableGroupMemberId =
      createAclTableGroupMember(aclTableGroupId, aclTableId);

  SaiStore s(0);
  s.reload();
  auto& store = s.get<SaiAclTableGroupMemberTraits>();

  SaiAclTableGroupMemberTraits::AdapterHostKey k{
      aclTableGroupId, aclTableId, kPriority()};
  auto got = store.get(k);
  EXPECT_NE(got, nullptr);
  EXPECT_EQ(got->adapterKey(), aclTableGroupMemberId);
  EXPECT_EQ(got->adapterKey(), uint64_t(aclTableId));
}

TEST_P(AclTableGroupStoreParamTest, aclTableGroupLoadCtor) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto obj = createObj<SaiAclTableGroupTraits>(aclTableGroupId);
  EXPECT_EQ(obj.adapterKey(), aclTableGroupId);
}

TEST_P(AclTableGroupStoreParamTest, aclTableGroupMemberLoadCtor) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto aclTableId = createAclTable(GetParam());
  auto aclTableGroupMemberId =
      createAclTableGroupMember(aclTableGroupId, aclTableId);

  auto obj = createObj<SaiAclTableGroupMemberTraits>(aclTableGroupMemberId);
  EXPECT_EQ(obj.adapterKey(), aclTableGroupMemberId);
}

TEST_P(AclTableGroupStoreParamTest, aclTableGroupCreateCtor) {
  SaiAclTableGroupTraits::CreateAttributes c{
      GetParam(), this->kBindPointTypeList(), this->kTableGroupType()};
  SaiAclTableGroupTraits::AdapterHostKey k{
      GetParam(), this->kBindPointTypeList(), this->kTableGroupType()};
  auto obj = createObj<SaiAclTableGroupTraits>(k, c, 0);
  EXPECT_EQ(GET_ATTR(AclTableGroup, Stage, obj.attributes()), GetParam());
}

TEST_P(AclTableGroupStoreParamTest, aclTableGroupMemberCreateCtor) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto aclTableId = createAclTable(GetParam());

  SaiAclTableGroupMemberTraits::CreateAttributes c{
      aclTableGroupId, aclTableId, this->kPriority()};

  SaiAclTableGroupMemberTraits::AdapterHostKey k{
      aclTableGroupId, aclTableId, this->kPriority()};
  auto obj = createObj<SaiAclTableGroupMemberTraits>(k, c, 0);
  EXPECT_EQ(
      GET_ATTR(AclTableGroupMember, TableGroupId, obj.attributes()),
      aclTableGroupId);
}

TEST_P(AclTableGroupStoreParamTest, serDeserAclTableGroupStore) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  verifyAdapterKeySerDeser<SaiAclTableGroupTraits>({aclTableGroupId});
}

TEST_P(AclTableGroupStoreParamTest, toStrAclTableGroupStore) {
  std::ignore = createAclTableGroup(GetParam());
  verifyToStr<SaiAclTableGroupTraits>();
}

TEST_P(AclTableGroupStoreParamTest, serDeserAclTableGroupMemberStore) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto aclTableId = createAclTable(GetParam());
  auto aclTableGroupMemberId =
      createAclTableGroupMember(aclTableGroupId, aclTableId);
  verifyAdapterKeySerDeser<SaiAclTableGroupMemberTraits>(
      {aclTableGroupMemberId});
}

TEST_P(AclTableGroupStoreParamTest, toStrAclTableGroupMemberStore) {
  auto aclTableGroupId = createAclTableGroup(GetParam());
  auto aclTableId = createAclTable(GetParam());
  std::ignore = createAclTableGroupMember(aclTableGroupId, aclTableId);
  verifyToStr<SaiAclTableGroupMemberTraits>();
}

INSTANTIATE_TEST_CASE_P(
    AclTableGroupStoreParamTestInstantiation,
    AclTableGroupStoreParamTest,
    testing::Values(SAI_ACL_STAGE_INGRESS, SAI_ACL_STAGE_EGRESS));
