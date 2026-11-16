// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AgentFsdbSyncManager.h"
#include "fboss/agent/DsfSubscription.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;
namespace facebook::fboss {
namespace {
constexpr auto kRemoteSwitchIdBegin = 4;
constexpr auto kSwitchIdGap = 4;
constexpr auto kSysPortBlockSize = 50;
constexpr auto kSysPortRangeMin =
    (kRemoteSwitchIdBegin / kSwitchIdGap) * kSysPortBlockSize;
constexpr auto intfV4AddrPrefix = "42.42.42.";
constexpr auto intfV6AddrPrefix = "42::";
constexpr auto kDynamicSysPortsOffset = 2;
std::shared_ptr<SystemPortMap> makeSysPortsForSwitchIds(
    const std::set<SwitchID>& remoteSwitchIds,
    int numSysPorts = 1) {
  auto sysPorts = std::make_shared<SystemPortMap>();
  for (auto switchId : remoteSwitchIds) {
    auto sysPortBegin =
        (switchId / kSwitchIdGap) * kSysPortBlockSize + kDynamicSysPortsOffset;
    for (auto sysPortId = sysPortBegin; sysPortId < sysPortBegin + numSysPorts;
         ++sysPortId) {
      sysPorts->addNode(makeSysPort(std::nullopt, sysPortId, switchId));
    }
  }
  return sysPorts;
}

std::shared_ptr<InterfaceMap> makeRifs(const SystemPortMap* sysPorts) {
  auto rifs = std::make_shared<InterfaceMap>();
  for (const auto& [id, sysPort] : *sysPorts) {
    auto rif = std::make_shared<Interface>(
        InterfaceID(id),
        std::optional<VlanID>(std::nullopt),
        folly::StringPiece("rif"),
        folly::MacAddress("01:02:03:04:05:06"),
        9000,
        false,
        true,
        cfg::InterfaceType::SYSTEM_PORT);
    folly::IPAddress ipv4(folly::to<std::string>(intfV4AddrPrefix, (id % 256)));
    folly::IPAddress ipv6(folly::to<std::string>(intfV6AddrPrefix, (id % 256)));
    Interface::Addresses addresses{{ipv4.asV4(), 31}, {ipv6.asV6(), 127}};
    state::NeighborEntries ndpTable, arpTable;
    for (auto isV6 : std::vector<bool>({true, false})) {
      state::NeighborEntryFields nbr;
      nbr.mac() = "01:02:03:04:05:06";
      cfg::PortDescriptor port;
      port.portId() = id;
      port.portType() = cfg::PortDescriptorType::SystemPort;
      nbr.portId() = port;
      nbr.interfaceId() = id;
      nbr.isLocal() = true;
      std::string ip = isV6 ? ipv6.str() : ipv4.str();
      nbr.ipaddress() = ip;
      if (isV6) {
        ndpTable.insert({ip, nbr});
      } else {
        arpTable.insert({ip, nbr});
      }
    }
    rif->setNdpTable(ndpTable);
    rif->setArpTable(arpTable);
    rif->setAddresses(addresses);
    rif->setScope(cfg::Scope::GLOBAL);
    rif->setRemoteInterfaceType(RemoteInterfaceType::DYNAMIC_ENTRY);
    rif->setRemoteLivenessStatus(LivenessStatus::LIVE);
    rifs->addNode(rif);
  }
  return rifs;
}
std::shared_ptr<Interface> makeRemoteIntf(
    const InterfaceID& intfId,
    const folly::IPAddressV4& v4Addr,
    uint8_t v4Mask,
    const folly::IPAddressV6& v6Addr,
    uint8_t v6Mask) {
  auto intf = std::make_shared<Interface>(
      intfId,
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("rif"),
      folly::MacAddress("01:02:03:04:05:06"),
      9000,
      false,
      true,
      cfg::InterfaceType::SYSTEM_PORT);
  Interface::Addresses addresses{{v4Addr, v4Mask}, {v6Addr, v6Mask}};
  intf->setAddresses(addresses);
  intf->setScope(cfg::Scope::GLOBAL);
  return intf;
}

bool toDelContainsPrefix(
    const std::vector<PrefixWithIntf>& toDel,
    const folly::CIDRNetwork& prefix) {
  return std::find_if(toDel.begin(), toDel.end(), [&prefix](const auto& entry) {
           return entry.first == prefix;
         }) != toDel.end();
}
} // namespace

template <uint16_t NumRemoteAsics, bool SubscribePatch>
struct TestParams {
  static auto constexpr kNumRemoteAsics = NumRemoteAsics;
  static auto constexpr kSubscribePatch = SubscribePatch;
};
using TestTypes = ::testing::Types<
    TestParams<1, true>,
    TestParams<1, false>,
    TestParams<2, true>,
    TestParams<2, false>>;

template <typename TestParam>
class DsfSubscriptionTest : public ::testing::Test {
 public:
  static auto constexpr kNumRemoteSwitchAsics = TestParam::kNumRemoteAsics;
  static auto constexpr kSubscribePatch = TestParam::kSubscribePatch;
  void SetUp() override {
    FLAGS_publish_state_to_fsdb = true;
    FLAGS_fsdb_sync_full_state = true;
    FLAGS_dsf_subscribe = false;
    FLAGS_dsf_subscribe_patch = kSubscribePatch;
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    fsdbTestServer_ = std::make_unique<fsdb::test::FsdbTestServer>();
    FLAGS_fsdbPort_high_priority = fsdbTestServer_->getFsdbPort();
    FLAGS_fsdbPort = fsdbTestServer_->getFsdbPort();
    pubSub_ = std::make_unique<fsdb::FsdbPubSubManager>("test-client");
    streamConnectPool_ = std::make_unique<folly::IOThreadPoolExecutor>(
        1,
        std::make_shared<folly::NamedThreadFactory>(
            "DsfSubscriberStreamConnect"));
    streamServePool_ = std::make_unique<folly::IOThreadPoolExecutor>(
        1,
        std::make_shared<folly::NamedThreadFactory>(
            "DsfSubscriberStreamServe"));
    hwUpdatePool_ = std::make_unique<folly::IOThreadPoolExecutor>(
        1, std::make_shared<folly::NamedThreadFactory>("DsfHwUpdate"));
  }

  cfg::SwitchConfig initialConfig() {
    auto config = testConfigA(cfg::SwitchType::VOQ);
    for (auto remoteSwitch : remoteSwitchIds()) {
      int remoteSwitchId = static_cast<int64_t>(remoteSwitch);
      auto dsfNode = makeDsfNodeCfg(remoteSwitchId);
      cfg::Range64 sysPortRange;
      sysPortRange.minimum() =
          remoteSwitchId / kSwitchIdGap * kSysPortBlockSize;
      sysPortRange.maximum() = *sysPortRange.minimum() + kSysPortBlockSize;
      dsfNode.systemPortRanges()->systemPortRanges()->push_back(sysPortRange);
      dsfNode.loopbackIps() = {"::1/128", "169.254.0.1/24"};
      config.dsfNodes()->insert(std::make_pair(remoteSwitchId, dsfNode));
    }
    return config;
  }

  void TearDown() override {
    fsdbTestServer_.reset();
    stopPublisher();
  }

  void createPublisher() {
    publisher_ = std::make_unique<AgentFsdbSyncManager>();
    publisher_->start();
  }
  std::set<SwitchID> remoteSwitchIds() const {
    std::set<SwitchID> remoteSwitchIds;
    for (auto i = 0; i < kNumRemoteSwitchAsics; ++i) {
      remoteSwitchIds.insert(SwitchID(kRemoteSwitchIdBegin + i * kSwitchIdGap));
    }
    return remoteSwitchIds;
  }
  std::shared_ptr<SystemPortMap> makeSysPorts(int numSysPorts = 1) const {
    return makeSysPortsForSwitchIds(remoteSwitchIds(), numSysPorts);
  }

