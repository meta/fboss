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

#include <fb303/ServiceData.h>
#include "fboss/agent/DsfSubscriber.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

namespace {
auto constexpr kSystemPortId = 42;
auto constexpr kRemoteSwitchId = 42;
auto constexpr kNumNewPorts = 3;
auto constexpr kInterfaceId = 42;
} // namespace

namespace facebook::fboss {
class SwitchStatsObserverTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto cfg = testConfigA(cfg::SwitchType::VOQ);
    handle_ = createTestHandle(&cfg);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    waitForStateUpdates(sw_);
  }

  HwSwitchMatcher matcher() const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  }

  void updateSysPortMap(MultiSwitchSystemPortMap* sysPorts, bool add) {
    for (int i = 0; i < kNumNewPorts; i++) {
      if (add) {
        sysPorts->addNode(
            makeSysPort(std::nullopt, kSystemPortId + i, kRemoteSwitchId),
            matcher());
      } else {
        sysPorts->removeNode(kSystemPortId + i);
      }
    }
  }

  void updateInterfaceMap(MultiSwitchInterfaceMap* intfs, bool add) {
    if (add) {
      auto rif = std::make_shared<Interface>(
          InterfaceID(kInterfaceId),
          std::optional<VlanID>(std::nullopt),
          folly::StringPiece("1001"),
          folly::MacAddress{},
          9000,
          false,
          false,
          cfg::InterfaceType::SYSTEM_PORT);
      auto [ndpTable, arpTable] = makeNbrs();
      rif->setNdpTable(ndpTable);
      rif->setArpTable(arpTable);
      intfs->addNode(rif, matcher());
    } else {
      intfs->removeNode(kInterfaceId);
    }
  }

 protected:
  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_{nullptr};
};

TEST_F(SwitchStatsObserverTest, systemPortCounters) {
  CounterCache counters(sw_);
  sw_->updateStats();
  counters.update();

  auto localSysPortCounter = SwitchStats::kCounterPrefix + "systemPort";
  EXPECT_TRUE(counters.checkExist(localSysPortCounter));
  auto initialCounterVal = counters.value(localSysPortCounter);
  EXPECT_GT(initialCounterVal, 0);

  sw_->updateStateBlocking(
      "Add new sysPort", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateSysPortMap(
            newState->getSystemPorts()->modify(&newState), /* add */ true);
        return newState;
      });
  counters.update();
  EXPECT_EQ(
      initialCounterVal + kNumNewPorts, counters.value(localSysPortCounter));

  sw_->updateStateBlocking(
      "Remove sysPort", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateSysPortMap(
            newState->getSystemPorts()->modify(&newState), /* add */ false);
        return newState;
      });
  counters.update();
  EXPECT_EQ(initialCounterVal, counters.value(localSysPortCounter));
}

TEST_F(SwitchStatsObserverTest, remoteSystemPortCounters) {
  CounterCache counters(sw_);

  sw_->updateStateBlocking(
      "Add new remote sysPort", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateSysPortMap(
            newState->getRemoteSystemPorts()->modify(&newState),
            /* add */ true);
        return newState;
      });
  counters.update();

  auto remoteSysPortCounter = SwitchStats::kCounterPrefix + "remoteSystemPort";
  EXPECT_TRUE(counters.checkExist(remoteSysPortCounter));
  auto initialCounterVal = counters.value(remoteSysPortCounter);
  EXPECT_EQ(initialCounterVal, kNumNewPorts);

  sw_->updateStateBlocking(
      "Remove sysPort", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateSysPortMap(
            newState->getRemoteSystemPorts()->modify(&newState),
            /* add */ false);
        return newState;
      });
  counters.update();
  EXPECT_EQ(counters.value(remoteSysPortCounter), 0);
}

