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

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncTimeout.h>

namespace folly {

class MacAddress;
class IPAddressV6;

} // namespace folly

namespace facebook::fboss {

class Interface;
class IPv6RAImpl;
class SwitchState;
class SwSwitch;

/**
 * IPv6RouteAdvertiser takes care of periodically sending out IPv6 route
 * advertisement packets on an interface.
 *
 * When you create an IPv6RouteAdvertiser object, it will begin sending out RA
 * packets at the interval specified in the interface's NdpConfig.  When you
 * destroy the IPv6RouteAdvertiser it will stop sending out RA packets.
 */
class IPv6RouteAdvertiser {
 public:
  IPv6RouteAdvertiser(
      SwSwitch* sw,
      const SwitchState* state,
      const Interface* intf);
  ~IPv6RouteAdvertiser();

  // IPv6RouteAdvertiser objects are movable
  IPv6RouteAdvertiser(IPv6RouteAdvertiser&& other) noexcept;
  IPv6RouteAdvertiser& operator=(IPv6RouteAdvertiser&& other) noexcept;

  static uint32_t getPacketSize(const Interface* intf);
  static void createAdvertisementPacket(
      SwSwitch* sw,
      const Interface* intf,
      folly::io::RWPrivateCursor* cursor,
      folly::MacAddress dstMac,
      const folly::IPAddressV6& dstIP);

 private:
  void stop();

  /*
   * All of the work is actually done by an IPv6RAImpl object.
   *
   * The separation between IPv6RouteAdvertiser and IPv6RAImpl is primarily to
   * handle proper synchronization with the background thread when the
   * IPv6RAImpl object is destroyed.  When the IPv6RAImpl is destroyed, the
   * IPv6RAImpl still needs to remain around briefly until the timeout can be
   * cancelled in the background thread.
   */
  IPv6RAImpl* adv_{nullptr};
};

} // namespace facebook::fboss