  std::shared_ptr<SwitchState> makeSwitchState() const {
    auto state = std::make_shared<SwitchState>();
    auto sysPorts = std::make_shared<MultiSwitchSystemPortMap>();
    auto intfs = std::make_shared<MultiSwitchInterfaceMap>();
    for (auto remoteSwitchId : remoteSwitchIds()) {
      auto sysPortMap = makeSysPortsForSwitchIds({remoteSwitchId});
      sysPorts->addMapNode(sysPortMap, matcher(remoteSwitchId));
      auto intfMap = makeRifs(sysPortMap.get());
      intfs->addMapNode(intfMap, matcher(remoteSwitchId));
    }
    state->resetSystemPorts(sysPorts);
    state->resetIntfs(intfs);
    return state;
  }
  std::shared_ptr<SwitchState> makeSwitchState(
      const std::shared_ptr<SystemPortMap>& sysPorts,
      const std::shared_ptr<InterfaceMap>& intfs) const {
    auto state = std::make_shared<SwitchState>();
    auto mSysPorts = std::make_shared<MultiSwitchSystemPortMap>();
    auto mIntfs = std::make_shared<MultiSwitchInterfaceMap>();
    CHECK(!sysPorts->empty());
    HwSwitchMatcher matcher(
        std::unordered_set<SwitchID>{
            sysPorts->cbegin()->second->getSwitchId()});
    mSysPorts->addMapNode(sysPorts, matcher);
    mIntfs->addMapNode(intfs, matcher);
    state->resetSystemPorts(mSysPorts);
    state->resetIntfs(mIntfs);
    return state;
  }
  void publishSwitchState(std::shared_ptr<SwitchState> state) {
    CHECK(publisher_);
    publisher_->stateUpdated(StateDelta(std::shared_ptr<SwitchState>(), state));
  }

  void updateDsfSubscriberState(
      const std::string& nodeName,
      fsdb::FsdbSubscriptionState newState) {
    publisher_->updateDsfSubscriberState(
        DsfSubscription::makeRemoteEndpoint(nodeName, folly::IPAddress("::1")),
        fsdb::FsdbSubscriptionState::DISCONNECTED, // old state doesn't matter
        newState);
  }

  void stopPublisher(bool gr = false) {
    if (publisher_) {
      publisher_->stop(gr);
      publisher_.reset();
    }
  }
  std::unique_ptr<DsfSubscription> createSubscription(
      const std::string& remoteEndpoint = "remote",
      const folly::IPAddress& remoteIp = folly::IPAddress("::1")) {
    fsdb::SubscriptionOptions opts{
        "test-sub", false /* subscribeStats */, FLAGS_dsf_gr_hold_time};
    return std::make_unique<DsfSubscription>(
        std::move(opts),
        streamConnectPool_->getEventBase(),
        streamServePool_->getEventBase(),
        hwUpdatePool_->getEventBase(),
        "local",
        remoteEndpoint,
        remoteSwitchIds(),
        folly::IPAddress("::1"),
        remoteIp,
        sw_);
  }

  HwSwitchMatcher matcher(uint32_t switchID = kRemoteSwitchIdBegin) const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchID)}));
  }

  std::shared_ptr<SystemPortMap> getRemoteSystemPorts() const {
    return sw_->getState()->getRemoteSystemPorts()->getAllNodes();
  }
  std::shared_ptr<InterfaceMap> getRemoteInterfaces() const {
    return sw_->getState()->getRemoteInterfaces()->getAllNodes();
  }
  DsfSessionState dsfSessionState() const {
    return *subscription_->dsfSessionThrift().state();
  }

  // Run multiple threads concurrently with synchronized start
  void runConcurrentThreads(std::vector<std::function<void()>>& threadFns) {
    std::atomic<bool> startFlag{false};
    std::vector<std::thread> threads;
    threads.reserve(threadFns.size());

    for (auto& fn : threadFns) {
      threads.emplace_back([&startFlag, fn]() {
        while (!startFlag.load()) {
          std::this_thread::yield();
        }
        fn();
      });
    }

    // Start all threads simultaneously
    startFlag.store(true);

    // Wait for all threads to complete
    for (auto& thread : threads) {
      thread.join();
    }
  }

  // Wait for event base queue to drain
  void waitForQueueDrain() {
    WITH_RETRIES({
      ASSERT_EVENTUALLY_EQ(
          this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
    });
  }

  // Verify final state after updates
  void verifySysPortAndRif(
      size_t beforeNumSysPorts,
      size_t beforeNumRifs,
      int expectedSysPortsPerSwitch) {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          this->getRemoteSystemPorts()->size(),
          beforeNumSysPorts +
              (this->kNumRemoteSwitchAsics * expectedSysPortsPerSwitch));
      EXPECT_EVENTUALLY_EQ(
          this->getRemoteInterfaces()->size(),
          beforeNumRifs +
              (this->kNumRemoteSwitchAsics * expectedSysPortsPerSwitch));
    });
  }

 protected:
  void verifyRemoteIntfRouteDelta(
      StateDelta delta,
      int expectedRouteAdded,
      int expectedRouteDeleted) {
    auto routesAdded = 0;
    auto routesDeleted = 0;

    for (const auto& fibsInfoDelta : delta.getFibsInfoDelta()) {
      for (const auto& routeDelta : fibsInfoDelta.getFibsMapDelta()) {
        DeltaFunctions::forEachChanged(
            routeDelta.getFibDelta<folly::IPAddressV4>(),
            [&](const auto& /*oldNode*/, const auto& /*newNode*/) {},
            [&](const auto& added) {
              EXPECT_TRUE(added->isConnected());
              EXPECT_EQ(added->getID().rfind(intfV4AddrPrefix, 0), 0);
              routesAdded++;
            },
            [&](const auto& /*removed*/) { routesDeleted++; });

        DeltaFunctions::forEachChanged(
            routeDelta.getFibDelta<folly::IPAddressV6>(),
            [&](const auto& /*oldNode*/, const auto& /*newNode*/) {},
            [&](const auto& added) {
              EXPECT_TRUE(added->isConnected());
              EXPECT_EQ(added->getID().rfind(intfV6AddrPrefix, 0), 0);
              routesAdded++;
            },
            [&](const auto& /*removed*/) { routesDeleted++; });
      }
    }
    EXPECT_EQ(routesAdded, expectedRouteAdded);
    EXPECT_EQ(routesDeleted, expectedRouteDeleted);
  }

  std::unique_ptr<fsdb::test::FsdbTestServer> fsdbTestServer_;
  std::unique_ptr<AgentFsdbSyncManager> publisher_;
  std::unique_ptr<fsdb::FsdbPubSubManager> pubSub_;
  std::unique_ptr<folly::IOThreadPoolExecutor> streamConnectPool_;
  std::unique_ptr<folly::IOThreadPoolExecutor> streamServePool_;
  std::unique_ptr<folly::IOThreadPoolExecutor> hwUpdatePool_;
  std::unique_ptr<HwTestHandle> handle_;
  std::shared_ptr<DsfSubscription> subscription_;
  SwSwitch* sw_;
};

TYPED_TEST_SUITE(DsfSubscriptionTest, TestTypes);

TYPED_TEST(DsfSubscriptionTest, Connect) {
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);
  std::optional<std::map<SwitchID, std::shared_ptr<SystemPortMap>>>
      recvSysPorts;
  std::optional<std::map<SwitchID, std::shared_ptr<InterfaceMap>>> recvIntfs;
  this->subscription_ = this->createSubscription();
  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });

  this->updateDsfSubscriberState(
      "local", fsdb::FsdbSubscriptionState::CONNECTED);
  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      this->dsfSessionState(), DsfSessionState::ESTABLISHED));
}

TYPED_TEST(DsfSubscriptionTest, ConnectDisconnect) {
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);
  std::optional<std::map<SwitchID, std::shared_ptr<SystemPortMap>>>
      recvSysPorts;
  std::optional<std::map<SwitchID, std::shared_ptr<InterfaceMap>>> recvIntfs;
  this->subscription_ = this->createSubscription();

  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });

  this->stopPublisher();
  WITH_RETRIES(
      ASSERT_EVENTUALLY_EQ(this->dsfSessionState(), DsfSessionState::CONNECT));
}

