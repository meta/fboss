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

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <optional>

namespace facebook::fboss {

USE_THRIFT_COW(TransceiverSpec)
/*
 * TransceiverSpec stores state about one of the Present TransceiverSpec entries
 * on the switch. Mainly use it as a reference to program Port.
 */
class TransceiverSpec
    : public ThriftStructNode<TransceiverSpec, state::TransceiverSpecFields> {
 public:
  using Base = ThriftStructNode<TransceiverSpec, state::TransceiverSpecFields>;
  explicit TransceiverSpec(TransceiverID id);
  static std::shared_ptr<TransceiverSpec> createPresentTransceiver(
      const TransceiverInfo& tcvrInfo);

  cfg::PlatformPortConfigOverrideFactor toPlatformPortConfigOverrideFactor()
      const;

  TransceiverID getID() const {
    return static_cast<TransceiverID>(get<switch_state_tags::id>()->cref());
  }

  std::optional<double> getCableLength() const {
    if (auto cableLength = get<switch_state_tags::cableLength>()) {
      return cableLength->cref();
    }
    return std::nullopt;
  }
  void setCableLength(double cableLength) {
    set<switch_state_tags::cableLength>(cableLength);
  }

  std::optional<MediaInterfaceCode> getMediaInterface() const {
    if (auto mediaInterface = get<switch_state_tags::mediaInterface>()) {
      return mediaInterface->cref();
    }
    return std::nullopt;
  }
  void setMediaInterface(MediaInterfaceCode mediaInterface) {
    set<switch_state_tags::mediaInterface>(mediaInterface);
  }

  std::optional<TransceiverManagementInterface> getManagementInterface() const {
    if (auto interface = get<switch_state_tags::managementInterface>()) {
      return interface->cref();
    }
    return std::nullopt;
  }
  void setManagementInterface(
      TransceiverManagementInterface managementInterface) {
    set<switch_state_tags::managementInterface>(managementInterface);
  }

  std::optional<Vendor> getVendor() const {
    if (auto vendor = get<switch_state_tags::vendor>()) {
      return vendor->toThrift();
    }
    return std::nullopt;
  }

  void setVendor(const Vendor& vendor) {
    set<switch_state_tags::vendor>(vendor);
  }

  std::optional<std::vector<TransceiverVendorOverride>> getVendorOverrides()
      const {
    if (auto vendorOverrides = get<switch_state_tags::vendorOverrides>()) {
      return vendorOverrides->toThrift();
    }
    return std::nullopt;
  }

  void setVendorOverrides(
      const std::vector<TransceiverVendorOverride>& vendorOverrides) {
    set<switch_state_tags::vendorOverrides>(vendorOverrides);
  }

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};
} // namespace facebook::fboss
