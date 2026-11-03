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
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/QosPolicy.h"
#include "fboss/agent/state/QosPolicyMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;
using std::make_shared;
using std::shared_ptr;

namespace {

std::vector<cfg::QosRule> dscpRules(
    std::vector<std::pair<uint8_t, std::vector<int16_t>>> rules) {
  std::vector<cfg::QosRule> dscpRules;
  for (const auto& pair : rules) {
    cfg::QosRule r;
    *r.queueId() = pair.first;
    *r.dscp() = pair.second;
    dscpRules.push_back(r);
  }
  return dscpRules;
}

void checkQosPolicyQosRules(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  ASSERT_EQ(swQosPolicy->getName(), *cfgQosPolicy.name());
  auto& swStateFromTcToDscp =
      swQosPolicy->getDscpMap()->cref<switch_state_tags::from>();
  DscpMap cfgDscpMap;
  for (const auto& rule : *cfgQosPolicy.rules()) {
    for (auto dscp : *rule.dscp()) {
      cfgDscpMap.addFromEntry(
          static_cast<TrafficClass>(*rule.queueId()), static_cast<DSCP>(dscp));
    }
  }
  ASSERT_EQ(swStateFromTcToDscp->toThrift(), cfgDscpMap.from());
}

void validateTrafficClass(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  std::set<std::pair<uint16_t, uint16_t>> cfgQueueMapEntries;
  std::set<std::pair<uint16_t, uint16_t>> swStateQueueMapEntries;
  for (const auto& entry : *qosMap.trafficClassToQueueId()) {
    cfgQueueMapEntries.emplace(entry.first, entry.second);
  }
  for (auto entry : std::as_const(*swQosPolicy->getTrafficClassToQueueId())) {
    swStateQueueMapEntries.emplace(entry.first, entry.second->cref());
  }
  EXPECT_EQ(cfgQueueMapEntries, swStateQueueMapEntries);
}

void validatePfcPriToQueue(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  std::set<std::pair<uint16_t, uint16_t>> cfgStatePfcPriority2Queue;
  std::set<std::pair<uint16_t, uint16_t>> swStatePfcPriority2Queue;

  if (qosMap.pfcPriorityToQueueId()) {
    for (const auto& entry : *qosMap.pfcPriorityToQueueId()) {
      cfgStatePfcPriority2Queue.emplace(entry.first, entry.second);
    }
  }
  if (auto pfcPriorityToQueueId = swQosPolicy->getPfcPriorityToQueueId()) {
    for (auto entry : std::as_const(*pfcPriorityToQueueId)) {
      swStatePfcPriority2Queue.emplace(entry.first, entry.second->cref());
    }
  }
  EXPECT_EQ(cfgStatePfcPriority2Queue, swStatePfcPriority2Queue);
}

void validateTrafficClassToPgId(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  std::set<std::pair<uint16_t, uint16_t>> cfgStateTc2PgId;
  std::set<std::pair<uint16_t, uint16_t>> swStateTc2PgId;

  if (qosMap.trafficClassToPgId()) {
    for (auto entry : *qosMap.trafficClassToPgId()) {
      cfgStateTc2PgId.emplace(entry.first, entry.second);
    }
  }

  if (auto trafficClassToPgId = swQosPolicy->getTrafficClassToPgId()) {
    for (auto entry : std::as_const(*trafficClassToPgId)) {
      swStateTc2PgId.emplace(entry.first, entry.second->cref());
    }
  }
  EXPECT_EQ(cfgStateTc2PgId, swStateTc2PgId);
}

void validateTrafficClassToVcId(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  // trafficClassToVcId is map<i16, i16>; keep the comparison signed so a
  // negative value does not silently wrap on insertion.
  std::set<std::pair<int16_t, int16_t>> cfgStateTc2VcId;
  std::set<std::pair<int16_t, int16_t>> swStateTc2VcId;

  if (qosMap.trafficClassToVcId()) {
    for (auto entry : *qosMap.trafficClassToVcId()) {
      cfgStateTc2VcId.emplace(entry.first, entry.second);
    }
  }

  if (auto trafficClassToVcId = swQosPolicy->getTrafficClassToVcId()) {
    for (auto entry : std::as_const(*trafficClassToVcId)) {
      swStateTc2VcId.emplace(entry.first, entry.second->cref());
    }
  }
  EXPECT_EQ(cfgStateTc2VcId, swStateTc2VcId);
}

void validateQueueToVcId(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  // queueToVcId is map<i16, i16>; keep the comparison signed so a negative
  // value does not silently wrap on insertion.
  std::set<std::pair<int16_t, int16_t>> cfgStateQueue2VcId;
  std::set<std::pair<int16_t, int16_t>> swStateQueue2VcId;

  if (qosMap.queueToVcId()) {
    for (auto entry : *qosMap.queueToVcId()) {
      cfgStateQueue2VcId.emplace(entry.first, entry.second);
    }
  }

  if (auto queueToVcId = swQosPolicy->getQueueToVcId()) {
    for (auto entry : std::as_const(*queueToVcId)) {
      swStateQueue2VcId.emplace(entry.first, entry.second->cref());
    }
  }
  EXPECT_EQ(cfgStateQueue2VcId, swStateQueue2VcId);
}

void validatePfcPriToPgId(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  std::set<std::pair<uint16_t, uint16_t>> cfgStatePfcPri2PgId;
  std::set<std::pair<uint16_t, uint16_t>> swStatePfcPri2PgId;

  if (qosMap.pfcPriorityToPgId()) {
    for (const auto& entry : *qosMap.pfcPriorityToPgId()) {
      cfgStatePfcPri2PgId.emplace(entry.first, entry.second);
    }
  }

  if (auto pfcPriorityToPgId = swQosPolicy->getPfcPriorityToPgId()) {
    for (auto entry : std::as_const(*pfcPriorityToPgId)) {
      swStatePfcPri2PgId.emplace(entry.first, entry.second->cref());
    }
  }
  EXPECT_EQ(swStatePfcPri2PgId, cfgStatePfcPri2PgId);
}

void validatePcpMap(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  if (!cfgQosPolicy.qosMap()) {
    auto swPcpMap = swQosPolicy->getPcpMap();
    EXPECT_FALSE(swPcpMap.has_value());
    return;
  }

  const auto& qosMap = *cfgQosPolicy.qosMap();

  if (qosMap.pcpMaps()) {
    // Config has PCP maps, switch state should have them too
    auto swPcpMap = swQosPolicy->getPcpMap();
    ASSERT_TRUE(swPcpMap.has_value());

    PcpMap cfgPcpMap(*qosMap.pcpMaps());
    EXPECT_EQ(swPcpMap->toThrift(), cfgPcpMap.toThrift());
  } else {
    // Config has no PCP maps, switch state should not have them either
    auto swPcpMap = swQosPolicy->getPcpMap();
    EXPECT_FALSE(swPcpMap.has_value());
  }
}

void checkQosPolicy(
    const cfg::QosPolicy& cfgQosPolicy,
    std::shared_ptr<QosPolicy> swQosPolicy) {
  ASSERT_EQ(swQosPolicy->getName(), *cfgQosPolicy.name());
  if (!cfgQosPolicy.rules()->empty()) {
    return checkQosPolicyQosRules(cfgQosPolicy, swQosPolicy);
  }
  // use either rules or qosMap
  const auto& qosMap = cfgQosPolicy.qosMap().value_or({});
  ASSERT_EQ(
      swQosPolicy->getDscpMap()->toThrift(),
      DscpMap(*qosMap.dscpMaps()).toThrift());
  ASSERT_EQ(
      swQosPolicy->getExpMap()->toThrift(),
      ExpMap(*qosMap.expMaps()).toThrift());

  validateTrafficClass(cfgQosPolicy, swQosPolicy);
  validatePfcPriToQueue(cfgQosPolicy, swQosPolicy);
  validateTrafficClassToPgId(cfgQosPolicy, swQosPolicy);
  validateTrafficClassToVcId(cfgQosPolicy, swQosPolicy);
  validateQueueToVcId(cfgQosPolicy, swQosPolicy);
  validatePfcPriToPgId(cfgQosPolicy, swQosPolicy);
  validatePcpMap(cfgQosPolicy, swQosPolicy);
}

void checkQosSwState(
    cfg::SwitchConfig& config,
    std::shared_ptr<SwitchState> state) {
  auto cfgQosPolicies = *config.qosPolicies();
  EXPECT_NE(nullptr, state);
  ASSERT_EQ(cfgQosPolicies.size(), state->getQosPolicies()->numNodes());

  for (const auto& cfgQosPolicy : cfgQosPolicies) {
    auto swQosPolicy = state->getQosPolicies()->getNodeIf(*cfgQosPolicy.name());
    checkQosPolicy(cfgQosPolicy, swQosPolicy);
  }
}

void checkDelta(
    const shared_ptr<MultiSwitchQosPolicyMap>& oldQosPolicies,
    const shared_ptr<MultiSwitchQosPolicyMap>& newQosPolicies,
    const std::set<std::string>& changedIDs,
    const std::set<std::string>& addedIDs,
    const std::set<std::string>& removedIDs) {
  auto oldState = make_shared<SwitchState>();
  oldState->resetQosPolicies(oldQosPolicies);
  auto newState = make_shared<SwitchState>();
  newState->resetQosPolicies(newQosPolicies);

  std::set<std::string> foundChanged;
  std::set<std::string> foundAdded;
  std::set<std::string> foundRemoved;
  StateDelta delta(oldState, newState);

  DeltaFunctions::forEachChanged(
      delta.getQosPoliciesDelta(),
      [&](const shared_ptr<QosPolicy>& oldQosPolicy,
          const shared_ptr<QosPolicy>& newQosPolicy) {
        EXPECT_EQ(oldQosPolicy->getID(), newQosPolicy->getID());
        EXPECT_NE(oldQosPolicy, newQosPolicy);

        auto ret = foundChanged.insert(oldQosPolicy->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<QosPolicy>& qosPolicy) {
        auto ret = foundAdded.insert(qosPolicy->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<QosPolicy>& qosPolicy) {
        auto ret = foundRemoved.insert(qosPolicy->getID());
        EXPECT_TRUE(ret.second);
      });

  EXPECT_EQ(changedIDs, foundChanged);
  EXPECT_EQ(addedIDs, foundAdded);
  EXPECT_EQ(removedIDs, foundRemoved);
}

cfg::QosMap cfgQosMap() {
  cfg::QosMap qosMap;
  qosMap.dscpMaps()->resize(8);
  qosMap.expMaps()->resize(8);

  std::vector<cfg::PcpQosMap> pcpMaps(8);
  qosMap.pcpMaps() = pcpMaps;

  std::map<int16_t, int16_t> pfc2Queue;
  std::map<int16_t, int16_t> tc2PgId;
  std::map<int16_t, int16_t> pfc2PgId;

  for (auto i = 0; i < 8; i++) {
    auto& dscpMap = qosMap.dscpMaps()[i];
    auto& expMap = qosMap.expMaps()[i];
    auto& pcpMap = (*qosMap.pcpMaps())[i];
    *dscpMap.internalTrafficClass() = i;
    *expMap.internalTrafficClass() = i;
    *pcpMap.internalTrafficClass() = i;
    for (auto j = 0; j < 4; j++) {
      dscpMap.fromDscpToTrafficClass()->push_back(j);
    }
    expMap.fromExpToTrafficClass()->push_back(i);
    pcpMap.fromPcpToTrafficClass()->push_back(i);
    dscpMap.fromTrafficClassToDscp() = *dscpMap.internalTrafficClass();
    expMap.fromTrafficClassToExp() = *expMap.internalTrafficClass();
    pcpMap.fromTrafficClassToPcp() = *pcpMap.internalTrafficClass();
    qosMap.trafficClassToQueueId()->emplace(i, i);
    pfc2Queue.emplace(i, i);
    tc2PgId.emplace(i, i);
  }
  qosMap.pfcPriorityToQueueId() = pfc2Queue;
  qosMap.trafficClassToPgId() = tc2PgId;
  qosMap.pfcPriorityToPgId() = pfc2PgId;
  return qosMap;
}

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

} // namespace

TEST(QosPolicy, AddSinglePolicy) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {46}}});
  config.qosPolicies()->push_back(p1);
  auto state = publishAndApplyConfig(stateV0, &config, platform.get());

  checkQosSwState(config, state);
}

