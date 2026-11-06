/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/PortUpdateHandler.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"

using namespace facebook::fboss;
using std::string;

namespace {

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

class PortUpdateHandlerTest : public ::testing::Test {
 public:
  void SetUp() override {
    handle = createTestHandle();
    sw = handle->getSw();
    initState = testStateA();

    addState = testStateA();
    // add port 21 which uses VLAN 1
    registerPort(addState, PortID(21), "port21", scope());
    addState->getVlans()->getNodeIf(VlanID(1))->addPort(
        PortID(21), false, false);
    deltaAdd = std::make_shared<StateDelta>(initState, addState);
    deltaRemove = std::make_shared<StateDelta>(addState, initState);

    // rename all port name fron portX to eth1/X/1
    initPorts = initState->getPorts();
    newPorts = std::make_shared<MultiSwitchPortMap>();
    for (const auto& origPortMap : std::as_const(*initPorts)) {
      for (const auto& origPort : std::as_const(*origPortMap.second)) {
        auto newPort = origPort.second->clone();
        newPort->setName(
            folly::to<string>("eth1/", origPort.second->getID(), "/1"));
        newPort->setOperState(true);
        newPorts->addNode(newPort, sw->getScopeResolver()->scope(newPort));
      }
    }
    auto changedState = initState->clone();
    changedState->resetPorts(newPorts);
    deltaChange = std::make_shared<StateDelta>(initState, changedState);

    portUpdateHandler = std::make_unique<PortUpdateHandler>(sw);
  }

  void expectPortCounterExist(
      CounterCache& counters,
      std::shared_ptr<MultiSwitchPortMap> ports) {
    for (const auto& portMap : std::as_const(*ports)) {
      for (const auto& port : std::as_const(*portMap.second)) {
        EXPECT_TRUE(counters.checkExist(port.second->getName() + ".up"));
        EXPECT_EQ(
            counters.value(port.second->getName() + ".up"),
            port.second->isUp());
      }
    }
  }

  void expectPortCounterNotExist(
      CounterCache& counters,
      std::shared_ptr<MultiSwitchPortMap> ports) {
    for (const auto& portMap : std::as_const(*ports)) {
      for (const auto& port : std::as_const(*portMap.second)) {
        EXPECT_FALSE(counters.checkExist(port.second->getName() + ".up"));
      }
    }
  }

  // Rebuilds the port map so that the returned state is a genuinely distinct
  // node. Port::modify() on an unpublished state would edit `in` in place and
  // the resulting delta would report no change.
  template <typename PortMutatorT>
  std::shared_ptr<SwitchState> stateWithModifiedPort(
      const std::shared_ptr<SwitchState>& in,
      PortMutatorT mutator) {
    auto out = in->clone();
    auto ports = std::make_shared<MultiSwitchPortMap>();
    for (const auto& portMap : std::as_const(*in->getPorts())) {
      for (const auto& origPort : std::as_const(*portMap.second)) {
        auto newPort = origPort.second->clone();
        if (newPort->getID() == kPortId()) {
          mutator(newPort);
        }
        ports->addNode(newPort, sw->getScopeResolver()->scope(newPort));
      }
    }
    out->resetPorts(ports);
    return out;
  }

  std::shared_ptr<SwitchState> stateWithUserMetaData(
      const std::shared_ptr<SwitchState>& in,
      std::optional<cfg::AclLookupClassPort> userMetaData) {
    return stateWithModifiedPort(in, [userMetaData](auto& port) {
      port->setUserMetaData(userMetaData);
    });
  }

  std::shared_ptr<SwitchState> stateWithIngressAclTable(
      const std::shared_ptr<SwitchState>& in,
      std::optional<std::string> ingressAclTableName) {
    return stateWithModifiedPort(in, [ingressAclTableName](auto& port) {
      port->setIngressAclTableName(ingressAclTableName);
    });
  }

  void expectAccessPolicyState(
      CounterCache& counters,
      const std::string& portName,
      cfg::AclLookupClassPort expected) {
    const auto counter = portName + ".access_policy_state";
    ASSERT_TRUE(counters.checkExist(counter));
    EXPECT_EQ(counters.value(counter), static_cast<int64_t>(expected));
  }

  PortID kPortId() const {
    return PortID(1);
  }

  void TearDown() override {
    sw = nullptr;
  }