TYPED_TEST(DsfSubscriptionTest, GR) {
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);
  FLAGS_dsf_gr_hold_time = 5;
  this->subscription_ = this->createSubscription();

  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });

  this->stopPublisher(true);
  this->createPublisher();
  this->publishSwitchState(state);

  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
    auto remoteRifs = this->getRemoteInterfaces();
    for (const auto [_, rif] : std::as_const(*remoteRifs)) {
      ASSERT_EVENTUALLY_EQ(rif->getNdpTable()->size(), 1);
      ASSERT_EVENTUALLY_EQ(rif->getArpTable()->size(), 1);
    }
  });
  auto assertStatus = [this](LivenessStatus expectedStatus) {
    auto assertObjStatus = [expectedStatus](const auto& objs) {
      std::for_each(
          objs->begin(), objs->end(), [expectedStatus](const auto& idAndObj) {
            if (!idAndObj.second->isStatic()) {
              EXPECT_EQ(
                  idAndObj.second->getRemoteLivenessStatus(), expectedStatus);
            }
          });
    };
    assertObjStatus(this->getRemoteSystemPorts());
    assertObjStatus(this->getRemoteInterfaces());
  };
  CounterCache counters(this->sw_);
  // Should be LIVE before GR expire
  assertStatus(LivenessStatus::LIVE);
  this->stopPublisher(true);
  auto grExpiredCounter =
      SwitchStats::kCounterPrefix + "dsfsession_gr_expired.sum.60";
  WITH_RETRIES({
    counters.update();
    ASSERT_EVENTUALLY_EQ(this->dsfSessionState(), DsfSessionState::CONNECT);
    ASSERT_EVENTUALLY_TRUE(counters.checkExist(grExpiredCounter));
    ASSERT_EVENTUALLY_EQ(counters.value(grExpiredCounter), 1);

    auto remoteRifs = this->getRemoteInterfaces();
    for (const auto [_, rif] : std::as_const(*remoteRifs)) {
      // Neighbors should get pruned
      if (!rif->isStatic()) {
        ASSERT_EVENTUALLY_EQ(rif->getNdpTable()->size(), 0);
        ASSERT_EVENTUALLY_EQ(rif->getArpTable()->size(), 0);
      }
    }
  });
  // Should be STATLE after GR expire
  assertStatus(LivenessStatus::STALE);
}

TYPED_TEST(DsfSubscriptionTest, DataUpdate) {
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);

  std::optional<std::map<SwitchID, std::shared_ptr<SystemPortMap>>>
      recvSysPorts;
  std::optional<std::map<SwitchID, std::shared_ptr<InterfaceMap>>> recvIntfs;
  this->subscription_ = this->createSubscription();

  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });

  auto sysPort2 = makeSysPort(
      std::nullopt,
      SystemPortID(kSysPortRangeMin + kDynamicSysPortsOffset + 1),
      kRemoteSwitchIdBegin);
  auto portMap = state->getSystemPorts()->modify(&state);
  portMap->addNode(sysPort2, this->matcher());
  this->publishSwitchState(state);

  WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
      this->getRemoteSystemPorts()->size(),
      (this->kNumRemoteSwitchAsics * 2) + 1));
}

TYPED_TEST(DsfSubscriptionTest, updateFailed) {
  CounterCache counters(this->sw_);
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);

  std::optional<std::map<SwitchID, std::shared_ptr<SystemPortMap>>>
      recvSysPorts;
  std::optional<std::map<SwitchID, std::shared_ptr<InterfaceMap>>> recvIntfs;
  this->subscription_ = this->createSubscription();

  WITH_RETRIES({
    // 2 sys ports added per ASIC - 1 RCY, 1 Dynamic
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), this->kNumRemoteSwitchAsics * 2);
    ASSERT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        this->getRemoteSystemPorts()->size());
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });
  waitForStateUpdates(this->sw_);

  // Fail HW update by returning current state
  EXPECT_HW_CALL(this->sw_, stateChangedImpl(_, _))
      .Times(::testing::AtLeast(1))
      .WillOnce(Return(this->sw_->getState()));
  auto sysPort2 = makeSysPort(
      std::nullopt,
      SystemPortID(kSysPortRangeMin + kDynamicSysPortsOffset + 1),
      kRemoteSwitchIdBegin);
  auto portMap = state->getSystemPorts()->modify(&state);
  portMap->addNode(sysPort2, this->matcher());
  this->publishSwitchState(state);
  auto dsfUpdateFailedCounter =
      SwitchStats::kCounterPrefix + "dsf_update_failed.sum.60";
  WITH_RETRIES({
    counters.update();
    ASSERT_EVENTUALLY_TRUE(counters.checkExist(dsfUpdateFailedCounter));
    ASSERT_EVENTUALLY_EQ(counters.value(dsfUpdateFailedCounter), 1);
  });
}

TYPED_TEST(DsfSubscriptionTest, updateWithRollbackProtection) {
  auto sysPorts = makeSysPortsForSwitchIds(
      std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}), 2);
  auto rifs = makeRifs(sysPorts.get());

  std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
  std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
  switchId2SystemPorts[SwitchID(kRemoteSwitchIdBegin)] = sysPorts;
  switchId2Intfs[SwitchID(kRemoteSwitchIdBegin)] = rifs;

  // Add remote interfaces
  const auto prevState = this->sw_->getState();
  this->subscription_ = this->createSubscription();
  this->subscription_->updateWithRollbackProtection(
      switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);

  const auto addedState = this->sw_->getState();
  this->verifyRemoteIntfRouteDelta(StateDelta(prevState, addedState), 2, 0);

  // Reapply config - ensure no change in remote interfaces
  this->sw_->applyConfig("Reload initial config", this->initialConfig());
  const auto configReappliedState = this->sw_->getState();
  this->verifyRemoteIntfRouteDelta(
      StateDelta(addedState, configReappliedState), 0, 0);

  // Change remote interface routes
  switchId2SystemPorts[SwitchID(kRemoteSwitchIdBegin)] =
      makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}), 2);
  switchId2Intfs[SwitchID(kRemoteSwitchIdBegin)] = makeRifs(sysPorts.get());

  const auto sysPort1Id = kSysPortRangeMin + kDynamicSysPortsOffset;
  Interface::Addresses updatedAddresses{
      {folly::IPAddressV4(
           folly::to<std::string>(intfV4AddrPrefix, (sysPort1Id % 256 + 10))),
       31},
      {folly::IPAddressV6(
           folly::to<std::string>(intfV6AddrPrefix, (sysPort1Id % 256 + 10))),
       127}};
  switchId2Intfs[SwitchID(kRemoteSwitchIdBegin)]
      ->find(sysPort1Id)
      ->second->setAddresses(updatedAddresses);

  this->subscription_->updateWithRollbackProtection(
      switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);

  auto modifiedState = this->sw_->getState();
  this->verifyRemoteIntfRouteDelta(
      StateDelta(configReappliedState, modifiedState), 2, 2);

  // Remove remote interface routes
  switchId2SystemPorts[SwitchID(kRemoteSwitchIdBegin)] =
      std::make_shared<SystemPortMap>();
  switchId2Intfs[SwitchID(kRemoteSwitchIdBegin)] =
      std::make_shared<InterfaceMap>();

  this->subscription_->updateWithRollbackProtection(
      switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);

  waitForStateUpdates(this->sw_);
  auto deletedState = this->sw_->getState();
  this->verifyRemoteIntfRouteDelta(
      StateDelta(modifiedState, deletedState), 0, 2);
}

