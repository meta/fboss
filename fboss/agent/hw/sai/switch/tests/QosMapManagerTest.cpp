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
#include "fboss/agent/hw/sai/switch/SaiQosMapManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/QosPolicy.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/types.h"

#include <string>

using namespace facebook::fboss;

/*
 * In these tests, we will assume 4 ports with one lane each, with IDs
 */

class QosMapManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT;
    ManagerTestBase::SetUp();
  }
  void validateQosPolicy(
      const SaiQosMapHandle* saiQosMapHandle,
      TestQosPolicy testQosPolicy) {
    EXPECT_TRUE(saiQosMapHandle);
    EXPECT_TRUE(saiQosMapHandle->dscpToTcMap);
    EXPECT_TRUE(saiQosMapHandle->tcToQueueMap);
    auto& qosMapApi = SaiApiTable::getInstance()->qosMapApi();
    auto dscpMapToValueList = qosMapApi.getAttribute(
        saiQosMapHandle->dscpToTcMap->adapterKey(),
        SaiQosMapTraits::Attributes::MapToValueList{});
    auto tcMapToValueList = qosMapApi.getAttribute(
        saiQosMapHandle->tcToQueueMap->adapterKey(),
        SaiQosMapTraits::Attributes::MapToValueList{});
    EXPECT_EQ(dscpMapToValueList.size(), testQosPolicy.size());
    EXPECT_EQ(tcMapToValueList.size(), testQosPolicy.size());
    for (const auto& mapping : testQosPolicy) {
      auto [dscp, tc, q] = mapping;
      bool foundDscp = false;
      bool foundTc = false;
      for (const auto& entry : dscpMapToValueList) {
        if (entry.key.dscp == dscp) {
          EXPECT_EQ(entry.value.tc, tc);
          foundDscp = true;
        }
      }
      for (const auto& entry : tcMapToValueList) {
        if (entry.key.tc == tc) {
          EXPECT_EQ(entry.value.queue_index, q);
          foundTc = true;
        }
      }
      EXPECT_TRUE(foundDscp);
      EXPECT_TRUE(foundTc);
    }
  }
  std::shared_ptr<SwitchState> makeSwitchState(
      std::shared_ptr<QosPolicy> policy,
      std::shared_ptr<SwitchState> in = std::make_shared<SwitchState>()) {
    in->publish();
    auto newState = in->clone();
    auto switchSettings =
        utility::getFirstNodeIf(newState->getSwitchSettings());
    if (switchSettings) {
      auto newSwitchSettings = switchSettings->modify(&newState);
      newSwitchSettings->setDefaultDataPlaneQosPolicy(policy);
    } else {
      auto newSwitchSettings = std::make_shared<SwitchSettings>();
      newSwitchSettings->setDefaultDataPlaneQosPolicy(policy);
      addSwitchSettingsToState(newState, newSwitchSettings);
    }
    return newState;
  }
};

#if defined(SAI_CBFC_SUPPORTED)
TEST_F(QosMapManagerTest, queueToVcMapBuiltAndBound) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  // The two lossless classes SUSWs run today: queue 2 -> vc 2, queue 6 -> vc 6.
  qp->setQueueToVcIdMap({{2, 2}, {6, 6}});
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle->queueToVcMap);

  auto& qosMapApi = SaiApiTable::getInstance()->qosMapApi();
  EXPECT_EQ(
      qosMapApi.getAttribute(
          handle->queueToVcMap->adapterKey(),
          SaiQosMapTraits::Attributes::Type{}),
      SAI_QOS_MAP_TYPE_QUEUE_TO_VC);

  // Keyed on queue_index, not tc -- by egress the traffic class has already
  // been spent selecting the queue.
  auto mapToValueList = qosMapApi.getAttribute(
      handle->queueToVcMap->adapterKey(),
      SaiQosMapTraits::Attributes::MapToValueList{});
  std::map<int, int> got;
  for (const auto& entry : mapToValueList) {
    got[entry.key.queue_index] = entry.value.vc;
  }
  const std::map<int, int> expected{{2, 2}, {6, 6}};
  EXPECT_EQ(got, expected);
}

