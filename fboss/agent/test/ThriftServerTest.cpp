/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/ServerStreamMultiPublisher.h>
#include "common/network/if/gen-cpp2/Address_types.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestPacketFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include "fboss/agent/test/MultiSwitchTestServer.h"

#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Timeout.h>
#include <folly/coro/UnboundedQueue.h>
#include <folly/portability/GTest.h>
#include <memory>

#include "common/thrift/cpp/server/code_frameworks/testing/Server.h"

using namespace facebook::fboss;
using namespace facebook::stats;
using facebook::network::toBinaryAddress;
using facebook::network::thrift::BinaryAddress;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::StringPiece;
using std::shared_ptr;
using std::unique_ptr;
using ::testing::_;
using ::testing::Return;

class MultiSwitchThriftHandlerMock;

class ThriftServerTest : public ::testing::Test {
 public:
  void SetUp() override {
    cfg::AgentConfig agentConfig;
    FLAGS_multi_switch = true;
    FLAGS_rx_sw_priority = true;
    agentConfig.sw() = testConfigA();
    agentConfig.sw()->switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU)},
        {1, createSwitchInfo(cfg::SwitchType::NPU)}};
    handle_ = createTestHandle(&agentConfig.sw().value());
    sw_ = handle_->getSw();
    sw_->setConfig(std::make_unique<AgentConfig>(std::move(agentConfig)));
  }

  void TearDown() override {
    // Stream/sink teardown runs completion callbacks on server threads that
    // reach back into SwSwitch, so the clients and the server have to be gone
    // before handle_ destroys it.
    multiSwitchClient_.reset();
    fbossCtlClient_.reset();
    evbThread1_.reset();
    evbThread2_.reset();
    swSwitchTestServer_.reset();
    mockMultiSwitchHandler_.reset();
  }

  void setupServerAndClients() {
    XLOG(DBG2) << "Initializing thrift server";

    // Setup test server
    std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
        handlers;
    handlers.emplace_back(std::make_shared<MultiSwitchThriftHandler>(sw_));
    handlers.emplace_back(std::make_shared<ThriftHandler>(sw_));
    swSwitchTestServer_ = std::make_unique<MultiSwitchTestServer>(handlers);

    // setup clients
    multiSwitchClient_ = setupSwSwitchClient<
        apache::thrift::Client<multiswitch::MultiSwitchCtrl>>(
        "testClient1", evbThread1_);
    fbossCtlClient_ = setupSwSwitchClient<apache::thrift::Client<FbossCtrl>>(
        "testClient2", evbThread2_);
  }

  void setupServerWithMockAndClients() {
    XLOG(DBG2) << "Initializing thrift server";

    std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
        handlers;
    mockMultiSwitchHandler_ =
        std::make_shared<MultiSwitchThriftHandlerMock>(sw_);
    handlers.emplace_back(mockMultiSwitchHandler_);
    handlers.emplace_back(std::make_shared<ThriftHandler>(sw_));
    // Setup test server
    swSwitchTestServer_ = std::make_unique<MultiSwitchTestServer>(handlers);

    // setup clients
    multiSwitchClient_ = setupSwSwitchClient<
        apache::thrift::Client<multiswitch::MultiSwitchCtrl>>(
        "testClient1", evbThread1_);
    fbossCtlClient_ = setupSwSwitchClient<apache::thrift::Client<FbossCtrl>>(
        "testClient2", evbThread2_);
  }

  template <typename clientT>
  std::unique_ptr<clientT> setupSwSwitchClient(
      std::string clientName,
      std::shared_ptr<folly::ScopedEventBaseThread>& evbThread) {
    evbThread = std::make_shared<folly::ScopedEventBaseThread>(clientName);
    auto channel = apache::thrift::PooledRequestChannel::newChannel(
        evbThread->getEventBase(),
        evbThread,
        [this](folly::EventBase& evb) mutable {
          return apache::thrift::RocketClientChannel::newChannel(
              folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(
                  &evb, "::1", swSwitchTestServer_->getPort())));
        });
    return std::make_unique<clientT>(std::move(channel));
  }

 protected:
  std::unique_ptr<MultiSwitchTestServer> swSwitchTestServer_;
  std::unique_ptr<apache::thrift::Client<FbossCtrl>> fbossCtlClient_;
  std::unique_ptr<apache::thrift::Client<multiswitch::MultiSwitchCtrl>>
      multiSwitchClient_;
  std::shared_ptr<MultiSwitchThriftHandlerMock> mockMultiSwitchHandler_;
  std::shared_ptr<folly::ScopedEventBaseThread> evbThread2_;
  std::shared_ptr<folly::ScopedEventBaseThread> evbThread1_;
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