TEST(QosPolicy, AddMultiplePolicies) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1, p2;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {45, 46}}});
  config.qosPolicies()->push_back(p1);
  *p2.name() = "qosPolicy_2";
  *p2.rules() = dscpRules({{7, {45, 46}}, {1, {44}}});
  config.qosPolicies()->push_back(p2);
  auto state = publishAndApplyConfig(stateV0, &config, platform.get());

  checkQosSwState(config, state);
}

TEST(QosPolicy, SerializePolicies) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1, p2;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {13}}});
  config.qosPolicies()->push_back(p1);
  *p2.name() = "qosPolicy_2";
  *p2.rules() = dscpRules({{7, {46}}, {1, {44, 45}}});
  config.qosPolicies()->push_back(p2);
  auto state = publishAndApplyConfig(stateV0, &config, platform.get());

  auto qosPolicies = state->getQosPolicies();
  validateThriftMapMapSerialization(*qosPolicies);
}

TEST(QosPolicy, SerializePoliciesWithMap) {
  cfg::SwitchConfig config;
  std::map<int16_t, int16_t> pfc2Queue;
  std::map<int16_t, int16_t> tc2PgId;
  std::map<int16_t, int16_t> pfc2PgId;

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1, p2;
  *p1.name() = "qosPolicy_1";
  *p2.name() = "qosPolicy_2";

  cfg::QosMap qosMap;
  qosMap.dscpMaps()->resize(8);
  for (auto i = 0; i < 8; i++) {
    *qosMap.dscpMaps()[i].internalTrafficClass() = i;
    for (auto j = 0; j < 8; j++) {
      qosMap.dscpMaps()[i].fromDscpToTrafficClass()->push_back(8 * i + j);
    }
    qosMap.dscpMaps()[i].fromTrafficClassToDscp() = i;
  }
  qosMap.expMaps()->resize(8);
  for (auto i = 0; i < 8; i++) {
    *qosMap.expMaps()[i].internalTrafficClass() = i;
    qosMap.expMaps()[i].fromExpToTrafficClass()->push_back(i);
    qosMap.expMaps()[i].fromTrafficClassToExp() = i;
  }

  for (auto i = 0; i < 8; i++) {
    qosMap.trafficClassToQueueId()->emplace(i, i);
    pfc2Queue.emplace(i, i);
    tc2PgId.emplace(i, i);
    pfc2PgId.emplace(i, i);
  }
  qosMap.pfcPriorityToQueueId() = pfc2Queue;
  qosMap.trafficClassToPgId() = tc2PgId;
  qosMap.pfcPriorityToPgId() = pfc2PgId;

  p1.qosMap() = qosMap;
  p2.qosMap() = qosMap;

  config.qosPolicies()->push_back(p1);
  config.qosPolicies()->push_back(p2);
  auto state = publishAndApplyConfig(stateV0, &config, platform.get());

  auto qosPolicies = state->getQosPolicies();
  validateThriftMapMapSerialization(*qosPolicies);
}

