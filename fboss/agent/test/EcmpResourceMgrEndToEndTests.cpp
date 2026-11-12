/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// End-to-end ERM tests: every route change and warm boot goes through the RIB,
// SwSwitch and ERM consolidate.

#include <gflags/gflags.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FileBasedWarmbootUtils.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/hw/mock/MockPlatformMapping.h"
#include "fboss/agent/hw/mock/MockTestHandle.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/single/MonolithicHwSwitchHandler.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/test/BaseEcmpResourceMgrMergeGroupsTests.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/EcmpResourceManagerTestUtils.h"
#include "fboss/lib/WarmBootFileUtils.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

namespace facebook::fboss {

namespace {
std::map<std::string, std::optional<NextHopSetID>> routeOverrideIds(
    const std::shared_ptr<SwitchState>& state) {
  std::map<std::string, std::optional<NextHopSetID>> ids;
  auto collect = [&](const auto& fib) {
    for (const auto& [_, route] : std::as_const(*fib)) {
      ids[route->prefix().str()] = route->isResolved()
          ? route->getForwardInfo().getOverrideNextHopSetID()
          : std::nullopt;
    }
  };
  collect(cfib(state));
  collect(cfib4(state));
  return ids;
}

void stripSomeOverrideIds(state::WarmbootState& wb) {
  for (auto& [_, fibInfo] : *wb.swSwitchState()->fibsInfoMap()) {
    auto numMerged = 0;
    for (auto& [__, container] : *fibInfo.fibsMap()) {
      for (auto& [___, route] : *container.fibV6()) {
        auto& fwd = *route.fwd();
        if (!fwd.overrideNextHopSetID().has_value() || numMerged++ % 2) {
          continue;
        }
        fwd.overrideNextHopSetID().reset();
      }
    }
  }
}
void stripNextHopIds(state::WarmbootState& wb) {
  auto stripEntry = [](state::RouteNextHopEntry& entry) {
    entry.normalizedResolvedNextHopSetID().reset();
    entry.resolvedNextHopSetID().reset();
    entry.clientNextHopSetID().reset();
    entry.overrideNextHopSetID().reset();
  };
  auto stripRoute = [&stripEntry](auto& route) {
    stripEntry(*route.fwd());
    for (auto& [_, entry] : *route.nexthopsmulti()->client2NextHopEntry()) {
      stripEntry(entry);
    }
  };
  for (auto& [_, fibInfo] : *wb.swSwitchState()->fibsInfoMap()) {
    for (auto& [__, container] : *fibInfo.fibsMap()) {
      for (auto& [___, route] : *container.fibV4()) {
        stripRoute(route);
      }
      for (auto& [___, route] : *container.fibV6()) {
        stripRoute(route);
      }
    }
    fibInfo.idToNextHop()->clear();
    fibInfo.idToNextHopIdSet()->clear();
    fibInfo.overrideIdToNextHopIdSet()->clear();
  }
  for (auto& [_, table] : *wb.routeTables()) {
    for (auto& [__, route] : *table.v4NetworkToRoute()) {
      stripRoute(route);
    }
    for (auto& [__, route] : *table.v6NetworkToRoute()) {
      stripRoute(route);
    }
  }
}

} // namespace

class EcmpResourceMgrEndToEndTest : public BaseEcmpResourceMgrMergeGroupsTest {
 public:
  std::optional<cfg::SwitchingMode> getBackupEcmpSwitchingMode()
      const override {
    return std::nullopt;
  }
  void setupFlags() const override {
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ecmp_resource_percentage = 35;
  }
  void TearDown() override {
    BaseEcmpResourceMgrMergeGroupsTest::TearDown();
    wbHandle_.reset();
  }

