// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"

#include <string>

#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"

DECLARE_bool(rx_vlan_untagged_packets);

namespace facebook::fboss::utility {

// comparator function for packet snooping to accept case that
// received packet header fields are not equal to originFrame
// but equal to passed in PacketMatchFields values
PacketComparatorFn makePacketComparator(const PacketMatchFields& fields) {
  return [fields](
             const utility::EthFrame& originFrame,
             const utility::EthFrame& receivedFrame) -> bool {
    auto hdr1 = receivedFrame.header();
    auto hdr2 = originFrame.header();

    if (hdr1.getVlanTags() != hdr2.getVlanTags() ||
        hdr1.getCsigTag() != hdr2.getCsigTag() ||
        hdr1.getEtherType() != hdr2.getEtherType() ||
        hdr1.getDstMac() != hdr2.getDstMac()) {
      return false;
    }

    // chec src mac
    if (hdr1.getSrcMac() != hdr2.getSrcMac()) {
      if (!fields.expectedSrcMac ||
          fields.expectedSrcMac.value() != hdr1.getSrcMac()) {
        return false;
      }
    }

    if (receivedFrame.v4PayLoad() != originFrame.v4PayLoad() ||
        receivedFrame.v6PayLoad() != originFrame.v6PayLoad() ||
        receivedFrame.mplsPayLoad() != originFrame.mplsPayLoad() ||
        receivedFrame.arpHdr() != originFrame.arpHdr() ||
        receivedFrame.macControlPayload() != originFrame.macControlPayload()) {
      return false;
    }

    return true;
  };
}

PacketSnooper::PacketSnooper(
    std::optional<PortID> port,
    std::optional<utility::EthFrame> expectedFrame,
    PacketComparatorFn packetComparator,
    packetSnooperReceivePacketType receivePktType)
    : receivePktType_(receivePktType),
      port_(std::move(port)),
      expectedFrame_(std::move(expectedFrame)),
      packetComparator_(std::move(packetComparator)) {
  if (FLAGS_rx_vlan_untagged_packets && expectedFrame_.has_value()) {
    /* strip vlans as rx will always be untagged */
    expectedFrame_->stripVlans();
  }
}

// validate expected packet type for snooping
// if receivePktType_ is PACKET_TYPE_PTP, validate PTP packets and drop others
// if receivePktType_ is PACKET_TYPE_TAJO_MOD, keep only Tajo MoD export packets
// PACKET_TYPE_CHENAB_MOD keeps only frames validated by the Chenab TAC parser
// if receivePktType_ is PACKET_TYPE_ALL, no validation
bool PacketSnooper::expectedReceivedPacketType(
    folly::io::Cursor& cursor) noexcept {
  if (cursor.totalLength() == 0) {
    return false;
  }
  switch (receivePktType_) {
    case packetSnooperReceivePacketType::PACKET_TYPE_PTP:
      // validate PTP packets
      if (!utility::isPtpEventPacket(cursor)) {
        return false;
      }
      break;
    case packetSnooperReceivePacketType::PACKET_TYPE_TAJO_MOD:
      // keep only Tajo Mirror-on-Drop export packets
      if (!utility::isTajoMirrorOnDropPacket(cursor)) {
        return false;
      }
      break;
    case packetSnooperReceivePacketType::PACKET_TYPE_CHENAB_MOD:
      if (!utility::isChenabMirrorOnDropPacket(cursor)) {
        return false;
      }
      break;
    case packetSnooperReceivePacketType::PACKET_TYPE_ALL:
      break;
    default:
      XLOG(ERR) << "Invalid packet type";
      return false;
  }
  return true;
}

void PacketSnooper::packetReceived(const RxPacket* pkt) noexcept {
  XLOG(DBG2) << "pkt received on port " << pkt->getSrcPort();
  if (port_.has_value() && port_.value() != pkt->getSrcPort()) {
    // packet arrived on port not of interest.
    return;
  }
  auto data = pkt->buf()->clone();
  folly::io::Cursor cursor{data.get()};
  auto frame = std::make_unique<utility::EthFrame>(cursor);

  if (packetComparator_.has_value() && expectedFrame_.has_value()) {
    if (!packetComparator_.value()(*expectedFrame_, *frame)) {
      XLOG(DBG2) << " Unexpected packet received "
                 << " expected: " << *expectedFrame_ << std::endl
                 << " got: " << *frame;
      return;
    }
  } else if (expectedFrame_.has_value() && *expectedFrame_ != *frame) {
    XLOG(DBG2) << " Unexpected packet received "
               << " expected: " << *expectedFrame_ << std::endl
               << " got: " << *frame;
    return;
  }

  cursor.reset(data.get());

  if (!expectedReceivedPacketType(cursor)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx_);
  receivedFrames_.push(std::move(frame));
  cv_.notify_all();
}

std::optional<utility::EthFrame> PacketSnooper::waitForPacket(
    uint32_t timeout_s) {
  std::unique_lock<std::mutex> lock(mtx_);

  while (receivedFrames_.empty()) {
    if (timeout_s > 0) {
      if (cv_.wait_for(lock, std::chrono::seconds(timeout_s)) ==
          std::cv_status::timeout) {
        return {};
      }
    } else {
      cv_.wait(lock);
    }
  }
  utility::EthFrame ret = *receivedFrames_.front();
  receivedFrames_.pop();
  return ret;
}

bool PacketSnooper::receivedPacket() {
  std::lock_guard<std::mutex> lock(mtx_);
  return !receivedFrames_.empty();
}

SwSwitchPacketSnooper::SwSwitchPacketSnooper(
    SwSwitch* sw,
    const std::string& name,
    std::optional<PortID> port,
    std::optional<utility::EthFrame> expectedFrame,
    PacketComparatorFn packetComparator,
    packetSnooperReceivePacketType receivePktType)
    : PacketSnooper(
          port,
          std::move(expectedFrame),
          std::move(packetComparator),
          receivePktType),
      sw_(sw),
      name_(name) {
  sw_->getPacketObservers()->registerPacketObserver(this, name_);
}

SwSwitchPacketSnooper::~SwSwitchPacketSnooper() {
  sw_->getPacketObservers()->unregisterPacketObserver(this, name_);
}

std::optional<std::unique_ptr<folly::IOBuf>>
SwSwitchPacketSnooper::waitForPacket(uint32_t timeout_s) {
  auto frame = PacketSnooper::waitForPacket(timeout_s);
  if (!frame) {
    return {};
  }
  auto buf = frame->toIOBuf();
  return std::move(buf);
}

} // namespace facebook::fboss::utility