TEST(QosPolicy, EmptyRules) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {}}});
  config.qosPolicies()->push_back(p1);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, TrafficClassToVcIdApplied) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  qosMap.trafficClassToVcId() = {{2, 2}, {6, 6}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto swQosPolicy = stateV1->getQosPolicies()->getNode("qosPolicy_1");
  const std::map<int16_t, int16_t> expected{{2, 2}, {6, 6}};
  EXPECT_EQ(swQosPolicy->getTrafficClassToVcId()->toThrift(), expected);
}

TEST(QosPolicy, TrafficClassToVcIdOutOfRange) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  // PORT_VC_VALUE_MAX is 31; 32 is one past the last valid VC index.
  qosMap.trafficClassToVcId() = {{2, 32}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, TrafficClassToVcIdNegative) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  // map<i16, i16> is signed, so a negative vc id is representable.
  qosMap.trafficClassToVcId() = {{2, -1}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, QueueToVcIdApplied) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  qosMap.queueToVcId() = {{2, 2}, {6, 6}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto swQosPolicy = stateV1->getQosPolicies()->getNode("qosPolicy_1");
  const std::map<int16_t, int16_t> expected{{2, 2}, {6, 6}};
  EXPECT_EQ(swQosPolicy->getQueueToVcId()->toThrift(), expected);
}

