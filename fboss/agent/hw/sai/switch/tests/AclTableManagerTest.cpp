/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/types.h"

#include <fb303/ServiceData.h>
#include <folly/ScopeGuard.h>

#include <string>

using namespace facebook::fboss;

namespace {
const std::string kAclTable2 = "AclTable2";
const std::string kAclTable3 = "AclTable3";
} // namespace

class AclTableManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
  }
  int kPriority() {
    return 1;
  }

  int kPriority2() {
    return 2;
  }

  uint8_t kDscp() {
    return 10;
  }

  uint8_t kDscp2() {
    return 20;
  }

  cfg::AclActionType kActionType() {
    return cfg::AclActionType::DENY;
  }
};

TEST_F(AclTableManagerTest, addAclTable) {
  auto aclTableId =
      saiManagerTable->aclTableManager()
          .getAclTableHandle(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE())
          ->aclTable->adapterKey();
  // Acl table is added as part of sai switch init in test setup
  auto stageGot = saiApiTable->aclApi().getAttribute(
      aclTableId, SaiAclTableTraits::Attributes::Stage());
  EXPECT_EQ(stageGot, SAI_ACL_STAGE_INGRESS);
}

TEST_F(AclTableManagerTest, addTwoAclTable) {
  // AclTable1 should already be added
  auto aclTableId =
      saiManagerTable->aclTableManager()
          .getAclTableHandle(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE())
          ->aclTable->adapterKey();
  auto table2 = std::make_shared<AclTable>(0, kAclTable2);
  AclTableSaiId aclTableId2 = saiManagerTable->aclTableManager().addAclTable(
      table2, cfg::AclStage::INGRESS, nullptr /*state*/);

  auto stageGot = saiApiTable->aclApi().getAttribute(
      aclTableId, SaiAclTableTraits::Attributes::Stage());
  EXPECT_EQ(stageGot, SAI_ACL_STAGE_INGRESS);

  auto stageGot2 = saiApiTable->aclApi().getAttribute(
      aclTableId2, SaiAclTableTraits::Attributes::Stage());
  EXPECT_EQ(stageGot2, SAI_ACL_STAGE_INGRESS);
}