TEST_F(QosMapManagerTest, noQueueToVcMapWhenUnconfigured) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  ASSERT_TRUE(handle);
  EXPECT_FALSE(handle->queueToVcMap);
}

TEST_F(QosMapManagerTest, vcQosMapsBoundToPort) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  qp->setTrafficClassToVcIdMap({{0, 2}});
  qp->setQueueToVcIdMap({{2, 2}});
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto portId = PortID(1);
  saiManagerTable->portManager().setQosPolicy(portId, "default");

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  auto portSaiId =
      saiManagerTable->portManager().getPortHandle(portId)->port->adapterKey();
  auto& portApi = SaiApiTable::getInstance()->portApi();

  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::QosTcToVcMap{}),
      handle->tcToVcMap->adapterKey());
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::QosQueueToVcMap{}),
      handle->queueToVcMap->adapterKey());
}

TEST_F(QosMapManagerTest, onlyTcToVcMapBoundToManagementPort) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  qp->setTrafficClassToVcIdMap({{0, 2}});
  qp->setQueueToVcIdMap({{2, 2}});
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto oldPort = makePort(TestPort{1});
  auto mgmtPort = oldPort->clone();
  mgmtPort->setPortType(cfg::PortType::MANAGEMENT_PORT);
  saiManagerTable->portManager().changePort(oldPort, mgmtPort);
  auto portId = mgmtPort->getID();
  saiManagerTable->portManager().setQosPolicy(portId, "default");

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  auto portSaiId =
      saiManagerTable->portManager().getPortHandle(portId)->port->adapterKey();
  auto& portApi = SaiApiTable::getInstance()->portApi();

  // TC_TO_VC goes with the shared DSCP_TO_TC map; QUEUE_TO_VC is skipped.
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::QosTcToVcMap{}),
      handle->tcToVcMap->adapterKey());
  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::QosQueueToVcMap{}),
      SAI_NULL_OBJECT_ID);
}

TEST_F(QosMapManagerTest, vcQosMapsReboundOnReapply) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  qp->setTrafficClassToVcIdMap({{0, 2}});
  qp->setQueueToVcIdMap({{2, 2}});
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto portId = PortID(1);
  saiManagerTable->portManager().setQosPolicy(portId, "default");
  // Reapplying must leave the binding intact, which is the rebuild path in
  // getSaiIdsForQosMaps().
  saiManagerTable->portManager().setQosPolicy(portId, "default");

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  auto portSaiId =
      saiManagerTable->portManager().getPortHandle(portId)->port->adapterKey();
  auto& portApi = SaiApiTable::getInstance()->portApi();

  EXPECT_EQ(
      portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::QosQueueToVcMap{}),
      handle->queueToVcMap->adapterKey());
}

TEST_F(QosMapManagerTest, queueToVcMapIsSeparateFromTcToVcMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  qp->setTrafficClassToVcIdMap({{0, 2}});
  qp->setQueueToVcIdMap({{2, 2}});
  saiManagerTable->qosMapManager().addQosMap(qp, true);

  auto* handle = saiManagerTable->qosMapManager().getQosMap();
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle->tcToVcMap);
  ASSERT_TRUE(handle->queueToVcMap);
  // Receive-side and send-side classification are distinct objects even when
  // they resolve to the same VC.
  EXPECT_NE(
      handle->tcToVcMap->adapterKey(), handle->queueToVcMap->adapterKey());
}
#endif

TEST_F(QosMapManagerTest, addQosMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  saiManagerTable->qosMapManager().addQosMap(qp, true);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  validateQosPolicy(saiQosMapHandle, testQosPolicy);
}

TEST_F(QosMapManagerTest, addQosMapDelta) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto newState = makeSwitchState(makeQosPolicy("default", testQosPolicy));
  applyNewState(newState);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  validateQosPolicy(saiQosMapHandle, testQosPolicy);
}