TYPED_TEST(DsfSubscriptionTest, setupNeighbors) {
  this->subscription_ = this->createSubscription();
  auto updateAndCompareTables = [this](
                                    const auto& sysPorts,
                                    const auto& rifs,
                                    bool publishState,
                                    bool noNeighbors = false) {
    if (publishState) {
      rifs->publish();
    }

    // this->subscription_->updateWithRollbackProtection is expected to set
    // isLocal to False, and rest of the structure should remain the same.
    auto expectedRifs = InterfaceMap(rifs->toThrift());
    for (auto intfIter : expectedRifs) {
      auto& intf = intfIter.second;
      for (auto& ndpEntry : *intf->getNdpTable()) {
        ndpEntry.second->setIsLocal(false);
        ndpEntry.second->setNoHostRoute(false);
      }
      for (auto& arpEntry : *intf->getArpTable()) {
        arpEntry.second->setIsLocal(false);
        arpEntry.second->setNoHostRoute(false);
      }
    }

    std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
    std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
    switchId2SystemPorts[SwitchID(kRemoteSwitchIdBegin)] = sysPorts;
    switchId2Intfs[SwitchID(kRemoteSwitchIdBegin)] = rifs;

    this->subscription_->updateWithRollbackProtection(
        switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);

    waitForStateUpdates(this->sw_);

    for (const auto& [_, intfMap] :
         std::as_const(*this->sw_->getState()->getRemoteInterfaces())) {
      for (const auto& [_, localRif] : std::as_const(*intfMap)) {
        if (localRif->isStatic()) {
          continue;
        }
        const auto& expectedRif = expectedRifs.at(localRif->getID());
        // Since resolved timestamp is only set locally, update expectedRifs to
        // the same timestamp such that they're the same, for both arp and ndp.
        for (const auto& [_, arp] : std::as_const(*localRif->getArpTable())) {
          EXPECT_TRUE(arp->getResolvedSince().has_value());
          if (arp->getResolvedSince().has_value()) {
            expectedRif->getArpTable()
                ->at(arp->getID())
                ->setResolvedSince(*arp->getResolvedSince());
          }
        }
        for (const auto& [_, ndp] : std::as_const(*localRif->getNdpTable())) {
          EXPECT_TRUE(ndp->getResolvedSince().has_value());
          if (ndp->getResolvedSince().has_value()) {
            expectedRif->getNdpTable()
                ->at(ndp->getID())
                ->setResolvedSince(*ndp->getResolvedSince());
          }
        }
      }
    }
    // neighbor entries are modified to set isLocal=false
    // Thus, if neighbor table is non-empty, programmed vs. actually
    // programmed would be unequal for published state.
    // for unpublished state, the passed state would be modified, and thus,
    // programmed vs actually programmed state would be equal.
    EXPECT_TRUE(
        rifs->toThrift() !=
            this->sw_->getState()
                ->getRemoteInterfaces()
                ->getAllNodes()
                ->toThrift() ||
        noNeighbors || !publishState);
  };

  auto verifySetupNeighbors = [&](bool publishState) {
    {
      // No neighbors
      auto sysPorts = makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}));
      auto rifs = makeRifs(sysPorts.get());
      updateAndCompareTables(
          sysPorts, rifs, publishState, true /* noNeighbors */);
    }
    {
      // add neighbors
      auto sysPorts = makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}));
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + kDynamicSysPortsOffset;
      auto [ndpTable, arpTable] = makeNbrs();
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // update neighbors
      auto sysPorts = makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}));
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + kDynamicSysPortsOffset;
      auto [ndpTable, arpTable] = makeNbrs();
      ndpTable.begin()->second.mac() = "06:05:04:03:02:01";
      arpTable.begin()->second.mac() = "06:05:04:03:02:01";
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // delete neighbors
      auto sysPorts = makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}));
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + kDynamicSysPortsOffset;
      auto [ndpTable, arpTable] = makeNbrs();
      ndpTable.erase(ndpTable.begin());
      arpTable.erase(arpTable.begin());
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // clear neighbors
      auto sysPorts = makeSysPortsForSwitchIds(
          std::set<SwitchID>({SwitchID(kRemoteSwitchIdBegin)}));
      auto rifs = makeRifs(sysPorts.get());
      updateAndCompareTables(
          sysPorts, rifs, publishState, true /* noNeighbors */);
    }
  };

  verifySetupNeighbors(false /* publishState */);
  verifySetupNeighbors(true /* publishState */);
}

TYPED_TEST(DsfSubscriptionTest, DataUpdateForLocalSwitchId) {
  CounterCache counters(this->sw_);
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);
  this->subscription_ = this->createSubscription();
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });
  auto localSwitchId = *this->sw_->getSwitchInfoTable().getSwitchIDs().begin();
  auto sysPorts = makeSysPortsForSwitchIds(std::set<SwitchID>({localSwitchId}));
  auto rifs = makeRifs(sysPorts.get());
  state = this->makeSwitchState(sysPorts, rifs);
  this->publishSwitchState(state);
  auto dsfUpdateFailedCounter =
      SwitchStats::kCounterPrefix + "dsf_update_failed.sum.60";
  WITH_RETRIES({
    counters.update();
    ASSERT_EVENTUALLY_TRUE(counters.checkExist(dsfUpdateFailedCounter));
    ASSERT_EVENTUALLY_GE(counters.value(dsfUpdateFailedCounter), 1);
  });
}

TYPED_TEST(DsfSubscriptionTest, FirstUpdateFailsValidation) {
  CounterCache counters(this->sw_);
  auto localSwitchId = *this->sw_->getSwitchInfoTable().getSwitchIDs().begin();
  auto sysPorts = makeSysPortsForSwitchIds(std::set<SwitchID>({localSwitchId}));
  auto rifs = makeRifs(sysPorts.get());
  auto state = this->makeSwitchState(sysPorts, rifs);
  this->createPublisher();
  this->publishSwitchState(state);
  this->subscription_ = this->createSubscription();
  auto dsfUpdateFailedCounter =
      SwitchStats::kCounterPrefix + "dsf_update_failed.sum.60";
  WITH_RETRIES({
    counters.update();
    ASSERT_EVENTUALLY_TRUE(counters.checkExist(dsfUpdateFailedCounter));
    ASSERT_EVENTUALLY_GE(counters.value(dsfUpdateFailedCounter), 1);
  });
  // Session should never get established now, since connection establish
  // should itself fail
  EXPECT_NE(this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  EXPECT_NE(this->dsfSessionState(), DsfSessionState::REMOTE_DISCONNECTED);
  EXPECT_NE(this->dsfSessionState(), DsfSessionState::ESTABLISHED);
}

TYPED_TEST(DsfSubscriptionTest, BogusIntfAdd) {
  CounterCache counters(this->sw_);
  auto localSwitchId = *this->sw_->getSwitchInfoTable().getSwitchIDs().begin();
  auto sysPorts = this->makeSysPorts();
  auto rifs = makeRifs(sysPorts.get());
  auto state = this->makeSwitchState(sysPorts, rifs);
  this->createPublisher();
  this->publishSwitchState(state);
  this->subscription_ = this->createSubscription();
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->dsfSessionState(), DsfSessionState::WAIT_FOR_REMOTE);
  });
  auto newSysPorts = this->makeSysPorts(2);
  auto newRifs = makeRifs(newSysPorts.get());
  auto newState = this->makeSwitchState(sysPorts, newRifs);
  this->publishSwitchState(newState);
  auto dsfUpdateFailedCounter =
      SwitchStats::kCounterPrefix + "dsf_update_failed.sum.60";
  WITH_RETRIES({
    counters.update();
    ASSERT_EVENTUALLY_TRUE(counters.checkExist(dsfUpdateFailedCounter));
    ASSERT_EVENTUALLY_GE(counters.value(dsfUpdateFailedCounter), 1);
  });
}

TYPED_TEST(DsfSubscriptionTest, RemoteEndpointString) {
  this->createPublisher();
  auto state = this->makeSwitchState();
  this->publishSwitchState(state);
  std::optional<std::map<SwitchID, std::shared_ptr<SystemPortMap>>>
      recvSysPorts;
  std::optional<std::map<SwitchID, std::shared_ptr<InterfaceMap>>> recvIntfs;
  this->subscription_ = this->createSubscription();
  std::unique_ptr<DsfSubscription> subscription2 =
      this->createSubscription("remote2", folly::IPAddress("::2"));

  std::string expectedEndpointStr = "remote_::1";
  EXPECT_EQ(this->subscription_->remoteEndpointStr(), expectedEndpointStr);
  std::string expectedEndpoint2Str = "remote2_::2";
  EXPECT_EQ(subscription2->remoteEndpointStr(), expectedEndpoint2Str);
}