TEST_F(SwitchStatsObserverTest, interfaceCounters) {
  CounterCache counters(sw_);
  sw_->updateStats();
  counters.update();

  auto localIntfCounter = SwitchStats::kCounterPrefix + "localRifs";
  auto localNdpCounter = SwitchStats::kCounterPrefix + "localResolvedNdp";
  auto localArpfCounter = SwitchStats::kCounterPrefix + "localResolvedArp";
  EXPECT_TRUE(counters.checkExist(localIntfCounter));
  EXPECT_TRUE(counters.checkExist(localNdpCounter));
  EXPECT_TRUE(counters.checkExist(localArpfCounter));

  auto initialIntfVal = counters.value(localIntfCounter);
  auto initialNdpVal = counters.value(localNdpCounter);
  auto initialArpfVal = counters.value(localArpfCounter);
  EXPECT_GT(initialIntfVal, 0);
  EXPECT_GT(initialNdpVal, 0);
  EXPECT_GT(initialArpfVal, 0);

  sw_->updateStateBlocking(
      "Add new intf", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateInterfaceMap(
            newState->getInterfaces()->modify(&newState),
            /* add */ true);
        return newState;
      });
  counters.update();
  EXPECT_EQ(initialIntfVal + 1, counters.value(localIntfCounter));
  EXPECT_EQ(initialNdpVal + 2, counters.value(localNdpCounter));
  EXPECT_EQ(initialArpfVal + 2, counters.value(localArpfCounter));

  sw_->updateStateBlocking(
      "Remove intf", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateInterfaceMap(
            newState->getInterfaces()->modify(&newState),
            /* add */ false);
        return newState;
      });
  counters.update();
  EXPECT_EQ(initialIntfVal, counters.value(localIntfCounter));
  EXPECT_EQ(initialNdpVal, counters.value(localNdpCounter));
  EXPECT_EQ(initialArpfVal, counters.value(localArpfCounter));
}

TEST_F(SwitchStatsObserverTest, remoteInterfaceCounters) {
  CounterCache counters(sw_);
  auto remoteIntfCounter = SwitchStats::kCounterPrefix + "remoteRifs";
  auto remoteNdpCounter = SwitchStats::kCounterPrefix + "remoteResolvedNdp";
  auto remoteArpCounter = SwitchStats::kCounterPrefix + "remoteResolvedArp";

  sw_->updateStateBlocking(
      "Add new intf", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateInterfaceMap(
            newState->getRemoteInterfaces()->modify(&newState),
            /* add */ true);
        return newState;
      });
  counters.update();
  EXPECT_TRUE(counters.checkExist(remoteIntfCounter));
  EXPECT_TRUE(counters.checkExist(remoteNdpCounter));
  EXPECT_TRUE(counters.checkExist(remoteArpCounter));
  EXPECT_EQ(counters.value(remoteIntfCounter), 1);
  EXPECT_EQ(counters.value(remoteNdpCounter), 2);
  EXPECT_EQ(counters.value(remoteArpCounter), 2);

  sw_->updateStateBlocking(
      "Add new intf", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateInterfaceMap(
            newState->getRemoteInterfaces()->modify(&newState),
            /* add */ false);
        return newState;
      });
  counters.update();
  EXPECT_EQ(counters.value(remoteIntfCounter), 0);
  EXPECT_EQ(counters.value(remoteNdpCounter), 0);
  EXPECT_EQ(counters.value(remoteArpCounter), 0);
}
TEST_F(SwitchStatsObserverTest, ribRouteProgrammingTimeUs) {
  sw_->stats()->ribRouteProgrammingTimeUs(std::chrono::microseconds(5000));
  fb303::ServiceData::get()->flushAllData();

  std::map<std::string, int64_t> counters;
  fb303::fbData->getCounters(counters);

  auto counterName =
      SwitchStats::kCounterPrefix + "rib_route_programming_time.us.p100";
  ASSERT_TRUE(counters.count(counterName));
  EXPECT_GE(counters[counterName], 5000);
}

} // namespace facebook::fboss