TEST(QosPolicy, QueueToVcIdOutOfRange) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  // PORT_VC_VALUE_MAX is 31; 32 is one past the last valid VC index.
  qosMap.queueToVcId() = {{2, 32}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, QueueToVcIdNegative) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  qosMap.dscpMaps() = {};
  qosMap.trafficClassToQueueId() = {{0, 0}};
  // map<i16, i16> is signed, so a negative vc id is representable.
  qosMap.queueToVcId() = {{2, -1}};
  p1.qosMap() = qosMap;
  config.qosPolicies()->push_back(p1);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, PolicyConflict) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1, p2;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {46}}});
  config.qosPolicies()->push_back(p1);
  // Both policies have the same name
  *p2.name() = "qosPolicy_1";
  *p2.rules() = dscpRules({{7, {46}}, {1, {44, 45}}});
  config.qosPolicies()->push_back(p2);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, PolicyInvalidDscpValues) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {44, 255}}});
  config.qosPolicies()->push_back(p1);
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(QosPolicy, PortDefaultQosPolicy) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());

  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  config.qosPolicies()->resize(1);
  *config.qosPolicies()[0].name() = "qp1";
  *config.qosPolicies()[0].rules() = dscpRules({{0, {44, 45, 46}}});
  cfg::TrafficPolicyConfig trafficPolicy;
  trafficPolicy.defaultQosPolicy() = "qp1";
  config.dataPlaneTrafficPolicy() = trafficPolicy;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  for (auto& portId : {1, 2}) {
    auto port = stateV1->getPort(PortID(portId));
    ASSERT_EQ("qp1", port->getQosPolicy().value());
  }
}