TEST_F(AclTableManagerTest, addPortBoundAclTable) {
  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("portBoundGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);

  auto table = std::make_shared<AclTable>(0, kAclTable2);
  const auto aclTableId = saiManagerTable->aclTableManager().addAclTable(
      table,
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);
  auto secondTable = std::make_shared<AclTable>(1, kAclTable3);
  const auto secondAclTableId = saiManagerTable->aclTableManager().addAclTable(
      secondTable,
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);

  const auto bindPoints = saiApiTable->aclApi().getAttribute(
      aclTableId, SaiAclTableTraits::Attributes::BindPointTypeList());
  EXPECT_EQ(bindPoints, std::vector<sai_int32_t>{SAI_ACL_BIND_POINT_TYPE_PORT});
  const auto secondBindPoints = saiApiTable->aclApi().getAttribute(
      secondAclTableId, SaiAclTableTraits::Attributes::BindPointTypeList());
  EXPECT_EQ(
      secondBindPoints, std::vector<sai_int32_t>{SAI_ACL_BIND_POINT_TYPE_PORT});

  auto* aclTableGroupHandle =
      saiManagerTable->aclTableGroupManager().getAclTableGroupHandle(
          SAI_ACL_STAGE_INGRESS, cfg::AclTableGroupBindPoint::PORT);
  ASSERT_NE(aclTableGroupHandle, nullptr);
  EXPECT_NE(
      saiManagerTable->aclTableGroupManager().getAclTableGroupMemberHandle(
          aclTableGroupHandle, kAclTable2),
      nullptr);
  EXPECT_NE(
      saiManagerTable->aclTableGroupManager().getAclTableGroupMemberHandle(
          aclTableGroupHandle, kAclTable3),
      nullptr);
}

TEST_F(
    AclTableManagerTest,
    removeObsoletePortBoundAclTablesFromPublishedState) {
  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("portBoundGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);

  auto obsoleteTable = std::make_shared<AclTable>(0, kAclTable2);
  auto retainedTable = std::make_shared<AclTable>(1, kAclTable3);
  saiManagerTable->aclTableManager().addAclTable(
      obsoleteTable,
      cfg::AclStage::INGRESS,
      nullptr,
      cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableManager().addAclTable(
      retainedTable,
      cfg::AclStage::INGRESS,
      nullptr,
      cfg::AclTableGroupBindPoint::PORT);

  auto makeState = [](const std::vector<std::shared_ptr<AclTable>>& tables) {
    auto tableMap = std::make_shared<AclTableMap>();
    for (const auto& table : tables) {
      tableMap->addTable(table);
    }
    auto group = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
    group->setName("portBoundGroup");
    group->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
    group->setAclTableMap(tableMap);
    auto groupMap = std::make_shared<AclTableGroupMap>();
    groupMap->addAclTableGroup(group);
    auto multiGroupMap = std::make_shared<MultiSwitchAclTableGroupMap>();
    multiGroupMap->addMapNode(
        groupMap, HwSwitchMatcher::defaultHwSwitchMatcher());
    auto state = std::make_shared<SwitchState>();
    state->resetPortAclTableGroups(multiGroupMap);
    state->publish();
    return state;
  };

  auto oldState = makeState({obsoleteTable, retainedTable});
  auto newState = makeState({retainedTable});
  saiManagerTable->aclTableManager().removeObsoletePortBoundAclTables(
      oldState, newState);

  EXPECT_EQ(
      saiManagerTable->aclTableManager().getAclTableHandle(kAclTable2),
      nullptr);
  EXPECT_NE(
      saiManagerTable->aclTableManager().getAclTableHandle(kAclTable3),
      nullptr);
}

TEST_F(AclTableManagerTest, recreateBoundPortAclTable) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };

  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("portBoundGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);

  auto oldTable = std::make_shared<AclTable>(0, kAclTable2);
  oldTable->setAclMap(std::make_shared<AclMap>());
  const auto oldAclTableId = saiManagerTable->aclTableManager().addAclTable(
      oldTable,
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);

  auto swPort = makePort(testInterfaces[0].remoteHosts[0].port);
  swPort->setIngressAclTableName(kAclTable2);
  saiManagerTable->portManager().setIngressAcl(swPort);

  auto newTable = oldTable->clone();
  newTable->setQualifiers({cfg::AclTableQualifier::L4_DST_PORT});
  saiManagerTable->aclTableManager().changedAclTable(
      oldTable,
      newTable,
      cfg::AclStage::INGRESS,
      nullptr /*state*/,
      cfg::AclTableGroupBindPoint::PORT);

  const auto* aclTableHandle =
      saiManagerTable->aclTableManager().getAclTableHandle(kAclTable2);
  ASSERT_NE(aclTableHandle, nullptr);
  const auto newAclTableId = aclTableHandle->aclTable->adapterKey();
  EXPECT_NE(newAclTableId, oldAclTableId);

  auto* portHandle =
      saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(portHandle, nullptr);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressAcl{}),
      newAclTableId);

  auto* aclTableGroupHandle =
      saiManagerTable->aclTableGroupManager().getAclTableGroupHandle(
          SAI_ACL_STAGE_INGRESS, cfg::AclTableGroupBindPoint::PORT);
  ASSERT_NE(aclTableGroupHandle, nullptr);
  EXPECT_NE(
      saiManagerTable->aclTableGroupManager().getAclTableGroupMemberHandle(
          aclTableGroupHandle, kAclTable2),
      nullptr);
}

TEST_F(AclTableManagerTest, addDupAclTable) {
  state::AclTableFields fields{};
  fields.priority() = 0;
  fields.id() = cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  auto table1 = std::make_shared<AclTable>(std::move(fields));
  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclTable(
          table1, cfg::AclStage::INGRESS, nullptr /*state*/),
      FbossError);
}

TEST_F(AclTableManagerTest, getAclTable) {
  auto handle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  EXPECT_TRUE(handle);
  EXPECT_TRUE(handle->aclTable);
}

TEST_F(AclTableManagerTest, checkNonExistentAclTable) {
  auto handle =
      saiManagerTable->aclTableManager().getAclTableHandle(kAclTable2);

  EXPECT_FALSE(handle);
}

TEST_F(AclTableManagerTest, legacyAclTablePriorityMigrationDoesNotRecreate) {
  auto legacyTable = std::make_shared<AclTable>(0, kAclTable2);
  auto migratedTable = std::make_shared<AclTable>(23, kAclTable2);

  EXPECT_FALSE(saiManagerTable->aclTableManager().needsAclTableRecreate(
      legacyTable, migratedTable, cfg::AclStage::INGRESS));
  EXPECT_FALSE(saiManagerTable->aclTableManager().needsAclTableRecreate(
      migratedTable, legacyTable, cfg::AclStage::INGRESS));
}

TEST_F(AclTableManagerTest, preMigrationAclTablePriorityChangeDoesNotRecreate) {
  auto oldTable = std::make_shared<AclTable>(1, kAclTable2);
  auto newTable = std::make_shared<AclTable>(23, kAclTable2);

  EXPECT_FALSE(saiManagerTable->aclTableManager().needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
}

TEST_F(AclTableManagerTest, configuredAclTablePriorityChangeRecreates) {
  auto oldTable = std::make_shared<AclTable>(23, kAclTable2);
  auto newTable = std::make_shared<AclTable>(24, kAclTable2);

  EXPECT_TRUE(saiManagerTable->aclTableManager().needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
}

TEST_F(AclTableManagerTest, addAclEntry) {
  auto aclTableId =
      saiManagerTable->aclTableManager()
          .getAclTableHandle(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE())
          ->aclTable->adapterKey();

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());

  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto tableIdGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::TableId());
  EXPECT_EQ(tableIdGot, aclTableId);
}

