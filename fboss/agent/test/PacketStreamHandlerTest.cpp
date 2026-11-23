// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/PacketStreamHandler.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/if/gen-cpp2/PacketStream.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/thrift_packet_stream/PacketStreamClient.h"
#include "fboss/lib/ThriftServiceUtils.h"

using namespace facebook::fboss;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
#if FOLLY_HAS_COROUTINES

// Test client that stores received packets for verification.
class TestPacketStreamClient : public PacketStreamClient {
 public:
  TestPacketStreamClient(const std::string& clientId, folly::EventBase* evb)
      : PacketStreamClient(clientId, evb) {}

  void recvPacket(TPacket&& packet) override {
    std::lock_guard<std::mutex> lk(mutex_);
    receivedPackets_.push_back(std::move(packet));
    cv_.notify_all();
  }

  bool waitForPackets(
      size_t count,
      std::chrono::milliseconds timeout = 2000ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    return cv_.wait_for(
        lk, timeout, [&] { return receivedPackets_.size() >= count; });
  }

  std::vector<TPacket> getReceivedPackets() {
    std::lock_guard<std::mutex> lk(mutex_);
    return receivedPackets_;
  }

  size_t receivedCount() {
    std::lock_guard<std::mutex> lk(mutex_);
    return receivedPackets_.size();
  }

 private:
  std::vector<TPacket> receivedPackets_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

// ---- Direct-call unit tests (no Thrift server) ----

class PacketStreamHandlerTest : public testing::Test {
 public:
  void SetUp() override {
    handle_ = createTestHandle(testStateAWithPortsUp());
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(steady_clock::now());

    for (const auto& portMap : std::as_const(*(sw_->getState()->getPorts()))) {
      const auto& ports = std::as_const(*portMap.second);
      if (!ports.empty()) {
        const auto& port = *ports.begin();
        activePort_ = port.second->getID();
        activePortName_ = port.second->getName();
      }
    }

    handler_ = std::make_shared<PacketStreamHandler>(
        sw_, "test_packet_stream", "TestAgent");
    sw_->setPacketStreamHandler(handler_.get());
  }

  void TearDown() override {
    if (sw_) {
      sw_->setPacketStreamHandler(nullptr);
    }
    handler_.reset();
  }

 protected:
  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_;
  PortID activePort_;
  std::string activePortName_;
  std::shared_ptr<PacketStreamHandler> handler_;
};

TEST_F(PacketStreamHandlerTest, HandlePacketDropsWhenNoClients) {
  auto iobuf = folly::IOBuf::create(64);
  iobuf->append(64);
  auto rxPkt = std::make_unique<MockRxPacket>(std::move(iobuf));
  rxPkt->setSrcPort(activePort_);

  EXPECT_NO_THROW(handler_->handlePacket(std::move(rxPkt)));
}

// ---- Thrift server + PacketStreamClient tests ----

class PacketStreamHandlerThriftTest : public testing::Test {
 public:
  void SetUp() override {
    handle_ = createTestHandle(testStateAWithPortsUp());
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(steady_clock::now());

    {
      const auto state = sw_->getState();
      const auto& portMaps = std::as_const(*state->getPorts());
      if (!portMaps.empty()) {
        const auto& portMap = *portMaps.begin();
        const auto& ports = std::as_const(*portMap.second);
        if (!ports.empty()) {
          const auto& port = *ports.begin();
          activePort_ = port.second->getID();
          activePortName_ = port.second->getName();
        }
      }
    }

    handler_ = std::make_shared<PacketStreamHandler>(
        sw_, "test_packet_stream", "TestAgent");
    sw_->setPacketStreamHandler(handler_.get());

    server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        handler_,
        facebook::fboss::ThriftServiceUtils::createThriftServerConfig());
  }

  void TearDown() override {
    server_.reset();
    if (sw_) {
      sw_->setPacketStreamHandler(nullptr);
    }
    handler_.reset();
  }

  // Connect a TestPacketStreamClient to the server and wait until connected.
  std::unique_ptr<TestPacketStreamClient> createConnectedClient(
      const std::string& clientId) {
    auto client = std::make_unique<TestPacketStreamClient>(
        clientId, clientEvbThread_.getEventBase());
    client->connectToServer("::1", server_->getPort());

    // Wait for connection to be established
    int retries = 30;
    while (!client->isConnectedToServer() && retries-- > 0) {
      /* sleep override */ std::this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(client->isConnectedToServer());
    return client;
  }

  std::unique_ptr<MockRxPacket> createTestRxPacket(
      PortID port,
      const std::string& payload = "test_payload") {
    auto iobuf = folly::IOBuf::copyBuffer(payload);
    auto rxPkt = std::make_unique<MockRxPacket>(std::move(iobuf));
    rxPkt->setSrcPort(port);
    return rxPkt;
  }

 protected:
  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_;
  PortID activePort_;
  std::string activePortName_;
  std::shared_ptr<PacketStreamHandler> handler_;
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
  folly::ScopedEventBaseThread clientEvbThread_{"PacketStreamTestClientEvb"};
};

// --- Client lifecycle test ---

TEST_F(PacketStreamHandlerThriftTest, clientLifecycle) {
  const std::string clientId = "lifecycle_client";
  auto client = createConnectedClient(clientId);

  EXPECT_TRUE(handler_->isClientConnected(clientId));

  client.reset(); // triggers disconnect
  /* sleep override */ std::this_thread::sleep_for(100ms);
  EXPECT_FALSE(handler_->isClientConnected(clientId));
}

// --- Tx path tests (Hardware -> Controller via ServerStream) ---

TEST_F(PacketStreamHandlerThriftTest, txPathStreamDelivery) {
  const std::string clientId = "tx_client";
  auto client = createConnectedClient(clientId);

  const std::string payload = "tx_data";
  handler_->handlePacket(createTestRxPacket(activePort_, payload));

  EXPECT_TRUE(client->waitForPackets(1));
  auto pkts = client->getReceivedPackets();
  ASSERT_EQ(pkts.size(), 1);
  EXPECT_EQ(*pkts[0].l2Port(), activePortName_);
  EXPECT_EQ(*pkts[0].buf(), payload);
}

TEST_F(PacketStreamHandlerThriftTest, txPathMultiplePackets) {
  const std::string clientId = "multi_pkt_client";
  auto client = createConnectedClient(clientId);

  constexpr int kPacketCount = 5;
  for (int i = 0; i < kPacketCount; ++i) {
    handler_->handlePacket(
        createTestRxPacket(activePort_, "pkt_" + std::to_string(i)));
  }

  // Wait for all packets to arrive
  EXPECT_TRUE(client->waitForPackets(kPacketCount));

  auto pkts = client->getReceivedPackets();
  ASSERT_EQ(pkts.size(), kPacketCount);
  for (int i = 0; i < kPacketCount; ++i) {
    EXPECT_EQ(*pkts[i].buf(), "pkt_" + std::to_string(i));
  }
}

#endif
} // unnamed namespace