class MultiSwitchThriftHandlerMock : public MultiSwitchThriftHandler {
 public:
  explicit MultiSwitchThriftHandlerMock(SwSwitch* sw)
      : MultiSwitchThriftHandler(sw) {}

  folly::coro::Task<apache::thrift::ServerStream<multiswitch::TxPacket>>
  co_getTxPackets(int64_t /*switchId*/) override {
    co_return folly::coro::co_invoke(
        [this]() mutable
            -> folly::coro::AsyncGenerator<multiswitch::TxPacket&&> {
          while (receivedPktsQueue_.size()) {
            multiswitch::TxPacket txPkt;
            multiswitch::RxPacket rxPkt;
            co_await folly::coro::timeout(
                receivedPktsQueue_.dequeue(rxPkt), std::chrono::seconds(1));
            txPkt.port() = rxPkt.port().value();
            co_yield std::move(txPkt);
          }
        });
  }

  folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>>
  co_notifyRxPacket(int64_t /*switchId*/) override {
    co_return apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>{
        [this](folly::coro::AsyncGenerator<multiswitch::RxPacket&&> gen)
            -> folly::coro::Task<bool> {
          while (auto item = co_await gen.next()) {
            receivedPktsQueue_.enqueue(std::move(*item));
          }
          co_return true;
        },
        10 /* buffer size */
    };
  }

 private:
  folly::coro::UnboundedQueue<multiswitch::RxPacket, true, true>
      receivedPktsQueue_;
};

TEST_F(ThriftServerTest, getNextStateOperDeltaUnknownSwitchIdThrows) {
  setupServerAndClients();
  // A crafted RPC with an unknown switchId must surface as a Thrift exception,
  // not crash the agent (previously a CHECK -> SIGABRT). switchIds 0 and 1 are
  // configured; 42 is not.
  multiswitch::StateOperDelta prevDelta;
  EXPECT_THROW(
      folly::coro::blockingWait(multiSwitchClient_->co_getNextStateOperDelta(
          42 /* unknown switchId */, prevDelta, 0 /* lastUpdateSeqNum */)),
      apache::thrift::TApplicationException);
}

TEST_F(ThriftServerTest, gracefulExitUnknownSwitchIdThrows) {
  setupServerAndClients();
  // Same for gracefulExit: an unknown switchId must not abort the agent.
  EXPECT_THROW(
      folly::coro::blockingWait(
          multiSwitchClient_->co_gracefulExit(42 /* unknown switchId */)),
      apache::thrift::TApplicationException);
}

TEST_F(ThriftServerTest, setPortStateBlocking) {
  // setup server and clients
  setupServerAndClients();

  const PortID port5{5};
  folly::coro::blockingWait(fbossCtlClient_->co_setPortState(port5, true));
  waitForStateUpdates(this->sw_);

  auto port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  EXPECT_TRUE(port->isEnabled());

  folly::coro::blockingWait(fbossCtlClient_->co_setPortState(port5, false));
  waitForStateUpdates(this->sw_);

  port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  EXPECT_FALSE(port->isEnabled());
}

TEST_F(ThriftServerTest, setPortDownOnSwitchExit) {
  // setup server and clients
  setupServerAndClients();

  // Mark two switches as connected so that SwSwitch won't abort while
  // shutting one of the switches
  sw_->getHwSwitchHandler()->connected(SwitchID(0));
  sw_->getHwSwitchHandler()->connected(SwitchID(1));

  const PortID port5{5};
  folly::coro::blockingWait(fbossCtlClient_->co_setPortState(port5, true));

  // Bring the port up
  sw_->linkStateChanged(port5, true, cfg::PortType::INTERFACE_PORT);
  WITH_RETRIES({
    auto port = sw_->getState()->getPorts()->getNodeIf(port5);
    EXPECT_EVENTUALLY_TRUE(port->isUp());
  });

  // Disconnect the first switch
  sw_->getHwSwitchHandler()->notifyHwSwitchDisconnected(
      SwitchID(0), false /*gracefulExit*/);

  WITH_RETRIES({
    auto port = sw_->getState()->getPorts()->getNodeIf(port5);
    EXPECT_EVENTUALLY_FALSE(port->isUp());
  });
}