TEST_F(QosMapManagerTest, removeQosMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  saiManagerTable->qosMapManager().addQosMap(qp, true);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);
  saiManagerTable->qosMapManager().removeQosMap(qp, true);
  saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_FALSE(saiQosMapHandle);
}

TEST_F(QosMapManagerTest, removeQosMapDelta) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto newState = makeSwitchState(makeQosPolicy("default", testQosPolicy));
  applyNewState(newState);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  validateQosPolicy(saiQosMapHandle, testQosPolicy);
  auto newerState = makeSwitchState(nullptr, newState);
  applyNewState(newerState);
  saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_FALSE(saiQosMapHandle);
}

TEST_F(QosMapManagerTest, changeQosMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  saiManagerTable->qosMapManager().addQosMap(qp, true);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);

  TestQosPolicy testQosPolicy2{{11, 2, 4}, {43, 0, 4}, {1, 1, 1}};
  auto qp2 = makeQosPolicy("default", testQosPolicy2);
  saiManagerTable->qosMapManager().changeQosMap(qp, qp2, true);
  saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);
  validateQosPolicy(saiQosMapHandle, testQosPolicy2);
}

TEST_F(QosMapManagerTest, changeQosMapDelta) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto newState = makeSwitchState(makeQosPolicy("default", testQosPolicy));
  applyNewState(newState);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  validateQosPolicy(saiQosMapHandle, testQosPolicy);

  TestQosPolicy testQosPolicy2{{11, 2, 4}, {43, 0, 4}, {1, 1, 1}};
  auto newerState =
      makeSwitchState(makeQosPolicy("default", testQosPolicy2), newState);
  applyNewState(newerState);
  saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);
  validateQosPolicy(saiQosMapHandle, testQosPolicy2);
}

TEST_F(QosMapManagerTest, validQosPolicy) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qosPolicy = makeQosPolicy("default", testQosPolicy);
  auto newState = makeSwitchState(qosPolicy);
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), newState)));
}

TEST_F(QosMapManagerTest, missingTCToQ) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qosPolicy = makeQosPolicy("default", testQosPolicy);
  qosPolicy->setTrafficClassToQueueIdMap(std::map<int16_t, int16_t>{});
  auto newState = makeSwitchState(qosPolicy);
  EXPECT_FALSE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), newState)));
}

TEST_F(QosMapManagerTest, missingDscpMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qosPolicy = makeQosPolicy("default", testQosPolicy);
  qosPolicy->setDscpMap(DscpMap{});
  auto newState = makeSwitchState(qosPolicy);
  EXPECT_FALSE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), newState)));
}

TEST_F(QosMapManagerTest, expMap) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qosPolicy = makeQosPolicy("default", testQosPolicy);
  ExpMap exp;
  exp.addFromEntry(TrafficClass{1}, EXP{2});
  exp.addToEntry(TrafficClass{2}, EXP{1});
  qosPolicy->setExpMap(exp);
  auto newState = makeSwitchState(qosPolicy);
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), newState)));
}

TEST_F(QosMapManagerTest, addPortQos) {
  auto switchState = std::make_shared<SwitchState>();
  auto qosPolicies = std::make_shared<MultiSwitchQosPolicyMap>();
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qosPolicy = makeQosPolicy("qos", testQosPolicy);
  qosPolicies->addNode(qosPolicy, scopeResolver().scope(qosPolicy));
  switchState->resetQosPolicies(qosPolicies);
  state::PortFields portFields;
  portFields.portId() = PortID(1);
  portFields.portName() = "eth1/1/1";
  auto port = std::make_shared<Port>(std::move(portFields));
  port->setQosPolicy("qos");
  switchState->getPorts()->addNode(port, scopeResolver().scope(port));
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), switchState)));
}

// By default the PFC priority -> priority group map is not programmed into
// SAI even when configured, since the map type is unsupported on some
// platforms. Programming is opt-in via a feature flag.
TEST_F(QosMapManagerTest, pfcPriorityToPgQosMapNotProgrammedByDefault) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  qp->setPfcPriorityToPgIdMap({{0, 5}, {7, 3}});

  saiManagerTable->qosMapManager().addQosMap(qp, true);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);
  EXPECT_FALSE(saiQosMapHandle->pfcPriorityToPgMap);
}