TYPED_TEST(DsfSubscriptionTest, QueueDsfUpdateRaceCondition) {
  // Test to reproduce race condition in queueDsfUpdate where:
  // 1. queueDsfUpdate is called - one update queued to hwUpdateEvb_
  // 2. processGRHoldTimerExpired is invoked - queues another event
  // 3. queueDsfUpdate is called again - (prior to the fix) if update from 1 not
  // yet processed, it will update nextDsfUpdate_ instead of queuing to
  // hwUpdateEvb_ The effect is only 2 events in the queue instead of 3, and the
  // last event clears all entries, so update from step 3 is lost.

  this->subscription_ = this->createSubscription();

  // Use Baton to block hwUpdateEvb_ and simulate the race condition
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });
  // Wait for the blocking event to be in progress
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  auto initialQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();
  auto finalNumSysPorts = 2;

  // Step 1: First queueDsfUpdate call - should queue one event
  queueSysPortUpdate(1 /* numSysPorts */);

  // Verify one event was queued
  auto queueSizeAfterFirst =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  EXPECT_EQ(queueSizeAfterFirst, initialQueueSize + 1);

  // Step 2: Call processGRHoldTimerExpired - should override the DsfUpdate
  this->subscription_->processGRHoldTimerExpired();
  auto queueSizeAfterGR =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  EXPECT_EQ(queueSizeAfterGR, initialQueueSize + 1);

  // Step 3: Second queueDsfUpdate call - should override the DsfUpdate
  auto finalQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  queueSysPortUpdate(finalNumSysPorts);
  EXPECT_EQ(finalQueueSize, initialQueueSize + 1);

  // Unblock the event base to process all queued events
  baton.post();

  // Wait for all events to be processed and exit
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  WITH_RETRIES({
    auto remoteRifs = this->getRemoteInterfaces();
    EXPECT_EVENTUALLY_EQ(
        remoteRifs->size(),
        beforeNumRifs + (this->kNumRemoteSwitchAsics * finalNumSysPorts));
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + (this->kNumRemoteSwitchAsics * finalNumSysPorts));
    for (const auto [_, rif] : std::as_const(*remoteRifs)) {
      EXPECT_EVENTUALLY_EQ(rif->getNdpTable()->size(), 1);
      EXPECT_EVENTUALLY_EQ(rif->getArpTable()->size(), 1);
    }
  });
}

TYPED_TEST(DsfSubscriptionTest, MultipleQueuedDsfUpdatesCoalesce) {
  // Test that multiple DSF updates queued in quick succession are coalesced
  // when no GR event occurs. Only the latest update should be processed.
  this->subscription_ = this->createSubscription();

  // Use Baton to block hwUpdateEvb_
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });

  // Wait for the blocking event to be in progress
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  auto initialQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Queue first update with 1 sysport per switch
  queueSysPortUpdate(1 /*numSysPorts*/);

  // First update should add one event
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Queue second update with 2 sysports per switch - should override the
  // dsfUpdate
  queueSysPortUpdate(2 /*numSysPorts*/);

  // Second update should NOT add a new event (coalesced with first)
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Queue third update with 3 sysports per switch - should override the
  // dsfUpdate
  queueSysPortUpdate(3 /*numSysPorts*/);

  // Third update should still be coalesced - only 1 event total
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Unblock the event base to process all queued events
  baton.post();

  // Wait for all events to be processed
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Verify the final state reflects the LAST update (3 sysports per switch)
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + (this->kNumRemoteSwitchAsics * 3));
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        beforeNumRifs + (this->kNumRemoteSwitchAsics * 3));
  });
}

TYPED_TEST(DsfSubscriptionTest, GREventSeparatesUpdates) {
  this->subscription_ = this->createSubscription();

  // Use Baton to block hwUpdateEvb_
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });

  // Wait for the blocking event to be in progress
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  auto initialQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Queue first update
  queueSysPortUpdate(1 /*numSysPorts*/);

  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Trigger GR event - this should queue GR update
  this->subscription_->processGRHoldTimerExpired();

  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Queue second update
  queueSysPortUpdate(2 /*numSysPorts*/);

  // Should have only 1 event with second update
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Queue third update - should coalesce with second (no GR between them)
  queueSysPortUpdate(3 /*numSysPorts*/);

  // Still 1 events (third should overwrite the second)
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Unblock the event base to process all queued events
  baton.post();

  // Wait for all events to be processed
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Verify the final state reflects the last update (3 sysports per switch)
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + (this->kNumRemoteSwitchAsics * 3));
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        beforeNumRifs + (this->kNumRemoteSwitchAsics * 3));
  });
}

TYPED_TEST(DsfSubscriptionTest, MultipleGREventsSeparateUpdates) {
  this->subscription_ = this->createSubscription();

  // Use Baton to block hwUpdateEvb_
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });

  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  auto initialQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();
  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Sequence: Update1 -> GR1 -> Update2 -> GR2 -> Update3
  // Expected events: only 1 - Update 3 will overwrite previous updates

  // Update 1
  queueSysPortUpdate(1 /*numSysPorts*/);
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // GR 1
  this->subscription_->processGRHoldTimerExpired();
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Update 2
  queueSysPortUpdate(2 /*numSysPorts*/);
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // GR 2
  this->subscription_->processGRHoldTimerExpired();
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Update 3
  queueSysPortUpdate(3 /*numSysPorts*/);
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Unblock the event base to process all queued events
  baton.post();

  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Final state should reflect last update
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + (this->kNumRemoteSwitchAsics * 3));
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        beforeNumRifs + (this->kNumRemoteSwitchAsics * 3));
  });
}

TYPED_TEST(DsfSubscriptionTest, UpdateSkippedWhenNewerUpdatesQueued) {
  this->subscription_ = this->createSubscription();

  // Use Baton to block hwUpdateEvb_ and simulate the race condition
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });
  // Wait for the blocking event to be in progress
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  auto initialQueueSize =
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Queue Update1 with 10 sysports per switch
  queueSysPortUpdate(10);
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Trigger GR event
  this->subscription_->processGRHoldTimerExpired();
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Queue Update2 with 2 sysports per switch
  queueSysPortUpdate(2);
  EXPECT_EQ(
      this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(),
      initialQueueSize + 1);

  // Record the initial state generation
  auto initialStateGeneration = this->sw_->getState()->getGeneration();

  // Unblock the event base to process all queued events
  baton.post();

  // Wait for all events to be processed
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Wait for state updates to complete
  waitForStateUpdates(this->sw_);

  // Verify that only one event is being enqueued and updated.
  auto finalStateGeneration = this->sw_->getState()->getGeneration();
  auto stateUpdates = finalStateGeneration - initialStateGeneration;

  // We expect one update from the last DsfUpdate.
  // Due to the state observer of AclNexthopHandler, it will schedule another
  // update for fib change
  EXPECT_EQ(stateUpdates, 2);
}

TYPED_TEST(DsfSubscriptionTest, ConcurrentQueueDsfUpdates) {
  // Test that multiple threads concurrently calling queueDsfUpdate() works
  // correctly. All updates should eventually be processed, and both the
  // dsfUpdateQueue and event base queue should be empty afterwards.
  this->subscription_ = this->createSubscription();

  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  constexpr int kNumThreads = 10;
  constexpr int kUpdatesPerThread = 5;
  constexpr int kFinalNumSysPorts = 3;

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Create thread functions
  std::vector<std::function<void()>> threadFns;
  threadFns.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threadFns.emplace_back([&, t]() {
      for (int i = 0; i < kUpdatesPerThread; ++i) {
        queueSysPortUpdate(
            (t * kUpdatesPerThread + i) % kUpdatesPerThread +
            1 /*numSysPorts*/);
      }
    });
  }

  // Run all threads concurrently
  this->runConcurrentThreads(threadFns);

  // Wait for all events to be processed
  this->waitForQueueDrain();

  // Verify dsfUpdateQueue_ is empty by checking we can still queue new updates
  // and they get processed normally
  queueSysPortUpdate(kFinalNumSysPorts /*numSysPorts*/);

  this->waitForQueueDrain();

  // Verify final state
  this->verifySysPortAndRif(
      beforeNumSysPorts, beforeNumRifs, kFinalNumSysPorts);
}