CO_TEST_F(ThriftServerTest, packetStreamAndSink) {
  // setup server and clients
  setupServerWithMockAndClients();

  int rxPktCount = 10;
  // Send packets to server using sink
  auto result = co_await multiSwitchClient_->co_notifyRxPacket(100);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::RxPacket&&> {
        for (int i = 0; i < rxPktCount; i++) {
          multiswitch::RxPacket pkt;
          pkt.port() = i;
          co_yield std::move(pkt);
        }
      }());
  EXPECT_TRUE(ret);

  // Get packets from server using stream and verify
  int expectedPort = 0;
  int txPktCount = 0;
  try {
    auto gen =
        (co_await multiSwitchClient_->co_getTxPackets(100)).toAsyncGenerator();
    while (const auto& val = co_await gen.next()) {
      // got value
      EXPECT_EQ(expectedPort++, *val->port());
      txPktCount++;
    }
    // server completed the stream
  } catch (const std::exception& e) {
    // server sent an exception, or there was a connection error
    XLOG(DBG2) << "Unable to connect " << e.what();
  }
  EXPECT_EQ(rxPktCount, txPktCount);
}

CO_TEST_F(ThriftServerTest, setPortStateSink) {
  // setup server and clients
  setupServerAndClients();

  const PortID port5{5};
  auto result = co_await multiSwitchClient_->co_notifyLinkChangeEvent(0);
  auto verifyOperState = [this](const PortID& portId, bool up) {
    WITH_RETRIES({
      auto port = this->sw_->getState()->getPorts()->getNodeIf(portId);
      if (up) {
        EXPECT_EVENTUALLY_TRUE(port->getOperState() == Port::OperState::UP);
      } else {
        EXPECT_EVENTUALLY_TRUE(port->getOperState() == Port::OperState::DOWN);
      }
    });
  };

  CounterCache counters(sw_);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::LinkChangeEvent&&> {
        // verify link event sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.link_event_sync_active"), 1);
        });
        {
          multiswitch::LinkEvent upEvent;
          upEvent.port() = port5;
          upEvent.up() = true;
          multiswitch::LinkChangeEvent changeEvent;
          changeEvent.linkStateEvent() = upEvent;
          changeEvent.eventType() =
              multiswitch::LinkChangeEventType::LINK_STATE;
          co_yield std::move(changeEvent);
          verifyOperState(port5, true);
        }
        {
          // set port oper state down
          multiswitch::LinkEvent downEvent;
          downEvent.port() = port5;
          downEvent.up() = false;
          multiswitch::LinkChangeEvent changeEvent;
          changeEvent.linkStateEvent() = downEvent;
          changeEvent.eventType() =
              multiswitch::LinkChangeEventType::LINK_STATE;
          co_yield std::move(changeEvent);
          verifyOperState(port5, false);
        }
      }());
  EXPECT_TRUE(ret);
  // flap counter should be 2
  counters.update();
  EXPECT_EQ(counters.value("port5.link_state.flap.sum"), 2);
  ThriftHandler handler(sw_);
  std::unique_ptr<std::vector<int32_t>> ports =
      std::make_unique<std::vector<int32_t>>();
  ports->push_back(5);
  handler.clearPortStats(std::move(ports));
  WITH_RETRIES({
    counters.update();
    EXPECT_EVENTUALLY_EQ(counters.value("port5.link_state.flap.sum"), 0);
  });
}

