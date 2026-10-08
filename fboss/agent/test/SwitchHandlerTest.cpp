/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fb303/ServiceData.h>
#include <folly/ScopeGuard.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/MultiHwSwitchHandler.h"
#include "fboss/agent/MultiSwitchThriftHandler.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/mnpu/MultiSwitchHwSwitchHandler.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <gflags/gflags.h>

#include <atomic>
#include <chrono>
#include <cstdlib>

using facebook::fboss::HwSwitchMatcher;
using facebook::fboss::SwitchID;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{
      std::unordered_set<SwitchID>{SwitchID(1), SwitchID(2)}};
}
} // namespace

using namespace facebook::fboss;

class SwSwitchHandlerTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_multi_switch = true;
    auto agentConfig = createAgentConfig();
    agentDirUtil_ = std::make_unique<AgentDirectoryUtil>(
        tmpDir_.path().string() + "/volatile",
        tmpDir_.path().string() + "/persistent");
    sw_ = createSwSwitchWithMultiSwitch(
        &agentConfig,
        agentDirUtil_.get(),
        [](const SwitchID& switchId,
           const cfg::SwitchInfo& info,
           SwSwitch* sw) {
          return std::make_unique<MultiSwitchHwSwitchHandler>(
              switchId, info, sw);
        });
    sw_->getHwSwitchHandler()->start();
  }

  void TearDown() override {
    sw_->getHwSwitchHandler()->stop();
    sw_.reset();
    // sw_ holds pointers to the shutdown evb and to a handler capturing this
    // fixture, so stop the evb only after sw_ is gone.
    if (shutdownEvbThread_.joinable()) {
      shutdownEvb_.terminateLoopSoon();
      shutdownEvbThread_.join();
    }
  }

 protected:
  AgentConfig createAgentConfig() {
    auto config = testConfigB();
    std::optional<cfg::SdkVersion> sdkVersion = cfg::SdkVersion();
    sdkVersion->asicSdk() = "testVersion";
    sdkVersion->saiSdk() = "testSAIVersion";
    config.sdkVersion() = sdkVersion.value();
    auto agentConfig = createEmptyAgentConfig()->thrift;
    agentConfig.sw() = config;
    return AgentConfig(agentConfig);
  }

  std::shared_ptr<SwitchState> getInitialTestState() {
    auto state = std::make_shared<SwitchState>();
    auto multiSwitchSwitchSettings = std::make_unique<MultiSwitchSettings>();
    auto addSwitchSettings = [&multiSwitchSwitchSettings](SwitchID switchId) {
      auto newSwitchSettings = std::make_shared<SwitchSettings>();
      newSwitchSettings->setSwitchIdToSwitchInfo(
          {std::make_pair(switchId, createSwitchInfo(cfg::SwitchType::NPU))});
      multiSwitchSwitchSettings->addNode(
          HwSwitchMatcher(std::unordered_set<SwitchID>({switchId}))
              .matcherString(),
          newSwitchSettings);
    };
    addSwitchSettings(SwitchID(1));
    addSwitchSettings(SwitchID(2));
    state->resetSwitchSettings(std::move(multiSwitchSwitchSettings));
    auto aclEntry = make_shared<AclEntry>(0, std::string("acl0"));
    auto acls = state->getAcls();
    acls->addNode(aclEntry, scope());
    state->resetAcls(acls);
    return state;
  }

  std::shared_ptr<SwitchState> addAcl(
      const std::shared_ptr<SwitchState>& state,
      int idx) {
    auto newState = state->clone();
    auto aclEntry =
        make_shared<AclEntry>(idx, folly::to<std::string>("acl", idx));
    auto acls = newState->getAcls()->modify(&newState);
    acls->addNode(aclEntry, scope());
    return newState;
  }

  MultiHwSwitchHandler* getHwSwitchHandler() {
    return sw_->getHwSwitchHandler();
  }

  // Register a graceful shutdown handler that only counts invocations, on a
  // fixture-owned event base (sw_ keeps pointers to both, so they must
  // outlive it - see TearDown()).
  void registerShutdownCounterHandler() {
    shutdownEvbThread_ = std::thread([this]() { shutdownEvb_.loopForever(); });
    shutdownEvb_.waitUntilRunning();
    sw_->registerGracefulShutdownHandler(
        &shutdownEvb_, [this]() { shutdownCount_.fetch_add(1); });
  }

  // Flush the shutdown evb and return how many times the handler ran.
  int shutdownHandlerRunCount() {
    shutdownEvb_.runInFbossEventBaseThreadAndWait([]() {});
    return shutdownCount_.load();
  }

  bool coldBootMarkerExists() {
    auto dirUtil = sw_->getDirUtil();
    if (checkFileExists(dirUtil->getSwColdBootOnceFile())) {
      return true;
    }
    for (const auto& [switchId, switchInfo] :
         sw_->getSwitchInfoTable().getSwitchIdToSwitchInfo()) {
      if (checkFileExists(
              dirUtil->getHwColdBootOnceFile(*switchInfo.switchIndex()))) {
        return true;
      }
    }
    return false;
  }

  // Swap in a thrift client table whose boot type and run state replies are
  // scripted; sw_ owns it, the returned pointer is for configuring it.
  HwSwitchThriftClientTableForTesting* installFakeThriftClientTable() {
    std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;
    for (const auto& [switchId, switchInfo] :
         sw_->getSwitchInfoTable().getSwitchIdToSwitchInfo()) {
      switchIdToSwitchInfo[static_cast<int64_t>(switchId)] = switchInfo;
    }
    auto table = std::make_unique<HwSwitchThriftClientTableForTesting>(
        0, switchIdToSwitchInfo);
    auto tablePtr = table.get();
    sw_->setHwSwitchThriftClientTableForTesting(std::move(table));
    return tablePtr;
  }

  std::unique_ptr<SwSwitch> sw_;
  folly::test::TemporaryDirectory tmpDir_;
  std::unique_ptr<AgentDirectoryUtil> agentDirUtil_;
  FbossEventBase shutdownEvb_{"GracefulShutdownTestEvb"};
  std::thread shutdownEvbThread_;
  std::atomic<int> shutdownCount_{0};
};

