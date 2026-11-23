/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/MKAServiceManager.h"
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/ThriftServiceUtils.h"
#include "gmock/gmock.h"

using ::testing::_;
using ::testing::AtLeast;
using namespace facebook::fboss;
using namespace std::chrono;

namespace {
#if FOLLY_HAS_COROUTINES
class PacketAcceptor
    : public facebook::fboss::AsyncPacketTransport::ReadCallback {
 public:
  explicit PacketAcceptor(std::shared_ptr<folly::Baton<>> baton)
      : baton_(baton) {
    if (baton_) {
      baton_->reset();
    }
  }

  void onReadError(
      const folly::AsyncSocketException& /*ex*/) noexcept override {
    baton_->post();
  }

  void onReadClosed() noexcept override {}

  void onDataAvailable(std::unique_ptr<folly::IOBuf> data) noexcept override {
    rxIOBufs_.emplace_back(std::move(data));
    baton_->post();
  }

  std::vector<std::unique_ptr<folly::IOBuf>> rxIOBufs_;
  std::shared_ptr<folly::Baton<>> baton_;
};

class MKAServiceManagerTest : public testing::Test {
 public:
  static constexpr std::array<uint8_t, 6> DSTMAC =
      {0x01, 0x80, 0xc2, 0x00, 0x00, 0x03};
  std::unique_ptr<HwTestHandle> setupTestHandle(bool enableMacsec = true) {
    auto switchFlags =
        enableMacsec ? SwitchFlags::ENABLE_MACSEC : SwitchFlags::DEFAULT;
    auto state = testStateAWithPortsUp();
    return createTestHandle(state, switchFlags);
  }
  std::unique_ptr<folly::IOBuf> createEapol() {
    auto iobuf = folly::IOBuf::create(64);
    iobuf->append(64);
    folly::io::RWPrivateCursor cursor(iobuf.get());
    cursor.push(DSTMAC.data(), folly::MacAddress::SIZE);
    cursor.push(
        MockPlatform::getMockLocalMac().bytes(), folly::MacAddress::SIZE);
    cursor.writeBE<uint16_t>(MKAServiceManager::ETHERTYPE_EAPOL);
    return iobuf;
  }

  TPacket createPacket(PortID activePort) {
    TPacket pkt;
    pkt.l2Port() = folly::to<std::string>(activePort);
    pkt.buf() = createEapol()->to<std::string>();
    return pkt;
  }

  void init(bool enableMacsec = true) {
    handle_ = setupTestHandle(enableMacsec);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(steady_clock::now());
    for (const auto& portMap : std::as_const(*(sw_->getState()->getPorts()))) {
      const auto& ports = std::as_const(*portMap.second);
      if (!ports.empty()) {
        const auto& port = *ports.begin();
        activePort_ = port.second->getID();
      }
    }
    if (enableMacsec) {
      MKAServiceManager* manager = sw_->getMKAServiceMgr();
      mkaServerStream_->connectClient(manager->getServerPort());
      baton_->reset();
      EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
      EXPECT_TRUE(mkaServerStream_->isConnectedToServer());
      EXPECT_TRUE(manager->isConnectedToMkaServer());
      mkaPktTransport_ =
          mkaServerStream_->listen(folly::to<std::string>(activePort_));
      mkaPktTransport_->setReadCallback(recvAcceptor_.get());
    }
  }
  void SetUp() override {
    mkaClientThread_ = std::make_unique<folly::ScopedEventBaseThread>();
    mkaTimerThread_ = std::make_unique<folly::ScopedEventBaseThread>();
    baton_ = std::make_shared<folly::Baton<>>();
    mkaServerStream_ = std::make_shared<BidirectionalPacketStream>(
        "mka_server",
        mkaClientThread_->getEventBase(),
        mkaTimerThread_->getEventBase(),
        10);
    mkaServer_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        mkaServerStream_,
        facebook::fboss::ThriftServiceUtils::createThriftServerConfig());
    FLAGS_fboss_mka_port = 0;
    FLAGS_mka_service_port = mkaServer_->getPort();
    FLAGS_mka_reconnect_timer = 10;
    recvAcceptor_ = std::make_unique<PacketAcceptor>(baton_);
  }