CO_TEST_F(ThriftServerTest, setPortActiveStateSink) {
  // setup server and clients
  setupServerAndClients();

  const PortID port5{5};
  auto result = co_await multiSwitchClient_->co_notifyLinkChangeEvent(0);
  auto verifyActiveState = [this](const PortID& portId, bool active) {
    WITH_RETRIES({
      auto port = this->sw_->getState()->getPorts()->getNodeIf(portId);
      if (active) {
        EXPECT_EVENTUALLY_TRUE(
            port->getActiveState() == Port::ActiveState::ACTIVE);
      } else {
        EXPECT_EVENTUALLY_TRUE(
            port->getActiveState() == Port::ActiveState::INACTIVE);
      }
    });
  };

  CounterCache counters(sw_);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::LinkChangeEvent&&> {
        // verify link event sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.link_event_sync_active"), 1);
        });
        {
          multiswitch::LinkActiveEvent activeEvent;
          activeEvent.port2IsActive()->insert({port5, true});
          multiswitch::LinkChangeEvent changeEvent;
          changeEvent.linkActiveEvents() = activeEvent;
          changeEvent.eventType() =
              multiswitch::LinkChangeEventType::LINK_ACTIVE;
          co_yield std::move(changeEvent);
          verifyActiveState(port5, true);
        }
        {
          multiswitch::LinkActiveEvent inactiveEvent;
          inactiveEvent.port2IsActive()->insert({port5, false});
          multiswitch::LinkChangeEvent changeEvent;
          changeEvent.linkActiveEvents() = inactiveEvent;
          changeEvent.eventType() =
              multiswitch::LinkChangeEventType::LINK_ACTIVE;
          co_yield std::move(changeEvent);
          verifyActiveState(port5, false);
        }
      }());
  EXPECT_TRUE(ret);
}
CO_TEST_F(ThriftServerTest, fdbEventTest) {
  // setup server and clients
  setupServerAndClients();

  auto result = co_await multiSwitchClient_->co_notifyFdbEvent(0);
  auto verifyMacState = [this](
                            const VlanID vlanId, std::string mac, bool added) {
    WITH_RETRIES({
      auto vlan = this->sw_->getState()->getVlans()->getNodeIf(vlanId);
      auto macTable = vlan->getMacTable();
      auto node = macTable->getMacIf(folly::MacAddress(mac));
      if (added) {
        EXPECT_EVENTUALLY_NE(nullptr, node);
      } else {
        EXPECT_EVENTUALLY_EQ(nullptr, node);
      }
    });
  };

  auto createFdbEntry = [](L2EntryUpdateType updateType) {
    multiswitch::FdbEvent fdbEvent;
    PortID port5{5};
    fdbEvent.entry()->port() = port5;
    fdbEvent.entry()->vlanID() = 1;
    fdbEvent.entry()->mac() = "00:01:02:03:04:05";
    fdbEvent.entry()->l2EntryType() = L2EntryType::L2_ENTRY_TYPE_VALIDATED;
    fdbEvent.updateType() = updateType;
    return fdbEvent;
  };

  CounterCache counters(sw_);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::FdbEvent&&> {
        // verify fdb event sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.fdb_event_sync_active"), 1);
        });
        // add a new mac entry to the table
        auto fdbEvent =
            createFdbEntry(L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
        co_yield std::move(fdbEvent);
        verifyMacState(VlanID(1), "00:01:02:03:04:05", true);

        // delete the mac entry
        auto fdbEvent2 =
            createFdbEntry(L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE);
        co_yield std::move(fdbEvent2);
        verifyMacState(VlanID(1), "00:01:02:03:04:05", false);
      }());
  EXPECT_TRUE(ret);
}

CO_TEST_F(ThriftServerTest, switchReachabilityChangeEventReceivedCounter) {
  setupServerAndClients();

  auto result =
      co_await multiSwitchClient_->co_notifySwitchReachabilityChangeEvent(0);
  const auto kReceivedCounter = SwitchStats::kCounterPrefix +
      "switch.0.switch_reachability_change_event_received.sum";
  CounterCache counters(sw_);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<
                multiswitch::SwitchReachabilityChangeEvent&&> {
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value(
                  "switch.0.switch_reachability_change_event_sync_active"),
              1);
        });
        co_yield multiswitch::SwitchReachabilityChangeEvent{};
        co_yield multiswitch::SwitchReachabilityChangeEvent{};
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(counters.value(kReceivedCounter), 2);
        });
      }());
  EXPECT_TRUE(ret);
}