TEST_F(AclTableManagerTest, addAclEntryWithTcpFlags) {
  auto addTcpFlagsEntry = [this](
                              int priority,
                              const std::string& name,
                              std::optional<uint8_t> tcpFlagsMask) {
    auto aclEntry = std::make_shared<AclEntry>(priority, name);
    aclEntry->setProto(6);
    aclEntry->setTcpFlagsBitMap(0x02);
    if (tcpFlagsMask) {
      aclEntry->setTcpFlagsMask(*tcpFlagsMask);
    }
    aclEntry->setActionType(kActionType());
    auto aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
        aclEntry,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
        nullptr /*state*/);
    return saiApiTable->aclApi()
        .getAttribute(
            aclEntryId, SaiAclEntryTraits::Attributes::FieldTcpFlags{})
        .getDataAndMask();
  };

  using DataAndMask = std::pair<uint8_t, uint8_t>;
  EXPECT_EQ(
      addTcpFlagsEntry(kPriority(), "AclEntry1", std::nullopt),
      DataAndMask(0x02, 0x3F));
  EXPECT_EQ(
      addTcpFlagsEntry(kPriority2(), "AclEntry2", 0x12),
      DataAndMask(0x02, 0x12));
}

TEST_F(AclTableManagerTest, defaultAclTableOmitsLookupClassPort) {
  auto* aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  ASSERT_NE(aclTableHandle, nullptr);
  EXPECT_FALSE(saiApiTable->aclApi().getAttribute(
      aclTableHandle->aclTable->adapterKey(),
      SaiAclTableTraits::Attributes::FieldPortUserMeta{}));
}

TEST_F(AclTableManagerTest, addAclTableWithLookupClassPort) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };

  auto aclTable = std::make_shared<AclTable>(0, kAclTable2);
  aclTable->setQualifiers({cfg::AclTableQualifier::LOOKUP_CLASS_PORT});
  saiManagerTable->aclTableManager().addAclTable(
      aclTable, cfg::AclStage::INGRESS, nullptr /*state*/);

  auto* aclTableHandle =
      saiManagerTable->aclTableManager().getAclTableHandle(kAclTable2);
  ASSERT_NE(aclTableHandle, nullptr);
  EXPECT_TRUE(saiApiTable->aclApi().getAttribute(
      aclTableHandle->aclTable->adapterKey(),
      SaiAclTableTraits::Attributes::FieldPortUserMeta{}));
}

TEST_F(
    AclTableManagerTest,
    publishedQualifiersMatchingAsicDefaultSkipRecreate) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  auto& aclTableManager = saiManagerTable->aclTableManager();

  // A table with no qualifiers is built from the ASIC's own set.
  auto oldTable = std::make_shared<AclTable>(0, kAclTable2);
  aclTableManager.addAclTable(oldTable, cfg::AclStage::INGRESS, nullptr);
  auto tableIdBefore =
      aclTableManager.getAclTableHandle(kAclTable2)->aclTable->adapterKey();

  auto supported =
      aclTableManager.getSupportedQualifierSet(cfg::AclStage::INGRESS);
  auto newTable = std::make_shared<AclTable>(0, kAclTable2);
  newTable->setQualifiers(
      std::vector<cfg::AclTableQualifier>(supported.begin(), supported.end()));

  // SAI cannot see the difference, so the table must survive untouched.
  EXPECT_FALSE(aclTableManager.needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
  aclTableManager.changedAclTable(
      oldTable, newTable, cfg::AclStage::INGRESS, nullptr /*state*/);
  EXPECT_EQ(
      aclTableManager.getAclTableHandle(kAclTable2)->aclTable->adapterKey(),
      tableIdBefore);
}

TEST_F(AclTableManagerTest, reorderedQualifiersSkipRecreate) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  auto& aclTableManager = saiManagerTable->aclTableManager();

  auto oldTable = std::make_shared<AclTable>(0, kAclTable2);
  oldTable->setQualifiers(
      {cfg::AclTableQualifier::DSCP, cfg::AclTableQualifier::SRC_IPV6});
  auto newTable = std::make_shared<AclTable>(0, kAclTable2);
  newTable->setQualifiers(
      {cfg::AclTableQualifier::SRC_IPV6, cfg::AclTableQualifier::DSCP});

  EXPECT_FALSE(aclTableManager.needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
}

