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
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmQosPolicy.h"
#include "fboss/agent/hw/bcm/BcmQosPolicyTable.h"
#include "fboss/agent/hw/bcm/BcmQosUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/utils/TrafficPolicyTestUtils.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

extern "C" {
#include <bcm/qos.h>
}

namespace {

using namespace facebook::fboss;
using namespace facebook::fboss::utility;

constexpr auto kQosMapIngressL3Flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L3;

int getNumHwQosMaps(BcmSwitch* hw, int desiredMapFlags = -1) {
  auto mapIdsAndFlags = getBcmQosMapIdsAndFlags(hw->getUnit());
  if (desiredMapFlags == -1) {
    return mapIdsAndFlags.size();
  }
  return std::count_if(
      mapIdsAndFlags.begin(), mapIdsAndFlags.end(), [&](auto mapIdAndFlag) {
        return std::get<1>(mapIdAndFlag) == desiredMapFlags;
      });
}

int getNumHwIngressL3QosMaps(BcmSwitch* hw) {
  return getNumHwQosMaps(hw, kQosMapIngressL3Flags);
}

void checkSwHwQosMapsMatch(
    BcmSwitch* hw,
    const std::shared_ptr<SwitchState>& state) {
  auto qosPolicyTable = state->getQosPolicies();
  auto bcmQosPolicyTable = hw->getQosPolicyTable();
  // default qos policy is mainted in sw switch state, but is maintained in same
  // qos policy map in bcm switch, need to reconcile this
  auto swStateSize = qosPolicyTable->numNodes() +
      (state->getDefaultDataPlaneQosPolicy() ? 1 : 0);
  ASSERT_EQ(swStateSize, bcmQosPolicyTable->getNumQosPolicies());
  ASSERT_EQ(swStateSize, getNumHwIngressL3QosMaps(hw));

  for (const auto& tableIter : std::as_const(*qosPolicyTable)) {
    for (const auto& [name, qosPolicy] : std::as_const(*tableIter.second)) {
      auto bcmQosPolicy =
          bcmQosPolicyTable->getQosPolicyIf(qosPolicy->getName());
      ASSERT_NE(nullptr, bcmQosPolicy);
      ASSERT_TRUE(bcmQosPolicy->policyMatches(qosPolicy));
    }
  }
}

void checkGportQosMap(
    BcmSwitch* hw,
    bcm_gport_t gport,
    const std::string& qosPolicyName) {
  auto qosPolicy = hw->getQosPolicyTable()->getQosPolicyIf(qosPolicyName);
  ASSERT_NE(nullptr, qosPolicy);

  int qosPolicyHandle;
  auto rv = bcm_qos_port_map_type_get(
      hw->getUnit(),
      gport,
      BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L3,
      &qosPolicyHandle);
  bcmCheckError(rv, "fail to get the Qos maps of gport=", gport);

  auto handle = qosPolicy->getHandle(BcmQosMap::Type::IP_INGRESS);
  // Cast the returned handle to the same type. For more context, refer
  // https://fb.workplace.com/groups/2737905709809114/permalink/3191881447744869/
  decltype(handle) returnedHandle(qosPolicyHandle);
  ASSERT_EQ(returnedHandle, handle);
}

void checkPortQosMap(
    BcmSwitch* hw,
    int portId,
    const std::string& qosPolicyName) {
  auto bcmPort = hw->getPortTable()->getBcmPort(portId);
  checkGportQosMap(hw, bcmPort->getBcmGport(), qosPolicyName);
}

void checkGportNoQosMap(BcmSwitch* hw, bcm_gport_t gport) {
  int qosPolicyHandle;
  auto rv = bcm_qos_port_map_type_get(
      hw->getUnit(),
      gport,
      BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L3,
      &qosPolicyHandle);
  EXPECT_EQ(rv, BCM_E_NOT_FOUND);
}

void checkPortNoQosMap(BcmSwitch* hw, int portId) {
  auto bcmPort = hw->getPortTable()->getBcmPort(portId);
  checkGportNoQosMap(hw, bcmPort->getBcmGport());
}

} // unnamed namespace

namespace facebook::fboss {

class BcmQosPolicyTest : public BcmTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfNPortConfig(
        getHwSwitch()->getPlatform()->getPlatformMapping(),
        getHwSwitch()->getPlatform()->getAsic(),
        {masterLogicalPortIds()[0], masterLogicalPortIds()[1]},
        getHwSwitch()->getPlatform()->supportsAddRemovePort());
  }
};