TYPED_TEST(DsfSubscriptionTest, ConcurrentQueueDsfUpdateAndGRExpiry) {
  // Test that multiple threads concurrently calling queueDsfUpdate and
  // processGRHoldTimerExpired do not cause deadlock. Both dsfUpdateQueue and
  // event base queue should be processed afterwards, and enqueueing new updates
  // should lead to normal processing.
  this->subscription_ = this->createSubscription();

  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  constexpr int kNumUpdateThreads = 5;
  constexpr int kNumGRThreads = 3;
  constexpr int kUpdatesPerThread = 10;
  constexpr int kGRsPerThread = 5;
  constexpr int kFinalNumSysPorts = 4;

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Create thread functions for both update and GR threads
  std::vector<std::function<void()>> threadFns;

  // Threads that call queueDsfUpdate
  threadFns.reserve(kNumUpdateThreads + kNumGRThreads);
  for (int t = 0; t < kNumUpdateThreads; ++t) {
    threadFns.emplace_back([&, t]() {
      for (int i = 0; i < kUpdatesPerThread; ++i) {
        queueSysPortUpdate(
            (t * kUpdatesPerThread + i) % kNumUpdateThreads +
            1 /*numSysPorts*/);
      }
    });
  }

  // Threads that call processGRHoldTimerExpired
  for (int t = 0; t < kNumGRThreads; ++t) {
    threadFns.emplace_back([&]() {
      for (int i = 0; i < kGRsPerThread; ++i) {
        this->subscription_->processGRHoldTimerExpired();
      }
    });
  }

  // Run all threads concurrently
  this->runConcurrentThreads(threadFns);

  // Wait for all events to be processed - should not deadlock
  this->waitForQueueDrain();

  // Verify the system is in a healthy state by enqueueing a new update
  // and checking it gets processed normally
  queueSysPortUpdate(kFinalNumSysPorts /*numSysPorts*/);

  this->waitForQueueDrain();

  // Verify final state
  this->verifySysPortAndRif(
      beforeNumSysPorts, beforeNumRifs, kFinalNumSysPorts);
}

TYPED_TEST(DsfSubscriptionTest, GRExpiryProcessedViaQueueDsfUpdate) {
  // Verify that GR expiry going through queueDsfUpdate (the new unified path)
  // actually marks remote ports/interfaces as STALE and clears neighbor tables.
  // Existing queue tests always end with a regular update overwriting GR,
  // so GR's effects are never verified through this path.
  this->subscription_ = this->createSubscription();

  // Add remote system ports and interfaces directly
  std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
  std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
  for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
    auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId});
    auto rifs = makeRifs(sysPorts.get());
    switchId2SystemPorts[remoteSwitchId] = sysPorts;
    switchId2Intfs[remoteSwitchId] = rifs;
  }
  this->subscription_->updateWithRollbackProtection(
      switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);
  waitForStateUpdates(this->sw_);

  // Verify remote ports are added and LIVE
  auto sysPortsBefore = this->getRemoteSystemPorts();
  ASSERT_GT(sysPortsBefore->size(), 0);
  for (const auto& [_, sysPort] : *sysPortsBefore) {
    if (sysPort->getRemoteSystemPortType().has_value() &&
        sysPort->getRemoteSystemPortType().value() ==
            RemoteSystemPortType::DYNAMIC_ENTRY) {
      EXPECT_EQ(sysPort->getRemoteLivenessStatus(), LivenessStatus::LIVE);
    }
  }

  // Trigger GR expiry through queueDsfUpdate (the new unified path)
  this->subscription_->processGRHoldTimerExpired();
  this->waitForQueueDrain();
  waitForStateUpdates(this->sw_);

  // Verify remote system ports are marked as STALE
  WITH_RETRIES({
    auto remotePorts = this->getRemoteSystemPorts();
    for (const auto& [_, sysPort] : *remotePorts) {
      if (sysPort->getRemoteSystemPortType().has_value() &&
          sysPort->getRemoteSystemPortType().value() ==
              RemoteSystemPortType::DYNAMIC_ENTRY) {
        EXPECT_EVENTUALLY_EQ(
            sysPort->getRemoteLivenessStatus(), LivenessStatus::STALE);
      }
    }
    // Verify remote interfaces are STALE with cleared neighbor tables
    auto remoteIntfs = this->getRemoteInterfaces();
    for (const auto& [_, rif] : *remoteIntfs) {
      if (rif->getRemoteInterfaceType().has_value() &&
          rif->getRemoteInterfaceType().value() ==
              RemoteInterfaceType::DYNAMIC_ENTRY) {
        EXPECT_EVENTUALLY_EQ(
            rif->getRemoteLivenessStatus(), LivenessStatus::STALE);
        EXPECT_EVENTUALLY_EQ(rif->getArpTable()->size(), 0);
        EXPECT_EVENTUALLY_EQ(rif->getNdpTable()->size(), 0);
      }
    }
  });
}

TYPED_TEST(DsfSubscriptionTest, GROverwritesPendingRegularUpdate) {
  // Test that when a regular update is queued and GR fires before processing,
  // the GR overwrites the regular update. The GR effects (marking ports STALE)
  // should be observed, not the regular update's new ports.
  this->subscription_ = this->createSubscription();

  // First add ports directly so GR has something to mark STALE
  std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
  std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
  for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
    auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId});
    auto rifs = makeRifs(sysPorts.get());
    switchId2SystemPorts[remoteSwitchId] = sysPorts;
    switchId2Intfs[remoteSwitchId] = rifs;
  }
  this->subscription_->updateWithRollbackProtection(
      switchId2SystemPorts, switchId2Intfs, false /*grExpiry*/);
  waitForStateUpdates(this->sw_);

  auto sysPortCountBefore = this->getRemoteSystemPorts()->size();
  auto rifCountBefore = this->getRemoteInterfaces()->size();
  ASSERT_GT(sysPortCountBefore, 0);

  // Block hwUpdateEvb to prevent processing
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Queue a regular update that would add MORE ports (3 per switch)
  DsfSubscription::DsfUpdate regularUpdate;
  for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
    auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, 3);
    auto rifs = makeRifs(sysPorts.get());
    regularUpdate.switchId2SystemPorts[remoteSwitchId] = sysPorts;
    regularUpdate.switchId2Intfs[remoteSwitchId] = rifs;
  }
  this->subscription_->queueDsfUpdate(std::move(regularUpdate));

  // Fire GR - should overwrite the regular update
  this->subscription_->processGRHoldTimerExpired();

  // Unblock to process the GR update (the last writer)
  baton.post();
  this->waitForQueueDrain();
  waitForStateUpdates(this->sw_);

  // Verify GR effects: ports should be STALE, no new ports added
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(), sysPortCountBefore);
    EXPECT_EVENTUALLY_EQ(this->getRemoteInterfaces()->size(), rifCountBefore);
    auto remotePorts = this->getRemoteSystemPorts();
    for (const auto& [_, sysPort] : *remotePorts) {
      if (sysPort->getRemoteSystemPortType().has_value() &&
          sysPort->getRemoteSystemPortType().value() ==
              RemoteSystemPortType::DYNAMIC_ENTRY) {
        EXPECT_EVENTUALLY_EQ(
            sysPort->getRemoteLivenessStatus(), LivenessStatus::STALE);
      }
    }
    auto remoteIntfs = this->getRemoteInterfaces();
    for (const auto& [_, rif] : *remoteIntfs) {
      if (rif->getRemoteInterfaceType().has_value() &&
          rif->getRemoteInterfaceType().value() ==
              RemoteInterfaceType::DYNAMIC_ENTRY) {
        EXPECT_EVENTUALLY_EQ(
            rif->getRemoteLivenessStatus(), LivenessStatus::STALE);
        EXPECT_EVENTUALLY_EQ(rif->getArpTable()->size(), 0);
        EXPECT_EVENTUALLY_EQ(rif->getNdpTable()->size(), 0);
      }
    }
  });
}

TYPED_TEST(DsfSubscriptionTest, StopCancelsPendingDsfUpdate) {
  // Test that stop() cancels a pending update by resetting nextDsfUpdate_.
  // The lambda on hwUpdateEvb_ should find null and return early.
  this->subscription_ = this->createSubscription();
  auto sysPortCountBefore = this->getRemoteSystemPorts()->size();

  // Block hwUpdateEvb to prevent processing
  folly::Baton<> baton;
  this->hwUpdatePool_->getEventBase()->runInEventBaseThread(
      [&]() { baton.wait(); });
  WITH_RETRIES({
    ASSERT_EVENTUALLY_EQ(
        this->hwUpdatePool_->getEventBase()->getNotificationQueueSize(), 0);
  });

  // Queue a regular update that would add ports
  DsfSubscription::DsfUpdate update;
  for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
    auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, 3);
    auto rifs = makeRifs(sysPorts.get());
    update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
    update.switchId2Intfs[remoteSwitchId] = rifs;
  }
  this->subscription_->queueDsfUpdate(std::move(update));

  // Verify update is pending
  {
    auto rlock = this->subscription_->nextDsfUpdate_.rlock();
    EXPECT_NE(*rlock, nullptr);
  }

  // Stop the subscription - should cancel the pending update
  this->subscription_->stop();

  // Verify update is cancelled
  {
    auto rlock = this->subscription_->nextDsfUpdate_.rlock();
    EXPECT_EQ(*rlock, nullptr);
  }

  // Unblock - the lambda should find null and return early
  baton.post();
  this->waitForQueueDrain();
  waitForStateUpdates(this->sw_);

  // Verify state unchanged - no new ports added
  EXPECT_EQ(this->getRemoteSystemPorts()->size(), sysPortCountBefore);
}