CO_TEST_F(ThriftServerTest, receivePktHandler) {
  // setup server and clients
  setupServerAndClients();

  CounterCache counters(sw_);
  // Send packets to server using sink
  auto result = co_await multiSwitchClient_->co_notifyRxPacket(0);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::RxPacket&&> {
        // verify rx pkt event sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.rx_pkt_event_sync_active"), 1);
        });
        multiswitch::RxPacket rxPkt;
        rxPkt.data() = std::make_unique<folly::IOBuf>(createV4Packet(
            folly::IPAddressV4("10.0.0.2"),
            folly::IPAddressV4("10.0.0.1"),
            MockPlatform::getMockLocalMac(),
            MockPlatform::getMockLocalMac()));
        rxPkt.port() = 1;
        rxPkt.vlan() = 1;
        rxPkt.length() = (*rxPkt.data())->computeChainDataLength();
        co_yield std::move(rxPkt);
      }());
  EXPECT_TRUE(ret);
  WITH_RETRIES({
    counters.update();
    EXPECT_EVENTUALLY_EQ(
        counters.value(SwitchStats::kCounterPrefix + "ipv4.mine.sum"),
        counters.prevValue(SwitchStats::kCounterPrefix + "ipv4.mine.sum") + 1);
  });
}

CO_TEST_F(ThriftServerTest, receivePktHandlerPriorityHandling) {
  // setup server and clients
  setupServerAndClients();

  CounterCache counters(sw_);
  // Send packets to server using sink
  auto result = co_await multiSwitchClient_->co_notifyRxPacket(0);
  int pktCount;
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::RxPacket&&> {
        // verify rx pkt event sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.rx_pkt_event_sync_active"), 1);
        });
        for (pktCount = 0; pktCount < 100000; pktCount++) {
          multiswitch::RxPacket rxPkt;
          rxPkt.data() = std::make_unique<folly::IOBuf>(createV4Packet(
              folly::IPAddressV4("10.0.0.2"),
              folly::IPAddressV4("10.0.0.1"),
              MockPlatform::getMockLocalMac(),
              MockPlatform::getMockLocalMac()));
          rxPkt.port() = 1;
          rxPkt.vlan() = 1;
          if (pktCount % 2) {
            rxPkt.cosQueue() = CpuCosQueueId(2);
          } else {
            rxPkt.cosQueue() = CpuCosQueueId(9);
          }
          rxPkt.length() = (*rxPkt.data())->computeChainDataLength();
          co_yield std::move(rxPkt);
        }
      }());
  EXPECT_TRUE(ret);
  WITH_RETRIES({
    counters.update();
    EXPECT_EVENTUALLY_EQ(
        counters.value(
            SwitchStats::kCounterPrefix + "hi_pri_pkts_received.sum"),
        pktCount / 2);
  });
  int midPriCount =
      counters.value(SwitchStats::kCounterPrefix + "mid_pri_pkts_received.sum");
  WITH_RETRIES({
    counters.update();
    EXPECT_EVENTUALLY_EQ(
        counters.value(
            SwitchStats::kCounterPrefix + "mid_pri_pkts_received.sum"),
        midPriCount);
    midPriCount = counters.value(
        SwitchStats::kCounterPrefix + "mid_pri_pkts_received.sum");
  });
  XLOG(DBG2) << "Total pkt sent: " << pktCount << " hi pri rx: "
             << counters.value(
                    SwitchStats::kCounterPrefix + "hi_pri_pkts_received.sum")
             << " mid pri rx: "
             << counters.value(
                    SwitchStats::kCounterPrefix + "mid_pri_pkts_received.sum");
  EXPECT_LE(midPriCount, pktCount / 2);
}
CO_TEST_F(ThriftServerTest, transmitPktHandler) {
  // setup server and clients
  setupServerAndClients();

  // Mark a switch as connected so that SwSwitch won't abort while tearing down
  // the thrift stream
  sw_->getHwSwitchHandler()->connected(SwitchID(1));

  std::string payloadPad(9216 * 2, 'f'); // jumbo pkt
  auto pkt = createV4Packet(
      folly::IPAddressV4("10.0.0.2"),
      folly::IPAddressV4("10.0.0.1"),
      MockPlatform::getMockLocalMac(),
      MockPlatform::getMockLocalMac(),
      payloadPad);

  auto txPkt = createTxPacket(sw_, pkt);
  auto origPktSize = txPkt->buf()->length();

  auto gen =
      (co_await multiSwitchClient_->co_getTxPackets(0)).toAsyncGenerator();

  sw_->sendPacketOutViaThriftStream(std::move(txPkt), SwitchID(0), PortID(5));

  const auto& val = co_await gen.next();
  // got packet
  EXPECT_EQ(5, *val->port());
  EXPECT_EQ(origPktSize, (*val->data())->computeChainDataLength());
  sw_->getHwSwitchHandler()->stop();
}

