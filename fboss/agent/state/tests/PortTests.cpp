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
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h" // NOLINT(misc-include-cleaner)
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/NodeMapDelta.h" // NOLINT(misc-include-cleaner)
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <boost/container/flat_map.hpp>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using boost::container::flat_map;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;

namespace {
void prepareDefaultSwPort(Platform* platform, shared_ptr<Port> port) {
  port->setAdminState(cfg::PortState::DISABLED);
  port->setSpeed(cfg::PortSpeed::XG);
  port->setProfileId(cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER);
  PlatformPortProfileConfigMatcher matcher{port->getProfileID(), port->getID()};
  if (auto profileConfig = platform->getPortProfileConfig(matcher)) {
    port->setProfileConfig(*profileConfig->iphy());
  } else {
    throw FbossError(
        "No port profile config found with matcher:", matcher.toString());
  }
  port->resetPinConfigs(
      platform->getPlatformMapping()->getPortIphyPinConfigs(matcher));
  port->setSerdesCustomCollection(
      platform->getPlatformMapping()->getPortSerdesCustomCollection(matcher));
}

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

// Test to validate isAnyInterfacePortInLoopbackMode
// Validate that none of the interface/ports are in loopback mode
// modify ports to be in loopback and check again
TEST(Port, checkPortLoopbackMode) {
  for (auto withPortRouterIntf : {false, true}) {
    auto platform = createMockPlatform();
    auto stateV0 = make_shared<SwitchState>();
    auto config =
        withPortRouterIntf ? testConfigA() : testConfigAWithPortInterfaces();

    auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
    ASSERT_NE(nullptr, stateV1);

    for (auto [_, intfMap] : std::as_const(*stateV1->getInterfaces())) {
      for (auto iter : std::as_const(*intfMap)) {
        auto intf = iter.second;
        EXPECT_FALSE(isAnyInterfacePortInLoopbackMode(stateV1, intf));
      }
    }

    // set all ports in the configuration to loopback
    // all interfaces should also point to these loopbacks
    for (auto& port : *config.ports()) {
      port.loopbackMode() = cfg::PortLoopbackMode::PHY;
    }
    auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());

    for (auto [_, intfMap] : std::as_const(*stateV1->getInterfaces())) {
      for (auto iter : std::as_const(*intfMap)) {
        EXPECT_TRUE(isAnyInterfacePortInLoopbackMode(stateV2, iter.second));
      }
    }
  }
}

TEST(Port, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  auto portV0 = stateV0->getPorts()->getNodeIf(PortID(1));
  EXPECT_FALSE(portV0->isPublished());
  EXPECT_EQ(PortID(1), portV0->getID());
  EXPECT_EQ("port1", portV0->getName());
  EXPECT_EQ(cfg::PortState::DISABLED, portV0->getAdminState());
  Port::VlanMembership emptyVlans;
  EXPECT_EQ(emptyVlans, portV0->getVlans());
  EXPECT_FALSE(portV0->getSampleDestination().has_value());

  portV0->publish();
  EXPECT_TRUE(portV0->isPublished());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  config.ports()[0].sampleDest() = cfg::SampleDestination::MIRROR;
  *config.ports()[0].sFlowIngressRate() = 10;
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 2;
  *config.vlans()[1].id() = 5;
  config.vlanPorts()->resize(2);
  *config.vlanPorts()[0].logicalPort() = 1;
  *config.vlanPorts()[0].vlanID() = 2;
  *config.vlanPorts()[0].emitTags() = false;
  *config.vlanPorts()[1].logicalPort() = 1;
  *config.vlanPorts()[1].vlanID() = 5;
  *config.vlanPorts()[1].emitTags() = true;
  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 2;
  *config.interfaces()[0].vlanID() = 2;
  config.interfaces()[0].mac() = "00:00:00:00:00:22";
  *config.interfaces()[1].intfID() = 5;
  *config.interfaces()[1].vlanID() = 5;
  config.interfaces()[1].mac() = "00:00:00:00:00:55";

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto portV1 = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, portV1);
  EXPECT_NE(portV0, portV1);
  EXPECT_EQ(stateV1->getPorts()->getPort("port1"), portV1);

  EXPECT_EQ(PortID(1), portV1->getID());
  EXPECT_EQ("port1", portV1->getName());
  EXPECT_EQ(cfg::PortState::ENABLED, portV1->getAdminState());
  EXPECT_EQ(cfg::PortSpeed::XG, portV1->getSpeed());
  EXPECT_EQ(
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      portV1->getProfileID());
  EXPECT_FALSE(portV1->isPublished());
  Port::VlanMembership expectedVlans;
  expectedVlans.insert(make_pair(VlanID(2), Port::VlanInfo(false, false)));
  expectedVlans.insert(make_pair(VlanID(5), Port::VlanInfo(true, false)));
  EXPECT_EQ(expectedVlans, portV1->getVlans());
  EXPECT_TRUE(portV1->getSampleDestination().has_value());
  EXPECT_EQ(
      cfg::SampleDestination::MIRROR, portV1->getSampleDestination().value());

  // Applying the same config again should result in no changes
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV1, &config, platform.get()));

  // Applying the same config with a new VLAN list should result in changes
  config.vlanPorts()->resize(1);
  *config.vlanPorts()[0].logicalPort() = 1;
  *config.vlanPorts()[0].vlanID() = 2021;
  *config.vlanPorts()[0].emitTags() = false;
  config.vlans()->resize(3);
  *config.vlans()[2].id() = 2021;
  config.interfaces()->resize(3);
  *config.interfaces()[2].intfID() = 2021;
  *config.interfaces()[2].vlanID() = 2021;
  config.interfaces()[2].mac() = "00:00:00:00:00:21";

  Port::VlanMembership expectedVlansV2;
  expectedVlansV2.insert(make_pair(VlanID(2021), Port::VlanInfo(false, false)));
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  auto portV2 = stateV2->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, portV2);
  EXPECT_NE(portV1, portV2);

  EXPECT_EQ(PortID(1), portV2->getID());
  EXPECT_EQ("port1", portV2->getName());
  EXPECT_EQ(cfg::PortState::ENABLED, portV2->getAdminState());
  EXPECT_EQ(cfg::PortSpeed::XG, portV2->getSpeed());
  EXPECT_EQ(
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      portV2->getProfileID());
  EXPECT_FALSE(portV2->isPublished());
  EXPECT_EQ(expectedVlansV2, portV2->getVlans());
  EXPECT_TRUE(portV1->getSampleDestination().has_value());
  EXPECT_EQ(
      cfg::SampleDestination::MIRROR, portV1->getSampleDestination().value());

  // Applying the same config with a different speed should result in changes
  config.ports()[0].speed() = cfg::PortSpeed::TWENTYFIVEG;
  config.ports()[0].profileID() =
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER;

  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  auto portV3 = stateV3->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, portV3);
  EXPECT_NE(portV2, portV3);
  EXPECT_EQ(cfg::PortSpeed::TWENTYFIVEG, portV3->getSpeed());
  EXPECT_EQ(
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      portV3->getProfileID());
}

