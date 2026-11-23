/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/tests/BcmTest.h"

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmAclEntry.h"
#include "fboss/agent/hw/bcm/BcmAclTable.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

using namespace facebook::fboss;
using namespace facebook::fboss::utility;

namespace {

void checkCoppAclMatch(
    std::shared_ptr<SwitchState> state,
    BcmSwitch* hw,
    int unit) {
  auto& swAcls = state->getAcls();
  int coppAclsCount = fpGroupNumAclEntries(
      unit, hw->getPlatform()->getAsic()->getDefaultACLGroupID());
  ASSERT_EQ(swAcls->numNodes(), coppAclsCount);
  // check all coop acls are sync between h/w and s/w
  for (auto& mIter : std::as_const(*swAcls)) {
    for (auto& iter : std::as_const(*mIter.second)) {
      const auto& swAcl = iter.second;
      auto hwAcl = hw->getAclTable()->getAclIf(swAcl->getPriority());
      ASSERT_NE(nullptr, hwAcl);
      ASSERT_TRUE(
          BcmAclEntry::isStateSame(
              hw,
              hw->getPlatform()->getAsic()->getDefaultACLGroupID(),
              hwAcl->getHandle(),
              swAcl));
    }
  }
}

void addInterface(cfg::SwitchConfig* config) {
  // add one interface, ipAddress
  config->interfaces()->resize(2);
  *config->interfaces()[1].intfID() = 2;
  config->vlans()->resize(3);
  *config->vlans()[2].id() = 2;
  *config->interfaces()[1].vlanID() = 2;
  config->interfaces()[1].routerID() = 0;
  config->interfaces()[1].mac() = "00:01:00:11:22:33";
  config->interfaces()[1].ipAddresses()->resize(1);
  config->interfaces()[1].ipAddresses()[0] = "::11:11:11/120";
}

void delInterface(cfg::SwitchConfig* config) {
  // remove the newly added interface and new ip of intf1
  config->interfaces()->pop_back();
  config->vlans()->pop_back();
}

void addIpAddress(cfg::SwitchConfig* config, int idx, const std::string& ip) {
  config->interfaces()[idx].ipAddresses()->push_back(ip);
}

} // namespace

namespace facebook::fboss {

class BcmAclCoppTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg = utility::oneL3IntfNoIPAddrConfig(
        getHwSwitch(), masterLogicalPortIds()[0]);
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg,
        getHwSwitchEnsemble()->getL3Asics(),
        getHwSwitchEnsemble()->isSai());
    return cfg;
  }
};

TEST_F(BcmAclCoppTest, CoppAcl) {
  auto setup = [this]() { applyNewConfig(initialConfig()); };
  auto verify = [this]() {
    checkCoppAclMatch(getProgrammedState(), getHwSwitch(), getUnit());
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmAclCoppTest, CoppAclAddDelInterface) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    addInterface(&newCfg);
    applyNewConfig(newCfg);
    delInterface(&newCfg);
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    // With DstClassL3 implementation, adding or deleting interface won't
    // affect copp acls amount
    checkCoppAclMatch(getProgrammedState(), getHwSwitch(), getUnit());
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmAclCoppTest, CoppAclAddInterfaceAddr) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    addIpAddress(&newCfg, 0, "10.1.1.1/24");
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    // With DstClassL3 implementation, adding or deleting local ip won't
    // affect copp acls amount
    checkCoppAclMatch(getProgrammedState(), getHwSwitch(), getUnit());
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmAclCoppTest, CoppAclAddInterface) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    addInterface(&newCfg);
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    // With DstClassL3 implementation, adding or deleting local ip won't
    // affect copp acls amount
    checkCoppAclMatch(getProgrammedState(), getHwSwitch(), getUnit());
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