  SwSwitch* sw{nullptr};
  std::unique_ptr<HwTestHandle> handle{nullptr};
  std::shared_ptr<SwitchState> initState;
  std::shared_ptr<SwitchState> addState;
  std::shared_ptr<MultiSwitchPortMap> initPorts;
  std::shared_ptr<MultiSwitchPortMap> newPorts;
  std::shared_ptr<StateDelta> deltaAdd;
  std::shared_ptr<StateDelta> deltaRemove;
  std::shared_ptr<StateDelta> deltaChange;
  std::unique_ptr<PortUpdateHandler> portUpdateHandler;
};

TEST_F(PortUpdateHandlerTest, PortAdded) {
  // // Cache the current stats
  CounterCache counters(sw);
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 0);

  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 20);

  portUpdateHandler->stateUpdated(*deltaAdd);
  // should be 21 PortStats now
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 21);

  counters.update();
  // make sure original PortStats still exist
  expectPortCounterExist(counters, initPorts);
  // make sure PortStats for port 21 is also created
  // EXPECT_TRUE(counters.checkExist("port21.link_state.flap.sum"));
  EXPECT_TRUE(counters.checkExist("port21.up"));
  EXPECT_EQ(counters.value("port21.up"), 0);
}

TEST_F(PortUpdateHandlerTest, PortRemoved) {
  // // Cache the current stats
  CounterCache counters(sw);
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 0);

  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(std::make_shared<SwitchState>(), addState));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 21);

  portUpdateHandler->stateUpdated(*deltaRemove);
  // should be 20 PortStats now
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 20);

  counters.update();
  // make sure original PortStats still exist
  expectPortCounterExist(counters, initPorts);
  // make sure PortStats for port 21 no longer exist
  EXPECT_FALSE(counters.checkExist("port21.up"));
}

TEST_F(PortUpdateHandlerTest, PortChanged) {
  // // Cache the current stats
  CounterCache counters(sw);
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 0);

  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 20);

  portUpdateHandler->stateUpdated(*deltaChange);
  // should be 20 PortStats now
  EXPECT_EQ(sw->stats()->getPortStats()->size(), 20);

  counters.update();
  // make sure PortStats with the new name is created
  expectPortCounterExist(counters, newPorts);
  expectPortCounterNotExist(counters, initPorts);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateDefaultsToUnconstrained) {
  CounterCache counters(sw);

  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  counters.update();
  // Ports in initState carry no userMetaData
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_UNCONSTRAINED);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateChanged) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  auto restrictedState = stateWithUserMetaData(
      initState, cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(initState, restrictedState));
  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);

  auto blockedState = stateWithUserMetaData(
      initState, cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(restrictedState, blockedState));
  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);

  // Clearing userMetaData reverts the port to unconstrained
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(blockedState, initState));
  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_UNCONSTRAINED);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateUnknownLookupClass) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  // A lookup class this agent does not know about is reported as unconstrained
  auto unknownState = stateWithUserMetaData(
      initState, static_cast<cfg::AclLookupClassPort>(42));
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(initState, unknownState));

  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_UNCONSTRAINED);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateFromIngressAclTable) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  auto restrictedState =
      stateWithIngressAclTable(initState, "AccessPolicyRestrictedTable");
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(initState, restrictedState));
  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);

  auto blockedState =
      stateWithIngressAclTable(initState, "AccessPolicyBlockTable");
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(restrictedState, blockedState));
  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateUnrelatedIngressAclTable) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  // An ingress ACL table that is not an access policy table leaves the port
  // unconstrained
  auto aclState = stateWithIngressAclTable(initState, "SomeOtherAclTable");
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(initState, aclState));

  counters.update();
  expectAccessPolicyState(
      counters, "port1", cfg::AclLookupClassPort::CLASS_PORT_UNCONSTRAINED);
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateFollowsPortRename) {
  CounterCache counters(sw);
  auto blockedState = stateWithUserMetaData(
      initState, cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), blockedState));

  auto renamedState = stateWithModifiedPort(
      blockedState, [](auto& port) { port->setName("eth1/1/1"); });
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(blockedState, renamedState));

  counters.update();
  expectAccessPolicyState(
      counters, "eth1/1/1", cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  EXPECT_FALSE(counters.checkExist("port1.access_policy_state"));
}

TEST_F(PortUpdateHandlerTest, AccessPolicyStateClearedOnPortRemoved) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(std::make_shared<SwitchState>(), addState));
  counters.update();
  ASSERT_TRUE(counters.checkExist("port21.access_policy_state"));

  portUpdateHandler->stateUpdated(*deltaRemove);

  counters.update();
  EXPECT_FALSE(counters.checkExist("port21.access_policy_state"));
}

TEST_F(PortUpdateHandlerTest, ActiveStateCounterNotSetWithoutActiveState) {
  CounterCache counters(sw);
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), initState));

  counters.update();
  EXPECT_FALSE(counters.checkExist("port1.active"));
}

TEST_F(PortUpdateHandlerTest, ActiveStateCounterSetOnPortAdded) {
  CounterCache counters(sw);
  // Warm boot replays the restored state as all ports newly added
  auto activeState = stateWithModifiedPort(
      initState, [](auto& port) { port->setActiveState(true); });
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), activeState));

  counters.update();
  ASSERT_TRUE(counters.checkExist("port1.active"));
  EXPECT_EQ(counters.value("port1.active"), 1);
}