// These tests deliberately stop the handler while clients still have calls in
// flight. Whichever side wins, the server is gone: a call that gets in before
// stop() is cancelled and comes back with no deltas, one that arrives after is
// rejected outright. Normalise the second into the first, because which one
// happens is pure timing and an uncaught throw out of a client thread takes the
// whole process down instead of failing one test.
//
// Only for calls whose payload the test does not depend on. Anywhere the
// content is asserted, synchronise instead.
namespace {
multiswitch::StateOperDelta getNextDeltaTolerateStop(
    MultiHwSwitchHandler* handler,
    int64_t switchId,
    std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
    int64_t lastUpdateSeqNum) {
  try {
    return handler->getNextStateOperDelta(
        switchId, std::move(prevOperResult), lastUpdateSeqNum);
  } catch (const FbossError& ex) {
    EXPECT_THAT(ex.what(), ::testing::HasSubstr("syncer not started"));
    return multiswitch::StateOperDelta{};
  }
}
} // namespace

TEST_F(SwSwitchHandlerTest, GetOperDelta) {
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = getInitialTestState();

  auto addRandomDelay = []() {
    std::this_thread::sleep_for(std::chrono::milliseconds{random() % 100});
  };

  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  auto delta = StateDelta(stateV0, stateV1);
  auto deltaOperDelta = delta.getOperDelta();
  std::thread stateUpdateThread([this, &deltas, &addRandomDelay, &stateV1]() {
    getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
    addRandomDelay();
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, false);
    EXPECT_EQ(stateReturned, stateV1);
    // Switch 1 cancels request
    getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
    // Switch 2 has pending request but server stops
    getHwSwitchHandler()->stop();
  });

  auto clientThreadBody =
      [this, &deltaOperDelta, &addRandomDelay](int64_t switchId) {
        int64_t ackNum{0};
        OperDeltaFilter filter((SwitchID(switchId)));
        // connect and get next state delta
        addRandomDelay();
        auto getEmptyOper = []() {
          auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
          operDelta->operDeltas() = {fsdb::OperDelta()};
          return operDelta;
        };
        auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
            switchId, getEmptyOper(), ackNum++);
        EXPECT_EQ(
            operDelta.operDeltas()->back(),
            *filter.filterWithSwitchStateRootPath(deltaOperDelta));
        // request next state delta. the empty oper passed serves as success
        // indicator for previous delta
        operDelta = getHwSwitchHandler()->getNextStateOperDelta(
            switchId, getEmptyOper(), ackNum++);
        // this request will be cancelled
        EXPECT_EQ(operDelta.operDeltas()->size(), 0);
      };

  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

TEST_F(SwSwitchHandlerTest, cancelHwSwitchWait) {
  std::thread serverThread([&]() {
    EXPECT_FALSE(getHwSwitchHandler()->waitUntilHwSwitchConnected());
  });
  std::thread serverStopThread([&]() { getHwSwitchHandler()->stop(); });
  serverThread.join();
  serverStopThread.join();
}

/*
 * Test with 2 clients.
 * - Client 1 alone connects initially. Update succeeds
 * - Client 2 connects later. Should get a full sync while
 *   client 1 gets an incremental update.
 */
TEST_F(SwSwitchHandlerTest, partialUpdateAndFullSync) {
  folly::Baton<> client1UpdateCompletedBaton;
  folly::Baton<> client2FinishedBaton;
  folly::Baton<> client1InitialSyncBaton;
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();
  auto stateV1 = getInitialTestState();
  stateV1->publish();
  auto stateV2 = this->addAcl(stateV1, 1);
  stateV2->publish();
  auto stateV3 = this->addAcl(stateV2, 2);
  stateV3->publish();
  auto stateV4 = this->addAcl(stateV3, 3);
  stateV4->publish();

  /* initial update delta */
  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  deltas.emplace_back(stateV1, stateV2);
  auto delta = StateDelta(stateV1, stateV2);
  /* second update delta */
  std::vector<StateDelta> deltas2;
  deltas2.emplace_back(stateV2, stateV3);
  deltas2.emplace_back(stateV3, stateV4);
  auto delta2 = StateDelta(stateV3, stateV4);
  /* full update delta */
  auto delta3 = StateDelta(stateV0, stateV4);

  getHwSwitchHandler()->connected(SwitchID(1));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  getHwSwitchHandler()->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), sw_->getState()), false);

  // Snapshot what the server just sent as initial sync. Reading sw_->getState()
  // again from a client thread races any background state update and would
  // compare the received delta against a state the server never sent.
  auto expectedInitialSync =
      StateDelta(std::make_shared<SwitchState>(), sw_->getState())
          .getOperDelta();

  std::thread stateUpdateThread([this,
                                 &deltas,
                                 &stateV2,
                                 &deltas2,
                                 &stateV4,
                                 &client2FinishedBaton,
                                 &client1InitialSyncBaton]() {
    // wait for client 1 to do initial sync
    client1InitialSyncBaton.wait();
    /* state update should succeed */
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, true);
    EXPECT_EQ(stateReturned, stateV2);
    auto stateReturned2 = getHwSwitchHandler()->stateChanged(deltas2, true);
    EXPECT_EQ(stateReturned2, stateV4);
    // wait for client 2 to do full sync
    client2FinishedBaton.wait();
    getHwSwitchHandler()->stop();
  });

  auto clientThreadBody = [this,
                           &delta,
                           &delta2,
                           &delta3,
                           &expectedInitialSync,
                           &client1UpdateCompletedBaton,
                           &client2FinishedBaton,
                           &client1InitialSyncBaton](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    // connect and get next state delta
    if (switchId == 2) {
      // wait for first client to send request
      client1UpdateCompletedBaton.wait();
    }
    // initial sync request
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    // should get a valid result
    EXPECT_TRUE(operDelta.operDeltas()->size() > 0);
    if (switchId == 1) {
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(expectedInitialSync));
      // signal to server to start further updates
      client1InitialSyncBaton.post();
      // request next delta which also serves as ack for initial delta
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      // client 1 should get incremental oper delta
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta.getOperDelta()));
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      // client 1 should get next incremental oper delta
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta2.getOperDelta()));
      // signal client 2 to start
      client1UpdateCompletedBaton.post();
      // ack for previous oper delta
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
    } else {
      // client 2 should get full oper delta
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta3.getOperDelta()));
      // client 2 done. sever can shut down
      client2FinishedBaton.post();
      // Ack the initial delta. We just told the server it may stop, so this
      // races that stop and its result is not depended on.
      operDelta = getNextDeltaTolerateStop(
          getHwSwitchHandler(), switchId, getEmptyOper(), ackNum++);
    }
  };
  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