TEST_F(BcmQosPolicyTest, QosPolicyCreate) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyCreateMultiple) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    addDscpQosPolicy(&newCfg, "qp2", {{7, {45, 46}}, {2, {31}}, {3, {7}}});
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyDelete) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {45, 46}}});
    addDscpQosPolicy(&newCfg, "qp2", {{7, {46}}, {2, {31}}, {3, {7}}});
    applyNewConfig(newCfg);
    delQosPolicy(&newCfg, "qp1");
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyModify) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    addDscpQosPolicy(&newCfg, "qp2", {{7, {46}}, {2, {31}}, {3, {7}}});
    applyNewConfig(newCfg);
    delQosPolicy(&newCfg, "qp1");
    delQosPolicy(&newCfg, "qp2");
    addDscpQosPolicy(&newCfg, "qp1", {{6, {46}}});
    addDscpQosPolicy(&newCfg, "qp2", {{7, {45}}, {4, {7}}});
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyInvalidQueueId) {
  auto newCfg = initialConfig();
  applyNewConfig(newCfg);
  addDscpQosPolicy(&newCfg, "qp1", {{BCM_COS_MIN - 1, {46}}});
  ASSERT_THROW(applyNewConfig(newCfg), FbossError);
  // Check that we are not leaking HW Qos maps
  ASSERT_EQ(0, getNumHwIngressL3QosMaps(getHwSwitch()));

  newCfg = initialConfig();
  addDscpQosPolicy(&newCfg, "qp1", {{BCM_COS_MAX + 1, {46}}});
  ASSERT_THROW(applyNewConfig(newCfg), FbossError);
  ASSERT_EQ(0, getNumHwIngressL3QosMaps(getHwSwitch()));
}

TEST_F(BcmQosPolicyTest, QosPolicyInvalidDscp) {
  // Dscp value range: [0;63]
  auto newCfg = initialConfig();
  addDscpQosPolicy(&newCfg, "qp1", {{7, {64}}});
  EXPECT_THROW(applyNewConfig(newCfg), FbossError);
  // Check that we are not leaking HW Qos maps
  ASSERT_EQ(0, getNumHwIngressL3QosMaps(getHwSwitch()));

  newCfg = initialConfig();
  addDscpQosPolicy(&newCfg, "qp1", {{7, {-1}}});
  EXPECT_THROW(applyNewConfig(newCfg), FbossError);
  ASSERT_EQ(0, getNumHwIngressL3QosMaps(getHwSwitch()));
}

TEST_F(BcmQosPolicyTest, QosPolicyDefault) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {45, 46}}});
    setDefaultQosPolicy(&newCfg, "qp1");
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    for (auto index : {0, 1}) {
      checkPortQosMap(getHwSwitch(), masterLogicalPortIds()[index], "qp1");
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyRemoveDefault) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {45, 46}}});
    setDefaultQosPolicy(&newCfg, "qp1");
    applyNewConfig(newCfg);

    unsetDefaultQosPolicy(&newCfg);
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    for (auto index : {0, 1}) {
      checkPortNoQosMap(getHwSwitch(), masterLogicalPortIds()[index]);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyOverrideDefault) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    addDscpQosPolicy(&newCfg, "qp2", {{3, {45}}});
    setDefaultQosPolicy(&newCfg, "qp1");
    overrideQosPolicy(&newCfg, masterLogicalPortIds()[0], "qp2");
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    checkPortQosMap(getHwSwitch(), masterLogicalPortIds()[0], "qp2");
    checkPortQosMap(getHwSwitch(), masterLogicalPortIds()[1], "qp1");
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyCPU) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    setCPUQosPolicy(&newCfg, "qp1");
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    checkGportQosMap(getHwSwitch(), 0, "qp1");
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicyCPURemove) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    addDscpQosPolicy(&newCfg, "qp1", {{7, {46}}});
    setCPUQosPolicy(&newCfg, "qp1");
    applyNewConfig(newCfg);
    unsetCPUQosPolicy(&newCfg);
    applyNewConfig(newCfg);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    checkGportNoQosMap(getHwSwitch(), 0);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQosPolicyTest, QosPolicySdkAssertions) {
  /*
   * This test ensures that the assumptions we made when designing the QoS
   * subsystem are true.
   */
  int qosMap;
  bcm_qos_map_t qosMapEntry;
  const auto kQosMapFlags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L3;

  // Create a map with 1 entry
  auto rv = bcm_qos_map_create(getUnit(), kQosMapFlags, &qosMap);
  bcmCheckError(rv, "failed to create a QoS map");
  bcm_qos_map_t_init(&qosMapEntry);
  qosMapEntry.dscp = 45;
  qosMapEntry.int_pri = 7;
  rv = bcm_qos_map_add(getUnit(), kQosMapFlags, &qosMapEntry, qosMap);
  bcmCheckError(rv, "failed to add an entry in QoS map");
  ASSERT_EQ(1, getNumHwIngressL3QosMaps(getHwSwitch()));

  // Attach the map to 2 ports
  rv = bcm_qos_port_map_set(getUnit(), masterLogicalPortIds()[0], qosMap, 0);
  bcmCheckError(rv, "failed to set QoS map on portId=", 0);
  rv = bcm_qos_port_map_set(getUnit(), masterLogicalPortIds()[1], qosMap, 0);
  bcmCheckError(rv, "failed to set QoS map on portId=", 1);

  // Add an entry after the map has been attached to a port
  bcm_qos_map_t_init(&qosMapEntry);
  qosMapEntry.dscp = 42;
  qosMapEntry.int_pri = 2;
  rv = bcm_qos_map_add(getUnit(), kQosMapFlags, &qosMapEntry, qosMap);
  bcmCheckError(rv, "failed to add an entry in QoS map");

  // Check that the map is attached to the port
  int portQosMap;
  rv = bcm_qos_port_map_type_get(
      getUnit(), masterLogicalPortIds()[0], kQosMapFlags, &portQosMap);
  if (rv != BCM_E_UNAVAIL) { // SDK 6.4.10 may not support this function
    bcmCheckError(rv, "failed to get QoS map for portId=", 0);
    ASSERT_EQ(portQosMap, qosMap);
  }

  // Check that our 2 custom entries are in the map
  int cnt;
  rv = bcm_qos_map_multi_get(getUnit(), kQosMapFlags, qosMap, 0, nullptr, &cnt);
  bcmCheckError(rv, "failed to get the number of entries in the Qos Map");
  std::vector<bcm_qos_map_t> qosMapEntries(cnt);
  rv = bcm_qos_map_multi_get(
      getUnit(), kQosMapFlags, qosMap, cnt, qosMapEntries.data(), &cnt);
  bcmCheckError(rv, "failed to get the entries of the Qos Map");
  cnt = 0;
  for (auto& entry : qosMapEntries) {
    cnt += !!entry.int_pri;
  }
  ASSERT_EQ(2, cnt);

  // Destroy the QoS map
  rv = bcm_qos_map_destroy(getUnit(), qosMap);
  bcmCheckError(rv, "failed to destroy QoS map");
  ASSERT_EQ(0, getNumHwIngressL3QosMaps(getHwSwitch()));

  // Check that the ports are not using the map anymore (even if we did not
  // explicitly detached the map from the ports)
  for (int i : {0, 1}) {
    rv = bcm_qos_port_map_type_get(
        getUnit(), masterLogicalPortIds()[i], kQosMapFlags, &portQosMap);
    if (rv != BCM_E_UNAVAIL) { // SDK 6.4.10 may not support this function
      ASSERT_EQ(BCM_E_NOT_FOUND, rv);
    }
  }
}

