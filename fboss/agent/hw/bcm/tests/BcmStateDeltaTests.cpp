/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"

#include "fboss/agent/hw/bcm/BcmSwitch.h"

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

namespace {
using namespace facebook::fboss;

void addQosPolicyWithRules(cfg::SwitchConfig& cfg, const std::string& name) {
  cfg::QosPolicy qosPolicy;
  *qosPolicy.name() = name;
  qosPolicy.rules()->resize(8);
  for (auto i = 0; i < 8; i++) {
    *qosPolicy.rules()[i].queueId() = i;
    for (auto j = 0; j < 8; j++) {
      qosPolicy.rules()[i].dscp()->push_back(8 * i + j);
    }
  }
  cfg.qosPolicies()->push_back(qosPolicy);
}

void addQosPolicyWithDscpMap(cfg::SwitchConfig& cfg, const std::string& name) {
  cfg::QosPolicy qosPolicy;
  *qosPolicy.name() = name;
  qosPolicy.qosMap() = cfg::QosMap();
  qosPolicy.qosMap()->dscpMaps()->resize(8);
  for (auto i = 0; i < 8; i++) {
    *qosPolicy.qosMap()->dscpMaps()[i].internalTrafficClass() = i;
    for (auto j = 0; j < 8; j++) {
      qosPolicy.qosMap()->dscpMaps()[i].fromDscpToTrafficClass()->push_back(
          8 * i + j);
    }
  }
  cfg.qosPolicies()->push_back(qosPolicy);
}

void addQosPolicyWithDscpAndExpMap(
    cfg::SwitchConfig& cfg,
    const std::string& name) {
  cfg::QosPolicy qosPolicy;
  *qosPolicy.name() = name;
  qosPolicy.qosMap() = cfg::QosMap();
  qosPolicy.qosMap()->dscpMaps()->resize(8);
  qosPolicy.qosMap()->expMaps()->resize(8);
  for (auto i = 0; i < 8; i++) {
    // dscp map
    *qosPolicy.qosMap()->dscpMaps()[i].internalTrafficClass() = i;
    for (auto j = 0; j < 8; j++) {
      qosPolicy.qosMap()->dscpMaps()[i].fromDscpToTrafficClass()->push_back(
          8 * i + j);
    }
    // exp map
    *qosPolicy.qosMap()->expMaps()[i].internalTrafficClass() = i;
    qosPolicy.qosMap()->expMaps()[i].fromExpToTrafficClass()->push_back(i);
    qosPolicy.qosMap()->expMaps()[i].fromTrafficClassToExp() = i;
  }
  cfg.qosPolicies()->push_back(qosPolicy);
}

void setDataPlaneTrafficPolicy(
    cfg::SwitchConfig& cfg,
    const std::string& name) {
  cfg::TrafficPolicyConfig trafficPolicy;

  trafficPolicy.defaultQosPolicy() = name;
  cfg.dataPlaneTrafficPolicy() = trafficPolicy;
}

void setDataPlaneTrafficPolicyForPort(
    cfg::SwitchConfig& cfg,
    const std::string& name,
    int port) {
  if (auto policy = cfg.dataPlaneTrafficPolicy()) {
    if (auto portIdToQosPolicy = policy->portIdToQosPolicy()) {
      portIdToQosPolicy->emplace(port, name);
    } else {
      policy->portIdToQosPolicy() = {{port, name}};
    }
  }
}

} // namespace
namespace facebook::fboss {

TEST_F(BcmTest, addPortFails) {
  const auto& portMap = getProgrammedState()->getPorts();
  auto highestPortIdPort = *std::max_element(
      portMap->cbegin()->second->cbegin(),
      portMap->cbegin()->second->cend(),
      [=](const auto& lport, const auto& rport) {
        return lport.second->getID() < rport.second->getID();
      });
  auto newState = getProgrammedState()->clone();
  auto newPortMap = newState->getPorts()->modify(&newState);
  state::PortFields portFields;
  portFields.portId() = PortID(highestPortIdPort.second->getID() + 1);
  portFields.portName() = "foo";
  newPortMap->addNode(
      std::make_shared<Port>(std::move(portFields)),
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));
  EXPECT_THROW(applyNewState(newState), FbossError);
}

TEST_F(BcmTest, removePortFails) {
  const auto& portMap = getProgrammedState()->getPorts();
  auto firstPort = *portMap->cbegin()->second->cbegin();
  auto newState = getProgrammedState()->clone();
  auto newPortMap = newState->getPorts()->modify(&newState);
  newPortMap->removeNode(firstPort.second->getID());
  EXPECT_THROW(applyNewState(newState), FbossError);
}