TEST_F(PortUpdateHandlerTest, ActiveStateCounterFollowsPortRename) {
  CounterCache counters(sw);
  auto activeState = stateWithModifiedPort(
      initState, [](auto& port) { port->setActiveState(true); });
  portUpdateHandler->stateUpdated(*std::make_shared<StateDelta>(
      std::make_shared<SwitchState>(), activeState));

  auto renamedState = stateWithModifiedPort(
      activeState, [](auto& port) { port->setName("eth1/1/1"); });
  portUpdateHandler->stateUpdated(
      *std::make_shared<StateDelta>(activeState, renamedState));

  counters.update();
  ASSERT_TRUE(counters.checkExist("eth1/1/1.active"));
  EXPECT_EQ(counters.value("eth1/1/1.active"), 1);
}

template <typename SwitchTypeT>
class PortUpdateHandlerLoopDetectionTest : public ::testing::Test {
 public:
  static auto constexpr switchType = SwitchTypeT::switchType;
  std::shared_ptr<SwitchState> initState() const {
    return testStateAWithPortsUp(switchType);
  }
  void SetUp() override {
    handle = createTestHandle(initState());
    sw = handle->getSw();
  }
  void TearDown() override {
    sw = nullptr;
  }
  int switchId() const {
    return switchType == cfg::SwitchType::VOQ ? 1 : 2;
  }
  HwSwitchMatcher matcher() const {
    return HwSwitchMatcher(
        std::unordered_set<SwitchID>({SwitchID(switchId())}));
  }
  cfg::PortState getLoopedPortExpectedState() const {
    if (switchType != cfg::SwitchType::FABRIC) {
      return cfg::PortState::ENABLED;
    }
    auto switchSettings =
        sw->getState()->getSwitchSettings()->getSwitchSettings(matcher());
    if (switchSettings->isSwitchDrained()) {
      // Switch is drained, no looped port disable
      return cfg::PortState::ENABLED;
    }
    auto port = sw->getState()->getPorts()->getNode(kPortId());
    if (port->isDrained() ||
        (port->getActiveState().has_value() && !port->isActive().value())) {
      // Port is drained, no looped port disable
      return cfg::PortState::ENABLED;
    }
    return cfg::PortState::DISABLED;
  }
  PortID kPortId() const {
    auto allPorts = sw->getState()->getPorts()->getAllNodes();
    // Return first fabric port
    for (const auto& [id, port] : *allPorts) {
      if (port->getPortType() == cfg::PortType::FABRIC_PORT) {
        return PortID(id);
      }
    }
    throw FbossError("No fabric port found");
  }
  void checkPortState(cfg::PortState expectedState) {
    ensureAllQueuedUpdatesDone();
    WITH_RETRIES({
      auto ports = sw->getState()->getPorts()->getAllNodes();
      for (const auto& [id, port] : *ports) {
        if (PortID(id) == kPortId()) {
          EXPECT_EVENTUALLY_EQ(port->getAdminState(), expectedState);
          if (expectedState == cfg::PortState::DISABLED) {
            EXPECT_EVENTUALLY_EQ(
                port->getActiveErrors(),
                std::vector<PortError>(
                    {PortError::ERROR_DISABLE_LOOP_DETECTED}));
          } else {
            EXPECT_EVENTUALLY_TRUE(port->getActiveErrors().empty());
          }

        } else {
          EXPECT_EVENTUALLY_EQ(port->getAdminState(), cfg::PortState::ENABLED);
          EXPECT_EVENTUALLY_TRUE(port->getActiveErrors().empty());
        }
      }
    });
  }
  void checkPortLedState(PortLedExternalState expectedState) {
    ensureAllQueuedUpdatesDone();
    WITH_RETRIES({
      auto ports = sw->getState()->getPorts()->getAllNodes();
      for (const auto& [id, port] : *ports) {
        if (PortID(id) == kPortId()) {
          ASSERT_EVENTUALLY_TRUE(port->getLedPortExternalState().has_value());
          EXPECT_EVENTUALLY_EQ(*port->getLedPortExternalState(), expectedState);
        } else {
          EXPECT_EVENTUALLY_FALSE(port->getLedPortExternalState().has_value());
        }
      }
    });
  }
  void ensureAllQueuedUpdatesDone() {
    sw->updateStateBlocking(
        "empty update",
        [](const std::shared_ptr<SwitchState>) { return nullptr; });
  }
  std::map<PortID, multiswitch::FabricConnectivityDelta> makeConnectivity(
      std::optional<FabricEndpoint> endpoint) const {
    multiswitch::FabricConnectivityDelta delta;
    if (endpoint) {
      delta.newConnectivity() = *endpoint;
    }
    std::map<PortID, multiswitch::FabricConnectivityDelta> connectivity;
    connectivity.insert({kPortId(), delta});
    return connectivity;
  }
  SwSwitch* sw{nullptr};
  std::unique_ptr<HwTestHandle> handle{nullptr};
};