TEST(Port, emptyConfig) {
  auto platform = createMockPlatform();
  PortID portID(1);
  auto state = make_shared<SwitchState>();
  addSwitchInfo(state);
  registerPort(state, PortID(1), "port1", scope());
  auto port = state->getPorts()->getNodeIf(portID);
  prepareDefaultSwPort(platform.get(), port);
  // Make sure we also update the port queues to default queue so that the
  // config change won't be triggered because of empty queue cfg.
  QueueConfig queues;
  for (int i = 0;
       i < platform->getAsic()->getDefaultNumPortQueues(
               cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT);
       i++) {
    auto queue = std::make_shared<PortQueue>(static_cast<uint8_t>(i));
    queue->setStreamType(cfg::StreamType::UNICAST);
    queues.push_back(queue);
  }
  state->getPorts()->getNodeIf(portID)->resetPortQueues(queues);

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  EXPECT_NE(nullptr, publishAndApplyConfig(state, &config, platform.get()));

  // If platform does not support addRemovePort (by default),
  // empty config should throw exception.
  cfg::SwitchConfig emptyConfig;
  EXPECT_THROW(
      publishAndApplyConfig(state, &emptyConfig, platform.get()), FbossError);

  // If platform supports addRemovePort, change should happen.
  ON_CALL(*platform.get(), supportsAddRemovePort())
      .WillByDefault(testing::Return(true));
  EXPECT_NE(
      nullptr, publishAndApplyConfig(state, &emptyConfig, platform.get()));
}

// validate that pause & pfc should not be enabled
// at the same time
TEST(Port, verifyPfcWithPauseConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  cfg::PortPfc pfc;
  cfg::PortPause pause;

  // enable pfc, pause
  pfc.tx() = true;
  pause.rx() = true;

  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  config.ports()[0].pfc() = pfc;
  config.ports()[0].pause() = pause;

  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
}

TEST(Port, verifyPfcConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto port = state->getPorts()->getNodeIf(PortID(1));
  EXPECT_FALSE(port->getPfc().has_value());

  cfg::SwitchConfig config;
  cfg::PortPfc pfc;

  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  config.ports()[0].pfc() = pfc;
  auto newState = publishAndApplyConfig(state, &config, platform.get());

  port = newState->getPorts()->getNodeIf(PortID(1));
  EXPECT_TRUE(port->getPfc().has_value());
  EXPECT_FALSE(*port->getPfc().value().tx());
  EXPECT_FALSE(*port->getPfc().value().rx());
  EXPECT_EQ(port->getPfc().value().portPgConfigName(), "");
  EXPECT_FALSE(port->getPfc().value().watchdog().has_value());

  pfc.tx() = true;
  pfc.rx() = true;
  pfc.portPgConfigName() = "foo";

  cfg::PfcWatchdog watchdog;
  watchdog.detectionTimeMsecs() = 15;
  watchdog.recoveryTimeMsecs() = 16;
  watchdog.recoveryAction() = cfg::PfcWatchdogRecoveryAction::DROP;
  pfc.watchdog() = watchdog;

  config.ports()[0].pfc() = pfc;
  // pgConfigName exists, but can't be found in cfg, throw exception
  EXPECT_THROW(
      publishAndApplyConfig(newState, &config, platform.get()), FbossError);

  // create an empty pgConfig for "foo"
  std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
  portPgConfigMap["foo"] = {};
  config.portPgConfigs() = portPgConfigMap;

  auto newState2 = publishAndApplyConfig(newState, &config, platform.get());
  port = newState2->getPorts()->getNodeIf(PortID(1));

  EXPECT_TRUE(port->getPfc().has_value());
  EXPECT_TRUE(*port->getPfc().value().tx());
  EXPECT_TRUE(*port->getPfc().value().rx());
  EXPECT_EQ(port->getPfc().value().portPgConfigName(), "foo");

  EXPECT_TRUE(port->getPfc()->watchdog().has_value());
  auto pfcWatchdog = port->getPfc()->watchdog().value();
  EXPECT_EQ(15, *pfcWatchdog.detectionTimeMsecs());
  EXPECT_EQ(16, *pfcWatchdog.recoveryTimeMsecs());
  EXPECT_EQ(
      cfg::PfcWatchdogRecoveryAction::DROP, *pfcWatchdog.recoveryAction());

  // Modify watchdog config and make sure it gets saved
  port = newState2->getPorts()->getNodeIf(PortID(1));
  auto pfc2 = port->getPfc().value();
  cfg::PfcWatchdog watchdog2 = pfc2.watchdog().value();
  // Change the recoveryAction to NO_DROP
  watchdog2.recoveryAction() = cfg::PfcWatchdogRecoveryAction::NO_DROP;
  pfc2.watchdog() = watchdog2;
  config.ports()[0].pfc() = pfc2;

  auto newState3 = publishAndApplyConfig(newState2, &config, platform.get());
  port = newState3->getPorts()->getNodeIf(PortID(1));
  EXPECT_TRUE(port->getPfc()->watchdog().has_value());
  auto pfcWatchdog2 = port->getPfc()->watchdog().value();
  EXPECT_EQ(15, *pfcWatchdog2.detectionTimeMsecs());
  EXPECT_EQ(16, *pfcWatchdog2.recoveryTimeMsecs());
  // Verify that the recoveryAction is NO_DROP as expected
  EXPECT_EQ(
      cfg::PfcWatchdogRecoveryAction::NO_DROP, *pfcWatchdog2.recoveryAction());
}

TEST(Port, pauseConfig) {
  auto platform = createMockPlatform();
  cfg::SwitchConfig emptyCfg{};
  auto state = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &emptyCfg, platform.get());
  addSwitchInfo(state);
  auto ports = state->getPorts()->modify(&state);
  auto portID = PortID(1);
  registerPort(state, PortID(1), "port1", scope());
  auto port = ports->getNodeIf(portID);
  prepareDefaultSwPort(platform.get(), port);
  // Make sure we also update the port queues to default queue so that the
  // config change won't be triggered because of empty queue cfg
  QueueConfig queues;
  for (int i = 0;
       i < platform->getAsic()->getDefaultNumPortQueues(
               cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT);
       i++) {
    auto queue = std::make_shared<PortQueue>(static_cast<uint8_t>(i));
    queue->setStreamType(cfg::StreamType::UNICAST);
    queues.push_back(queue);
  }
  state->getPorts()->getNodeIf(PortID(1))->resetPortQueues(queues);

  auto verifyPause = [&state](cfg::PortPause expectPause) {
    auto currentPort = state->getPorts()->getNodeIf(PortID(1));
    auto pause = currentPort->getPause();
    EXPECT_EQ(expectPause, pause);
  };

  auto changePause = [&](cfg::PortPause newPause) {
    auto oldPause = state->getPorts()->getNodeIf(PortID(1))->getPause();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    config.ports()[0].pause() = newPause;
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldPause != newPause) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      verifyPause(newPause);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  // Verify the default pause config is no pause for either tx or rx
  cfg::PortPause expected;
  verifyPause(expected);

  // Now change it each time
  changePause(expected);

  *expected.tx() = false;
  *expected.rx() = true;
  changePause(expected);

  *expected.tx() = true;
  *expected.rx() = false;
  changePause(expected);

  *expected.tx() = true;
  *expected.rx() = true;
  changePause(expected);
}