 protected:
  // Warm boots a new SwSwitch from `wb` through SwSwitch::init. With
  // applyConfig, also runs the initial config apply, the first RIB to FIB
  // update.
  // Without seedState, init uses the state preInit reconstructs, as in
  // production, so the RIB shares the FIB's route objects.
  SwSwitch* warmboot(
      const state::WarmbootState& wb,
      bool applyConfig = true,
      bool seedState = true) {
    auto [state, rib] = reconstructStateAndRib(wb, true /*hasL3*/);
    auto cfg = onePortPerIntfConfig(
        kNumIntfs,
        getBackupEcmpSwitchingMode(),
        getEcmpCompressionThresholdPct());
    cfg.switchSettings()->switchIdToSwitchInfo() = {
        std::make_pair(0, createSwitchInfo(cfg::SwitchType::NPU))};
    std::vector<std::unique_ptr<MockPlatform>> platforms;
    platforms.emplace_back(createMockPlatform(cfg::SwitchType::NPU, 0, &cfg));
    auto* platform = platforms.front().get();
    auto agentConfig = createEmptyAgentConfig()->thrift;
    agentConfig.sw() = cfg;
    platform->setConfig(std::make_unique<AgentConfig>(agentConfig));
    // SwSwitch::preInit only warm boots from a state file in the warm boot dir.
    const auto* dirUtil = platform->getDirectoryUtil();
    utilCreateDir(dirUtil->getWarmBootDir());
    WarmBootFileUtils::storeWarmBootState(
        folly::to<std::string>(
            dirUtil->getWarmBootDir(), "/", FLAGS_thrift_switch_state_file),
        wb);
    WarmBootFileUtils::setCanWarmBoot(dirUtil->getSwSwitchCanWarmBootFile());
    HwSwitchHandlerInitFn handlerInitFn = [platform](
                                              const SwitchID& switchId,
                                              const cfg::SwitchInfo& info,
                                              SwSwitch* sw) {
      return std::make_unique<MonolithicHwSwitchHandler>(
          platform, switchId, info, sw);
    };
    auto newSw = std::make_unique<SwSwitch>(
        std::move(handlerInitFn),
        std::make_unique<MockPlatformMapping>(),
        platform->getDirectoryUtil(),
        platform->supportsAddRemovePort(),
        platform->config(),
        seedState ? state : nullptr);
    newSw->setConfig(std::make_unique<AgentConfig>(agentConfig));
    HwInitResult ret;
    ret.switchState = state;
    ret.rib = std::move(rib);
    ret.bootType = BootType::WARM_BOOT;
    getMockHw(newSw.get())->setInitialState(state);
    EXPECT_HW_CALL(newSw.get(), initImpl(testing::_, testing::_, false))
        .WillOnce(testing::Return(testing::ByMove(std::move(ret))));
    EXPECT_HW_CALL(newSw.get(), getBootType)
        .WillRepeatedly(testing::Return(BootType::WARM_BOOT));
    // HwSwitch::initLight forces cold boot on mock ASICs.
    auto* rawSw = newSw.get();
    HwSwitchInitFn warmbootInitFn =
        [rawSw](HwSwitchCallback* callback, bool failHwCallsOnWarmboot) {
          auto* hw = getMockHw(rawSw);
          auto result = hw->initImpl(
              callback, BootType::WARM_BOOT, failHwCallsOnWarmboot);
          result.bootType = BootType::WARM_BOOT;
          hw->setInitialState(std::make_shared<SwitchState>());
          return result;
        };
    // One restart tracker per process; the old switch already initialized it.
    restart_time::stop();
    newSw->init(nullptr, warmbootInitFn, SwitchFlags::DEFAULT);
    waitForStateUpdates(newSw.get());
    wbCfg_ = cfg;
    if (applyConfig) {
      applyInitialConfig(newSw.get());
    }
    wbHandle_ = std::make_unique<MockTestHandle>(
        std::move(newSw), std::move(platforms));
    wbHandle_->prepareForTesting();
    return wbHandle_->getSw();
  }

  // The initial config apply: the first RIB to FIB update after warm boot.
  void applyInitialConfig(SwSwitch* sw) {
    sw->applyConfig("initial config", wbCfg_);
    sw->initialConfigApplied(std::chrono::steady_clock::now());
    waitForStateUpdates(sw);
  }