  void TearDown() override {
    mkaServerStream_->stopClient();
    mkaTimerThread_->getEventBase()->terminateLoopSoon();
    mkaClientThread_->getEventBase()->terminateLoopSoon();
    baton_->reset();
    EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(100)));
  }

  void validateRecvPacket(MKAServiceManager* manager, PortID port) {
    // Cache the current stats
    CounterCache counters(sw_);
    manager->recvPacket(createPacket(port));
    counters.update();
    counters.checkDelta(
        SwitchStats::kCounterPrefix + "mka_service.recvd.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "mkpdu.send.sum", 1);
  }

  void validateRecvFailed(CounterCache& counters) {
    counters.update();
    counters.checkDelta(
        SwitchStats::kCounterPrefix + "mka_service.recvd.sum", 0);
    counters.checkDelta(SwitchStats::kCounterPrefix + "mkpdu.send.sum", 0);
  }

  void sendPktToMka() {
    baton_->reset();
    auto iobuf = createEapol();
    std::unique_ptr<MockRxPacket> rxPkt =
        std::make_unique<MockRxPacket>(std::move(iobuf));
    rxPkt->setSrcPort(activePort_);
    auto vlan =
        sw_->getState()->getPorts()->getNodeIf(activePort_)->getIngressVlan();
    rxPkt->setSrcVlan(vlan);
    EXPECT_NO_THROW(sw_->packetReceivedThrowExceptionOnError(std::move(rxPkt)));
  }

  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_;
  PortID activePort_;
  std::unique_ptr<folly::ScopedEventBaseThread> mkaTimerThread_;
  std::unique_ptr<folly::ScopedEventBaseThread> mkaClientThread_;
  std::shared_ptr<BidirectionalPacketStream> mkaServerStream_;
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> mkaServer_;
  std::shared_ptr<folly::Baton<>> baton_;
  std::shared_ptr<AsyncPacketTransport> mkaPktTransport_;
  std::unique_ptr<PacketAcceptor> recvAcceptor_;
};

TEST_F(MKAServiceManagerTest, SendTest) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  validateRecvPacket(manager, activePort_);
}

TEST_F(MKAServiceManagerTest, EmptyMgr) {
  init(false);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_EQ(manager, nullptr);
}

TEST_F(MKAServiceManagerTest, SendTestLocalMgr) {
  init(false);
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  EXPECT_EQ(sw_->getMKAServiceMgr(), nullptr);
  MKAServiceManager manager(sw_);
  validateRecvPacket(&manager, activePort_);
}

TEST_F(MKAServiceManagerTest, SendInvalidPort) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(0);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  manager->recvPacket(createPacket(PortID(9999)));
}

TEST_F(MKAServiceManagerTest, SendInvalidPacket) {
  init();
  CounterCache counters(sw_);
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(0);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  manager->recvPacket(TPacket());
  TPacket pkt;
  pkt.l2Port() = "test";
  pkt.buf() = "test";
  manager->recvPacket(std::move(pkt));
  validateRecvFailed(counters);
}

TEST_F(MKAServiceManagerTest, RecvPktFromMkaServer) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  baton_->reset();
  TPacket pkt = createPacket(activePort_);
  mkaServerStream_->send(std::move(pkt));
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
}

TEST_F(MKAServiceManagerTest, SendPktToMkaServer) {
  init();
  baton_->reset();
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(100)));
  CounterCache counters(sw_);
  sendPktToMka();
  EXPECT_TRUE(baton_->try_wait_for(std::chrono::milliseconds(200)));
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "mkpdu.recvd.sum", 1);
}

TEST_F(MKAServiceManagerTest, SendPktToMkaServerUnregisteredPort) {
  init(false);
  CounterCache counters(sw_);
  sendPktToMka();
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "mkpdu.recvd.sum", 0);
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "mkpdu.err.port_not_regd", 0);
}

TEST_F(MKAServiceManagerTest, MkPduLate) {
  init();
  baton_->reset();
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(100)));

  CounterCache counters(sw_);

  sendPktToMka();
  EXPECT_TRUE(baton_->try_wait_for(std::chrono::milliseconds(200)));

  // sleep override
  std::this_thread::sleep_for(1000ms);

  sendPktToMka();
  EXPECT_TRUE(baton_->try_wait_for(std::chrono::milliseconds(200)));

  auto portName =
      sw_->getState()->getPorts()->getNodeIf(activePort_)->getName();
  auto counterName = portName + ".mkpdu.interval.ms.p100";
  WITH_RETRIES({
    SCOPED_TRACE(counterName);
    counters.update();
    EXPECT_EVENTUALLY_TRUE(counters.checkExist(counterName));
    EXPECT_EVENTUALLY_GE(counters.value(counterName), 1000);
  });
}

#endif
} // unnamed namespace