TEST(Port, loopbackModeConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());
  auto verifyLoopbackMode = [&state](cfg::PortLoopbackMode expectLoopbackMode) {
    auto port = state->getPorts()->getNodeIf(PortID(1));
    auto loopbackMode = port->getLoopbackMode();
    EXPECT_EQ(expectLoopbackMode, loopbackMode);
  };

  auto changeAndVerifyLoopbackMode =
      [&](cfg::PortLoopbackMode newLoopbackMode) {
        auto oldLoopbackMode =
            state->getPorts()->getNodeIf(PortID(1))->getLoopbackMode();
        cfg::SwitchConfig config;
        config.ports()->resize(1);
        preparedMockPortConfig(
            config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
        *config.ports()[0].loopbackMode() = newLoopbackMode;
        auto newState = publishAndApplyConfig(state, &config, platform.get());

        if (oldLoopbackMode != newLoopbackMode) {
          EXPECT_NE(nullptr, newState);
          state = newState;
          verifyLoopbackMode(newLoopbackMode);
        } else {
          EXPECT_EQ(nullptr, newState);
        }
      };

  // Verify the default loopback mode is NONE
  cfg::PortLoopbackMode expected{cfg::PortLoopbackMode::NONE};
  verifyLoopbackMode(expected);

  // Now change it each time

  expected = cfg::PortLoopbackMode::PHY;
  changeAndVerifyLoopbackMode(expected);

  expected = cfg::PortLoopbackMode::MAC;
  changeAndVerifyLoopbackMode(expected);

  expected = cfg::PortLoopbackMode::NIF;
  changeAndVerifyLoopbackMode(expected);

  expected = cfg::PortLoopbackMode::NONE;
  changeAndVerifyLoopbackMode(expected);
}

TEST(Port, sampleDestinationConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());
  auto changeAndVerifySampleDestination =
      [](std::unique_ptr<MockPlatform>& platform,
         std::shared_ptr<SwitchState>& state,
         std::optional<cfg::SampleDestination> newDestination) {
        auto oldDestination =
            state->getPorts()->getNodeIf(PortID(1))->getSampleDestination();
        cfg::SwitchConfig config;
        config.ports()->resize(1);
        preparedMockPortConfig(
            config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
        if (newDestination.has_value()) {
          config.ports()[0].sampleDest() = newDestination.value();
        }
        auto newState = publishAndApplyConfig(state, &config, platform.get());

        if (oldDestination != newDestination) {
          EXPECT_NE(nullptr, newState);
          state = newState;
          auto portSampleDest =
              state->getPorts()->getNodeIf(PortID(1))->getSampleDestination();
          EXPECT_EQ(portSampleDest, newDestination);
        } else {
          EXPECT_EQ(nullptr, newState);
        }
      };

  // Verify the default sample destination is NONE
  EXPECT_EQ(
      std::nullopt,
      state->getPorts()->getNodeIf(PortID(1))->getSampleDestination());

  // Now change it and verify change is properly configured
  changeAndVerifySampleDestination(
      platform, state, cfg::SampleDestination::MIRROR);
  changeAndVerifySampleDestination(
      platform, state, cfg::SampleDestination::CPU);
  changeAndVerifySampleDestination(platform, state, std::nullopt);
}

/*
 * Test that forEachChanged(StateDelta::getNodeIfsDelta(), ...) invokes
 * the callback for the specified list of changed ports.
 */
void checkChangedPorts(
    const shared_ptr<MultiSwitchPortMap>& oldPorts,
    const shared_ptr<MultiSwitchPortMap>& newPorts,
    const std::set<uint16_t> changedIDs) {
  auto oldState = make_shared<SwitchState>();
  oldState->resetPorts(oldPorts);
  auto newState = make_shared<SwitchState>();
  newState->resetPorts(newPorts);

  std::set<uint16_t> invokedPorts;
  StateDelta delta(oldState, newState);
  DeltaFunctions::forEachChanged(
      delta.getPortsDelta(),
      [&](const shared_ptr<Port>& oldPort, const shared_ptr<Port>& newPort) {
        EXPECT_EQ(oldPort->getID(), newPort->getID());
        EXPECT_NE(oldPort, newPort);

        auto ret = invokedPorts.insert(oldPort->getID());
        EXPECT_TRUE(ret.second);
      });

  EXPECT_EQ(changedIDs, invokedPorts);
}

TEST(PortMap, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto portsV0 = stateV0->getPorts();
  addSwitchInfo(stateV0);
  auto registerPort = [&](int i) {
    state::PortFields portFields;
    portFields.portId() = PortID(i);
    portFields.portName() = fmt::format("port{}", i);
    auto port = std::make_shared<Port>(std::move(portFields));
    prepareDefaultSwPort(platform.get(), port);
    QueueConfig defaultQueues;
    for (int q = 0;
         q < platform->getAsic()->getDefaultNumPortQueues(
                 cfg::StreamType::UNICAST, cfg::PortType::INTERFACE_PORT);
         q++) {
      auto defaultQueue = std::make_shared<PortQueue>(static_cast<uint8_t>(q));
      defaultQueue->setStreamType(cfg::StreamType::UNICAST);
      defaultQueues.push_back(defaultQueue);
    }
    port->resetPortQueues(defaultQueues);
    portsV0->addNode(port, scope());
  };
  for (int i = 1; i <= 4; i++) {
    registerPort(i);
  }
  portsV0->publish();

  validateThriftMapMapSerialization(*portsV0);

  auto port1 = portsV0->getNodeIf(PortID(1));
  auto port2 = portsV0->getNodeIf(PortID(2));
  auto port3 = portsV0->getNodeIf(PortID(3));
  auto port4 = portsV0->getNodeIf(PortID(4));

  cfg::SwitchConfig config;
  config.ports()->resize(4);
  for (int i = 0; i < 4; ++i) {
    preparedMockPortConfig(
        config.ports()[i],
        i + 1,
        fmt::format("port{}", i + 1),
        cfg::PortState::DISABLED);
  }
  EXPECT_NE(nullptr, publishAndApplyConfig(stateV0, &config, platform.get()));

  // Enable port 2
  *config.ports()[1].state() = cfg::PortState::ENABLED;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto portsV1 = stateV1->getPorts();
  ASSERT_NE(nullptr, portsV1);
  EXPECT_EQ(4, portsV1->numNodes());

  // Only port 2 should have changed
  EXPECT_EQ(port1, portsV1->getNodeIf(PortID(1)));
  EXPECT_NE(port2, portsV1->getNodeIf(PortID(2)));
  EXPECT_EQ(port3, portsV1->getNodeIf(PortID(3)));
  EXPECT_EQ(port4, portsV1->getNodeIf(PortID(4)));
  checkChangedPorts(portsV0, portsV1, {2});

  auto newPort2 = portsV1->getNodeIf(PortID(2));
  EXPECT_EQ(cfg::PortState::ENABLED, newPort2->getAdminState());
  EXPECT_EQ(cfg::PortState::DISABLED, port1->getAdminState());
  EXPECT_EQ(cfg::PortState::DISABLED, port3->getAdminState());
  EXPECT_EQ(cfg::PortState::DISABLED, port4->getAdminState());

  // The new PortMap and port 2 should still be unpublished.
  // The remaining other ports are the same and were previously published
  EXPECT_FALSE(portsV1->isPublished());
  EXPECT_FALSE(newPort2->isPublished());
  EXPECT_TRUE(port1->isPublished());
  // Publish portsV1 now.
  portsV1->publish();
  EXPECT_TRUE(portsV1->isPublished());
  EXPECT_TRUE(newPort2->isPublished());
  EXPECT_TRUE(port1->isPublished());

  validateThriftMapMapSerialization(*portsV1);

  // Applying the same config again should do nothing.
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV1, &config, platform.get()));

  // Now mark all ports up
  *config.ports()[0].state() = cfg::PortState::ENABLED;
  *config.ports()[1].state() = cfg::PortState::ENABLED;
  *config.ports()[2].state() = cfg::PortState::ENABLED;
  *config.ports()[3].state() = cfg::PortState::ENABLED;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  auto portsV2 = stateV2->getPorts();
  ASSERT_NE(nullptr, portsV2);

  EXPECT_NE(port1, portsV2->getNodeIf(PortID(1)));
  EXPECT_EQ(newPort2, portsV2->getNodeIf(PortID(2)));
  EXPECT_NE(port3, portsV2->getNodeIf(PortID(3)));
  EXPECT_NE(port4, portsV2->getNodeIf(PortID(4)));

  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV2->getNodeIf(PortID(1))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV2->getNodeIf(PortID(2))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV2->getNodeIf(PortID(3))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV2->getNodeIf(PortID(4))->getAdminState());
  checkChangedPorts(portsV1, portsV2, {1, 3, 4});

  EXPECT_FALSE(portsV2->getNodeIf(PortID(1))->isPublished());
  EXPECT_TRUE(portsV2->getNodeIf(PortID(2))->isPublished());
  EXPECT_FALSE(portsV2->getNodeIf(PortID(3))->isPublished());
  EXPECT_FALSE(portsV2->getNodeIf(PortID(4))->isPublished());
  portsV2->publish();
  EXPECT_TRUE(portsV2->getNodeIf(PortID(1))->isPublished());
  EXPECT_TRUE(portsV2->getNodeIf(PortID(2))->isPublished());
  EXPECT_TRUE(portsV2->getNodeIf(PortID(3))->isPublished());
  EXPECT_TRUE(portsV2->getNodeIf(PortID(4))->isPublished());

  // If we disable port3 from the config, it should be marked down
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  preparedMockPortConfig(
      config.ports()[2], 3, "port3", cfg::PortState::DISABLED);
  preparedMockPortConfig(config.ports()[3], 4);
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  auto portsV3 = stateV3->getPorts();
  ASSERT_NE(nullptr, portsV3);

  EXPECT_EQ(4, portsV3->numNodes());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV3->getNodeIf(PortID(1))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV3->getNodeIf(PortID(2))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::DISABLED, portsV3->getNodeIf(PortID(3))->getAdminState());
  EXPECT_EQ(
      cfg::PortState::ENABLED, portsV3->getNodeIf(PortID(4))->getAdminState());
  checkChangedPorts(portsV2, portsV3, {3});
  validateThriftMapMapSerialization(*portsV3);
}