/*
 * Test with 2 clients.
 * - Both clients request oper delta.
 * - Client 1 updates delta to hw fully.
 * - Client 2 updates delta to hw partially.
 * - Server should rollback oper delta. Client1 gets
 *  full rollback update while client2 gets partial rollback update.
 */
TEST_F(SwSwitchHandlerTest, rollbackFailedHwSwitchUpdate) {
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();
  auto stateV1 = getInitialTestState();
  stateV1->publish();
  auto stateV2 = this->addAcl(stateV1, 1);
  stateV2->publish();

  /* initial update delta */
  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  deltas.emplace_back(stateV1, stateV2);
  auto delta2 = StateDelta(stateV1, stateV2);

  std::thread stateUpdateThread([this, &deltas, &stateV0]() {
    getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, false);
    // update should rollback
    EXPECT_EQ(stateReturned, stateV0);
    getHwSwitchHandler()->stop();
  });
  auto clientThreadBody = [this, &stateV0, &stateV1, &stateV2, &delta2](
                              int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    ASSERT_GT(operDelta.operDeltas()->size(), 0);
    if (switchId == 1) {
      /* return success */
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      // server should return rollback oper delta
      ASSERT_GT(operDelta.operDeltas()->size(), 0);
      auto expectedDelta = StateDelta(stateV1, stateV0);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(expectedDelta.getOperDelta()));
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
    } else {
      /* return failure with partial oper delta */
      auto operDeltaRet = std::make_unique<multiswitch::StateOperDelta>();
      operDeltaRet->operDeltas() = {delta2.getOperDelta()};
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, std::move(operDeltaRet), ackNum++);
      ASSERT_GT(operDelta.operDeltas()->size(), 0);

      // server should return a rollback oper which remove the partial
      // update
      auto expectedDelta = StateDelta(stateV2, stateV0);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(expectedDelta.getOperDelta()));
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
    }
  };
  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

/*
 * Test with 2 clients.
 * - Both clients request oper delta and updates state successfully
 * - Client 2 disconnects. Update should continue with client 1
 * - Client 1 reconnects and gets a full oper sync
 */