TEST(QosPolicy, PortQosPolicyOverride) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());

  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  config.qosPolicies()->resize(2);
  *config.qosPolicies()[0].name() = "qp1";
  *config.qosPolicies()[0].rules() = dscpRules({{0, {46}}});
  *config.qosPolicies()[1].name() = "qp2";
  *config.qosPolicies()[1].rules() = dscpRules({{1, {46}}});
  cfg::TrafficPolicyConfig trafficPolicy;
  trafficPolicy.defaultQosPolicy() = "qp1";
  trafficPolicy.portIdToQosPolicy() = {{1, "qp2"}};
  config.dataPlaneTrafficPolicy() = trafficPolicy;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_EQ("qp2", stateV1->getPort(PortID(1))->getQosPolicy().value());
  ASSERT_EQ("qp1", stateV1->getPort(PortID(2))->getQosPolicy().value());
}

TEST(QosPolicy, QosPolicyDelta) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::QosPolicy p1, p2;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules({{7, {46}}});
  config.qosPolicies()->push_back(p1);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto qosPoliciesV1 = stateV1->getQosPolicies();

  /* Change 1 Policy */
  auto& p = config.qosPolicies()->back();
  *p.rules() = dscpRules({{7, {45}}});
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  auto qosPoliciesV2 = stateV2->getQosPolicies();
  checkDelta(qosPoliciesV1, qosPoliciesV2, {"qosPolicy_1"}, {}, {});

  /* Add 1 Policy */
  *p2.name() = "qosPolicy_2";
  *p2.rules() = dscpRules({{2, {36}}, {3, {34, 35}}});
  config.qosPolicies()->push_back(p2);
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  auto qosPoliciesV3 = stateV3->getQosPolicies();
  checkDelta(qosPoliciesV2, qosPoliciesV3, {}, {"qosPolicy_2"}, {});

  /* Remove 1 Policy */
  config.qosPolicies()[0] = config.qosPolicies()[1];
  config.qosPolicies()->pop_back();
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  auto qosPoliciesV4 = stateV4->getQosPolicies();
  checkDelta(qosPoliciesV3, qosPoliciesV4, {}, {}, {"qosPolicy_1"});
}

TEST(QosPolicy, QosMap) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  *policy.name() = "qosPolicy";
  policy.qosMap() = cfgQosMap();
  state = publishAndApplyConfig(state, &config, platform.get());
  checkQosPolicy(policy, state->getQosPolicies()->getNodeIf("qosPolicy"));

  // modiify qos map
  policy.qosMap()->dscpMaps()->pop_back();
  policy.qosMap()->expMaps()->pop_back();
  state = publishAndApplyConfig(state, &config, platform.get());
  checkQosPolicy(policy, state->getQosPolicies()->getNodeIf("qosPolicy"));

  // remove expMaps altogether
  policy.qosMap()->expMaps()->clear();
  state = publishAndApplyConfig(state, &config, platform.get());
  checkQosPolicy(policy, state->getQosPolicies()->getNodeIf("qosPolicy"));

  // allow both qos rule or maps
  *policy.rules() = dscpRules({{2, {36}}, {3, {34, 35}}});
  EXPECT_NO_THROW(publishAndApplyConfig(state, &config, platform.get()));
}