TEST(PortMap, iterateOrder) {
  // The NodeMapDelta::Iterator code assumes that the PortMap iterator walks
  // through the ports in sorted order (sorted by PortID).
  //
  // Add a test to ensure that this always remains true.  (If we ever change
  // the underlying map data structure used for PortMap, we will need to update
  // the StateDelta code.)
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(99), "a", scope());
  registerPort(state, PortID(37), "b", scope());
  registerPort(state, PortID(88), "c", scope());
  registerPort(state, PortID(4), "d", scope());
  state->publish();

  auto ports = state->getPorts()->cbegin()->second;

  auto it = ports->cbegin();
  ASSERT_NE(ports->cend(), it);
  EXPECT_EQ(PortID(4), it->second->getID());
  EXPECT_EQ("d", it->second->getName());

  ++it;
  ASSERT_NE(ports->cend(), it);
  EXPECT_EQ(PortID(37), it->second->getID());
  EXPECT_EQ("b", it->second->getName());

  ++it;
  ASSERT_NE(ports->cend(), it);
  EXPECT_EQ(PortID(88), it->second->getID());
  EXPECT_EQ("c", it->second->getName());

  ++it;
  ASSERT_NE(ports->cend(), it);
  EXPECT_EQ(PortID(99), it->second->getID());
  EXPECT_EQ("a", it->second->getName());

  ++it;
  EXPECT_EQ(ports->cend(), it);

  validateThriftMapMapSerialization(*ports);
}

TEST(Port, portFabricType) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (auto portMap : *stateV1->getPorts()) {
    for (auto port : *portMap.second) {
      EXPECT_EQ(port.second->getPortType(), cfg::PortType::INTERFACE_PORT);
      auto newPort = port.second->clone();
      newPort->setPortType(cfg::PortType::FABRIC_PORT);
      auto newerPort = std::make_shared<Port>(newPort->toThrift());
      EXPECT_EQ(newerPort->getPortType(), cfg::PortType::FABRIC_PORT);
    }
  }
}

TEST(Port, portDrainInterfacePort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  config.ports()[0].portType() = cfg::PortType::INTERFACE_PORT;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(port->getPortDrainState(), cfg::PortDrainState::UNDRAINED);

  config.ports()[0].drainState() = cfg::PortDrainState::DRAINED;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  port = stateV2->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(port->getPortDrainState(), cfg::PortDrainState::DRAINED);
}

TEST(Port, portDrainFabricPort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  config.ports()[0].portType() = cfg::PortType::FABRIC_PORT;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(port->getPortDrainState(), cfg::PortDrainState::UNDRAINED);

  config.ports()[0].drainState() = cfg::PortDrainState::DRAINED;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  port = stateV2->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(port->getPortDrainState(), cfg::PortDrainState::DRAINED);
}

TEST(Port, portDrainManagementPort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  config.ports()->resize(1);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  config.ports()[0].portType() = cfg::PortType::MANAGEMENT_PORT;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(port->getPortDrainState(), cfg::PortDrainState::UNDRAINED);

  config.ports()[0].drainState() = cfg::PortDrainState::DRAINED;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
}

TEST(Port, portFabricTypeApplyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (auto& port : *config.ports()) {
    EXPECT_EQ(port.portType(), cfg::PortType::INTERFACE_PORT);
    port.portType() = cfg::PortType::FABRIC_PORT;
  }
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  for (auto portMap : *stateV2->getPorts()) {
    for (auto port : *portMap.second) {
      EXPECT_EQ(port.second->getPortType(), cfg::PortType::FABRIC_PORT);
      EXPECT_NE(
          *port.second, *stateV1->getPorts()->getNodeIf(port.second->getID()));
    }
  }
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV2, &config, platform.get()));
  // Flip back to intefac_port type
  for (auto& port : *config.ports()) {
    port.portType() = cfg::PortType::INTERFACE_PORT;
  }
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  for (auto portMap : std::as_const(*stateV3->getPorts())) {
    for (auto port : *portMap.second) {
      EXPECT_EQ(port.second->getPortType(), cfg::PortType::INTERFACE_PORT);
      EXPECT_NE(
          *port.second, *stateV2->getPorts()->getNodeIf(port.second->getID()));
    }
  }
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV3, &config, platform.get()));
}

