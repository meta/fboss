/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// Copyright 2014-present Facebook. All Rights Reserved.
#pragma once
#include <folly/io/async/AsyncTimeout.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include "fboss/agent/lldp/LinkNeighborDB.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"

namespace folly {
namespace io {
class Cursor;
}
} // namespace folly

enum LldpValidationResult { SUCCESS, MISMATCH, MISSING };

namespace facebook::fboss {

class RxPacket;
class TxPacket;
class SwSwitch;

class LldpManager : private folly::AsyncTimeout {
  /*
   * LldpManager is the class that manages Lldp support.
   * Responsible for processing received LLDP frames and maintaining the
   * LLDP neighbors table.
   * Also, responsible for periodically sending LLDP frames on all the ports
   * to inform of this switch's presence to its neighbors. Hence inheriting
   * the AsyncTimeout class for that purpose.
   *
   * http://www.ieee802.org/1/files/public/docs2002/lldp-protocol-00.pdf
   */
 public:
  enum : uint16_t {
    ETHERTYPE_LLDP = 0x88CC,
    LLDP_INTERVAL = 5 * 1000, // 5s is normal min value
    TLV_TYPE_BITS_LENGTH = 7,
    TLV_LENGTH_BITS_LENGTH = 9,
    TLV_TYPE_LEFT_SHIFT_OFFSET = 9,
    TLV_LENGTH_LEFT_SHIFT_OFFSET = 1,
    CHASSIS_TLV_LENGTH = 0x07,
    SYSTEM_CAPABILITY_TLV_LENGTH = 0x4,
    SYSTEM_CAPABILITY_SWITCH = 1 << 2, // 3rd bit for switch
    SYSTEM_CAPABILITY_ROUTER = 1 << 4, // 5th bit for router
    TTL_TLV_LENGTH = 0x2,
    TTL_TLV_VALUE = 120,
    PDU_END_TLV_LENGTH = 0,
  };
  explicit LldpManager(SwSwitch* sw);
  ~LldpManager() override;
  static const folly::MacAddress LLDP_DEST_MAC;
  static const folly::MacAddress LLDP_CUSTOMER_BRIDGE_MAC;

  /*
   * Start the timer to send LLDP packets.
   *
   * This may be called from any thread.
   */
  void start();

  /*
   * Stop sending LLDP packets.
   *
   * This must be called while the SwSwitch background thread is still
   * running.
   */
  void stop();

  void handlePacket(
      std::unique_ptr<RxPacket> pkt,
      folly::MacAddress dst,
      folly::MacAddress src,
      folly::io::Cursor cursor);

  // Create an LLDP packet
  static std::unique_ptr<TxPacket> createLldpPkt(
      SwSwitch* sw,
      const folly::MacAddress macaddr,
      const std::optional<VlanID>& vlanID,
      const std::string& hostname,
      const std::string& portname,
      const std::string& portdesc,
      const uint16_t ttl,
      const uint16_t capabilities,
      const std::optional<bool> portDrainState = std::nullopt);

  static std::unique_ptr<TxPacket> createLldpPkt(
      const facebook::fboss::utility::AllocatePktFn& allocate,
      const folly::MacAddress macaddr,
      const std::optional<VlanID>& vlanID,
      const std::string& hostname,
      const std::string& portname,
      const std::string& portdesc,
      const uint16_t ttl,
      const uint16_t capabilities,
      const std::optional<bool> portDrainState = std::nullopt);

  static void fillLldpTlv(
      TxPacket* pkt,
      const folly::MacAddress macaddr,
      const std::optional<VlanID>& vlanID,
      const std::string& systemdescr,
      const std::string& hostname,
      const std::string& portname,
      const std::string& portdesc,
      const uint16_t ttl,
      const uint16_t capabilities,
      const std::optional<bool> portDrainState = std::nullopt,
      const std::optional<folly::MacAddress> dstMac = std::nullopt);

  static std::unique_ptr<TxPacket> createLldpPktCustomBridge(
      const facebook::fboss::utility::AllocatePktFn& allocate,
      const folly::MacAddress macaddr,
      const std::optional<VlanID>& vlanID,
      const std::string& hostname,
      const std::string& portname,
      const std::string& portdesc,
      const uint16_t ttl,
      const uint16_t capabilities);

  // This function is internal.  It is only public for use in unit tests.
  void sendLldpOnAllPorts();

  // Public only for unit tests.
  std::chrono::milliseconds sendNextLldpSlot();

  LinkNeighborDB* getDB() {
    return &db_;
  }

  void portDown(PortID port) {
    db_.portDown(port);
  }

  // Helper to calculate the packet size which createLldpPkt will allocate.
  // Required for unit testing, if nothing else.
  static uint32_t LldpPktSize(
      const std::string& hostname,
      const std::string& portname,
      const std::string& portdesc,
      const std::string& sysDesc,
      bool includePortDrainState = false);

 private:
  void timeoutExpired() noexcept override;
  void sendLldpInfo(const std::shared_ptr<Port>& port);

  SwSwitch* sw_{nullptr};
  std::chrono::milliseconds intervalMsecs_;
  LinkNeighborDB db_;
  std::vector<PortID> txPorts_;
  size_t nextTxPort_{0};
};

} // namespace facebook::fboss