TEST_F(AclTableManagerTest, narrowerQualifiersRecreateAclTable) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  auto& aclTableManager = saiManagerTable->aclTableManager();

  // Carry an entry so the recreate exercises the entry remove and re-add.
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());
  auto aclMap = std::make_shared<AclMap>();
  aclMap->addNode(aclEntry);

  auto oldTable = std::make_shared<AclTable>(0, kAclTable2);
  oldTable->setAclMap(aclMap);
  aclTableManager.addAclTable(oldTable, cfg::AclStage::INGRESS, nullptr);
  aclTableManager.addAclEntry(aclEntry, kAclTable2, nullptr /*state*/);
  auto tableIdBefore =
      aclTableManager.getAclTableHandle(kAclTable2)->aclTable->adapterKey();

  auto newTable = std::make_shared<AclTable>(0, kAclTable2);
  newTable->setAclMap(aclMap);
  newTable->setQualifiers({cfg::AclTableQualifier::DSCP});

  EXPECT_TRUE(aclTableManager.needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
  aclTableManager.changedAclTable(
      oldTable, newTable, cfg::AclStage::INGRESS, nullptr /*state*/);

  auto* aclTableHandle = aclTableManager.getAclTableHandle(kAclTable2);
  ASSERT_NE(aclTableHandle, nullptr);
  EXPECT_NE(aclTableHandle->aclTable->adapterKey(), tableIdBefore);
  EXPECT_FALSE(saiApiTable->aclApi().getAttribute(
      aclTableHandle->aclTable->adapterKey(),
      SaiAclTableTraits::Attributes::FieldSrcIpV6{}));
  EXPECT_NE(
      aclTableManager.getAclEntryHandle(
          aclTableHandle, kPriority(), "AclEntry1"),
      nullptr);
}

TEST_F(AclTableManagerTest, changedTablePropertiesRecreateAclTable) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  auto& aclTableManager = saiManagerTable->aclTableManager();

  auto qualifiers = std::vector<cfg::AclTableQualifier>{
      cfg::AclTableQualifier::DSCP, cfg::AclTableQualifier::SRC_IPV6};
  // Both priorities are past the migration point, so neither is normalized
  // away.
  auto oldTable = std::make_shared<AclTable>(23, kAclTable2);
  oldTable->setQualifiers(qualifiers);
  auto newTable = std::make_shared<AclTable>(24, kAclTable2);
  newTable->setQualifiers(qualifiers);

  EXPECT_TRUE(aclTableManager.needsAclTableRecreate(
      oldTable, newTable, cfg::AclStage::INGRESS));
}

TEST_F(AclTableManagerTest, addAclEntryWithCounter) {
  auto aclTableId =
      saiManagerTable->aclTableManager()
          .getAclTableHandle(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE())
          ->aclTable->adapterKey();

  auto counter = cfg::TrafficCounter();
  *counter.name() = "stat0.c";
  MatchAction action = MatchAction();
  action.setTrafficCounter(counter);

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setAclAction(action);

  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto tableIdGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::TableId());
  EXPECT_EQ(tableIdGot, aclTableId);

  auto aclCounterIdGot =
      saiApiTable->aclApi()
          .getAttribute(
              aclEntryId, SaiAclEntryTraits::Attributes::ActionCounter())
          .getData();

  auto tableIdGot2 = saiApiTable->aclApi().getAttribute(
      AclCounterSaiId(aclCounterIdGot),
      SaiAclCounterTraits::Attributes::TableId());
  EXPECT_EQ(tableIdGot2, aclTableId);
}

TEST_F(AclTableManagerTest, addAclEntryWithLookupClassPort) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };

  auto aclTable = std::make_shared<AclTable>(0, kAclTable2);
  aclTable->setQualifiers({cfg::AclTableQualifier::LOOKUP_CLASS_PORT});
  saiManagerTable->aclTableManager().addAclTable(
      aclTable, cfg::AclStage::INGRESS, nullptr /*state*/);

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setLookupClassPort(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  aclEntry->setActionType(kActionType());

  auto aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry, kAclTable2, nullptr /*state*/);

  auto field = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::FieldPortUserMeta{});
  EXPECT_EQ(
      field.getDataAndMask().first,
      static_cast<uint32_t>(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED));
  EXPECT_EQ(field.getDataAndMask().second, 0x3F);

  auto secondAclEntry =
      std::make_shared<AclEntry>(kPriority2(), std::string("AclEntry2"));
  secondAclEntry->setLookupClassPort(
      cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  secondAclEntry->setActionType(kActionType());

  auto secondAclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      secondAclEntry, kAclTable2, nullptr /*state*/);
  auto secondField = saiApiTable->aclApi().getAttribute(
      secondAclEntryId, SaiAclEntryTraits::Attributes::FieldPortUserMeta{});
  EXPECT_EQ(
      secondField.getDataAndMask().first,
      static_cast<uint32_t>(cfg::AclLookupClassPort::CLASS_PORT_BLOCKED));
  EXPECT_EQ(secondField.getDataAndMask().second, 0x3F);
}