TEST_F(SwSwitchHandlerTest, reconnectingHwSwitch) {
  folly::Baton<> serverUpdateCompletedBaton;
  folly::Baton<> serverRestartBaton;
  folly::Baton<> client1InitialSyncBaton;
  folly::Baton<> client2InitialSyncBaton;
  folly::Baton<> clientResyncBaton;
  folly::Baton<> client1GotRestartDeltaBaton;
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();
  auto stateV1 = getInitialTestState();
  stateV1->publish();
  auto stateV2 = this->addAcl(stateV1, 1);
  stateV2->publish();
  auto stateV3 = this->addAcl(stateV2, 2);
  stateV3->publish();
  auto stateV4 = this->addAcl(stateV2, 3);
  stateV4->publish();
  auto stateV5 = this->addAcl(stateV4, 4);
  stateV5->publish();

  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  deltas.emplace_back(stateV1, stateV2);
  auto delta = StateDelta(stateV1, stateV2);
  std::vector<StateDelta> deltas2;
  deltas2.emplace_back(stateV2, stateV3);
  deltas2.emplace_back(stateV3, stateV4);
  auto delta2 = StateDelta(stateV3, stateV4);
  auto delta2Full = StateDelta(stateV0, stateV4);
  std::vector<StateDelta> deltas3;
  deltas3.emplace_back(stateV4, stateV5);
  auto delta3 = StateDelta(stateV4, stateV5);
  std::vector<StateDelta> deltas4;
  deltas4.emplace_back(stateV0, stateV5);
  auto delta4 = StateDelta(stateV0, stateV5);

  auto deltaOperDelta = delta.getOperDelta();
  auto delta3OperDelta = delta3.getOperDelta();

  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  getHwSwitchHandler()->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), sw_->getState()), false);

  // Snapshot what the server just sent as initial sync. Reading sw_->getState()
  // again from a client thread races any background state update and would
  // compare the received delta against a state the server never sent.
  auto expectedInitialSync =
      StateDelta(std::make_shared<SwitchState>(), sw_->getState())
          .getOperDelta();

  auto agentConfig = createAgentConfig();
  auto agentDirUtil = AgentDirectoryUtil(
      tmpDir_.path().string() + "/volatile",
      tmpDir_.path().string() + "/persist");
  auto newSwSwitch = createSwSwitchWithMultiSwitch(
      &agentConfig,
      &agentDirUtil,
      [](const SwitchID& switchId, const cfg::SwitchInfo& info, SwSwitch* sw) {
        return std::make_unique<MultiSwitchHwSwitchHandler>(switchId, info, sw);
      });
  auto newHwSwitchHandler = newSwSwitch->getHwSwitchHandler();
  newHwSwitchHandler->start();

  std::thread stateUpdateThread([this,
                                 &deltas,
                                 &deltas2,
                                 &deltas3,
                                 &deltas4,
                                 &stateV2,
                                 &stateV4,
                                 &stateV5,
                                 &serverUpdateCompletedBaton,
                                 &serverRestartBaton,
                                 &client1InitialSyncBaton,
                                 &client2InitialSyncBaton,
                                 &clientResyncBaton,
                                 &client1GotRestartDeltaBaton,
                                 &newHwSwitchHandler]() {
    // wait for initial sync to complete on both switches
    client1InitialSyncBaton.wait();
    client2InitialSyncBaton.wait();
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, true);
    EXPECT_EQ(stateReturned, stateV2);
    // Switch 2 cancels request
    getHwSwitchHandler()->notifyHwSwitchGracefulExit(2);
    // Switch 1 continues to operate
    stateReturned = getHwSwitchHandler()->stateChanged(deltas2, true);
    EXPECT_EQ(stateReturned, stateV4);
    serverUpdateCompletedBaton.post();
    // wait for client 2 to resync
    clientResyncBaton.wait();
    // resume normal updates
    stateReturned = getHwSwitchHandler()->stateChanged(deltas3, true);
    EXPECT_EQ(stateReturned, stateV5);
    getHwSwitchHandler()->stop();
    // server restarts
    serverRestartBaton.post();
    stateReturned = newHwSwitchHandler->stateChanged(deltas4, false);
    // The client asserts on delta4's contents, so it has to have taken it
    // before the handler goes away. Without this the stop() below can beat the
    // fetch and the call is rejected instead.
    client1GotRestartDeltaBaton.wait();
    newHwSwitchHandler->stop();
  });

  auto clientThreadBody = [this,
                           &deltaOperDelta,
                           &delta2,
                           &delta2Full,
                           &delta3OperDelta,
                           &delta4,
                           &expectedInitialSync,
                           &serverUpdateCompletedBaton,
                           &serverRestartBaton,
                           &client1InitialSyncBaton,
                           &client2InitialSyncBaton,
                           &clientResyncBaton,
                           &client1GotRestartDeltaBaton,
                           &newHwSwitchHandler](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    EXPECT_EQ(
        operDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(expectedInitialSync));

    if (switchId == 1) {
      client1InitialSyncBaton.post();
      // request next state delta. the empty oper passed serves as success
      // indicator for previous delta
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(deltaOperDelta));
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta2.getOperDelta()));
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta3OperDelta));
      // this request will be cancelled
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      EXPECT_EQ(operDelta.operDeltas()->size(), 0);
    } else {
      client2InitialSyncBaton.post();
      // request next state delta. the empty oper passed serves as success
      // indicator for previous delta
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(deltaOperDelta));
      // ack for previous request. this request will be cancelled
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
      // response for cancelled oper request
      EXPECT_EQ(operDelta.operDeltas()->size(), 0);

      // wait for server to finish second update which updates
      // only client1
      serverUpdateCompletedBaton.wait();
      // switch 2 reconnects
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), 0 /*lastUpdateSeqNum*/);
      // expect full response
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta2Full.getOperDelta()));
      // Server should continue from last update seqnum
      EXPECT_EQ(operDelta.seqNum().value(), ackNum);

      clientResyncBaton.post();
      // normal update resumes
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), operDelta.seqNum().value());
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta3OperDelta));

      // This request races the stop() the update thread has already issued
      // on the old handler, so it is either cancelled or rejected.
      operDelta = getNextDeltaTolerateStop(
          getHwSwitchHandler(),
          switchId,
          getEmptyOper(),
          operDelta.seqNum().value());
      EXPECT_EQ(operDelta.operDeltas()->size(), 0);

      // wait for server to restart
      serverRestartBaton.wait();
      operDelta = newHwSwitchHandler->getNextStateOperDelta(
          switchId, getEmptyOper(), operDelta.seqNum().value());
      // server should send full response again
      EXPECT_EQ(
          operDelta.operDeltas()->back(),
          *filter.filterWithSwitchStateRootPath(delta4.getOperDelta()));
      // seq number will reset
      EXPECT_EQ(operDelta.seqNum().value(), 1);
      // Release the update thread to stop the handler now that delta4 has been
      // taken and checked.
      client1GotRestartDeltaBaton.post();
      // Ack the delta and wait for the next one. The update thread calls
      // stop() two lines after handing us delta4, so this call races it. If we
      // get in first the request is cancelled and comes back empty; if stop()
      // gets in first the handler rejects the call outright. Both mean the
      // server is gone, and which one we see is pure timing, so accept either.
      // Letting the throw escape this thread terminates the whole process.
      try {
        operDelta = newHwSwitchHandler->getNextStateOperDelta(
            switchId, getEmptyOper(), operDelta.seqNum().value());
        EXPECT_EQ(operDelta.operDeltas()->size(), 0);
      } catch (const FbossError& ex) {
        EXPECT_THAT(ex.what(), ::testing::HasSubstr("syncer not started"));
      }
    }
  };

  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