TEST(Port, portSerilization) {
  state::PortFields portFields;
  portFields.portId() = PortID(42);
  portFields.portName() = "test_port";
  auto port = std::make_shared<Port>(std::move(portFields));
  EXPECT_EQ(port->getName(), "test_port");
  EXPECT_EQ(port->getID(), PortID(42));

  // IphyLinkStatus
  EXPECT_EQ(port->getIPhyLinkFaultStatus(), std::nullopt);
  phy::LinkFaultStatus linkFaultStatus;
  linkFaultStatus.localFault() = true;
  port->setIPhyLinkFaultStatus(linkFaultStatus);
  EXPECT_TRUE(port->getIPhyLinkFaultStatus().has_value());
  EXPECT_EQ(port->getIPhyLinkFaultStatus().value(), linkFaultStatus);

  // AsicPrbs, GbSystemPrbs & GbLinePrbs
  phy::PortPrbsState prbsState;
  EXPECT_EQ(port->getAsicPrbs(), prbsState);
  EXPECT_EQ(port->getGbSystemPrbs(), prbsState);
  EXPECT_EQ(port->getGbLinePrbs(), prbsState);
  prbsState.polynominal() = 42;
  port->setAsicPrbs(prbsState);
  port->setGbSystemPrbs(prbsState);
  port->setGbLinePrbs(prbsState);
  EXPECT_EQ(port->getAsicPrbs(), prbsState);
  EXPECT_EQ(port->getGbSystemPrbs(), prbsState);
  EXPECT_EQ(port->getGbLinePrbs(), prbsState);

  // Pfc Priorities
  EXPECT_TRUE(port->getPfcPriorities().empty());
  std::optional<std::vector<int16_t>> pfcPriorities{{42}};
  port->setPfcPriorities(pfcPriorities);
  EXPECT_TRUE(!port->getPfcPriorities().empty());
  EXPECT_EQ(port->getPfcPriorities().size(), 1);

  // expected LLDP values
  EXPECT_TRUE(port->getLLDPValidations().empty());
  PortFields::LLDPValidations lldpMap{{cfg::LLDPTag::TTL, "ttlTag"}};
  port->setExpectedLLDPValues(lldpMap);
  EXPECT_EQ(port->getLLDPValidations().size(), 1);

  // expected Neighbor reachability values
  EXPECT_TRUE(port->getExpectedNeighborValues()->empty());
  cfg::PortNeighbor nbr;
  nbr.remoteSystem() = "RemoteA";
  nbr.remotePort() = "portA";
  port->setExpectedNeighborReachability({nbr});
  EXPECT_EQ(port->getExpectedNeighborValues()->size(), 1);

  // RxSaks
  EXPECT_TRUE(
      port->cref<switch_state_tags::rxSecureAssociationKeys>()->empty());
  state::MKASakKey sakKey;
  sakKey.sci() = mka::MKASci{};
  sakKey.associationNum() = 42;
  state::RxSak rxSak;
  rxSak.sakKey() = sakKey;
  rxSak.sak() = mka::MKASak{};
  std::vector<state::RxSak> rxSaks{rxSak};
  port->set<switch_state_tags::rxSecureAssociationKeys>(rxSaks);
  EXPECT_EQ(
      port->cref<switch_state_tags::rxSecureAssociationKeys>()->size(), 1);

  // txSecureAssociationKey
  EXPECT_EQ(port->getTxSak(), std::nullopt);
  port->setTxSak(mka::MKASak{});
  EXPECT_TRUE(port->getTxSak().has_value());

  EXPECT_FALSE(port->getMacsecDesired());
  EXPECT_FALSE(port->getDropUnencrypted());
  port->setMacsecDesired(true);
  port->setDropUnencrypted(true);
  EXPECT_TRUE(port->getMacsecDesired());
  EXPECT_TRUE(port->getDropUnencrypted());

  validateNodeSerialization(*port);
}

TEST(Port, verifyInterfaceIDsForNonVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto getExpectedPort2Interface = [](const auto& config) {
    flat_map<VlanID, int32_t> vlan2Interface;
    for (const auto& interfaceCfg : *config.interfaces()) {
      vlan2Interface[VlanID(*interfaceCfg.vlanID())] = *interfaceCfg.intfID();
    }

    flat_map<PortID, Port::VlanMembership> port2Vlans;
    for (const auto& vp : *config.vlanPorts()) {
      PortID portID(*vp.logicalPort());
      VlanID vlanID(*vp.vlanID());

      port2Vlans[portID].insert(
          std::make_pair(vlanID, Port::VlanInfo(*vp.emitTags(), false)));
    }

    flat_map<PortID, int32_t> port2Interface;
    for (const auto& portCfg : *config.ports()) {
      auto portID = PortID(*portCfg.logicalID());
      for (const auto& [vlanID, vlanInfo] : port2Vlans[portID]) {
        auto it = vlan2Interface.find(vlanID);
        EXPECT_TRUE(it != vlan2Interface.end());
        port2Interface.insert(std::make_pair(portID, it->second));
      }
    }
    return port2Interface;
  };

  auto expectedPort2Interface = getExpectedPort2Interface(config);

  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      auto expectedIntfID = InterfaceID(expectedPort2Interface[portID]);
      EXPECT_EQ(expectedIntfID, port.second->getInterfaceID());
    }
  }
}

// A port with no interface must be reported as such rather than CHECK-failing
// in Port::getInterfaceID(). The RX path relies on this to drop the packet.
TEST(Port, getInterfaceIDForPortWithNoInterface) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto state = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, state);

  auto portID = PortID(*config.ports()[0].logicalID());
  ASSERT_EQ(1u, state->getPorts()->getNodeIf(portID)->getInterfaceIDs().size());

  state->publish();
  state->getPorts()->getNodeIf(portID)->modify(&state)->setInterfaceIDs({});
  ASSERT_TRUE(state->getPorts()->getNodeIf(portID)->getInterfaceIDs().empty());

  EXPECT_EQ(
      std::nullopt, state->getInterfaceIDForPortIf(PortDescriptor(portID)));
  // NOLINTNEXTLINE(modernize-type-traits): gtest EXPECT_THROW internals
  EXPECT_THROW(
      state->getInterfaceIDForPort(PortDescriptor(portID)), FbossError);
}

TEST(Port, verifyInterfaceIDsForVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  SwitchIdScopeResolver scopeResolver(
      *config.switchSettings()->switchIdToSwitchInfo());
  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      auto sysPortId = getSystemPortID(
          portID, stateV1, scopeResolver.scope(portID).switchId());
      auto expectedIntfID = InterfaceID(static_cast<uint32_t>(sysPortId));
      EXPECT_EQ(expectedIntfID, port.second->getInterfaceID());
    }
  }
}

TEST(Port, verifySysPortRangeForNonVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      EXPECT_TRUE(stateV1->getAssociatedSystemPortRangesIf(portID)
                      .systemPortRanges()
                      ->empty());
    }
  }
}

TEST(Port, verifySwitchIdForVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      EXPECT_EQ(
          stateV1->getAssociatedSwitchID(portID), SwitchID(kVoqSwitchIdBegin));
    }
  }
}

TEST(Port, verifySwitchIdForNonVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      EXPECT_THROW(stateV1->getAssociatedSwitchID(portID), FbossError);
    }
  }
}

