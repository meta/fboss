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

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class IsolationGroupApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    isolationGroupApi = std::make_unique<IsolationGroupApi>();
  }

  IsolationGroupSaiId createIsolationGroup(
      sai_isolation_group_type_t type = SAI_ISOLATION_GROUP_TYPE_PORT) const {
    return isolationGroupApi->create<SaiIsolationGroupTraits>({type}, 0);
  }

  IsolationGroupMemberSaiId createMember(
      const sai_object_id_t isolationGroupId,
      const sai_object_id_t isolationObject) const {
    SaiIsolationGroupMemberTraits::Attributes::IsolationGroupId
        groupIdAttribute{isolationGroupId};
    SaiIsolationGroupMemberTraits::Attributes::IsolationObject objectAttribute{
        isolationObject};
    return isolationGroupApi->create<SaiIsolationGroupMemberTraits>(
        {groupIdAttribute, objectAttribute}, 0);
  }

  void checkIsolationGroup(IsolationGroupSaiId id) const {
    EXPECT_EQ(id, fs->isolationGroupManager.get(id).id);
  }

  void checkMember(
      IsolationGroupSaiId groupId,
      IsolationGroupMemberSaiId memberId) const {
    EXPECT_EQ(memberId, fs->isolationGroupManager.getMember(memberId).id);
    EXPECT_EQ(
        groupId,
        fs->isolationGroupManager.getMember(memberId).isolationGroupId);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<IsolationGroupApi> isolationGroupApi;
};

TEST_F(IsolationGroupApiTest, createIsolationGroup) {
  auto id = createIsolationGroup();
  checkIsolationGroup(id);
}

TEST_F(IsolationGroupApiTest, removeIsolationGroup) {
  auto id = createIsolationGroup();
  checkIsolationGroup(id);
  isolationGroupApi->remove(id);
}

TEST_F(IsolationGroupApiTest, multipleIsolationGroups) {
  auto id1 = createIsolationGroup();
  auto id2 = createIsolationGroup();
  checkIsolationGroup(id1);
  checkIsolationGroup(id2);
  EXPECT_NE(id1, id2);
}

TEST_F(IsolationGroupApiTest, getIsolationGroupTypeAttribute) {
  auto id = createIsolationGroup(SAI_ISOLATION_GROUP_TYPE_BRIDGE_PORT);
  auto type = isolationGroupApi->getAttribute(
      id, SaiIsolationGroupTraits::Attributes::Type());
  EXPECT_EQ(type, SAI_ISOLATION_GROUP_TYPE_BRIDGE_PORT);
}

TEST_F(IsolationGroupApiTest, createMember) {
  auto groupId = createIsolationGroup();
  auto memberId = createMember(groupId, 42);
  checkMember(groupId, memberId);
}

TEST_F(IsolationGroupApiTest, removeMember) {
  auto groupId = createIsolationGroup();
  auto memberId = createMember(groupId, 42);
  checkMember(groupId, memberId);
  isolationGroupApi->remove(memberId);
}

TEST_F(IsolationGroupApiTest, memberListReflectsMembers) {
  auto groupId = createIsolationGroup();
  auto memberListEmpty = isolationGroupApi->getAttribute(
      groupId, SaiIsolationGroupTraits::Attributes::IsolationMemberList());
  EXPECT_EQ(memberListEmpty.size(), 0);

  auto memberId1 = createMember(groupId, 42);
  auto memberId2 = createMember(groupId, 43);
  EXPECT_NE(memberId1, memberId2);

  auto memberList = isolationGroupApi->getAttribute(
      groupId, SaiIsolationGroupTraits::Attributes::IsolationMemberList());
  EXPECT_EQ(memberList.size(), 2);
}

TEST_F(IsolationGroupApiTest, getMemberAttributes) {
  auto groupId = createIsolationGroup();
  const sai_object_id_t isolationObject = 42;
  auto memberId = createMember(groupId, isolationObject);

  auto groupIdGot = isolationGroupApi->getAttribute(
      memberId, SaiIsolationGroupMemberTraits::Attributes::IsolationGroupId());
  auto objectGot = isolationGroupApi->getAttribute(
      memberId, SaiIsolationGroupMemberTraits::Attributes::IsolationObject());

  EXPECT_EQ(groupIdGot, groupId);
  EXPECT_EQ(objectGot, isolationObject);
}

TEST_F(IsolationGroupApiTest, setIsolationGroupAttribute) {
  auto groupId = createIsolationGroup();
  // TYPE is CREATE_ONLY and the member list is READ_ONLY, so neither is
  // settable after create.
  SaiIsolationGroupTraits::Attributes::Type typeAttribute{
      SAI_ISOLATION_GROUP_TYPE_PORT};
  SaiIsolationGroupTraits::Attributes::IsolationMemberList
      memberListAttribute{};
  EXPECT_THROW(
      isolationGroupApi->setAttribute(groupId, typeAttribute), SaiApiError);
  EXPECT_THROW(
      isolationGroupApi->setAttribute(groupId, memberListAttribute),
      SaiApiError);
}

TEST_F(IsolationGroupApiTest, setMemberAttribute) {
  auto groupId = createIsolationGroup();
  auto memberId = createMember(groupId, 42);
  // Both member attributes are CREATE_ONLY.
  SaiIsolationGroupMemberTraits::Attributes::IsolationGroupId groupIdAttribute{
      groupId};
  SaiIsolationGroupMemberTraits::Attributes::IsolationObject objectAttribute{
      43};
  EXPECT_THROW(
      isolationGroupApi->setAttribute(memberId, groupIdAttribute), SaiApiError);
  EXPECT_THROW(
      isolationGroupApi->setAttribute(memberId, objectAttribute), SaiApiError);
}

TEST_F(IsolationGroupApiTest, formatIsolationGroupTypeAttribute) {
  SaiIsolationGroupTraits::Attributes::Type type{SAI_ISOLATION_GROUP_TYPE_PORT};
  std::string expected(
      "Type: " + std::to_string(SAI_ISOLATION_GROUP_TYPE_PORT));
  EXPECT_EQ(expected, fmt::format("{}", type));
}

TEST_F(IsolationGroupApiTest, formatMemberIsolationObjectAttribute) {
  SaiIsolationGroupMemberTraits::Attributes::IsolationObject object{42};
  std::string expected("IsolationObject: 42");
  EXPECT_EQ(expected, fmt::format("{}", object));
}