TEST_F(AclTableManagerTest, addTwoAclEntry) {
  auto aclTableId =
      saiManagerTable->aclTableManager()
          .getAclTableHandle(
              cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE())
          ->aclTable->adapterKey();

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());

  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto tableIdGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::TableId());
  EXPECT_EQ(tableIdGot, aclTableId);

  auto aclEntry2 =
      std::make_shared<AclEntry>(kPriority2(), std::string("AclEntry2"));
  aclEntry2->setDscp(kDscp2());
  aclEntry2->setActionType(kActionType());

  AclEntrySaiId aclEntryId2 = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry2,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto tableIdGot2 = saiApiTable->aclApi().getAttribute(
      aclEntryId2, SaiAclEntryTraits::Attributes::TableId());
  EXPECT_EQ(tableIdGot2, aclTableId);
}

TEST_F(AclTableManagerTest, addDupAclEntry) {
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());

  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto dupAclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  dupAclEntry->setDscp(kDscp());
  dupAclEntry->setActionType(cfg::AclActionType::DENY);

  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclEntry(
          dupAclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          nullptr /*state*/),
      FbossError);
}

TEST_F(AclTableManagerTest, getAclEntry) {
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());

  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  EXPECT_TRUE(aclTableHandle);
  EXPECT_TRUE(aclTableHandle->aclTable);

  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));

  EXPECT_TRUE(aclEntryHandle);
  EXPECT_TRUE(aclEntryHandle->aclEntry);
}

TEST_F(AclTableManagerTest, checkNonExistentAclEntry) {
  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  EXPECT_TRUE(aclTableHandle);
  EXPECT_TRUE(aclTableHandle->aclTable);

  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  EXPECT_FALSE(aclEntryHandle);
}

TEST_F(AclTableManagerTest, aclMirroring) {
  std::string mirrorId = "mirror1";
  auto mirror = std::make_shared<Mirror>(
      mirrorId,
      std::make_optional<PortDescriptor>(PortID(1)),
      std::optional<folly::IPAddress>());
  mirror->setEgressPortDesc(PortDescriptor(PortID(1)));
  saiManagerTable->mirrorManager().addNode(mirror);
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry->setDscp(kDscp());
  aclEntry->setActionType(kActionType());
  MatchAction matchAction = MatchAction();
  matchAction.setIngressMirror(mirrorId);
  aclEntry->setAclAction(matchAction);
  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);
  SaiMirrorHandle* mirrorHandle =
      saiManagerTable->mirrorManager().getMirrorHandle(mirrorId);
  auto gotMirrorSaiIdList = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::ActionMirrorIngress());
  EXPECT_EQ((gotMirrorSaiIdList.getData())[0], mirrorHandle->adapterKey());
}

TEST_F(AclTableManagerTest, addAclEntryWithL4DstPortRange) {
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  cfg::Range range;
  range.minimum() = 1000;
  range.maximum() = 2000;
  aclEntry->setL4DstPortRange(range);
  aclEntry->setActionType(kActionType());

  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  ASSERT_TRUE(aclEntryHandle);
  ASSERT_TRUE(aclEntryHandle->dstPortRange);

  auto rangeType = saiApiTable->aclApi().getAttribute(
      aclEntryHandle->dstPortRange->adapterKey(),
      SaiAclRangeTraits::Attributes::Type());
  EXPECT_EQ(rangeType, SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE);

  auto rangeLimit = saiApiTable->aclApi().getAttribute(
      aclEntryHandle->dstPortRange->adapterKey(),
      SaiAclRangeTraits::Attributes::Limit());
  EXPECT_EQ(rangeLimit.min, 1000);
  EXPECT_EQ(rangeLimit.max, 2000);
}