TEST(Port, verifySysPortRangeForVoqSwitches) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);

  auto config = testConfigA(cfg::SwitchType::VOQ);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (const auto& portMap : std::as_const(*(stateV1->getPorts()))) {
    for (auto port : *portMap.second) {
      auto portID = port.second->getID();
      EXPECT_FALSE(stateV1->getAssociatedSystemPortRangesIf(portID)
                       .systemPortRanges()
                       ->empty());
    }
  }
}
TEST(Port, verifyNeighborReachability) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);

  cfg::PortNeighbor nbr;
  nbr.remoteSystem() = "RemoteA";
  nbr.remotePort() = "portA";

  config.ports()[0].expectedNeighborReachability() = {nbr};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (const auto& nbrIter : *(stateV1->getPorts()
                                   ->getNodeIf(PortID(1))
                                   ->getExpectedNeighborValues())) {
    EXPECT_EQ(
        nbrIter->cref<switch_config_tags::remoteSystem>()->toThrift(),
        "RemoteA");
    EXPECT_EQ(
        nbrIter->cref<switch_config_tags::remotePort>()->toThrift(), "portA");
  }
}

TEST(Port, portDrainState) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (auto portMap : *stateV1->getPorts()) {
    for (auto port : *portMap.second) {
      EXPECT_EQ(
          port.second->getPortDrainState(), cfg::PortDrainState::UNDRAINED);
      auto newPort = port.second->clone();
      newPort->setPortDrainState(cfg::PortDrainState::DRAINED);
      auto newerPort = std::make_shared<Port>(newPort->toThrift());
      EXPECT_EQ(newerPort->getPortDrainState(), cfg::PortDrainState::DRAINED);
    }
  }
}

TEST(Port, portModifyUnpublished) {
  auto state = make_shared<SwitchState>();
  state::PortFields portFields;
  portFields.portId() = PortID(42);
  portFields.portName() = "test_port";
  auto port = std::make_shared<Port>(std::move(portFields));
  state->getPorts()->addNode(port, scope());
  EXPECT_EQ(port.get(), port->modify(&state));
}

TEST(Port, portModifyPublished) {
  auto state = make_shared<SwitchState>();
  state::PortFields portFields;
  portFields.portId() = PortID(42);
  portFields.portName() = "test_port";
  auto port = std::make_shared<Port>(std::move(portFields));
  state->getPorts()->addNode(port, scope());
  state->publish();
  EXPECT_NE(port.get(), port->modify(&state));
}

TEST(Port, portErrors) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = testConfigA();

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  for (auto portMap : *stateV1->getPorts()) {
    for (auto [_, port] : *portMap.second) {
      EXPECT_TRUE(port->getActiveErrors().empty());
      auto newPort = port->clone();
      std::vector<PortError> expectedErrors{
          PortError::ERROR_DISABLE_LOOP_DETECTED};
      newPort->addError(PortError::ERROR_DISABLE_LOOP_DETECTED);
      EXPECT_EQ(newPort->getActiveErrors(), expectedErrors);
      {
        auto newerPort = std::make_shared<Port>(newPort->toThrift());
        EXPECT_EQ(newerPort->getActiveErrors(), expectedErrors);
      }
      newPort->removeError(PortError::ERROR_DISABLE_LOOP_DETECTED);
      EXPECT_TRUE(newPort->getActiveErrors().empty());
      {
        auto newerPort = std::make_shared<Port>(newPort->toThrift());
        EXPECT_TRUE(newerPort->getActiveErrors().empty());
      }
    }
  }
}