CO_TEST_F(ThriftServerTest, statsUpdate) {
  // setup server and clients
  setupServerAndClients();

  ThriftHandler handler(sw_);
  auto portName = sw_->getState()->getPorts()->getNodeIf(PortID(5))->getName();

  auto getTestStatUpdate = [&portName](
                               int64_t sysPortQueue1Bytes = 10,
                               int64_t sysPortQueue2Bytes = 20) {
    multiswitch::HwSwitchStats stats;
    stats.timestamp() = 1000;

    // port stats
    HwPortStats portStats;
    portStats.inBytes_() = 10000;
    portStats.outBytes_() = 20000;
    for (auto queueId = 0; queueId < 10; queueId++) {
      portStats.queueOutBytes_()[queueId] = queueId * 10;
      portStats.queueOutDiscardBytes_()[queueId] = queueId * 2;
    }
    stats.hwPortStats() = {{portName, std::move(portStats)}};

    // hw resource stats
    stats.hwResourceStats()->acl_counters_free() = 50;

    // sys port stats
    HwSysPortStats sysPortStats;
    sysPortStats.queueOutBytes_() = {
        {1, sysPortQueue1Bytes}, {2, sysPortQueue2Bytes}};
    stats.sysPortStats() = {{portName, std::move(sysPortStats)}};

    // fabric reachability stats
    FabricReachabilityStats reachabilityStats;
    reachabilityStats.mismatchCount() = 10;
    reachabilityStats.missingCount() = 20;
    stats.fabricReachabilityStats() = std::move(reachabilityStats);

    // cpu port stats
    CpuPortStats cpuPortStats;
    cpuPortStats.queueInPackets_() = {{1, 10}, {2, 20}};
    stats.cpuPortStats() = std::move(cpuPortStats);

    return stats;
  };

  uint16_t switchIndex = 0;

  CounterCache counters(sw_);
  // Send packets to server using sink
  auto result = co_await multiSwitchClient_->co_syncHwStats(0);
  auto ret = co_await result.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::HwSwitchStats&&> {
        // verify stats sync is active
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.0.stats_event_sync_active"), 1);
        });
        co_yield getTestStatUpdate();
      }());
  EXPECT_TRUE(ret);
  EXPECT_EQ(sw_->getHwSwitchStatsExpensive(switchIndex), getTestStatUpdate());
  auto result1 = co_await multiSwitchClient_->co_syncHwStats(1);
  auto ret1 = co_await result1.sink(
      [&]() -> folly::coro::AsyncGenerator<multiswitch::HwSwitchStats&&> {
        WITH_RETRIES({
          counters.update();
          EXPECT_EVENTUALLY_EQ(
              counters.value("switch.1.stats_event_sync_active"), 1);
        });
        co_yield getTestStatUpdate(30, 40);
      }());
  EXPECT_TRUE(ret1);
  EXPECT_EQ(sw_->getHwSwitchStatsExpensive(1), getTestStatUpdate(30, 40));
  sw_->updateStats();
  EXPECT_EQ(sw_->getFabricReachabilityStats().mismatchCount().value(), 20);
  EXPECT_EQ(sw_->getFabricReachabilityStats().missingCount().value(), 40);
  auto agentStats = sw_->fillFsdbStats();
  EXPECT_EQ(agentStats.hwPortStats()[portName].inBytes_().value(), 10000);
  EXPECT_EQ(
      agentStats.sysPortStats()[portName].queueOutBytes_().value()[1], 10);
  EXPECT_EQ(
      agentStats.sysPortStatsMap()[1][portName].queueOutBytes_().value()[1],
      30);
  // CHECK entry in switchIndex map
  EXPECT_EQ(
      agentStats.hwResourceStatsMap()[switchIndex].acl_counters_free().value(),
      50);
  // Check old global entry
  EXPECT_EQ(agentStats.hwResourceStats()->acl_counters_free().value(), 50);
  // verify that swswitch continues caching hwswitch stats after fsdb update
  auto fdsbStats = sw_->fillFsdbStats();
  EXPECT_EQ(sw_->getHwSwitchStatsExpensive(switchIndex), getTestStatUpdate());
  PortInfoThrift portInfo;
  handler.getPortInfo(portInfo, 5);
  EXPECT_EQ(portInfo.input()->bytes().value(), 10000);
  EXPECT_EQ(portInfo.output()->bytes().value(), 20000);
  std::map<std::string, HwPortStats> hwPortStats;
  handler.getHwPortStats(hwPortStats);
  EXPECT_EQ(hwPortStats[portName].inBytes_(), 10000);
  EXPECT_EQ(hwPortStats[portName].outBytes_(), 20000);
  std::map<std::string, HwSysPortStats> hwSysPortStats;
  handler.getSysPortStats(hwSysPortStats);
  EXPECT_FALSE(hwSysPortStats.contains(portName));
  EXPECT_EQ(
      hwSysPortStats[folly::to<std::string>("switch.0.", portName)]
          .queueOutBytes_()
          .value()[1],
      10);
  EXPECT_EQ(
      hwSysPortStats[folly::to<std::string>("switch.1.", portName)]
          .queueOutBytes_()
          .value()[1],
      30);
  std::map<int, CpuPortStats> cpuPortStats;
  handler.getAllCpuPortStats(cpuPortStats);
  EXPECT_EQ(cpuPortStats[0], getTestStatUpdate().cpuPortStats().value());
}

