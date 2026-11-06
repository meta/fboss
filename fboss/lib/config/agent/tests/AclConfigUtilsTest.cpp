/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <algorithm>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/ChenabAsic.h"
#include "fboss/agent/hw/switch_asics/Qumran4DAsic.h"
#include "fboss/agent/hw/switch_asics/Tomahawk5Asic.h"
#include "fboss/lib/config/agent/AclConfigUtils.h"

namespace facebook::fboss {
namespace {

cfg::SwitchInfo makeSwitchInfo(
    cfg::AsicType asicType,
    cfg::SwitchType switchType) {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = switchType;
  switchInfo.asicType() = asicType;
  switchInfo.switchIndex() = 0;
  switchInfo.switchMac() = "02:00:00:00:00:01";
  return switchInfo;
}

void expectAclMigrationWithAdditionalTable(
    const HwAsic& asic,
    std::string_view additionalTableName) {
  cfg::AclEntry acl;
  acl.name() = "existing-acl";
  cfg::SwitchConfig config;
  config.acls() = {acl};

  EXPECT_TRUE(utility::setupDefaultAclTableGroups(config, asic));

  const auto& groups = *config.aclTableGroups();
  const auto ingressGroup =
      std::find_if(groups.begin(), groups.end(), [](const auto& group) {
        return *group.stage() == cfg::AclStage::INGRESS;
      });
  ASSERT_NE(ingressGroup, groups.end());
  const auto& tables = *ingressGroup->aclTables();
  ASSERT_EQ(tables.size(), 2);
  EXPECT_EQ(
      *tables.front().name(),
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  EXPECT_EQ(*tables.front().aclEntries(), std::vector<cfg::AclEntry>{acl});
  EXPECT_EQ(*tables.back().name(), additionalTableName);
  EXPECT_TRUE(tables.back().aclEntries()->empty());
  EXPECT_TRUE(config.acls()->empty());
}

} // namespace

TEST(AclConfigUtilsTest, SetsUpDefaultAclTableGroup) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));

  cfg::AclEntry acl;
  acl.name() = "existing-acl";
  cfg::SwitchConfig config;
  config.acls() = {acl};

  EXPECT_TRUE(utility::setupDefaultAclTableGroups(config, asic));
  EXPECT_TRUE(utility::setupDefaultAclTableGroups(config, asic));

  cfg::AclTable table;
  table.name() = cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  table.priority() = 0;
  table.aclEntries() = {acl};
  table.actionTypes() = {};
  table.qualifiers() = {};
  table.udfGroups() = {};
  cfg::AclTableGroup group;
  group.name() = "acl-table-group-ingress";
  group.aclTables() = {table};
  group.stage() = cfg::AclStage::INGRESS;
  cfg::SwitchConfig expected;
  expected.aclTableGroups() = {group};

  EXPECT_EQ(config, expected);
}

TEST(AclConfigUtilsTest, AddsEntryDirectlyToDefaultAclTable) {
  Tomahawk5Asic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_TOMAHAWK5, cfg::SwitchType::NPU));
  cfg::SwitchConfig config;
  ASSERT_TRUE(utility::setupDefaultAclTableGroups(config, asic));

  cfg::AclEntry acl;
  acl.name() = "cpu-acl";
  utility::addAclEntryToDefaultAclTable(config, acl);

  EXPECT_TRUE(config.acls()->empty());
  ASSERT_EQ(config.aclTableGroups()->size(), 1);
  const auto& tables = config.aclTableGroups()->front().aclTables().value();
  ASSERT_EQ(tables.size(), 1);
  EXPECT_EQ(*tables.front().aclEntries(), std::vector<cfg::AclEntry>{acl});
}

TEST(AclConfigUtilsTest, MigratesAclsForQumran4DMultipleTables) {
  Qumran4DAsic asic(
      0,
      makeSwitchInfo(cfg::AsicType::ASIC_TYPE_QUMRAN4D, cfg::SwitchType::VOQ));
  expectAclMigrationWithAdditionalTable(asic, "ipv6-acl-table");
}

TEST(AclConfigUtilsTest, MigratesAclsForChenabMultipleTables) {
  ChenabAsic asic(
      0, makeSwitchInfo(cfg::AsicType::ASIC_TYPE_CHENAB, cfg::SwitchType::NPU));
  expectAclMigrationWithAdditionalTable(asic, "ttld-acl-table");
}

} // namespace facebook::fboss