TEST_F(SwSwitchHandlerTest, switchRunStateTest) {
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = getInitialTestState();
  folly::Baton<> client1StartBaton;
  folly::Baton<> client2StartBaton;

  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  auto delta = StateDelta(stateV0, stateV1);
  auto operDeltaExpected = delta.getOperDelta();
  auto checkState = [&](auto state,
                        std::optional<int32_t> switchId = std::nullopt) {
    WITH_RETRIES({
      auto hwSwitchState = getHwSwitchHandler()->getHwSwitchRunStates();
      for (const auto& [id, runState] : hwSwitchState) {
        if (switchId.has_value() && id != switchId.value()) {
          continue;
        }
        EXPECT_EVENTUALLY_EQ(runState, state);
      }
    });
  };
  checkState(SwitchRunState::UNINITIALIZED);

  // Setup initial state before clients connect to enable INITIALIZED state
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  getHwSwitchHandler()->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), sw_->getState()), false);

  auto initialSyncExpected =
      StateDelta(std::make_shared<SwitchState>(), sw_->getState())
          .getOperDelta();

  std::thread stateUpdateThread([this,
                                 &delta,
                                 &stateV1,
                                 &checkState,
                                 &client1StartBaton,
                                 &client2StartBaton]() {
    getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
    checkState(SwitchRunState::INITIALIZED);
    client1StartBaton.post();
    client2StartBaton.post();
    checkState(SwitchRunState::CONFIGURED);
    auto stateReturned = getHwSwitchHandler()->stateChanged(delta, false);
    EXPECT_EQ(stateReturned, stateV1);
    getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
    checkState(SwitchRunState::EXITING, 0);
    getHwSwitchHandler()->stop();
  });

  auto clientThreadBody = [this,
                           &initialSyncExpected,
                           &operDeltaExpected,
                           &client1StartBaton,
                           &client2StartBaton](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    EXPECT_EQ(
        operDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(initialSyncExpected));
    if (switchId == 1) {
      client1StartBaton.wait();
    } else {
      client2StartBaton.wait();
    }
    // request next state delta. the empty oper passed serves as success
    // indicator for previous delta
    operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    EXPECT_EQ(
        operDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(operDeltaExpected));
    // this request will be cancelled
    operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    // this request will be cancelled
    EXPECT_EQ(operDelta.operDeltas()->size(), 0);
  };

  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

// client should be able to get full sync when switch is in configured
// state without having to wait for next state delta
TEST_F(SwSwitchHandlerTest, initialSync) {
  folly::Baton<> client1Baton;
  folly::Baton<> client2Baton;
  auto sw = sw_.get();
  getHwSwitchHandler()->connected(SwitchID(1));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  getHwSwitchHandler()->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), sw->getState()), false);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());

  auto clientThreadBody = [&sw](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = sw->getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    EXPECT_TRUE(*operDelta.isFullState());
    auto initialDelta =
        StateDelta(std::make_shared<SwitchState>(), sw->getState());
    EXPECT_EQ(
        operDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(initialDelta.getOperDelta()));

    // ack for initial delta
    operDelta = sw->getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
  };

  std::thread clientRequestThread1([&]() {
    clientThreadBody(1);
    client1Baton.post();
  });
  std::thread clientRequestThread2([&]() {
    clientThreadBody(2);
    client2Baton.post();
  });

  client1Baton.wait();
  client2Baton.wait();

  clientRequestThread1.join();
  clientRequestThread2.join();

  sw->getHwSwitchHandler()->stop();
}

// client should be able to get full sync in the following case
// - swswitch is not ready when client connects
// - swswitch moves to configured state and client gets a full sync
TEST_F(SwSwitchHandlerTest, initialSyncSwSwitchNotConfigured) {
  folly::Baton<> client1Baton;
  folly::Baton<> client2Baton;
  auto sw = sw_.get();

  auto clientThreadBody = [&sw](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto initialOperDelta = sw->getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    EXPECT_TRUE(*initialOperDelta.isFullState());

    // ack for initial delta
    auto operDelta = sw->getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    auto initialDelta =
        StateDelta(std::make_shared<SwitchState>(), sw->getState());
    EXPECT_EQ(
        initialOperDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(initialDelta.getOperDelta()));
  };

  std::thread clientRequestThread1([&]() {
    clientThreadBody(1);
    client1Baton.post();
  });
  std::thread clientRequestThread2([&]() {
    clientThreadBody(2);
    client2Baton.post();
  });
  getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::PUBLISH_STATS);
  getHwSwitchHandler()->stateChanged(
      StateDelta(std::make_shared<SwitchState>(), sw->getState()), false);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());

  // check multi_switch status
  CounterCache counters(sw_.get());
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "multi_switch.sum.60", FLAGS_multi_switch);

  client1Baton.wait();
  client2Baton.wait();

  waitForStateUpdates(sw);

  clientRequestThread1.join();
  clientRequestThread2.join();

  sw->getHwSwitchHandler()->stop();
}

TEST_F(SwSwitchHandlerTest, connectionStatusCount) {
  CounterCache counters(sw_.get());
  auto checkConnectionStatus = [&](const auto& status) {
    AgentStats agentStats;
    getHwSwitchHandler()->fillHwAgentConnectionStatus(agentStats);
    EXPECT_EQ(agentStats.hwagentConnectionStatus()[0], status);
  };
  checkConnectionStatus(0);
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  counters.update();
  // Ideally the absolute value of connection status can be checked here.
  // However it would fail if all tests are run in a single shot as the
  // counter updates from previous tests can be carried over to this test.
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "switch.0.connection_status", 1);
  checkConnectionStatus(1);
  getHwSwitchHandler()->disconnected(SwitchID(1));
  counters.update();
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "switch.0.connection_status", -1);
  checkConnectionStatus(0);
}