TEST_F(BcmQosPolicyTest, QosPolicyConfigMigrate) {
  auto setup = [=, this]() {
    auto config = initialConfig();
    config.qosPolicies()->resize(1);
    *config.qosPolicies()[0].name() = "qp";
    config.qosPolicies()[0].rules()->resize(8);
    for (auto i = 0; i < 8; i++) {
      *config.qosPolicies()[0].rules()[i].queueId() = i;
      config.qosPolicies()[0].rules()[i].dscp()->resize(8);
      for (auto j = 0; j < 8; j++) {
        config.qosPolicies()[0].rules()[i].dscp()[j] = 8 * i + j;
      }
    }
    cfg::TrafficPolicyConfig dataPlaneTrafficPolicy;
    dataPlaneTrafficPolicy.defaultQosPolicy() = "qp";
    config.dataPlaneTrafficPolicy() = dataPlaneTrafficPolicy;
    applyNewConfig(config);
  };

  auto verify = [=, this]() {
    checkSwHwQosMapsMatch(getHwSwitch(), getProgrammedState());
    for (auto index : {0, 1}) {
      checkPortQosMap(getHwSwitch(), masterLogicalPortIds()[index], "qp");
    }
  };

  auto setupPostWb = [=, this]() {
    auto config = initialConfig();
    cfg::QosMap qosMap;
    qosMap.dscpMaps()->resize(8);
    for (auto i = 0; i < 8; i++) {
      *qosMap.dscpMaps()[i].internalTrafficClass() = i;
      for (auto j = 0; j < 8; j++) {
        qosMap.dscpMaps()[i].fromDscpToTrafficClass()->push_back(8 * i + j);
      }
    }
    qosMap.expMaps()->resize(8);
    for (auto i = 0; i < 8; i++) {
      *qosMap.expMaps()[i].internalTrafficClass() = i;
      qosMap.expMaps()[i].fromExpToTrafficClass()->push_back(i);
      qosMap.expMaps()[i].fromTrafficClassToExp() = i;
    }

    for (auto i = 0; i < 8; i++) {
      qosMap.trafficClassToQueueId()->emplace(i, i);
    }

    config.qosPolicies()->resize(1);
    *config.qosPolicies()[0].name() = "qp";
    config.qosPolicies()[0].qosMap() = qosMap;

    cfg::TrafficPolicyConfig dataPlaneTrafficPolicy;
    dataPlaneTrafficPolicy.defaultQosPolicy() = "qp";
    config.dataPlaneTrafficPolicy() = dataPlaneTrafficPolicy;
    applyNewConfig(config);
  };
  verifyAcrossWarmBoots(setup, verify, setupPostWb, verify);
}

} // namespace facebook::fboss