  // Every merged route has an override id in field 5 whose members are the
  // NextHop ids of its override nexthops, and the RIB carries the same id.
  void expectStateAndRibConsistent(SwSwitch* sw) {
    auto state = sw->getState();
    const auto& fibInfo = state->getFibsInfoMap()->cbegin()->second;
    std::unordered_map<NextHop, int64_t> nhopToId;
    auto idToNhop = fibInfo->getIdToNextHopMap();
    ASSERT_NE(idToNhop, nullptr);
    for (const auto& [id, nhop] : std::as_const(*idToNhop)) {
      nhopToId.emplace(
          util::fromThrift(nhop->toThrift(), true /* allowV6NonLinkLocal */),
          id);
    }
    auto overrides = fibInfo->getOverrideIdToNextHopIdSetMap();
    ASSERT_NE(overrides, nullptr);
    auto overrideMap = overrides->toThrift();
    auto numMerged = 0;
    for (const auto& [_, route] : std::as_const(*cfib(state))) {
      const auto& fwd = route->getForwardInfo();
      if (!fwd.hasOverrideNextHops()) {
        continue;
      }
      ++numMerged;
      auto id = fwd.getOverrideNextHopSetID();
      ASSERT_TRUE(id.has_value()) << route->str();
      std::set<int64_t> expected;
      auto overrideNhops = fwd.getOverrideNextHops();
      for (const auto& nhop : *overrideNhops) {
        auto itr = nhopToId.find(nhop);
        ASSERT_NE(itr, nhopToId.end()) << nhop.str();
        expected.insert(itr->second);
      }
      auto oitr = overrideMap.find(static_cast<int64_t>(*id));
      ASSERT_NE(oitr, overrideMap.end()) << route->str();
      EXPECT_EQ(
          std::set<int64_t>(oitr->second.begin(), oitr->second.end()), expected)
          << route->str();
      EXPECT_EQ(
          sw->getRib()
              ->longestMatch(route->prefix().network(), RouterID(0))
              ->getForwardInfo()
              .getOverrideNextHopSetID(),
          id)
          << route->str();
    }
    EXPECT_GT(numMerged, 0);
    assertOverrideSetIdsConsistency(*sw->getEcmpResourceManager(), state);
  }

  // The RIB imports the warm boot FIB's route objects. Init and the first
  // FIB update must keep that sharing, or every RIB to FIB update deep
  // compares each route.
  void expectRibSharesFibRoutes(SwSwitch* sw) {
    auto numMerged = 0;
    for (const auto& [_, route] : std::as_const(*cfib(sw->getState()))) {
      auto ribRoute =
          sw->getRib()->longestMatch(route->prefix().network(), RouterID(0));
      ASSERT_NE(ribRoute, nullptr) << route->str();
      if (ribRoute->prefix() != route->prefix()) {
        continue;
      }
      EXPECT_EQ(ribRoute, route) << route->str();
      numMerged += route->getForwardInfo().hasOverrideNextHops() ? 1 : 0;
    }
    EXPECT_GT(numMerged, 0);
  }

  // ERM's ids are consistent with the state, and the RIB carries the same
  // override nexthops and override id as the FIB for every route.
  void expectErmRibFibConsistent(SwSwitch* sw) {
    auto state = sw->getState();
    assertOverrideSetIdsConsistency(*sw->getEcmpResourceManager(), state);
    for (const auto& [_, route] : std::as_const(*cfib(state))) {
      auto ribRoute =
          sw->getRib()->longestMatch(route->prefix().network(), RouterID(0));
      ASSERT_NE(ribRoute, nullptr) << route->str();
      if (ribRoute->prefix() != route->prefix()) {
        continue;
      }
      const auto& fwd = route->getForwardInfo();
      const auto& ribFwd = ribRoute->getForwardInfo();
      EXPECT_EQ(ribFwd.getOverrideNextHops(), fwd.getOverrideNextHops())
          << route->str();
      EXPECT_EQ(ribFwd.getOverrideNextHopSetID(), fwd.getOverrideNextHopSetID())
          << route->str();
    }
  }