TEST(QosPolicy, DefaultQosPolicy) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  *policy.name() = "qosPolicy";
  policy.qosMap() = cfgQosMap();
  state = publishAndApplyConfig(state, &config, platform.get());
  checkQosPolicy(policy, state->getQosPolicies()->getNodeIf("qosPolicy"));
  // default policy is not set
  EXPECT_EQ(state->getDefaultDataPlaneQosPolicy(), nullptr);
  EXPECT_EQ(
      utility::getFirstNodeIf(state->getSwitchSettings())
          ->getDefaultDataPlaneQosPolicy(),
      nullptr);

  // set default policy
  cfg::TrafficPolicyConfig defaultQosPolicy;
  defaultQosPolicy.defaultQosPolicy() = "qosPolicy";
  config.dataPlaneTrafficPolicy() = defaultQosPolicy;
  state = publishAndApplyConfig(state, &config, platform.get());

  EXPECT_NE(state->getDefaultDataPlaneQosPolicy(), nullptr);
  EXPECT_NE(
      utility::getFirstNodeIf(state->getSwitchSettings())
          ->getDefaultDataPlaneQosPolicy(),
      nullptr);
  checkQosPolicy(policy, state->getDefaultDataPlaneQosPolicy());
  EXPECT_EQ(
      state->getDefaultDataPlaneQosPolicy(),
      utility::getFirstNodeIf(state->getSwitchSettings())
          ->getDefaultDataPlaneQosPolicy());
  EXPECT_EQ(state->getQosPolicies()->getNodeIf("qosPolicy"), nullptr);
}

TEST(QosPolicy, DefaultQosPolicyOnPorts) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());
  registerPort(state, PortID(2), "port2", scope());

  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);

  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  *policy.name() = "qosPolicy";
  policy.qosMap() = cfgQosMap();
  cfg::TrafficPolicyConfig defaultQosPolicy;
  defaultQosPolicy.defaultQosPolicy() = "qosPolicy";
  config.dataPlaneTrafficPolicy() = defaultQosPolicy;
  state = publishAndApplyConfig(state, &config, platform.get());

  checkQosPolicy(policy, state->getDefaultDataPlaneQosPolicy());
  for (auto& portId : {1, 2}) {
    auto port = state->getPort(PortID(portId));
    ASSERT_EQ("qosPolicy", port->getQosPolicy().value());
  }
}

TEST(QosPolicy, QosPolicyPortOverride) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());
  registerPort(state, PortID(2), "port2", scope());

  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);

  config.qosPolicies()->resize(2);
  auto& policy0 = config.qosPolicies()[0];
  *policy0.name() = "qosPolicy0";
  policy0.qosMap() = cfgQosMap();

  auto& policy1 = config.qosPolicies()[1];
  *policy1.name() = "qosPolicy1";
  policy1.qosMap() = cfgQosMap();
  policy1.qosMap()->expMaps()->clear();

  cfg::TrafficPolicyConfig defaultQosPolicy;
  defaultQosPolicy.defaultQosPolicy() = "qosPolicy0";
  defaultQosPolicy.portIdToQosPolicy() = {{1, "qosPolicy1"}};
  config.dataPlaneTrafficPolicy() = defaultQosPolicy;

  state = publishAndApplyConfig(state, &config, platform.get());

  checkQosPolicy(policy0, state->getDefaultDataPlaneQosPolicy());
  checkQosPolicy(policy1, state->getQosPolicies()->getNodeIf("qosPolicy1"));

  const auto port0 = state->getPort(PortID(1));
  ASSERT_EQ("qosPolicy1", port0->getQosPolicy().value());
  const auto port1 = state->getPort(PortID(2));
  ASSERT_EQ("qosPolicy0", port1->getQosPolicy().value());
}

