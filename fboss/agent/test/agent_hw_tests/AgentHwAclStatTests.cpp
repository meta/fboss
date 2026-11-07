// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentHwTest.h"

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"

namespace facebook::fboss {

class AgentHwAclStatTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if (!FLAGS_enable_acl_table_group) {
      return {
          ProductionFeature::SINGLE_ACL_TABLE,
          ProductionFeature::L3_FORWARDING};
    }
    return {
        ProductionFeature::MULTI_ACL_TABLE, ProductionFeature::L3_FORWARDING};
  }

  cfg::AclEntry* addDscpAcl(
      cfg::SwitchConfig* cfg,
      const std::string& aclName,
      SwitchID switchID = SwitchID(0)) {
    cfg::AclEntry acl{};
    acl.name() = aclName;
    acl.actionType() = cfg::AclActionType::PERMIT;
    // ACL requires at least one qualifier
    acl.dscp() = 0x24;
    utility::addEtherTypeToAcl(
        hwAsicForSwitch(switchID), &acl, cfg::EtherType::IPv6);

    return utility::addAcl(cfg, acl, cfg::AclStage::INGRESS);
  }
};

class AgentHwAclStatCounterTypeTest : public AgentHwAclStatTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentHwAclStatTest::getProductionFeaturesVerified();
    features.push_back(
        ProductionFeature::SEPARATE_BYTE_AND_PACKET_ACL_COUNTERS);
    return features;
  }
};

TEST_F(AgentHwAclStatTest, AclStatCreate) {
  auto setup = [=, this]() {
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"},
        {"stat0"},
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwAclStatTest, AclStatCreateDeleteCreate) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    this->applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));
    auto newCfg = initialConfig(ensemble);
    applyNewConfig(newCfg);

    EXPECT_FALSE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"},
        {"stat0"},
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));

    auto newCfg2 = initialConfig(ensemble);
    addDscpAcl(&newCfg2, "acl0");
    utility::addAclStat(
        &newCfg2,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg2);
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"},
        {"stat0"},
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwAclStatTest, AclStatCreateShared) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"},
        {"stat"},
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwAclStatTest, AclStatCreateMultiple) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"},
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl1"},
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics())));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwAclStatCounterTypeTest, AclStatChangeCounterType) {
  if (!isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
    GTEST_SKIP();
  }

  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(&newCfg, "acl0", "stat0", {cfg::CounterType::PACKETS});
    this->applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, {cfg::CounterType::PACKETS}));
  };

  auto setupPostWB = [=, this]() {
    if (!isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
      return;
    }
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(&newCfg, "acl0", "stat0", {cfg::CounterType::BYTES});
    this->applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    if (!isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
      return;
    }
    auto& ensemble = *getAgentEnsemble();
    auto client = ensemble.getHwAgentTestClient(SwitchID(0));

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, {cfg::CounterType::BYTES}));
  };
  verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatMultipleActions) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    /* The ACL will have 2 actions: a counter and a queue */
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    const auto& asic = getAsic(switchId);
    cfg::MatchAction matchAction =
        utility::getToQueueAction(&asic, 0, ensemble.isSai());
    cfg::MatchToAction action = cfg::MatchToAction();
    *action.matcher() = "acl0";
    *action.action() = matchAction;
    newCfg.dataPlaneTrafficPolicy()->matchToAction()->push_back(action);
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwAclStatTest, AclStatDelete) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 0);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 0);
    EXPECT_EQ(*statCountInfo.counterCount(), 0);

    EXPECT_FALSE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatCreatePostWarmBoot) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    applyNewConfig(newCfg);
  };

  auto verify = [=]() {};

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatDeleteSharedPostWarmBoot) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"}, {"stat"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 0);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 0);
    EXPECT_EQ(*statCountInfo.counterCount(), 0);

    EXPECT_FALSE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"}, {"stat"}, counterTypes));
    EXPECT_TRUE(client->sync_isAclStatDeleted("stat"));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatCreateSharedPostWarmBoot) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    applyNewConfig(newCfg);
  };

  auto verify = [=]() {};

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"}, {"stat"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatDeleteShared) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"}, {"stat"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl1"}, {"stat"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatRename) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat1"}, counterTypes));
    EXPECT_TRUE(client->sync_isAclStatDeleted("stat0"));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatRenameShared) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0", "acl1"}, {"stat0"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verifyPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 2);
    EXPECT_EQ(*statCountInfo.counterCount(), 2 * (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl1"}, {"stat1"}, counterTypes));
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
}

TEST_F(AgentHwAclStatTest, AclStatShuffle) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    addDscpAcl(&newCfg, "acl1");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 2);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 2);
    EXPECT_EQ(*statCountInfo.counterCount(), 2 * (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl1"}, {"stat1"}, counterTypes));
  };

  auto setupPostWB = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl1");
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl1",
        "stat1",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verify);
}

TEST_F(AgentHwAclStatTest, StatNumberOfCounters) {
  auto setup = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto newCfg = initialConfig(ensemble);
    addDscpAcl(&newCfg, "acl0");
    utility::addAclStat(
        &newCfg,
        "acl0",
        "stat0",
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics()));
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    auto& ensemble = *getAgentEnsemble();
    auto switchId = scopeResolver().scope(masterLogicalPortIds()[0]).switchId();
    auto client = ensemble.getHwAgentTestClient(switchId);
    auto counterTypes =
        utility::getAclCounterTypes(ensemble.getHwAsicTable()->getL3Asics());

    utility::AclStatCountInfo statCountInfo;
    client->sync_getDefaultAclTableStatCountInfo(statCountInfo);
    EXPECT_EQ(*statCountInfo.aclEntryCount(), 1);
    EXPECT_EQ(*statCountInfo.aclStatCount(), 1);
    EXPECT_EQ(*statCountInfo.counterCount(), (int)counterTypes.size());

    EXPECT_TRUE(client->sync_isStatProgrammedInDefaultAclTable(
        {"acl0"}, {"stat0"}, counterTypes));
  };

  auto setupPostWB = [=]() {};

  // checkAclStatSize is a no-op in SAI, so verifyPostWB reuses verify
  this->verifyAcrossWarmBoots(setup, verify, setupPostWB, verify);
}

} // namespace facebook::fboss