TEST_F(SwSwitchHandlerTest, operAckTimeoutCount) {
  FLAGS_oper_delta_ack_timeout = 5;
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = getInitialTestState();

  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  auto delta = StateDelta(stateV0, stateV1);
  std::thread stateUpdateThread([this, &deltas]() {
    getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
    CounterCache counters(sw_.get());
    counters.update();
    auto prevCounter = counters.value(
        SwitchStats::kCounterPrefix + "switch." + folly::to<std::string>(0) +
        ".hwupdate_timeouts.sum.60");
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, false);
    WITH_RETRIES({
      counters.update();
      EXPECT_EVENTUALLY_EQ(
          counters.value(
              SwitchStats::kCounterPrefix + "switch." +
              folly::to<std::string>(0) + ".hwupdate_timeouts.sum.60"),
          prevCounter + 1);
    });
    getHwSwitchHandler()->stop();
  });

  auto clientThreadBody = [this](int64_t switchId) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    // only second switch sends ack
    if (switchId == 2) {
      operDelta = getHwSwitchHandler()->getNextStateOperDelta(
          switchId, getEmptyOper(), ackNum++);
    }
  };

  std::thread clientRequestThread1([&]() { clientThreadBody(1); });
  std::thread clientRequestThread2([&]() { clientThreadBody(2); });

  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

// HwSwitch 1 dies holding an unacked delta; its replacement is full-synced with
// the seqnum the dead session last reported.
TEST_F(SwSwitchHandlerTest, restartedHwSwitchAckAfterStaleSeqNum) {
  gflags::FlagSaver flagSaver;
  FLAGS_oper_delta_ack_timeout = 5;
  auto stateV0 = std::make_shared<SwitchState>();
  auto stateV1 = getInitialTestState();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);

  auto getEmptyOper = []() {
    auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
    operDelta->operDeltas() = {fsdb::OperDelta()};
    return operDelta;
  };
  folly::Baton<> deadSessionGotDelta;
  std::atomic<bool> done{false};

  std::thread deadSession([&]() {
    constexpr int64_t kStaleSeqNum = 2;
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        1, getEmptyOper(), kStaleSeqNum);
    EXPECT_FALSE(operDelta.operDeltas()->empty());
    deadSessionGotDelta.post();
  });

  auto ackEverything = [&](int64_t switchId) {
    int64_t lastSeqNum{0};
    while (!done.load()) {
      auto operDelta = getNextDeltaTolerateStop(
          getHwSwitchHandler(), switchId, getEmptyOper(), lastSeqNum);
      lastSeqNum = *operDelta.seqNum();
    }
  };
  std::thread switch2Session([&]() { ackEverything(2); });
  std::thread restartedSession([&]() {
    deadSessionGotDelta.wait();
    ackEverything(1);
  });

  getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
  const auto start = std::chrono::steady_clock::now();
  auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, false);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  done = true;
  getHwSwitchHandler()->stop();
  deadSession.join();
  switch2Session.join();
  restartedSession.join();

  EXPECT_EQ(stateReturned, stateV1);
  EXPECT_LT(elapsed, std::chrono::seconds(FLAGS_oper_delta_ack_timeout));
}

/*
 * Test with 1 client
 * - Client updates delta to hw partially.
 * - Server should rollback oper delta. Client gets partial rollback update
 */
TEST_F(SwSwitchHandlerTest, verifyRollback) {
  auto stateV0 = std::make_shared<SwitchState>();
  stateV0->publish();
  auto stateV1 = getInitialTestState();
  stateV1->publish();
  auto stateV2 = this->addAcl(stateV1, 1);
  stateV2->publish();
  auto stateV3 = this->addAcl(stateV2, 2);
  stateV3->publish();

  auto delta0 = StateDelta(stateV0, stateV1);
  auto delta1 = StateDelta(stateV1, stateV2);
  auto delta2 = StateDelta(stateV2, stateV3);

  std::vector<StateDelta> deltas;
  deltas.emplace_back(stateV0, stateV1);
  deltas.emplace_back(stateV1, stateV2);
  deltas.emplace_back(stateV2, stateV3);

  std::thread stateUpdateThread([this, &deltas, &stateV0]() {
    getHwSwitchHandler()->waitUntilAllHwSwitchesConnected();
    auto stateReturned = getHwSwitchHandler()->stateChanged(deltas, false);
    // update should rollback
    EXPECT_EQ(stateReturned, stateV0);
    getHwSwitchHandler()->stop();
  });
  auto clientThreadBody = [this, &stateV0](
                              int64_t switchId,
                              const StateDelta& failedDelta,
                              const std::shared_ptr<SwitchState>& newState) {
    int64_t ackNum{0};
    OperDeltaFilter filter((SwitchID(switchId)));
    // connect and get next state delta
    auto getEmptyOper = []() {
      auto operDelta = std::make_unique<multiswitch::StateOperDelta>();
      operDelta->operDeltas() = {fsdb::OperDelta()};
      return operDelta;
    };
    auto operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
    ASSERT_GT(operDelta.operDeltas()->size(), 0);
    /* return failure with partial oper delta */
    auto operDeltaRet = std::make_unique<multiswitch::StateOperDelta>();
    operDeltaRet->operDeltas() = {failedDelta.getOperDelta()};
    operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, std::move(operDeltaRet), ackNum++);
    ASSERT_GT(operDelta.operDeltas()->size(), 0);

    // server should return a rollback oper which remove the partial
    // update
    auto expectedDelta = StateDelta(newState, stateV0);
    EXPECT_EQ(
        operDelta.operDeltas()->back(),
        *filter.filterWithSwitchStateRootPath(expectedDelta.getOperDelta()));
    operDelta = getHwSwitchHandler()->getNextStateOperDelta(
        switchId, getEmptyOper(), ackNum++);
  };
  std::thread clientRequestThread1(
      [&]() { clientThreadBody(1, delta2, stateV3); });
  std::thread clientRequestThread2(
      [&]() { clientThreadBody(2, delta0, stateV1); });
  stateUpdateThread.join();
  clientRequestThread1.join();
  clientRequestThread2.join();
}