TEST_F(ThriftServerTest, counterStatsAccumulation) {
  // Populate counterStats on switch 0
  multiswitch::HwSwitchStats stats0;
  HwSwitchCounter counter0;
  counter0.bytes() = 100;
  counter0.packets() = 10;
  stats0.counterStats()->routeCounters()["foo"] = counter0;
  HwSwitchCounter counter0bar;
  counter0bar.bytes() = 50;
  stats0.counterStats()->routeCounters()["bar"] = counter0bar;
  sw_->updateHwSwitchStats(0, stats0);

  // Populate counterStats on switch 1 with overlapping counter name
  multiswitch::HwSwitchStats stats1;
  HwSwitchCounter counter1;
  counter1.bytes() = 200;
  counter1.packets() = 20;
  stats1.counterStats()->routeCounters()["foo"] = counter1;
  HwSwitchCounter counter1baz;
  counter1baz.bytes() = 75;
  counter1baz.packets() = 5;
  stats1.counterStats()->routeCounters()["baz"] = counter1baz;
  sw_->updateHwSwitchStats(1, stats1);

  auto routeCounters = sw_->getRouteCounters();
  ThriftHandler handler(sw_);
  std::map<std::string, HwSwitchCounter> thriftRouteCounters;
  handler.getRouteCounters(thriftRouteCounters);
  EXPECT_EQ(routeCounters, thriftRouteCounters);
  auto agentStats = sw_->fillFsdbStats();
  EXPECT_EQ(routeCounters, *agentStats.counterStats()->routeCounters());

  // "foo" exists on both switches — values should be summed
  EXPECT_EQ(routeCounters.size(), 3);
  EXPECT_EQ(*routeCounters.at("foo").bytes(), 300);
  EXPECT_EQ(*routeCounters.at("foo").packets(), 30);

  // "bar" only on switch 0
  EXPECT_EQ(*routeCounters.at("bar").bytes(), 50);
  EXPECT_FALSE(routeCounters.at("bar").packets().has_value());

  // "baz" only on switch 1
  EXPECT_EQ(*routeCounters.at("baz").bytes(), 75);
  EXPECT_EQ(*routeCounters.at("baz").packets(), 5);
}