  cfg::SwitchConfig wbCfg_;
  std::unique_ptr<HwTestHandle> wbHandle_;
};

// A merge forms, goes away and a new merge forms across three updates through
// the real SwSwitch and RIB: after each, ERM, the FIB and the RIB agree on
// every route's override nexthops and override id.
TEST_F(EcmpResourceMgrEndToEndTest, mergeLifecycleKeepsRibAndFibConsistent) {
  auto firstPfx = nextPrefix();
  addNextRoute();
  EXPECT_FALSE(sw_->getEcmpResourceManager()->getMergedGroups().empty());
  expectErmRibFibConsistent(sw_);

  rmRoute(firstPfx);
  EXPECT_TRUE(sw_->getEcmpResourceManager()->getMergedGroups().empty());
  expectErmRibFibConsistent(sw_);

  addRoute(nextPrefix(), *nextNhopSets(1).begin());
  EXPECT_FALSE(sw_->getEcmpResourceManager()->getMergedGroups().empty());
  expectErmRibFibConsistent(sw_);
}

// Same-binary warm boot: override ids are unchanged, the state and the RIB
// agree, and the RIB keeps sharing route objects with the FIB through init and
// config apply.
TEST_F(EcmpResourceMgrEndToEndTest, sameVersionWarmbootKeepsOverrideIds) {
  addNextRoute();
  addNextRoute();
  auto before = routeOverrideIds(sw_->getState());
  auto* newSw = warmboot(
      sw_->gracefulExitState(), false /*applyConfig*/, false /*seedState*/);
  EXPECT_FALSE(newSw->getRib()->nextHopIdsChangedAtWarmboot());
  EXPECT_EQ(routeOverrideIds(newSw->getState()), before);
  expectStateAndRibConsistent(newSw);
  expectRibSharesFibRoutes(newSw);
  applyInitialConfig(newSw);
  expectStateAndRibConsistent(newSw);
  expectRibSharesFibRoutes(newSw);
}

// Downgrade to a binary without override ids and back: the persisted map
// survives, but some merged routes lost their ids and the old binary formed a
// merge that never got one. Init gives existing merges their old ids back and
// the new merge a fresh id above every persisted one.
TEST_F(EcmpResourceMgrEndToEndTest, strippedOverrideIdsRestampedAtInit) {
  addNextRoute();
  addNextRoute();
  addNextRoute();
  auto before = routeOverrideIds(sw_->getState());
  auto wb = sw_->gracefulExitState();
  // The lowest id's merge becomes the new merge: no map entry, no route ids.
  const auto& anyOverrideMap = *wb.swSwitchState()
                                    ->fibsInfoMap()
                                    ->begin()
                                    ->second.overrideIdToNextHopIdSet();
  ASSERT_GE(anyOverrideMap.size(), 2);
  auto newMergeId = anyOverrideMap.begin()->first;
  auto clearNewMergeId = [newMergeId](auto& route) {
    auto id = route.fwd()->overrideNextHopSetID();
    if (id.has_value() && *id == newMergeId) {
      id.reset();
    }
  };
  for (auto& [_, fibInfo] : *wb.swSwitchState()->fibsInfoMap()) {
    fibInfo.overrideIdToNextHopIdSet()->erase(newMergeId);
    for (auto& [__, container] : *fibInfo.fibsMap()) {
      for (auto& [___, route] : *container.fibV6()) {
        clearNewMergeId(route);
      }
    }
  }
  for (auto& [_, table] : *wb.routeTables()) {
    for (auto& [__, route] : *table.v6NetworkToRoute()) {
      clearNewMergeId(route);
    }
  }
  stripSomeOverrideIds(wb);
  auto persistedMax = anyOverrideMap.rbegin()->first;
  auto* newSw = warmboot(wb, false /*applyConfig*/);

  auto after = routeOverrideIds(newSw->getState());
  auto numNewMergeRoutes = 0;
  for (const auto& [pfx, id] : before) {
    if (!id.has_value() || static_cast<int64_t>(*id) != newMergeId) {
      EXPECT_EQ(after[pfx], id) << pfx;
      continue;
    }
    ++numNewMergeRoutes;
    ASSERT_TRUE(after[pfx].has_value()) << pfx;
    EXPECT_GT(static_cast<int64_t>(*after[pfx]), persistedMax) << pfx;
  }
  EXPECT_GT(numNewMergeRoutes, 0);
  expectStateAndRibConsistent(newSw);
}

// Warm boots with a persisted override id that no merge uses: init frees it
// from ERM and the switch state.
TEST_F(EcmpResourceMgrEndToEndTest, staleOverrideIdsFreedAtInit) {
  addNextRoute();
  addNextRoute();
  addNextRoute();
  auto wb = sw_->gracefulExitState();
  stripSomeOverrideIds(wb);
  int64_t staleId{0};
  for (auto& [_, fibInfo] : *wb.swSwitchState()->fibsInfoMap()) {
    auto& overrideMap = *fibInfo.overrideIdToNextHopIdSet();
    ASSERT_FALSE(overrideMap.empty());
    staleId = overrideMap.rbegin()->first + 1;
    // Every NextHop: members no live merge has.
    auto& staleMembers = overrideMap[staleId];
    for (const auto& [id, __] : *fibInfo.idToNextHop()) {
      staleMembers.insert(id);
    }
  }
  auto* newSw = warmboot(wb, false /*applyConfig*/);
  const auto* erm = newSw->getEcmpResourceManager();
  EXPECT_FALSE(erm->getOverrideSetIdManager().getIdToMembers().contains(
      NextHopSetID(staleId)));
  auto overrides = newSw->getState()
                       ->getFibsInfoMap()
                       ->cbegin()
                       ->second->getOverrideIdToNextHopIdSetMap()
                       ->toThrift();
  EXPECT_FALSE(overrides.contains(staleId));
  expectStateAndRibConsistent(newSw);
}

// Warm boots with two persisted NextHop ids for the same NextHop: init keeps
// one, override sets use only surviving ids, and the RIB agrees.
TEST_F(EcmpResourceMgrEndToEndTest, nextHopCollapseFixedAtInit) {
  addNextRoute();
  addNextRoute();
  auto wb = sw_->gracefulExitState();
  auto& fibInfos = *wb.swSwitchState()->fibsInfoMap();
  const auto& anyFibInfo = fibInfos.begin()->second;
  ASSERT_FALSE(anyFibInfo.overrideIdToNextHopIdSet()->empty());
  auto memberId =
      *anyFibInfo.overrideIdToNextHopIdSet()->begin()->second.begin();
  auto duplicateId = anyFibInfo.idToNextHop()->rbegin()->first + 1000;
  for (auto& [_, fibInfo] : fibInfos) {
    (*fibInfo.idToNextHop())[duplicateId] = fibInfo.idToNextHop()->at(memberId);
    for (auto& [__, members] : *fibInfo.idToNextHopIdSet()) {
      if (members.erase(memberId)) {
        members.insert(duplicateId);
        break;
      }
    }
  }
  auto* newSw = warmboot(wb, false /*applyConfig*/);
  ASSERT_TRUE(newSw->getRib()->nextHopIdsChangedAtWarmboot());
  const auto* erm = newSw->getEcmpResourceManager();
  expectStateAndRibConsistent(newSw);
  auto state = newSw->getState();
  auto idToNhop =
      state->getFibsInfoMap()->cbegin()->second->getIdToNextHopMap();
  std::set<int64_t> stateIds;
  for (const auto& [id, _] : std::as_const(*idToNhop)) {
    stateIds.insert(id);
  }
  ASSERT_NE(stateIds.contains(memberId), stateIds.contains(duplicateId));
  for (const auto& [_, members] :
       erm->getOverrideSetIdManager().getIdToMembers()) {
    for (auto member : members) {
      EXPECT_TRUE(stateIds.contains(static_cast<int64_t>(member)));
    }
  }
  applyInitialConfig(newSw);
  expectStateAndRibConsistent(newSw);
}

// Warm boots from a manager-off binary with no NextHop ids: init stamps the
// RIB's ids into the state and assigns override ids before any FIB update.
TEST_F(EcmpResourceMgrEndToEndTest, managerOffToOnAssignsOverrideIdsAtInit) {
  addNextRoute();
  addNextRoute();
  gflags::FlagSaver saver;
  FLAGS_resolve_nexthops_from_id = false;
  auto wb = sw_->gracefulExitState();
  stripNextHopIds(wb);
  auto* newSw = warmboot(wb, false /*applyConfig*/);
  ASSERT_TRUE(newSw->getRib()->nextHopIdsChangedAtWarmboot());
  expectStateAndRibConsistent(newSw);
  auto afterInit = newSw->getState();
  applyInitialConfig(newSw);
  for (const auto& [_, route] : std::as_const(*cfib(afterInit))) {
    if (!route->getForwardInfo().hasOverrideNextHops()) {
      continue;
    }
    auto now = cfib(newSw->getState())->getRouteIf(route->prefix());
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(now, route) << route->str();
  }
  expectStateAndRibConsistent(newSw);
}

} // namespace facebook::fboss