/*
 * Losing the last connection without a graceful exit is the crash path: cold
 * boot markers and an inline exit, not the scheduled warm shutdown.
 */
TEST_F(SwSwitchHandlerTest, lossOfLastHwSwitchConnectionExits) {
  auto deathTestStyle = ::testing::FLAGS_gtest_death_test_style;
  SCOPE_EXIT {
    ::testing::FLAGS_gtest_death_test_style = deathTestStyle;
  };
  // SwSwitch has threads running; re-exec the child instead of forking them.
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  getHwSwitchHandler()->connected(SwitchID(1));

  EXPECT_EXIT(
      getHwSwitchHandler()->disconnected(SwitchID(1)),
      ::testing::ExitedWithCode(EXIT_SUCCESS),
      "No active HwSwitch connections");
}

/*
 * waitUntilHwSwitchConnected() is satisfied by the first switch to register.
 * A caller that fans a state update out to every switch needs all of them:
 * with only some connected the rest return HWSWITCH_STATE_UPDATE_CANCELLED
 * rather than applying or failing the update, so no rollback happens and the
 * update looks like it succeeded.
 */
TEST_F(SwSwitchHandlerTest, waitUntilAllHwSwitchesConnectedNeedsEveryOne) {
  std::atomic<bool> allConnected{false};
  std::thread waiter([this, &allConnected]() {
    EXPECT_TRUE(getHwSwitchHandler()->waitUntilAllHwSwitchesConnected());
    allConnected = true;
  });

  getHwSwitchHandler()->connected(SwitchID(1));
  // Deterministic sync point, no sleep: the one-switch wait is satisfied by
  // switch 1 alone, so once it returns switch 1 is registered and switch 2 is
  // not. The all-switches wait cannot have returned in that state.
  EXPECT_TRUE(getHwSwitchHandler()->waitUntilHwSwitchConnected());
  EXPECT_FALSE(allConnected.load());

  getHwSwitchHandler()->connected(SwitchID(2));
  waiter.join();
  EXPECT_TRUE(allConnected.load());
}

/*
 * Every waiter blocked on the all-switches wait is woken when the last switch
 * connects, not just one of them and not only on the first connection.
 */
TEST_F(SwSwitchHandlerTest, everyWaiterWakesWhenTheLastSwitchConnects) {
  constexpr int kWaiters = 3;
  std::atomic<int> woken{0};
  std::vector<std::thread> waiters;
  waiters.reserve(kWaiters);
  for (int i = 0; i < kWaiters; ++i) {
    waiters.emplace_back([this, &woken]() {
      EXPECT_TRUE(getHwSwitchHandler()->waitUntilAllHwSwitchesConnected());
      ++woken;
    });
  }

  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  for (auto& waiter : waiters) {
    waiter.join();
  }
  EXPECT_EQ(woken.load(), kWaiters);
}

TEST_F(SwSwitchHandlerTest, gracefulExitOfLastHwSwitchExitsWithWarmBootState) {
  registerShutdownCounterHandler();
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));

  getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
  EXPECT_EQ(shutdownHandlerRunCount(), 0);

  getHwSwitchHandler()->notifyHwSwitchGracefulExit(2);
  EXPECT_EQ(shutdownHandlerRunCount(), 1);
  EXPECT_FALSE(coldBootMarkerExists());
}

TEST_F(SwSwitchHandlerTest, crashOfLastHwSwitchAfterGracefulExitColdBoots) {
  auto deathTestStyle = ::testing::FLAGS_gtest_death_test_style;
  SCOPE_EXIT {
    ::testing::FLAGS_gtest_death_test_style = deathTestStyle;
  };
  // SwSwitch has threads running; re-exec the child instead of forking them.
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  registerShutdownCounterHandler();
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));

  getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
  EXPECT_EQ(shutdownHandlerRunCount(), 0);

  // The crash path, not a warm shutdown: markers are written and the process
  // exits inline.
  EXPECT_EXIT(
      getHwSwitchHandler()->disconnected(SwitchID(2)),
      ::testing::ExitedWithCode(EXIT_SUCCESS),
      "No active HwSwitch connections");
}

namespace {
std::shared_ptr<SwitchState> addAcl1(
    const std::shared_ptr<SwitchState>& state) {
  auto newState = state->clone();
  auto aclEntry = make_shared<AclEntry>(1, std::string("acl1"));
  // StateUpdateValidator rejects an ACL entry with no qualifier
  aclEntry->setDscp(0x24);
  auto acls = newState->getAcls()->modify(&newState);
  acls->addNode(aclEntry, scope());
  return newState;
}
} // namespace

/*
 * After the last HwSwitch exits gracefully, the warm shutdown is only
 * scheduled, so an update cancelled by that exit fails while isExiting() is
 * still false. It must be dropped, not crash the agent.
 */
TEST_F(SwSwitchHandlerTest, updateFailureAfterLastGracefulExitIsDropped) {
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  // The handler only counts, so isExiting() stays false.
  registerShutdownCounterHandler();

  // read fb303 directly: CounterCache::checkDelta is a no-op in OSS builds
  constexpr auto kDropCounter = "hw_update_dropped_during_shutdown";
  auto dropsBefore = facebook::fb303::fbData->getCounters()[kDropCounter];

  getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
  getHwSwitchHandler()->notifyHwSwitchGracefulExit(2);
  ASSERT_TRUE(sw_->isGracefulShutdownRequested());
  ASSERT_FALSE(sw_->isExiting());

  // No oper delta client ever attached, so the update is cancelled and the
  // applied state differs from the desired one.
  sw_->updateStateBlocking("update during graceful shutdown", addAcl1);
  waitForStateUpdates(sw_.get());

  EXPECT_EQ(sw_->getState()->getAcls()->getNodeIf("acl1"), nullptr);
  EXPECT_EQ(
      facebook::fb303::fbData->getCounters()[kDropCounter], dropsBefore + 1);
  EXPECT_EQ(shutdownHandlerRunCount(), 1);
}