TEST(QosPolicy, QosPolicyConfigMigrateNoDelta) {
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  std::vector<std::pair<uint8_t, std::vector<int16_t>>> rules{
      {0, {6, 7, 8, 9, 12, 13, 14, 15}},
      {1, {0,  1,  2,  3,  4,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 28,
           29, 30, 31, 33, 34, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
           47, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63}},
      {2, {5}},
      {4, {10, 11}},
      {6, {32, 35, 26, 27}},
      {7, {46}}};
  cfg::QosPolicy p1;
  *p1.name() = "qosPolicy_1";
  *p1.rules() = dscpRules(rules);
  config.qosPolicies()->push_back(p1);
  cfg::TrafficPolicyConfig policy;
  policy.defaultQosPolicy() = "qosPolicy_1";
  config.dataPlaneTrafficPolicy() = policy;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());

  cfg::QosPolicy p2;
  *p2.name() = "qosPolicy_1";
  cfg::QosMap qosMap;
  for (const auto& entry : rules) {
    cfg::DscpQosMap dscpMap;
    *dscpMap.internalTrafficClass() = entry.first;
    for (auto dscp : entry.second) {
      dscpMap.fromDscpToTrafficClass()->push_back(dscp);
    }
    qosMap.dscpMaps()->push_back(dscpMap);
  }

  for (auto i = 0; i < 8; i++) {
    cfg::ExpQosMap expMap;
    *expMap.internalTrafficClass() = i;
    expMap.fromExpToTrafficClass()->push_back(i);
    expMap.fromTrafficClassToExp() = i;
    qosMap.expMaps()->push_back(expMap);
    qosMap.trafficClassToQueueId()->emplace(i, i);
  }
  p2.qosMap() = qosMap;
  config.qosPolicies()[0] = p2;
  auto stateV2 = publishAndApplyConfig(stateV0, &config, platform.get());

  StateDelta delta(stateV1, stateV2);
  const auto& policyDelta = delta.getQosPoliciesDelta();
  ASSERT_EQ(policyDelta.begin(), policyDelta.end());
}

TEST(QosPolicy, InvalidPortQosPolicy) {
  cfg::SwitchConfig config0;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  config0.ports()->resize(1);
  preparedMockPortConfig(config0.ports()[0], 1);
  cfg::TrafficPolicyConfig trafficPolicy0;
  trafficPolicy0.portIdToQosPolicy() = {{1, "qp3"}};
  config0.dataPlaneTrafficPolicy() = trafficPolicy0;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config0, platform.get()), FbossError);

  cfg::SwitchConfig config1;
  auto stateV1 = make_shared<SwitchState>();
  registerPort(stateV1, PortID(1), "port1", scope());

  config1.ports()->resize(1);
  preparedMockPortConfig(config1.ports()[0], 1);
  cfg::TrafficPolicyConfig trafficPolicy1;
  trafficPolicy1.defaultQosPolicy() = "qp1";
  config1.dataPlaneTrafficPolicy() = trafficPolicy1;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config1, platform.get()), FbossError);

  cfg::SwitchConfig config2;
  auto stateV2 = make_shared<SwitchState>();
  registerPort(stateV2, PortID(1), "port1", scope());

  cfg::TrafficPolicyConfig cpuPolicy;
  cpuPolicy.defaultQosPolicy() = "qp1";
  cfg::CPUTrafficPolicyConfig cpuTrafficPolicy;
  cpuTrafficPolicy.trafficPolicy() = cpuPolicy;
  config2.cpuTrafficPolicy() = cpuTrafficPolicy;
  EXPECT_THROW(
      publishAndApplyConfig(stateV2, &config2, platform.get()), FbossError);
}

TEST(QosPolicy, InvalidTrafficClassToPgId) {
  cfg::QosMap qosMap;
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  std::map<int16_t, int16_t> tc2PgId;
  // populate pg id > max
  tc2PgId.emplace(0, cfg::switch_config_constants::PORT_PG_VALUE_MAX() + 1);

  qosMap.trafficClassToPgId() = tc2PgId;
  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  policy.name() = "qosPolicy";
  policy.qosMap() = qosMap;
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
}

TEST(QosPolicy, InvalidPfcPriorityToPgId) {
  cfg::QosMap qosMap;
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  std::map<int16_t, int16_t> pfc2PgId;
  pfc2PgId.emplace(
      0,
      cfg::switch_config_constants::PORT_PG_VALUE_MAX() +
          1); // pg 8 is illegal value
  qosMap.pfcPriorityToPgId() = pfc2PgId;

  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  policy.name() = "qosPolicy";
  policy.qosMap() = qosMap;
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);

  pfc2PgId.clear();
  pfc2PgId.emplace(
      cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX() + 1, 0);
  qosMap.pfcPriorityToPgId() = pfc2PgId;
  config.qosPolicies()->resize(1);
  policy.qosMap() = qosMap;
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
}