TYPED_TEST(DsfSubscriptionTest, NewUpdateAfterProcessingSchedulesNewLambda) {
  // After one update is fully processed (nextDsfUpdate_ becomes null),
  // a new update should schedule a new lambda and be processed correctly.
  // This verifies the re-scheduling logic in queueDsfUpdate.
  this->subscription_ = this->createSubscription();
  auto beforeNumSysPorts = this->getRemoteSystemPorts()->size();
  auto beforeNumRifs = this->getRemoteInterfaces()->size();

  auto queueSysPortUpdate = [&](int numSysPorts) {
    DsfSubscription::DsfUpdate update;
    for (const auto& remoteSwitchId : this->remoteSwitchIds()) {
      auto sysPorts = makeSysPortsForSwitchIds({remoteSwitchId}, numSysPorts);
      auto rifs = makeRifs(sysPorts.get());
      update.switchId2SystemPorts[remoteSwitchId] = sysPorts;
      update.switchId2Intfs[remoteSwitchId] = rifs;
    }
    this->subscription_->queueDsfUpdate(std::move(update));
  };

  // Queue first update and let it process
  queueSysPortUpdate(1);
  this->waitForQueueDrain();
  waitForStateUpdates(this->sw_);

  // Verify first update was applied
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + this->kNumRemoteSwitchAsics);
  });

  // Verify nextDsfUpdate_ is null after processing
  {
    auto rlock = this->subscription_->nextDsfUpdate_.rlock();
    EXPECT_EQ(*rlock, nullptr);
  }

  // Queue second update - should schedule a NEW lambda since
  // nextDsfUpdate_ was null
  queueSysPortUpdate(2);
  this->waitForQueueDrain();
  waitForStateUpdates(this->sw_);

  // Verify second update was applied
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteSystemPorts()->size(),
        beforeNumSysPorts + (this->kNumRemoteSwitchAsics * 2));
    EXPECT_EVENTUALLY_EQ(
        this->getRemoteInterfaces()->size(),
        beforeNumRifs + (this->kNumRemoteSwitchAsics * 2));
  });
}

TYPED_TEST(DsfSubscriptionTest, RouteDeleteCancelsRouteAdd) {
  // Reproduce bug where the cancel-out logic in processRemoteInterfaceRoutes
  // incorrectly cancels a route add for a changed RIF when a different RIF
  // with the same prefix is simultaneously removed.
  //
  // Scenario:
  //   Old state: RIF 6040 (prefix 42.42.42.100/31), RIF 6043 (prefix
  //   42.42.42.200/31)
  //   New state: RIF 6040 (prefix 42.42.42.200/31), RIF 6043 removed
  //
  // In processDelta, changed nodes are processed before removed nodes:
  //   Changed RIF 6040: delete 100/31, add 200/31
  //   Removed RIF 6043: delete 200/31 -> finds 200/31 in toAdd -> BUG: cancels
  //   the add!
  //
  // Result: the route for 42.42.42.200/31 is never added for RIF 6040.

  auto state = this->sw_->getState();

  // Old RIF 6040 with prefix X (42.42.42.100/31, 42::100/127)
  auto oldRifA = makeRemoteIntf(
      InterfaceID(6040),
      folly::IPAddressV4("42.42.42.100"),
      31,
      folly::IPAddressV6("42::100"),
      127);
  // New RIF 6040 with prefix Y (42.42.42.200/31, 42::200/127)
  auto newRifA = makeRemoteIntf(
      InterfaceID(6040),
      folly::IPAddressV4("42.42.42.200"),
      31,
      folly::IPAddressV6("42::200"),
      127);
  // RIF 6043 with prefix Y (being removed)
  auto rifB = makeRemoteIntf(
      InterfaceID(6043),
      folly::IPAddressV4("42.42.42.200"),
      31,
      folly::IPAddressV6("42::200"),
      127);

  IntfRouteTable remoteIntfRoutesToAdd;
  RouterIDToPrefixes remoteIntfRoutesToDel;

  // Simulate processDelta order: changed nodes first, then removed nodes.
  // Changed RIF 6040: delete old routes (prefix X), add new routes (prefix Y)
  processRemoteInterfaceRoutes(
      oldRifA, state, false, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);
  processRemoteInterfaceRoutes(
      newRifA, state, true, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);

  // Verify intermediate state: prefix Y in toAdd, prefix X in toDel
  auto prefixY_v4 = folly::IPAddress::createNetwork("42.42.42.200/31");
  auto prefixY_v6 = folly::IPAddress::createNetwork("42::200/127");
  auto prefixX_v4 = folly::IPAddress::createNetwork("42.42.42.100/31");
  auto prefixX_v6 = folly::IPAddress::createNetwork("42::100/127");

  {
    auto& toAdd = remoteIntfRoutesToAdd[RouterID(0)];
    EXPECT_NE(toAdd.find(prefixY_v4), toAdd.end());
    EXPECT_NE(toAdd.find(prefixY_v6), toAdd.end());
    auto& toDel = remoteIntfRoutesToDel[RouterID(0)];
    EXPECT_TRUE(toDelContainsPrefix(toDel, prefixX_v4));
    EXPECT_TRUE(toDelContainsPrefix(toDel, prefixX_v6));
  }

  // Removed RIF 6043: delete routes (prefix Y)
  processRemoteInterfaceRoutes(
      rifB, state, false, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);

  // After processing the removal, prefix Y should STILL be in toAdd for
  // RIF 6040. The delete of prefix Y from RIF 6043 should not cancel the
  // add of prefix Y for the different RIF 6040.
  auto& toAdd = remoteIntfRoutesToAdd[RouterID(0)];
  EXPECT_NE(toAdd.find(prefixY_v4), toAdd.end())
      << "Route add for 42.42.42.200/31 (RIF 6040) was incorrectly cancelled "
      << "by removal of RIF 6043 with the same prefix";
  EXPECT_NE(toAdd.find(prefixY_v6), toAdd.end())
      << "Route add for 42::200/127 (RIF 6040) was incorrectly cancelled "
      << "by removal of RIF 6043 with the same prefix";

  // Verify the add entries are for RIF 6040 (not RIF 6043)
  if (toAdd.find(prefixY_v4) != toAdd.end()) {
    EXPECT_EQ(toAdd[prefixY_v4].first, InterfaceID(6040));
  }
  if (toAdd.find(prefixY_v6) != toAdd.end()) {
    EXPECT_EQ(toAdd[prefixY_v6].first, InterfaceID(6040));
  }
}