/*
 * Without a requested shutdown, an update that fails to apply and is not HW
 * failure protected is still fatal.
 */
TEST_F(SwSwitchHandlerTest, updateFailureWithoutShutdownRequestIsFatal) {
  auto deathTestStyle = ::testing::FLAGS_gtest_death_test_style;
  SCOPE_EXIT {
    ::testing::FLAGS_gtest_death_test_style = deathTestStyle;
  };
  // SwSwitch has threads running; re-exec the child instead of forking them.
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));
  sw_->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  ASSERT_FALSE(sw_->isGracefulShutdownRequested());

  EXPECT_DEATH(
      sw_->updateStateBlocking("update with no shutdown requested", addAcl1),
      "Failed to apply update to HW and the update is not marked");
}

TEST_F(SwSwitchHandlerTest, gracefulExitHonorsExitForAnyHwDisconnect) {
  gflags::FlagSaver flagSaver;
  auto deathTestStyle = ::testing::FLAGS_gtest_death_test_style;
  SCOPE_EXIT {
    ::testing::FLAGS_gtest_death_test_style = deathTestStyle;
  };
  // SwSwitch has threads running; re-exec the child instead of forking them.
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  FLAGS_exit_for_any_hw_disconnect = true;
  getHwSwitchHandler()->connected(SwitchID(1));
  getHwSwitchHandler()->connected(SwitchID(2));

  // Switch 2 is still connected, so SwSwitch would run on without switch 1.
  EXPECT_DEATH(
      getHwSwitchHandler()->notifyHwSwitchGracefulExit(1),
      "exit_for_any_hw_disconnect is enabled");
}

TEST_F(
    SwSwitchHandlerTest,
    gracefulExitOfLastHwSwitchIgnoresExitForAnyHwDisconnect) {
  gflags::FlagSaver flagSaver;
  FLAGS_exit_for_any_hw_disconnect = true;
  registerShutdownCounterHandler();
  getHwSwitchHandler()->connected(SwitchID(1));

  // Like disconnected(), losing the last connection exits SwSwitch the normal
  // way instead of tripping the flag: here the warm shutdown, no markers.
  getHwSwitchHandler()->notifyHwSwitchGracefulExit(1);
  EXPECT_EQ(shutdownHandlerRunCount(), 1);
  EXPECT_FALSE(coldBootMarkerExists());
}

TEST_F(SwSwitchHandlerTest, coldBootedHwSwitchAwaitingSyncForcesSwColdBoot) {
  auto thriftTable = installFakeThriftClientTable();
  thriftTable->setBootType(BootType::COLD_BOOT);
  thriftTable->setRunState(SwitchRunState::INITIALIZED);
  getHwSwitchHandler()->connected(SwitchID(1));

  EXPECT_THROW(
      sw_->exitIfConnectedHwSwitchColdBooted(), SwSwitchColdBootRequiredError);
  EXPECT_TRUE(checkFileExists(sw_->getDirUtil()->getSwColdBootOnceFile()));
}

TEST_F(SwSwitchHandlerTest, coldBootedHwSwitchAlreadyConfiguredStaysWarm) {
  auto thriftTable = installFakeThriftClientTable();
  thriftTable->setBootType(BootType::COLD_BOOT);
  thriftTable->setRunState(SwitchRunState::CONFIGURED);
  getHwSwitchHandler()->connected(SwitchID(1));

  EXPECT_NO_THROW(sw_->exitIfConnectedHwSwitchColdBooted());
  EXPECT_FALSE(checkFileExists(sw_->getDirUtil()->getSwColdBootOnceFile()));
}

TEST_F(SwSwitchHandlerTest, warmBootedHwSwitchAwaitingSyncStaysWarm) {
  auto thriftTable = installFakeThriftClientTable();
  thriftTable->setBootType(BootType::WARM_BOOT);
  thriftTable->setRunState(SwitchRunState::INITIALIZED);
  getHwSwitchHandler()->connected(SwitchID(1));

  EXPECT_NO_THROW(sw_->exitIfConnectedHwSwitchColdBooted());
  EXPECT_FALSE(checkFileExists(sw_->getDirUtil()->getSwColdBootOnceFile()));
}

TEST_F(SwSwitchHandlerTest, disconnectedHwSwitchIsNotQueriedForBootType) {
  auto thriftTable = installFakeThriftClientTable();
  thriftTable->setBootType(BootType::COLD_BOOT);
  thriftTable->setRunState(SwitchRunState::INITIALIZED);

  EXPECT_NO_THROW(sw_->exitIfConnectedHwSwitchColdBooted());
  EXPECT_EQ(thriftTable->getBootTypeQueryCount(), 0);
  EXPECT_FALSE(checkFileExists(sw_->getDirUtil()->getSwColdBootOnceFile()));
}

TEST_F(SwSwitchHandlerTest, unreachableHwSwitchBootTypeContinuesWarm) {
  auto thriftTable = installFakeThriftClientTable();
  thriftTable->setBootType(BootType::COLD_BOOT);
  thriftTable->setRunState(SwitchRunState::INITIALIZED);
  thriftTable->setShouldThrowOnGetBootType(true);
  getHwSwitchHandler()->connected(SwitchID(1));

  EXPECT_NO_THROW(sw_->exitIfConnectedHwSwitchColdBooted());
  EXPECT_EQ(thriftTable->getBootTypeQueryCount(), 5);
  EXPECT_FALSE(checkFileExists(sw_->getDirUtil()->getSwColdBootOnceFile()));
}