// Test interPacketGapBits configuration: default value, config changes,
// getter/setter methods, and serialization/deserialization
TEST(Port, interPacketGapBitsConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto changeAndVerifyInterPacketGapBits =
      [](std::unique_ptr<MockPlatform>& platform,
         std::shared_ptr<SwitchState>& state,
         std::optional<int32_t> newInterPacketGapBits) {
        auto oldInterPacketGapBits =
            state->getPorts()->getNodeIf(PortID(1))->getInterPacketGapBits();
        cfg::SwitchConfig config;
        config.ports()->resize(1);
        preparedMockPortConfig(
            config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
        if (newInterPacketGapBits.has_value()) {
          config.ports()[0].interPacketGapBits() =
              newInterPacketGapBits.value();
        }
        auto newState = publishAndApplyConfig(state, &config, platform.get());

        if (oldInterPacketGapBits != newInterPacketGapBits) {
          EXPECT_NE(nullptr, newState);
          state = newState;
          auto portInterPacketGapBits =
              state->getPorts()->getNodeIf(PortID(1))->getInterPacketGapBits();
          EXPECT_EQ(portInterPacketGapBits, newInterPacketGapBits);
        } else {
          EXPECT_EQ(nullptr, newState);
        }
      };

  // Verify the default interPacketGapBits is nullopt
  EXPECT_EQ(
      std::nullopt,
      state->getPorts()->getNodeIf(PortID(1))->getInterPacketGapBits());

  // Test setting various values and verify changes are properly configured
  changeAndVerifyInterPacketGapBits(platform, state, 96);
  changeAndVerifyInterPacketGapBits(platform, state, 64);
  changeAndVerifyInterPacketGapBits(platform, state, 128);
  changeAndVerifyInterPacketGapBits(platform, state, std::nullopt);

  // Test direct getter/setter methods
  auto port = state->getPorts()->getNodeIf(PortID(1));
  auto newPort = port->clone();

  // Test setting a value
  newPort->setInterPacketGapBits(96);
  EXPECT_TRUE(newPort->getInterPacketGapBits().has_value());
  EXPECT_EQ(newPort->getInterPacketGapBits().value(), 96);

  // Test setting nullopt
  newPort->setInterPacketGapBits(std::nullopt);
  EXPECT_FALSE(newPort->getInterPacketGapBits().has_value());

  // Test serialization/deserialization
  newPort->setInterPacketGapBits(64);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_TRUE(serializedPort->getInterPacketGapBits().has_value());
  EXPECT_EQ(serializedPort->getInterPacketGapBits().value(), 64);
}

// Test amIdles configuration: default value, config changes,
// getter/setter methods, and serialization/deserialization
TEST(Port, amIdlesConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto changeAndVerifyAmIdles = [](std::unique_ptr<MockPlatform>& platform,
                                   std::shared_ptr<SwitchState>& state,
                                   std::optional<bool> newAmIdles) {
    auto oldAmIdles = state->getPorts()->getNodeIf(PortID(1))->getAmIdles();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (newAmIdles.has_value()) {
      config.ports()[0].amIdles() = newAmIdles.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldAmIdles != newAmIdles) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      auto portAmIdles = state->getPorts()->getNodeIf(PortID(1))->getAmIdles();
      EXPECT_EQ(portAmIdles, newAmIdles);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  // Verify the default amIdles is nullopt
  EXPECT_EQ(
      std::nullopt, state->getPorts()->getNodeIf(PortID(1))->getAmIdles());

  // Test setting various values and verify changes are properly configured
  changeAndVerifyAmIdles(platform, state, false); // nullopt → false
  changeAndVerifyAmIdles(platform, state, true); // false → true
  changeAndVerifyAmIdles(platform, state, false); // true → false
  changeAndVerifyAmIdles(platform, state, std::nullopt); // false → nullopt
  changeAndVerifyAmIdles(platform, state, true); // nullopt → true
  changeAndVerifyAmIdles(platform, state, std::nullopt); // true → nullopt

  // Test direct getter/setter methods
  auto port = state->getPorts()->getNodeIf(PortID(1));
  auto newPort = port->clone();

  // Test setting true
  newPort->setAmIdles(true);
  EXPECT_TRUE(newPort->getAmIdles().has_value());
  EXPECT_EQ(newPort->getAmIdles().value(), true);

  // Test setting false
  newPort->setAmIdles(false);
  EXPECT_TRUE(newPort->getAmIdles().has_value());
  EXPECT_EQ(newPort->getAmIdles().value(), false);

  // Test setting nullopt
  newPort->setAmIdles(std::nullopt);
  EXPECT_FALSE(newPort->getAmIdles().has_value());

  // Test serialization/deserialization
  newPort->setAmIdles(true);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_TRUE(serializedPort->getAmIdles().has_value());
  EXPECT_EQ(serializedPort->getAmIdles().value(), true);
}

// Test clmEnable (Cable Length Measurement) configuration: default value,
// config changes, getter/setter methods, and serialization/deserialization
TEST(Port, clmEnableConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto changeAndVerifyClmEnable = [](std::unique_ptr<MockPlatform>& platform,
                                     std::shared_ptr<SwitchState>& state,
                                     std::optional<bool> newClmEnable) {
    auto oldClmEnable = state->getPorts()->getNodeIf(PortID(1))->getClmEnable();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (newClmEnable.has_value()) {
      config.ports()[0].clmEnable() = newClmEnable.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldClmEnable != newClmEnable) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      auto portClmEnable =
          state->getPorts()->getNodeIf(PortID(1))->getClmEnable();
      EXPECT_EQ(portClmEnable, newClmEnable);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  // Verify the default clmEnable is nullopt
  EXPECT_EQ(
      std::nullopt, state->getPorts()->getNodeIf(PortID(1))->getClmEnable());

  // Test setting various values and verify changes are properly configured
  changeAndVerifyClmEnable(platform, state, false); // nullopt → false
  changeAndVerifyClmEnable(platform, state, true); // false → true
  changeAndVerifyClmEnable(platform, state, false); // true → false
  changeAndVerifyClmEnable(platform, state, std::nullopt); // false → nullopt
  changeAndVerifyClmEnable(platform, state, true); // nullopt → true
  changeAndVerifyClmEnable(platform, state, std::nullopt); // true → nullopt

  // Test direct getter/setter methods
  auto port = state->getPorts()->getNodeIf(PortID(1));
  auto newPort = port->clone();

  // Test setting true
  newPort->setClmEnable(true);
  EXPECT_TRUE(newPort->getClmEnable().has_value());
  EXPECT_EQ(newPort->getClmEnable().value(), true);

  // Test setting false
  newPort->setClmEnable(false);
  EXPECT_TRUE(newPort->getClmEnable().has_value());
  EXPECT_EQ(newPort->getClmEnable().value(), false);

  // Test setting nullopt
  newPort->setClmEnable(std::nullopt);
  EXPECT_FALSE(newPort->getClmEnable().has_value());

  // Test serialization/deserialization
  newPort->setClmEnable(true);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_TRUE(serializedPort->getClmEnable().has_value());
  EXPECT_EQ(serializedPort->getClmEnable().value(), true);
}

// Test linkTraining (Link Training IEEE 802.3 Cl.72/93/162) configuration:
// default value, config changes, getter/setter methods, and
// serialization/deserialization
TEST(Port, linkTrainingConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto changeAndVerifyLinkTraining = [](std::unique_ptr<MockPlatform>& platform,
                                        std::shared_ptr<SwitchState>& state,
                                        std::optional<bool> newLinkTraining) {
    auto oldLinkTraining =
        state->getPorts()->getNodeIf(PortID(1))->getLinkTraining();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (newLinkTraining.has_value()) {
      config.ports()[0].linkTraining() = newLinkTraining.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldLinkTraining != newLinkTraining) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      auto portLinkTraining =
          state->getPorts()->getNodeIf(PortID(1))->getLinkTraining();
      EXPECT_EQ(portLinkTraining, newLinkTraining);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  // Verify the default linkTraining is nullopt
  EXPECT_EQ(
      std::nullopt, state->getPorts()->getNodeIf(PortID(1))->getLinkTraining());

  // Test setting various values and verify changes are properly configured
  changeAndVerifyLinkTraining(platform, state, false);
  changeAndVerifyLinkTraining(platform, state, true);
  changeAndVerifyLinkTraining(platform, state, false);
  changeAndVerifyLinkTraining(platform, state, std::nullopt);
  changeAndVerifyLinkTraining(platform, state, true);
  changeAndVerifyLinkTraining(platform, state, std::nullopt);

  // Test direct getter/setter methods
  auto port = state->getPorts()->getNodeIf(PortID(1));
  auto newPort = port->clone();

  newPort->setLinkTraining(true);
  EXPECT_TRUE(newPort->getLinkTraining().has_value());
  EXPECT_EQ(newPort->getLinkTraining().value(), true);

  newPort->setLinkTraining(false);
  EXPECT_TRUE(newPort->getLinkTraining().has_value());
  EXPECT_EQ(newPort->getLinkTraining().value(), false);

  newPort->setLinkTraining(std::nullopt);
  EXPECT_FALSE(newPort->getLinkTraining().has_value());

  // Test serialization/deserialization
  newPort->setLinkTraining(true);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_TRUE(serializedPort->getLinkTraining().has_value());
  EXPECT_EQ(serializedPort->getLinkTraining().value(), true);
}

TEST(Port, precodingConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto changeAndVerifyPrecoding = [](std::unique_ptr<MockPlatform>& platform,
                                     std::shared_ptr<SwitchState>& state,
                                     std::optional<bool> newTxPrecoding,
                                     std::optional<bool> newRxPrecoding) {
    auto port = state->getPorts()->getNodeIf(PortID(1));
    auto oldTxPrecoding = port->getTxPrecoding();
    auto oldRxPrecoding = port->getRxPrecoding();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (newTxPrecoding.has_value()) {
      config.ports()[0].txPrecoding() = newTxPrecoding.value();
    }
    if (newRxPrecoding.has_value()) {
      config.ports()[0].rxPrecoding() = newRxPrecoding.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldTxPrecoding != newTxPrecoding || oldRxPrecoding != newRxPrecoding) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      auto newPort = state->getPorts()->getNodeIf(PortID(1));
      EXPECT_EQ(newPort->getTxPrecoding(), newTxPrecoding);
      EXPECT_EQ(newPort->getRxPrecoding(), newRxPrecoding);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  auto port = state->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(std::nullopt, port->getTxPrecoding());
  EXPECT_EQ(std::nullopt, port->getRxPrecoding());

  changeAndVerifyPrecoding(platform, state, false, std::nullopt);
  changeAndVerifyPrecoding(platform, state, false, true);
  changeAndVerifyPrecoding(platform, state, true, false);
  changeAndVerifyPrecoding(platform, state, std::nullopt, false);
  changeAndVerifyPrecoding(platform, state, std::nullopt, std::nullopt);

  port = state->getPorts()->getNodeIf(PortID(1));
  auto newPort = port->clone();

  newPort->setTxPrecoding(true);
  EXPECT_EQ(newPort->getTxPrecoding(), true);
  EXPECT_EQ(newPort->getRxPrecoding(), std::nullopt);

  newPort->setRxPrecoding(false);
  EXPECT_EQ(newPort->getTxPrecoding(), true);
  EXPECT_EQ(newPort->getRxPrecoding(), false);

  newPort->setTxPrecoding(std::nullopt);
  EXPECT_EQ(newPort->getTxPrecoding(), std::nullopt);
  EXPECT_EQ(newPort->getRxPrecoding(), false);

  newPort->setTxPrecoding(false);
  newPort->setRxPrecoding(true);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_EQ(serializedPort->getTxPrecoding(), false);
  EXPECT_EQ(serializedPort->getRxPrecoding(), true);
}

TEST(Port, rejectsLinkTrainingWithPrecoding) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto expectInvalidConfig = [&](bool txPrecoding, bool rxPrecoding) {
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    config.ports()[0].linkTraining() = true;
    config.ports()[0].txPrecoding() = txPrecoding;
    config.ports()[0].rxPrecoding() = rxPrecoding;

    EXPECT_THROW(
        publishAndApplyConfig(state, &config, platform.get()), FbossError);
  };

  expectInvalidConfig(true, false);
  expectInvalidConfig(false, true);
}

TEST(Port, linkScanModeConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto applyAndVerify = [&](std::optional<cfg::LinkScanMode> newMode) {
    auto oldMode = state->getPorts()->getNodeIf(PortID(1))->getLinkScanMode();
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (newMode.has_value()) {
      config.ports()[0].linkScanMode() = newMode.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    if (oldMode == newMode) {
      EXPECT_EQ(nullptr, newState);
      return;
    }
    ASSERT_NE(nullptr, newState);
    state = newState;
    EXPECT_EQ(
        state->getPorts()->getNodeIf(PortID(1))->getLinkScanMode(), newMode);
  };

  EXPECT_EQ(
      std::nullopt, state->getPorts()->getNodeIf(PortID(1))->getLinkScanMode());

  applyAndVerify(cfg::LinkScanMode::HARDWARE);
  applyAndVerify(cfg::LinkScanMode::SOFTWARE);
  applyAndVerify(cfg::LinkScanMode::SOFTWARE);
  // Dropping the field from config clears it from switch state
  applyAndVerify(std::nullopt);

  auto newPort = state->getPorts()->getNodeIf(PortID(1))->clone();
  newPort->setLinkScanMode(cfg::LinkScanMode::HARDWARE);
  auto serializedPort = std::make_shared<Port>(newPort->toThrift());
  EXPECT_EQ(serializedPort->getLinkScanMode(), cfg::LinkScanMode::HARDWARE);

  newPort->setLinkScanMode(std::nullopt);
  EXPECT_FALSE(newPort->toThrift().linkScanMode().has_value());
}

TEST(Port, userMetaDataConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto applyAndVerify =
      [&](std::optional<cfg::AclLookupClassPort> newMetadata) {
        auto oldMetadata =
            state->getPorts()->getNodeIf(PortID(1))->getUserMetaData();
        cfg::SwitchConfig config;
        config.ports()->resize(1);
        preparedMockPortConfig(
            config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
        if (newMetadata.has_value()) {
          config.ports()[0].userMetaData() = *newMetadata;
        }
        auto newState = publishAndApplyConfig(state, &config, platform.get());

        if (oldMetadata == newMetadata) {
          EXPECT_EQ(nullptr, newState);
          return;
        }
        ASSERT_NE(nullptr, newState);
        state = newState;
        EXPECT_EQ(
            state->getPorts()->getNodeIf(PortID(1))->getUserMetaData(),
            newMetadata);
      };

  applyAndVerify(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  applyAndVerify(cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  applyAndVerify(cfg::AclLookupClassPort::CLASS_PORT_BLOCKED);
  applyAndVerify(std::nullopt);

  auto cloned = state->getPorts()->getNodeIf(PortID(1))->clone();
  cloned->setUserMetaData(cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  auto serialized = std::make_shared<Port>(cloned->toThrift());
  EXPECT_EQ(
      serialized->getUserMetaData(),
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
}

// Test holdoff timer fields: default values, applyConfig propagation,
// getter/setter methods, and serialization/deserialization.
TEST(Port, holdoffTimerConfig) {
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());

  auto applyAndVerify = [&](std::optional<int32_t> downMs,
                            std::optional<int32_t> upMs) {
    auto port = state->getPorts()->getNodeIf(PortID(1));
    auto oldDown = port->getPortDownHoldoffTimeMs();
    auto oldUp = port->getPortUpHoldoffTimeMs();

    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(
        config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
    if (downMs.has_value()) {
      config.ports()[0].portDownHoldoffTimeMs() = downMs.value();
    }
    if (upMs.has_value()) {
      config.ports()[0].portUpHoldoffTimeMs() = upMs.value();
    }
    auto newState = publishAndApplyConfig(state, &config, platform.get());

    bool changed = oldDown != downMs || oldUp != upMs;
    if (changed) {
      EXPECT_NE(nullptr, newState);
      state = newState;
      auto updated = state->getPorts()->getNodeIf(PortID(1));
      EXPECT_EQ(updated->getPortDownHoldoffTimeMs(), downMs);
      EXPECT_EQ(updated->getPortUpHoldoffTimeMs(), upMs);
    } else {
      EXPECT_EQ(nullptr, newState);
    }
  };

  // Defaults: both holdoff timers unset.
  auto initial = state->getPorts()->getNodeIf(PortID(1));
  EXPECT_EQ(std::nullopt, initial->getPortDownHoldoffTimeMs());
  EXPECT_EQ(std::nullopt, initial->getPortUpHoldoffTimeMs());

  // Set, change, and clear values via cfg.
  applyAndVerify(100, 200);
  applyAndVerify(150, 250);
  applyAndVerify(std::nullopt, std::nullopt);
  applyAndVerify(0, 0);

  // Direct getter/setter round-trip.
  auto cloned = state->getPorts()->getNodeIf(PortID(1))->clone();
  cloned->setPortDownHoldoffTimeMs(500);
  cloned->setPortUpHoldoffTimeMs(750);
  EXPECT_EQ(cloned->getPortDownHoldoffTimeMs(), 500);
  EXPECT_EQ(cloned->getPortUpHoldoffTimeMs(), 750);

  cloned->setPortDownHoldoffTimeMs(std::nullopt);
  cloned->setPortUpHoldoffTimeMs(std::nullopt);
  EXPECT_EQ(cloned->getPortDownHoldoffTimeMs(), std::nullopt);
  EXPECT_EQ(cloned->getPortUpHoldoffTimeMs(), std::nullopt);

  // Serialize / deserialize round-trip.
  cloned->setPortDownHoldoffTimeMs(123);
  cloned->setPortUpHoldoffTimeMs(456);
  auto serialized = std::make_shared<Port>(cloned->toThrift());
  EXPECT_EQ(serialized->getPortDownHoldoffTimeMs(), 123);
  EXPECT_EQ(serialized->getPortUpHoldoffTimeMs(), 456);

  // Negative cfg values must be rejected at applyConfig time.
  cfg::SwitchConfig negConfig;
  negConfig.ports()->resize(1);
  preparedMockPortConfig(
      negConfig.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  negConfig.ports()[0].portDownHoldoffTimeMs() = -1;
  EXPECT_THROW(
      publishAndApplyConfig(state, &negConfig, platform.get()), FbossError);
  negConfig.ports()[0].portDownHoldoffTimeMs().reset();
  negConfig.ports()[0].portUpHoldoffTimeMs() = -100;
  EXPECT_THROW(
      publishAndApplyConfig(state, &negConfig, platform.get()), FbossError);
}