TEST_F(AclTableManagerTest, removeAclEntryWithRange) {
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  cfg::Range range;
  range.minimum() = 1000;
  range.maximum() = 2000;
  aclEntry->setL4DstPortRange(range);
  aclEntry->setActionType(kActionType());

  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto& rangeStore = saiStore->get<SaiAclRangeTraits>();
  EXPECT_EQ(rangeStore.size(), 1);

  saiManagerTable->aclTableManager().removeAclEntry(
      aclEntry, cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto aclEntryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  EXPECT_FALSE(aclEntryHandle);
  EXPECT_EQ(rangeStore.size(), 0);
}

TEST_F(AclTableManagerTest, twoEntriesSameRange) {
  cfg::Range range;
  range.minimum() = 1000;
  range.maximum() = 2000;

  auto aclEntry1 =
      std::make_shared<AclEntry>(kPriority(), std::string("AclEntry1"));
  aclEntry1->setL4DstPortRange(range);
  aclEntry1->setActionType(kActionType());
  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry1,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto aclEntry2 =
      std::make_shared<AclEntry>(kPriority2(), std::string("AclEntry2"));
  aclEntry2->setL4DstPortRange(range);
  aclEntry2->setActionType(kActionType());
  saiManagerTable->aclTableManager().addAclEntry(
      aclEntry2,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      nullptr /*state*/);

  auto& rangeStore = saiStore->get<SaiAclRangeTraits>();
  EXPECT_EQ(rangeStore.size(), 1);

  auto aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto handle1 = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("AclEntry1"));
  auto handle2 = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority2(), std::string("AclEntry2"));
  ASSERT_TRUE(handle1->dstPortRange);
  ASSERT_TRUE(handle2->dstPortRange);
  EXPECT_EQ(
      handle1->dstPortRange->adapterKey(), handle2->dstPortRange->adapterKey());

  saiManagerTable->aclTableManager().removeAclEntry(
      aclEntry1, cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  EXPECT_EQ(rangeStore.size(), 1);

  saiManagerTable->aclTableManager().removeAclEntry(
      aclEntry2, cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  EXPECT_EQ(rangeStore.size(), 0);
}

class AclTableManagerPbrTest : public AclTableManagerTest {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    AclTableManagerTest::SetUp();
  }
  void TearDown() override {
    AclTableManagerTest::TearDown();
    FLAGS_enable_nexthop_id_manager = false;
  }
  // A NextHopSetID that is never allocated, for negative-resolution tests.
  static constexpr int64_t kUnallocatedNhgId = 987654321;
};

TEST_F(AclTableManagerPbrTest, addPbrAclEntryWithMatchAndRedirectNhg) {
  // 1. Allocate two named NHGs: one the entry matches on, one it redirects to.
  // RIB would do this; here we drive it directly so the name->id mapping lands
  // in FibInfo, which is how HW resolves the ids to nexthops.
  RouteNextHopSet matchNhops;
  matchNhops.insert(makeNextHop(testInterfaces[0]));
  nextHopIDManager_->allocateNamedNextHopGroup("matchNhg", matchNhops);
  RouteNextHopSet redirectNhops;
  redirectNhops.insert(makeNextHop(testInterfaces[1]));
  nextHopIDManager_->allocateNamedNextHopGroup("redirectNhg", redirectNhops);
  auto matchNhgIdOpt = nextHopIDManager_->getNextHopSetIDForName("matchNhg");
  ASSERT_TRUE(matchNhgIdOpt.has_value());
  auto matchNhgId = static_cast<int64_t>(*matchNhgIdOpt);
  auto redirectNhgIdOpt =
      nextHopIDManager_->getNextHopSetIDForName("redirectNhg");
  ASSERT_TRUE(redirectNhgIdOpt.has_value());
  auto redirectNhgId = static_cast<int64_t>(*redirectNhgIdOpt);
  auto state = getProgrammedState();

  // 2. Build a PBR AclEntry: match on an NHG -> redirect to another NHG. The
  // RIB resolves the config redirect NHG name to a NextHopSetID; switch state
  // (and HW) carry the id. (The TC field is exercised at the SAI API level;
  // matching on it here would require a table that declares the TC qualifier.)
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrEntry"));
  aclEntry->setNextHopGroupId(matchNhgId);
  MatchAction action;
  action.setRedirectNextHopGroupId(redirectNhgId);
  aclEntry->setAclAction(action);

  // 3. Program it.
  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      state);

  // 4. The entry handle holds refs on both NHGs, so the SAI NHG objects
  // survive even though no route references them.
  auto* aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto* entryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("PbrEntry"));
  ASSERT_NE(entryHandle, nullptr);
  ASSERT_NE(entryHandle->matchNhgHandle, nullptr);
  ASSERT_NE(entryHandle->redirectNhgHandle, nullptr);

  // 5. The SAI entry's match-NHG field and redirect action point at exactly
  // those held NHG OIDs.
  auto matchNhgGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::FieldRouteDestination());
  EXPECT_EQ(
      matchNhgGot.getDataAndMask().first,
      entryHandle->matchNhgHandle->nextHopGroup->adapterKey());
  auto redirectGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::ActionRedirect());
  EXPECT_EQ(
      redirectGot.getData(),
      entryHandle->redirectNhgHandle->nextHopGroup->adapterKey());

  // 6. Removing the entry drops its NHG refs (the SAI ACL entry destructs
  // before the NHG handles it held).
  saiManagerTable->aclTableManager().removeAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      state);
  EXPECT_EQ(
      saiManagerTable->aclTableManager().getAclEntryHandle(
          aclTableHandle, kPriority(), std::string("PbrEntry")),
      nullptr);
}

