/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/agent_hw_tests/AgentRollbackTests.h"

#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"

namespace facebook::fboss {

cfg::SwitchConfig AgentRollbackTest::initialConfig(
    const AgentEnsemble& ensemble) const {
  return utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      true /*interfaceHasSubnet*/);
}

void AgentRollbackTest::setCmdLineFlagOverrides() const {
  AgentHwTest::setCmdLineFlagOverrides();
}

void AgentRollbackTest::SetUp() {
  AgentHwTest::SetUp();
}

namespace {
std::optional<StateDeltaApplication> getDeltaApplicationForRollback() {
  StateDeltaApplication deltaApplication;
  deltaApplication.mode() = DeltaApplicationMode::ROLLBACK;
  return deltaApplication;
}

std::shared_ptr<SwitchState> resolveNeighborState(
    const std::shared_ptr<SwitchState>& state,
    bool needL2Entry) {
  utility::EcmpSetupAnyNPorts4 v4EcmpHelper(state, needL2Entry, RouterID(0));
  utility::EcmpSetupAnyNPorts6 v6EcmpHelper(state, needL2Entry, RouterID(0));
  auto newState = v4EcmpHelper.resolveNextHops(state, 1);
  return v6EcmpHelper.resolveNextHops(newState, 1);
}

std::shared_ptr<SwitchState> unresolveNeighborState(
    const std::shared_ptr<SwitchState>& state,
    bool needL2Entry) {
  utility::EcmpSetupAnyNPorts4 v4EcmpHelper(state, needL2Entry, RouterID(0));
  utility::EcmpSetupAnyNPorts6 v6EcmpHelper(state, needL2Entry, RouterID(0));
  auto newState = v4EcmpHelper.unresolveNextHops(state, 1);
  return v6EcmpHelper.unresolveNextHops(newState, 1);
}

std::shared_ptr<SwitchState> setPortAdminState(
    const std::shared_ptr<SwitchState>& state,
    PortID portId,
    cfg::PortState adminState) {
  auto newState = state->clone();
  auto port = newState->getPorts()->getNodeIf(portId)->modify(&newState);
  port->setAdminState(adminState);
  return newState;
}

// qsfp_service populates transceiverMaps via programInternalPhyPorts on its
// own schedule after the agent starts, so it can change mid-test. The same
// update also rewrites port profile and pin configs when the platform mapping
// has transceiver-dependent portConfigOverrides; those are not masked here.
state::SwitchState toThriftWithoutTransceivers(
    const std::shared_ptr<SwitchState>& state) {
  auto thriftState = state->toThrift();
  thriftState.transceiverMaps()->clear();
  return thriftState;
}
} // namespace

TEST_F(AgentRollbackTest, rollbackNeighborResolution) {
  auto verify = [this] {
    auto needL2Entry = getSw()->needL2EntryForNeighbor();
    auto origState = getProgrammedState();

    // Rollback neighbor resolution
    EXPECT_THROW(
        getSw()->updateStateWithHwFailureProtection(
            "resolve neighbors for rollback",
            [needL2Entry](const std::shared_ptr<SwitchState>& state) {
              return resolveNeighborState(state, needL2Entry);
            },
            getDeltaApplicationForRollback()),
        FbossHwUpdateError);
    EXPECT_EQ(
        toThriftWithoutTransceivers(origState),
        toThriftWithoutTransceivers(getProgrammedState()));

    // Actually resolve neighbors (persistent change)
    getSw()->updateStateBlocking(
        "resolve neighbors persistently",
        [needL2Entry](const std::shared_ptr<SwitchState>& state) {
          return resolveNeighborState(state, needL2Entry);
        });
    auto resolvedState = getProgrammedState();

    // Rollback neighbor un-resolution
    EXPECT_THROW(
        getSw()->updateStateWithHwFailureProtection(
            "unresolve neighbors for rollback",
            [needL2Entry](const std::shared_ptr<SwitchState>& state) {
              return unresolveNeighborState(state, needL2Entry);
            },
            getDeltaApplicationForRollback()),
        FbossHwUpdateError);
    EXPECT_EQ(
        toThriftWithoutTransceivers(resolvedState),
        toThriftWithoutTransceivers(getProgrammedState()));

    // Unresolve neighbors (restore to original)
    getSw()->updateStateBlocking(
        "unresolve neighbors",
        [needL2Entry](const std::shared_ptr<SwitchState>& state) {
          return unresolveNeighborState(state, needL2Entry);
        });
    EXPECT_EQ(
        toThriftWithoutTransceivers(origState),
        toThriftWithoutTransceivers(getProgrammedState()));
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentRollbackTest, rollbackLinkUpAndDown) {
  auto verify = [this] {
    auto portId = masterLogicalPortIds()[0];

    // Rollback link down
    EXPECT_THROW(
        getSw()->updateStateWithHwFailureProtection(
            "bring down port for rollback",
            [portId](const std::shared_ptr<SwitchState>& state) {
              return setPortAdminState(state, portId, cfg::PortState::DISABLED);
            },
            getDeltaApplicationForRollback()),
        FbossHwUpdateError);
    for (auto id : masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(
            getProgrammedState()->getPorts()->getNodeIf(id)->isPortUp());
      });
    }

    // Actually bring port down (persistent change)
    getSw()->updateStateBlocking(
        "disable port", [portId](const std::shared_ptr<SwitchState>& state) {
          return setPortAdminState(state, portId, cfg::PortState::DISABLED);
        });

    // Rollback link up
    EXPECT_THROW(
        getSw()->updateStateWithHwFailureProtection(
            "bring up port for rollback",
            [portId](const std::shared_ptr<SwitchState>& state) {
              return setPortAdminState(state, portId, cfg::PortState::ENABLED);
            },
            getDeltaApplicationForRollback()),
        FbossHwUpdateError);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_FALSE(
          getProgrammedState()->getPorts()->getNodeIf(portId)->isPortUp());
    });

    // Bring port back up
    getSw()->updateStateBlocking(
        "re-enable port", [portId](const std::shared_ptr<SwitchState>& state) {
          return setPortAdminState(state, portId, cfg::PortState::ENABLED);
        });
    for (auto id : masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(
            getProgrammedState()->getPorts()->getNodeIf(id)->isPortUp());
      });
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentQphRollbackTest, rollbackWithQPHConfig) {
  auto setup = [this] {
    auto cfg = initialConfig(*getAgentEnsemble());
    utility::addQueuePerHostQueueConfig(&cfg);
    utility::addQueuePerHostAcls(&cfg, getAgentEnsemble()->isSai());
    applyNewConfig(cfg);
  };

  auto verify = [this] {
    auto origState = getProgrammedState();

    // Perform a no-op rollback with QPH config present
    EXPECT_NO_THROW(
        getSw()->updateStateWithHwFailureProtection(
            "no-op rollback with QPH",
            [](const std::shared_ptr<SwitchState>& state) {
              // Return a clone to create a delta (even if identical content)
              return state->clone();
            },
            getDeltaApplicationForRollback()));

    // State should be unchanged after rollback
    EXPECT_EQ(
        toThriftWithoutTransceivers(origState),
        toThriftWithoutTransceivers(getProgrammedState()));
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
