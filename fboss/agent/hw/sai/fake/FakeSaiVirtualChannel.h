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

#include "fboss/agent/hw/sai/fake/FakeManager.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class FakeVirtualChannel {
 public:
  FakeVirtualChannel(sai_object_id_t port, sai_uint8_t index)
      : port_(port), index_(index) {}

  sai_object_id_t getPort() const {
    return port_;
  }
  sai_uint8_t getIndex() const {
    return index_;
  }
  sai_uint32_t getCbfcReceiverNativeCreditLimit() const {
    return cbfcReceiverNativeCreditLimit_;
  }
  sai_object_id_t getCbfcSenderCreditProfile() const {
    return cbfcSenderCreditProfile_;
  }
  void setCbfcSenderCreditProfile(sai_object_id_t profile) {
    cbfcSenderCreditProfile_ = profile;
  }
  bool getCbfcReceiverEnable() const {
    return cbfcReceiverEnable_;
  }
  void setCbfcReceiverEnable(bool enable) {
    cbfcReceiverEnable_ = enable;
  }
  bool getCbfcSenderEnable() const {
    return cbfcSenderEnable_;
  }
  void setCbfcSenderEnable(bool enable) {
    cbfcSenderEnable_ = enable;
  }

  sai_object_id_t id{};

 private:
  // PORT and INDEX are MANDATORY_ON_CREATE | CREATE_ONLY | KEY, so they have
  // no setters.
  sai_object_id_t port_;
  sai_uint8_t index_;
  // READ_ONLY on real hardware; canned here, as on the port.
  sai_uint32_t cbfcReceiverNativeCreditLimit_{500};
  sai_object_id_t cbfcSenderCreditProfile_{SAI_NULL_OBJECT_ID};
  bool cbfcReceiverEnable_{false};
  bool cbfcSenderEnable_{false};
};

class FakeCbfcCreditProfile {
 public:
  FakeCbfcCreditProfile(sai_object_id_t poolId, sai_uint64_t reservedCreditSize)
      : poolId_(poolId), reservedCreditSize_(reservedCreditSize) {}

  sai_object_id_t getPoolId() const {
    return poolId_;
  }
  sai_uint64_t getReservedCreditSize() const {
    return reservedCreditSize_;
  }
  void setReservedCreditSize(sai_uint64_t reservedCreditSize) {
    reservedCreditSize_ = reservedCreditSize;
  }

  sai_object_id_t id{};

 private:
  sai_object_id_t poolId_;
  sai_uint64_t reservedCreditSize_;
};

using FakeVirtualChannelManager =
    FakeManager<sai_object_id_t, FakeVirtualChannel>;
using FakeCbfcCreditProfileManager =
    FakeManager<sai_object_id_t, FakeCbfcCreditProfile>;

void populate_virtual_channel_api(
    sai_virtual_channel_api_t** virtual_channel_api);

} // namespace facebook::fboss