TEST_F(AclTableManagerPbrTest, addPbrAclEntryMatchesOnTrafficClass) {
  // The fake rejects entry fields not declared as table qualifiers, so program
  // a table that declares TC. enable_acl_table_group lets the table's own
  // qualifier list take effect.
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  const auto kTcTable = std::string("PbrTcTable");
  auto table = std::make_shared<AclTable>(0, kTcTable);
  table->setQualifiers({cfg::AclTableQualifier::TC});
  saiManagerTable->aclTableManager().addAclTable(
      table, cfg::AclStage::INGRESS, nullptr /*state*/);

  // A PBR entry matching on traffic class programs FieldTc with a full-value
  // (0xFF) mask.
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrTcEntry"));
  aclEntry->setTrafficClass(3);
  aclEntry->setActionType(kActionType());

  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry, kTcTable, nullptr /*state*/);

  auto fieldTcGot = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::FieldTc());
  EXPECT_EQ(fieldTcGot.getDataAndMask().first, 3);
  EXPECT_EQ(fieldTcGot.getDataAndMask().second, 0xFF);
}

TEST_F(AclTableManagerPbrTest, pbrMatchNhgUnknownIdThrows) {
  // A match-on-NHG id with no FibInfo mapping must fail loudly, not silently
  // drop the match.
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrBadMatch"));
  aclEntry->setNextHopGroupId(kUnallocatedNhgId);
  aclEntry->setActionType(kActionType());
  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclEntry(
          aclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          getProgrammedState()),
      FbossError);
}

TEST_F(AclTableManagerPbrTest, pbrRedirectNhgUnknownIdThrows) {
  // A redirect to a NextHopSetID with no FibInfo mapping must fail loudly.
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrBadRedirect"));
  MatchAction action;
  action.setRedirectNextHopGroupId(kUnallocatedNhgId);
  aclEntry->setAclAction(action);
  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclEntry(
          aclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          getProgrammedState()),
      FbossError);
}

TEST_F(AclTableManagerPbrTest, pbrRedirectNhgEmptyProgramsDrop) {
  // 1. A named NHG whose members all failed to resolve carries an id that maps
  // to the empty set. Redirecting at it cannot forward, so the entry is
  // programmed to drop instead of being rejected.
  nextHopIDManager_->allocateNamedNextHopGroup("emptyNhg", RouteNextHopSet{});
  auto emptyNhgIdOpt = nextHopIDManager_->getNextHopSetIDForName("emptyNhg");
  ASSERT_TRUE(emptyNhgIdOpt.has_value());

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrEmptyRedirect"));
  MatchAction action;
  action.setRedirectNextHopGroupId(static_cast<int64_t>(*emptyNhgIdOpt));
  aclEntry->setAclAction(action);

  // 2. Programming succeeds and holds no redirect NHG.
  AclEntrySaiId aclEntryId = saiManagerTable->aclTableManager().addAclEntry(
      aclEntry,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
      getProgrammedState());
  auto* aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  auto* entryHandle = saiManagerTable->aclTableManager().getAclEntryHandle(
      aclTableHandle, kPriority(), std::string("PbrEmptyRedirect"));
  ASSERT_NE(entryHandle, nullptr);
  EXPECT_EQ(entryHandle->redirectNhgHandle, nullptr);

  // 3. The SAI entry drops rather than forwarding to a group it cannot use.
  auto packetAction = saiApiTable->aclApi().getAttribute(
      aclEntryId, SaiAclEntryTraits::Attributes::ActionPacketAction());
  EXPECT_EQ(packetAction.getData(), SAI_PACKET_ACTION_DROP);
}