TYPED_TEST(DsfSubscriptionTest, RouteAddCancelsRouteDelete) {
  // Reproduce the symmetric bug where the cancel-out logic in
  // processRemoteInterfaceRoutes incorrectly cancels a pending route delete
  // when a new RIF with the same prefix is added.
  //
  // Scenario:
  //   Old state: RIF 6040 (prefix 42.42.42.100/31)
  //   New state: RIF 6040 (prefix 42.42.42.200/31), RIF 6043 added with prefix
  //   42.42.42.100/31
  //
  // In processDelta, changed nodes are processed before added nodes:
  //   Changed RIF 6040: delete 100/31, add 200/31
  //   Added RIF 6043:   add 100/31 -> finds 100/31 in toDel -> BUG: cancels
  //   the delete!
  //
  // Result: the stale route for 42.42.42.100/31 pointing to old RIF 6040 is
  // never deleted, and the new route for RIF 6043 is never added.

  auto state = this->sw_->getState();

  // Old RIF 6040 with prefix X (42.42.42.100/31, 42::100/127)
  auto oldRifA = makeRemoteIntf(
      InterfaceID(6040),
      folly::IPAddressV4("42.42.42.100"),
      31,
      folly::IPAddressV6("42::100"),
      127);
  // New RIF 6040 with prefix Y (42.42.42.200/31, 42::200/127)
  auto newRifA = makeRemoteIntf(
      InterfaceID(6040),
      folly::IPAddressV4("42.42.42.200"),
      31,
      folly::IPAddressV6("42::200"),
      127);
  // New RIF 6043 with prefix X (being added)
  auto newRifB = makeRemoteIntf(
      InterfaceID(6043),
      folly::IPAddressV4("42.42.42.100"),
      31,
      folly::IPAddressV6("42::100"),
      127);

  IntfRouteTable remoteIntfRoutesToAdd;
  RouterIDToPrefixes remoteIntfRoutesToDel;

  auto prefixX_v4 = folly::IPAddress::createNetwork("42.42.42.100/31");
  auto prefixX_v6 = folly::IPAddress::createNetwork("42::100/127");

  // Simulate processDelta order: changed nodes first, then added nodes.
  // Changed RIF 6040: delete old routes (prefix X), add new routes (prefix Y)
  processRemoteInterfaceRoutes(
      oldRifA, state, false, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);
  processRemoteInterfaceRoutes(
      newRifA, state, true, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);

  // Verify intermediate state: prefix X in toDel
  {
    auto& toDel = remoteIntfRoutesToDel[RouterID(0)];
    EXPECT_TRUE(toDelContainsPrefix(toDel, prefixX_v4));
    EXPECT_TRUE(toDelContainsPrefix(toDel, prefixX_v6));
  }

  // Added RIF 6043: add routes (prefix X)
  processRemoteInterfaceRoutes(
      newRifB, state, true, remoteIntfRoutesToAdd, remoteIntfRoutesToDel);

  // After processing the addition, prefix X should be in toAdd for RIF 6043.
  // The add of prefix X for RIF 6043 should not cancel the pending delete
  // of prefix X from the different RIF 6040.
  auto& toAdd = remoteIntfRoutesToAdd[RouterID(0)];
  EXPECT_NE(toAdd.find(prefixX_v4), toAdd.end())
      << "Route add for 42.42.42.100/31 (RIF 6043) was incorrectly cancelled "
      << "by pending delete of the same prefix from RIF 6040";
  EXPECT_NE(toAdd.find(prefixX_v6), toAdd.end())
      << "Route add for 42::100/127 (RIF 6043) was incorrectly cancelled "
      << "by pending delete of the same prefix from RIF 6040";

  // Verify the add entries are for RIF 6043 (not RIF 6040)
  if (toAdd.find(prefixX_v4) != toAdd.end()) {
    EXPECT_EQ(toAdd[prefixX_v4].first, InterfaceID(6043));
  }
  if (toAdd.find(prefixX_v6) != toAdd.end()) {
    EXPECT_EQ(toAdd[prefixX_v6].first, InterfaceID(6043));
  }

  // Prefix X should NOT be in toDel — the pending delete was cancelled because
  // the add for RIF 6043 will replace the route via addOrReplaceRouteImpl.
  // Having prefix X in both toAdd and toDel would cause the delete to win
  // (RIB processes adds first, then deletes), removing the route entirely.
  auto& toDel = remoteIntfRoutesToDel[RouterID(0)];
  EXPECT_FALSE(toDelContainsPrefix(toDel, prefixX_v4))
      << "Route delete for 42.42.42.100/31 should have been cancelled "
      << "because the add for RIF 6043 will replace the route";
  EXPECT_FALSE(toDelContainsPrefix(toDel, prefixX_v6))
      << "Route delete for 42::100/127 should have been cancelled "
      << "because the add for RIF 6043 will replace the route";
}

TYPED_TEST(
    DsfSubscriptionTest,
    CrossSwitchRouteDeleteDoesNotRemoveOtherSwitchRoute) {
  // Reproduce cross-switch stale route bug: when two remote switches swap
  // interface prefixes and restart independently, the second switch's DSF
  // update deletes the first switch's route because deletes only match by
  // prefix, not interface ID.
  //
  // Symmetric swap scenario (3 DSF switches: A, B, C; C subscribes to both):
  //   Initial: Switch A has intf 401 with prefix 100.0.0.0/24
  //            Switch B has intf 402 with prefix 101.0.0.0/24
  //   After:   Switch A has intf 401 with prefix 101.0.0.0/24 (was B's)
  //            Switch B has intf 402 with prefix 100.0.0.0/24 (was A's)
  //
  // A's old = B's new, B's old = A's new. Whichever switch restarts
  // second triggers the bug.
  //
  // On switch C, updates arrive as separate getUpdatedState() calls:
  //   Call 1 (switch A restarts): changed intf 401 deletes 100/24, adds 101/24
  //   Call 2 (switch B restarts): changed intf 402 deletes 101/24, adds 100/24
  //
  // Without the fix, call 2's delete of 101/24 removes switch A's newly
  // added route (from call 1) because the RIB only matches by prefix.
  // The fix carries InterfaceID in the delete entry so the RIB can verify
  // ownership before deleting.

  auto state = this->sw_->getState();

  // --- Call 1: Switch A update (intf 401: 100/24 -> 101/24) ---
  auto oldRifA = makeRemoteIntf(
      InterfaceID(401),
      folly::IPAddressV4("100.0.0.1"),
      24,
      folly::IPAddressV6("100::1"),
      64);
  auto newRifA = makeRemoteIntf(
      InterfaceID(401),
      folly::IPAddressV4("101.0.0.1"),
      24,
      folly::IPAddressV6("101::1"),
      64);

  IntfRouteTable toAddCall1;
  RouterIDToPrefixes toDelCall1;

  processRemoteInterfaceRoutes(oldRifA, state, false, toAddCall1, toDelCall1);
  processRemoteInterfaceRoutes(newRifA, state, true, toAddCall1, toDelCall1);

  auto prefix100_v4 = folly::IPAddress::createNetwork("100.0.0.0/24");
  auto prefix101_v4 = folly::IPAddress::createNetwork("101.0.0.0/24");

  // Verify call 1: add 101/24 for intf 401, delete 100/24 from intf 401
  EXPECT_NE(
      toAddCall1[RouterID(0)].find(prefix101_v4),
      toAddCall1[RouterID(0)].end());
  EXPECT_EQ(toAddCall1[RouterID(0)][prefix101_v4].first, InterfaceID(401));
  EXPECT_TRUE(toDelContainsPrefix(toDelCall1[RouterID(0)], prefix100_v4));
  // Verify the delete carries the correct interface ID
  auto& delEntries1 = toDelCall1[RouterID(0)];
  auto delIt1 = std::find_if(
      delEntries1.begin(), delEntries1.end(), [&prefix100_v4](const auto& e) {
        return e.first == prefix100_v4;
      });
  ASSERT_NE(delIt1, delEntries1.end());
  EXPECT_EQ(delIt1->second, InterfaceID(401));

  // --- Call 2: Switch B update (intf 402: 101/24 -> 100/24) ---
  auto oldRifB = makeRemoteIntf(
      InterfaceID(402),
      folly::IPAddressV4("101.0.0.1"),
      24,
      folly::IPAddressV6("101::1"),
      64);
  auto newRifB = makeRemoteIntf(
      InterfaceID(402),
      folly::IPAddressV4("100.0.0.1"),
      24,
      folly::IPAddressV6("100::1"),
      64);

  IntfRouteTable toAddCall2;
  RouterIDToPrefixes toDelCall2;

  processRemoteInterfaceRoutes(oldRifB, state, false, toAddCall2, toDelCall2);
  processRemoteInterfaceRoutes(newRifB, state, true, toAddCall2, toDelCall2);

  // Verify call 2: add 100/24 for intf 402, delete 101/24 from intf 402
  EXPECT_NE(
      toAddCall2[RouterID(0)].find(prefix100_v4),
      toAddCall2[RouterID(0)].end());
  EXPECT_EQ(toAddCall2[RouterID(0)][prefix100_v4].first, InterfaceID(402));
  EXPECT_TRUE(toDelContainsPrefix(toDelCall2[RouterID(0)], prefix101_v4));
  // Verify the delete carries intf 402 (not 401)
  auto& delEntries2 = toDelCall2[RouterID(0)];
  auto delIt2 = std::find_if(
      delEntries2.begin(), delEntries2.end(), [&prefix101_v4](const auto& e) {
        return e.first == prefix101_v4;
      });
  ASSERT_NE(delIt2, delEntries2.end());
  EXPECT_EQ(delIt2->second, InterfaceID(402));
}

} // namespace facebook::fboss
