/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/RxPacket.h"

namespace facebook::fboss {

class SwRxPacket : public RxPacket {
 public:
  explicit SwRxPacket(std::unique_ptr<folly::IOBuf> buf) noexcept {
    buf_ = std::move(buf);
    len_ = buf_->computeChainDataLength();
  }

  // Noexcept move constructor
  SwRxPacket(SwRxPacket&& other) noexcept {
    buf_ = std::move(other.buf_);
    len_ = other.len_;
    setSrcPort(other.getSrcPort());
    setSrcVlan(other.getSrcVlanIf());
    if (other.cosQueue()) {
      setCosQueue(*other.cosQueue());
    }
    if (other.isFromAggregatePort()) {
      setSrcAggregatePort(other.getSrcAggregatePort());
    }
  }

  void setSrcPort(PortID id) {
    srcPort_ = id;
  }
  void setSrcVlan(std::optional<VlanID> srcVlan) {
    srcVlan_ = srcVlan;
  }
  void setSrcAggregatePort(AggregatePortID srcAggregatePort) {
    isFromAggregatePort_ = true;
    srcAggregatePort_ = srcAggregatePort;
  }
  void setCosQueue(uint8_t cosQueue) {
    cosQueue_ = cosQueue;
  }
  void setPacketType(std::optional<PacketType> packetType) {
    packetType_ = packetType;
  }

 private:
  // Forbidden copy constructor and assignment operator
  SwRxPacket(SwRxPacket const&) = delete;
  SwRxPacket& operator=(SwRxPacket const&) = delete;
};

} // namespace facebook::fboss