TEST_F(BcmTest, validQosPolicyConfigWithRules) {
  auto cfg =
      utility::onePortPerInterfaceConfig(getHwSwitch(), masterLogicalPortIds());
  auto state0 = applyNewConfig(cfg);
  addQosPolicyWithRules(cfg, "qp");
  setDataPlaneTrafficPolicy(cfg, "qp");
  auto newState = applyNewConfig(cfg);
  EXPECT_TRUE(getHwSwitch()->isValidStateUpdate(StateDelta(state0, newState)));
}

TEST_F(BcmTest, validQosPolicyConfigWithRulesAndDifferentPolicyForPort) {
  auto cfg =
      utility::onePortPerInterfaceConfig(getHwSwitch(), masterLogicalPortIds());
  auto state0 = applyNewConfig(cfg);
  addQosPolicyWithRules(cfg, "qp0");
  setDataPlaneTrafficPolicy(cfg, "qp0");
  addQosPolicyWithRules(cfg, "qp1");
  setDataPlaneTrafficPolicyForPort(cfg, "qp1", *cfg.ports()[0].logicalID());
  auto newState = applyNewConfig(cfg);
  EXPECT_TRUE(getHwSwitch()->isValidStateUpdate(StateDelta(state0, newState)));
}

TEST_F(BcmTest, expAndDscpQosMapDefaultQosPolicy) {
  auto cfg =
      utility::onePortPerInterfaceConfig(getHwSwitch(), masterLogicalPortIds());
  auto state0 = applyNewConfig(cfg);
  addQosPolicyWithDscpAndExpMap(cfg, "qp");
  setDataPlaneTrafficPolicy(cfg, "qp");
  auto newState = applyNewConfig(cfg);
  EXPECT_TRUE(getHwSwitch()->isValidStateUpdate(StateDelta(state0, newState)));
}

TEST_F(BcmTest, dscpQosMapForPort) {
  auto cfg =
      utility::onePortPerInterfaceConfig(getHwSwitch(), masterLogicalPortIds());
  auto state0 = applyNewConfig(cfg);
  addQosPolicyWithDscpAndExpMap(cfg, "qp0");
  setDataPlaneTrafficPolicy(cfg, "qp0");
  addQosPolicyWithDscpMap(cfg, "qp1");
  setDataPlaneTrafficPolicyForPort(cfg, "qp1", *cfg.ports()[0].logicalID());
  auto newState = applyNewConfig(cfg);
  EXPECT_TRUE(getHwSwitch()->isValidStateUpdate(StateDelta(state0, newState)));
}

TEST_F(BcmTest, expQosMapForPort) {
  auto cfg =
      utility::onePortPerInterfaceConfig(getHwSwitch(), masterLogicalPortIds());
  auto state0 = applyNewConfig(cfg);
  addQosPolicyWithDscpMap(cfg, "qp0");
  setDataPlaneTrafficPolicy(cfg, "qp0");
  addQosPolicyWithDscpAndExpMap(cfg, "qp1");
  setDataPlaneTrafficPolicyForPort(cfg, "qp1", *cfg.ports()[0].logicalID());
  auto newState = applyNewConfig(cfg);
  EXPECT_FALSE(getHwSwitch()->isValidStateUpdate(StateDelta(state0, newState)));
}

TEST_F(BcmTest, validInterfaceConfig) {
  if (!getHwSwitch()->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_L3_INTERFACE)) {
    GTEST_SKIP();
  }
  auto cfg = utility::oneL3IntfTwoPortConfig(
      getHwSwitch()->getPlatform()->getPlatformMapping(),
      getHwSwitch()->getPlatform()->getAsic(),
      masterLogicalPortIds()[0],
      masterLogicalPortIds()[1],
      getHwSwitch()->getPlatform()->supportsAddRemovePort());
  auto oldState = applyNewConfig(cfg);
  // move a port to default vlan
  auto portCfg = utility::findCfgPort(cfg, masterLogicalPortIds()[1]);
  portCfg->ingressVlan() = utility::kDefaultVlanId4094;
  cfg.interfaces()->resize(2);
  cfg.interfaces()[1].intfID() = utility::kDefaultVlanId4094;
  cfg.interfaces()[1].vlanID() = utility::kDefaultVlanId4094;
  cfg.interfaces()[1].routerID() = 0;
  cfg.interfaces()[1].ipAddresses()->resize(2);
  cfg.interfaces()[1].ipAddresses()[0] = "2.2.2.2/24";
  cfg.interfaces()[1].ipAddresses()[1] = "2::1/64";
  EXPECT_THROW(applyNewConfig(cfg), FbossError);
}

} // namespace facebook::fboss