// With the feature flag on, the PFC priority -> priority group map is
// programmed into SAI with the configured entries.
TEST_F(QosMapManagerTest, pfcPriorityToPgQosMapProgrammedWhenEnabled) {
  gflags::FlagSaver flagSaver;
  FLAGS_enable_pfc_priority_to_pg_map = true;
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  const std::map<int16_t, int16_t> pfcPriorityToPg{{0, 5}, {7, 3}};
  qp->setPfcPriorityToPgIdMap(pfcPriorityToPg);

  saiManagerTable->qosMapManager().addQosMap(qp, true);
  auto saiQosMapHandle = saiManagerTable->qosMapManager().getQosMap();
  EXPECT_TRUE(saiQosMapHandle);
  EXPECT_TRUE(saiQosMapHandle->pfcPriorityToPgMap);

  auto& qosMapApi = SaiApiTable::getInstance()->qosMapApi();
  EXPECT_EQ(
      qosMapApi.getAttribute(
          saiQosMapHandle->pfcPriorityToPgMap->adapterKey(),
          SaiQosMapTraits::Attributes::Type{}),
      SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_PRIORITY_GROUP);
  auto mapToValueList = qosMapApi.getAttribute(
      saiQosMapHandle->pfcPriorityToPgMap->adapterKey(),
      SaiQosMapTraits::Attributes::MapToValueList{});
  std::map<int16_t, int16_t> gotPfcPriorityToPg;
  for (const auto& entry : mapToValueList) {
    gotPfcPriorityToPg.emplace(entry.key.prio, entry.value.pg);
  }
  EXPECT_EQ(gotPfcPriorityToPg, pfcPriorityToPg);
}

TEST_F(QosMapManagerTest, getPfcPriorityToQueueId) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  const std::map<int16_t, int16_t> pfcPriorityToQueue{{3, 7}, {4, 2}};
  qp->setPfcPriorityToQueueIdMap(pfcPriorityToQueue);

  saiManagerTable->qosMapManager().addQosMap(qp, true);
  EXPECT_EQ(
      saiManagerTable->qosMapManager().getPfcPriorityToQueueId(),
      pfcPriorityToQueue);
}

TEST_F(QosMapManagerTest, getPfcPriorityToQueueIdEmptyWhenAbsent) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  // No pfcPriorityToQueueId on the policy => empty.
  saiManagerTable->qosMapManager().addQosMap(qp, true);
  EXPECT_TRUE(
      saiManagerTable->qosMapManager().getPfcPriorityToQueueId().empty());
}

TEST_F(QosMapManagerTest, changAddsPortQos) {
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto oldState = makeSwitchState(makeQosPolicy("qos", testQosPolicy));
  state::PortFields portFields;
  portFields.portId() = PortID(1);
  portFields.portName() = "eth1/1/1";
  auto port = std::make_shared<Port>(std::move(portFields));
  port->setQosPolicy("qos");
  auto ports = std::make_shared<MultiSwitchPortMap>();
  ports->addNode(port, scopeResolver().scope(port));
  oldState->resetPorts(ports);
  oldState->publish();
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(std::make_shared<SwitchState>(), oldState)));
  auto newState = oldState->clone();
  auto newPort = port->modify(&newState);
  auto qosPolicies = std::make_shared<MultiSwitchQosPolicyMap>();
  auto qosPolicy = makeQosPolicy("qos", testQosPolicy);
  qosPolicies->addNode(qosPolicy, scopeResolver().scope(qosPolicy));
  newState->resetQosPolicies(qosPolicies);
  auto switchSettings = utility::getFirstNodeIf(newState->getSwitchSettings());
  auto newSwitchSettings = switchSettings->modify(&newState);
  newSwitchSettings->setDefaultDataPlaneQosPolicy(nullptr);
  newPort->setQosPolicy("qos");
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(
      StateDelta(oldState, newState)));
}
