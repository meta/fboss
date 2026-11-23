// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/PacketObserver.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/types.h"

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <gtest/gtest.h>
#include <condition_variable>
#include <optional>
#include <queue>

namespace facebook::fboss {
class RxPacket;
} // namespace facebook::fboss

namespace facebook::fboss::utility {

using PacketComparatorFn =
    std::optional<std::function<bool(utility::EthFrame, utility::EthFrame)>>;

// Packet rewrite fields
struct PacketMatchFields {
  std::optional<folly::MacAddress> expectedSrcMac;
};

// Packet comparator function with acceptable packet rewrite fields
PacketComparatorFn makePacketComparator(const PacketMatchFields& criteria = {});

// define received packet type for packetSnooper
enum class packetSnooperReceivePacketType {
  PACKET_TYPE_ALL,
  PACKET_TYPE_PTP,
  // Keep only Tajo Mirror-on-Drop export packets; drop unrelated CPU-punted
  // traffic that shares the punt path on Tajo.
  PACKET_TYPE_TAJO_MOD,
  // Keep only frames validated by the Chenab TAC parser, excluding unrelated
  // CPU-punted traffic.
  PACKET_TYPE_CHENAB_MOD,
};

class PacketSnooper : public PacketObserverIf {
 public:
  PacketSnooper(
      std::optional<PortID> port = std::nullopt,
      std::optional<utility::EthFrame> expectedFrame = std::nullopt,
      PacketComparatorFn packetComparator = std::nullopt,
      packetSnooperReceivePacketType receivePktType =
          packetSnooperReceivePacketType::PACKET_TYPE_ALL);

  void packetReceived(const RxPacket* pkt) noexcept override;
  bool expectedReceivedPacketType(folly::io::Cursor& cursor) noexcept;

  virtual ~PacketSnooper() override {
    if (!ignoreUnclaimedRxPkts_) {
      EXPECT_TRUE(receivedFrames_.empty());
    }
  }
  // Wait until timeout (seconds), If timeout = 0, wait forever.
  std::optional<utility::EthFrame> waitForPacket(uint32_t timeout_s = 0);
  // Whether a matching packet has arrived, without waiting for one.
  bool receivedPacket();
  void ignoreUnclaimedRxPkts() {
    ignoreUnclaimedRxPkts_ = true;
  }

 private:
  packetSnooperReceivePacketType receivePktType_;
  std::optional<PortID> port_;
  std::optional<utility::EthFrame> expectedFrame_;
  PacketComparatorFn packetComparator_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::queue<std::unique_ptr<utility::EthFrame>> receivedFrames_;
  bool ignoreUnclaimedRxPkts_{false};
};

class SwSwitchPacketSnooper : public PacketSnooper {
 public:
  SwSwitchPacketSnooper(
      SwSwitch* sw,
      const std::string& name,
      std::optional<PortID> port = std::nullopt,
      std::optional<utility::EthFrame> expectedFrame = std::nullopt,
      PacketComparatorFn packetComparator = std::nullopt,
      packetSnooperReceivePacketType receivePktType =
          packetSnooperReceivePacketType::PACKET_TYPE_ALL);

  std::optional<std::unique_ptr<folly::IOBuf>> waitForPacket(
      uint32_t timeout_s);

  virtual ~SwSwitchPacketSnooper() override;

 private:
  SwSwitch* sw_;
  std::string name_;
};

} // namespace facebook::fboss::utility