TYPED_TEST_SUITE(PortUpdateHandlerLoopDetectionTest, DsfSwitchTypeTestTypes);

TYPED_TEST(PortUpdateHandlerLoopDetectionTest, createLoop) {
  FabricEndpoint endpoint;
  endpoint.isAttached() = true;
  endpoint.switchId() = this->switchId();
  endpoint.portId() = this->kPortId();
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  this->checkPortLedState(PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
  this->checkPortState(this->getLoopedPortExpectedState());
  // Reset connectivity to mark port as unattached - this is what will
  // happen once link goes down due to port being admin disabled.
  // Port admin state should be retained but Led state should go
  // back to none, since the port is no longer looped
  endpoint.isAttached() = false;
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  this->checkPortLedState(PortLedExternalState::NONE);
  this->checkPortState(this->getLoopedPortExpectedState());
}

TYPED_TEST(PortUpdateHandlerLoopDetectionTest, createLoopReenablePort) {
  FabricEndpoint endpoint;
  endpoint.isAttached() = true;
  endpoint.switchId() = this->switchId();
  endpoint.portId() = this->kPortId();
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  this->checkPortLedState(PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
  this->checkPortState(this->getLoopedPortExpectedState());
  // Reset connectivity to mark port as unattached - this is what will
  // happen once link goes down due to port being admin disabled.
  // Port admin state should be retained but Led state should go
  // back to none, since the port is no longer looped
  endpoint.isAttached() = false;
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  this->checkPortLedState(PortLedExternalState::NONE);
  this->checkPortState(this->getLoopedPortExpectedState());
  this->sw->updateStateBlocking(
      "Re-enable port", [this](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        auto port = out->getPorts()->getNodeIf(this->kPortId());
        auto newPort = port->modify(&out);
        newPort->setAdminState(cfg::PortState::ENABLED);
        return out;
      });
  this->checkPortLedState(PortLedExternalState::NONE);
  this->checkPortState(cfg::PortState::ENABLED);
}

TYPED_TEST(PortUpdateHandlerLoopDetectionTest, createLoopPortDrained) {
  this->sw->updateStateBlocking(
      "Drain port", [this](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        auto port = out->getPorts()->getNodeIf(this->kPortId());
        auto newPort = port->modify(&out);
        newPort->setPortDrainState(cfg::PortDrainState::DRAINED);
        return out;
      });
  FabricEndpoint endpoint;
  endpoint.isAttached() = true;
  endpoint.switchId() = this->switchId();
  endpoint.portId() = this->kPortId();
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  // Drained ports don't get disabled
  EXPECT_EQ(this->getLoopedPortExpectedState(), cfg::PortState::ENABLED);
  this->checkPortLedState(PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
  this->checkPortState(this->getLoopedPortExpectedState());
  // Reset connectivity old admin state should be retained
  this->sw->linkConnectivityChanged(this->makeConnectivity(std::nullopt));
  // Still enabled
  EXPECT_EQ(this->getLoopedPortExpectedState(), cfg::PortState::ENABLED);
  this->checkPortLedState(PortLedExternalState::NONE);
  this->checkPortState(this->getLoopedPortExpectedState());
}

TYPED_TEST(PortUpdateHandlerLoopDetectionTest, createLoopSwitchDrained) {
  this->sw->updateStateBlocking(
      "Drain switch", [this](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        auto settings =
            out->getSwitchSettings()->getSwitchSettings(this->matcher());
        auto newSettings = settings->modify(&out);
        newSettings->setActualSwitchDrainState(cfg::SwitchDrainState::DRAINED);
        return out;
      });
  FabricEndpoint endpoint;
  endpoint.isAttached() = true;
  endpoint.switchId() = this->switchId();
  endpoint.portId() = this->kPortId();
  this->sw->linkConnectivityChanged(this->makeConnectivity(endpoint));
  // Drained ports don't get disabled
  EXPECT_EQ(this->getLoopedPortExpectedState(), cfg::PortState::ENABLED);
  this->checkPortLedState(PortLedExternalState::CABLING_ERROR_LOOP_DETECTED);
  this->checkPortState(this->getLoopedPortExpectedState());
  // Reset connectivity old admin state should be retained
  this->sw->linkConnectivityChanged(this->makeConnectivity(std::nullopt));
  // Still enabled
  EXPECT_EQ(this->getLoopedPortExpectedState(), cfg::PortState::ENABLED);
  this->checkPortLedState(PortLedExternalState::NONE);
  this->checkPortState(this->getLoopedPortExpectedState());
}
} // unnamed namespace