TEST_F(AclTableManagerPbrTest, pbrMatchNhgEmptySkipsEntry) {
  nextHopIDManager_->allocateNamedNextHopGroup("emptyNhg", RouteNextHopSet{});
  auto emptyNhgIdOpt = nextHopIDManager_->getNextHopSetIDForName("emptyNhg");
  ASSERT_TRUE(emptyNhgIdOpt.has_value());

  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrEmptyMatch"));
  aclEntry->setNextHopGroupId(static_cast<int64_t>(*emptyNhgIdOpt));
  aclEntry->setActionType(kActionType());

  EXPECT_EQ(
      saiManagerTable->aclTableManager().addAclEntry(
          aclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          getProgrammedState()),
      AclEntrySaiId{0});
  auto* aclTableHandle = saiManagerTable->aclTableManager().getAclTableHandle(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  EXPECT_EQ(
      saiManagerTable->aclTableManager().getAclEntryHandle(
          aclTableHandle, kPriority(), std::string("PbrEmptyMatch")),
      nullptr);
}

TEST_F(AclTableManagerPbrTest, pbrMatchNhgNullStateThrows) {
  // Resolving a PBR NHG requires SwitchState; a null state must throw rather
  // than silently program an entry without its match NHG.
  RouteNextHopSet matchNhops;
  matchNhops.insert(makeNextHop(testInterfaces[0]));
  nextHopIDManager_->allocateNamedNextHopGroup("matchNhg", matchNhops);
  auto matchNhgIdOpt = nextHopIDManager_->getNextHopSetIDForName("matchNhg");
  ASSERT_TRUE(matchNhgIdOpt.has_value());
  auto matchNhgId = static_cast<int64_t>(*matchNhgIdOpt);
  auto aclEntry =
      std::make_shared<AclEntry>(kPriority(), std::string("PbrNullState"));
  aclEntry->setNextHopGroupId(matchNhgId);
  aclEntry->setActionType(kActionType());
  EXPECT_THROW(
      saiManagerTable->aclTableManager().addAclEntry(
          aclEntry,
          cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(),
          nullptr),
      FbossError);
}

TEST_F(
    AclTableManagerPbrTest,
    failedBoundPortAclTableRecreateCleansUpStagedTable) {
  FLAGS_enable_acl_table_group = true;
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };

  auto aclTableGroup = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  aclTableGroup->setName("portBoundGroup");
  aclTableGroup->setBindPoint(cfg::AclTableGroupBindPoint::PORT);
  saiManagerTable->aclTableGroupManager().addAclTableGroup(aclTableGroup);

  const std::string counterName = "stagedAclEntryCounter";
  cfg::TrafficCounter counter;
  counter.name() = counterName;
  counter.types() = {cfg::CounterType::PACKETS};
  MatchAction action;
  action.setTrafficCounter(counter);
  auto counterEntry = std::make_shared<AclEntry>(
      kPriority(), std::string("AValidCounterEntry"));
  counterEntry->setDscp(kDscp());
  counterEntry->setAclAction(action);

  auto unresolvableEntry = std::make_shared<AclEntry>(
      kPriority2(), std::string("ZUnresolvablePbrEntry"));
  unresolvableEntry->setNextHopGroupId(kUnallocatedNhgId);
  unresolvableEntry->setActionType(kActionType());
  auto aclMap = std::make_shared<AclMap>();
  aclMap->addNode(counterEntry);
  aclMap->addNode(unresolvableEntry);

  auto oldTable = std::make_shared<AclTable>(0, kAclTable2);
  oldTable->setQualifiers(
      {cfg::AclTableQualifier::DSCP, cfg::AclTableQualifier::L4_DST_PORT});
  oldTable->setAclMap(aclMap);
  const auto oldAclTableId = saiManagerTable->aclTableManager().addAclTable(
      oldTable,
      cfg::AclStage::INGRESS,
      getProgrammedState(),
      cfg::AclTableGroupBindPoint::PORT);

  auto swPort = makePort(testInterfaces[0].remoteHosts[0].port);
  swPort->setIngressAclTableName(kAclTable2);
  saiManagerTable->portManager().setIngressAcl(swPort);

  auto newTable = oldTable->clone();
  newTable->setQualifiers(
      {cfg::AclTableQualifier::DSCP,
       cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
       cfg::AclTableQualifier::L4_DST_PORT});
  EXPECT_THROW(
      saiManagerTable->aclTableManager().changedAclTable(
          oldTable,
          newTable,
          cfg::AclStage::INGRESS,
          getProgrammedState(),
          cfg::AclTableGroupBindPoint::PORT),
      FbossError);
  EXPECT_FALSE(
      facebook::fb303::fbData->getStatMap()->contains(
          folly::to<std::string>(counterName, ".packets")));

  const auto stagedAclTableName =
      folly::to<std::string>(kAclTable2, "-recreate");
  EXPECT_EQ(
      saiManagerTable->aclTableManager().getAclTableHandle(stagedAclTableName),
      nullptr);
  auto* aclTableGroupHandle =
      saiManagerTable->aclTableGroupManager().getAclTableGroupHandle(
          SAI_ACL_STAGE_INGRESS, cfg::AclTableGroupBindPoint::PORT);
  ASSERT_NE(aclTableGroupHandle, nullptr);
  EXPECT_EQ(
      saiManagerTable->aclTableGroupManager().getAclTableGroupMemberHandle(
          aclTableGroupHandle, stagedAclTableName),
      nullptr);

  auto* portHandle =
      saiManagerTable->portManager().getPortHandle(swPort->getID());
  ASSERT_NE(portHandle, nullptr);
  EXPECT_EQ(
      saiApiTable->portApi().getAttribute(
          portHandle->port->adapterKey(),
          SaiPortTraits::Attributes::IngressAcl{}),
      oldAclTableId);
}