TEST(QosPolicy, InvalidPfcPriority) {
  cfg::QosMap qosMap;
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  std::map<int16_t, int16_t> pfc2Queue;
  pfc2Queue.emplace(
      cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX() + 1,
      0); // pfc pri 8 is illegal value

  qosMap.pfcPriorityToQueueId() = pfc2Queue;
  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  policy.name() = "qosPolicy";
  policy.qosMap() = qosMap;
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
}

TEST(QosPolicy, ValidatePfcPriorityDelta) {
  cfg::QosMap qosMap;
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  // add entries for the pfcPri->QueueId Map
  // update should go through
  std::map<int16_t, int16_t> pfc2Queue = {{0, 0}, {1, 1}};
  qosMap.pfcPriorityToQueueId() = pfc2Queue;
  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  policy.name() = "qosPolicy";
  policy.qosMap() = qosMap;

  auto state1 = publishAndApplyConfig(state, &config, platform.get());
  EXPECT_NE(nullptr, state1);
  checkQosSwState(config, state1);

  // add entries for the pfcPri->PgId Map only
  // update should go through
  std::map<int16_t, int16_t> pfc2Pg = {{2, 0}, {3, 1}};
  qosMap.pfcPriorityToPgId() = pfc2Pg;
  policy.qosMap() = qosMap;

  auto state2 = publishAndApplyConfig(state1, &config, platform.get());
  EXPECT_NE(nullptr, state2);
  checkQosSwState(config, state2);

  // update both pfcPri->queue and pfcPri -> pgId maps
  // at the same time
  pfc2Queue.emplace(3, 3);
  pfc2Pg.emplace(4, 2);
  qosMap.pfcPriorityToQueueId() = pfc2Queue;
  qosMap.pfcPriorityToPgId() = pfc2Pg;
  policy.qosMap() = qosMap;

  auto state3 = publishAndApplyConfig(state2, &config, platform.get());
  EXPECT_NE(nullptr, state3);
  checkQosSwState(config, state3);

  // reset the pfcPri->queue only
  // update should go through
  qosMap.pfcPriorityToQueueId().reset();
  policy.qosMap() = qosMap;
  auto state4 = publishAndApplyConfig(state3, &config, platform.get());
  EXPECT_NE(nullptr, state4);
  checkQosSwState(config, state4);

  // reset the pfcPri -> pgId  map as well
  qosMap.pfcPriorityToPgId().reset();
  policy.qosMap() = qosMap;

  auto state5 = publishAndApplyConfig(state4, &config, platform.get());
  EXPECT_NE(nullptr, state5);
  checkQosSwState(config, state5);
}

TEST(QosPolicy, ValidatePgIdDelta) {
  cfg::QosMap qosMap;
  cfg::SwitchConfig config;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  std::map<int16_t, int16_t> tc2PgId = {{0, 0}, {1, 1}};
  qosMap.trafficClassToPgId() = tc2PgId;
  config.qosPolicies()->resize(1);
  auto& policy = config.qosPolicies()[0];
  policy.name() = "qosPolicy";
  policy.qosMap() = qosMap;

  auto state1 = publishAndApplyConfig(state, &config, platform.get());
  EXPECT_NE(nullptr, state1);

  checkQosPolicy(policy, state1->getQosPolicies()->getNodeIf("qosPolicy"));

  tc2PgId.emplace(3, 3);
  qosMap.pfcPriorityToQueueId() = tc2PgId;
  policy.qosMap() = qosMap;

  auto state2 = publishAndApplyConfig(state1, &config, platform.get());
  EXPECT_NE(nullptr, state2);

  checkQosPolicy(policy, state2->getQosPolicies()->getNodeIf("qosPolicy"));

  qosMap.trafficClassToPgId().reset();
  policy.qosMap() = qosMap;
  auto state3 = publishAndApplyConfig(state2, &config, platform.get());
  EXPECT_NE(nullptr, state3);
  checkQosSwState(config, state3);
}
