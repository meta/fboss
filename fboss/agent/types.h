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

#include <iosfwd>
#include <map>
#include <optional>

#include <cstdint>
#include <type_traits>

#include "fboss/agent/StrongTypes.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

// Per group type ECMP settings, as carried on SwitchSettings. A group type with
// no entry is unconfigured, which for most types means the attribute is left
// alone rather than programmed false. An empty map is therefore the same as
// nobody using the feature, and is the default.
using EcmpGroupSettingsMap =
    std::map<cfg::EcmpGroupType, cfg::EcmpGroupSettings>;

template <typename>
struct is_fboss_key_object_type {
  static constexpr bool value = false;
};
} // namespace facebook::fboss

FBOSS_STRONG_TYPE(uint8_t, ChannelID)
FBOSS_STRONG_TYPE(uint16_t, TransceiverID)
FBOSS_STRONG_TYPE(uint16_t, AggregatePortID)
FBOSS_STRONG_TYPE(uint16_t, PortID)
FBOSS_STRONG_TYPE(uint8_t, PimID)
FBOSS_STRONG_TYPE(uint16_t, VlanID)
FBOSS_STRONG_TYPE(uint32_t, RouterID)
FBOSS_STRONG_TYPE(uint32_t, InterfaceID)
FBOSS_STRONG_TYPE(int, VrfID)
FBOSS_STRONG_TYPE(uint16_t, SwitchID);
FBOSS_STRONG_TYPE(uint16_t, BridgeID);
FBOSS_STRONG_TYPE(uint16_t, TrafficClass);
FBOSS_STRONG_TYPE(uint8_t, DSCP);
FBOSS_STRONG_TYPE(uint8_t, EXP);
FBOSS_STRONG_TYPE(uint8_t, PCP);
FBOSS_STRONG_TYPE(uint32_t, PciVendorId);
FBOSS_STRONG_TYPE(uint32_t, PciDeviceId);
FBOSS_STRONG_TYPE(uint8_t, PfcPriority);
FBOSS_STRONG_TYPE(uint8_t, MdioControllerID);
FBOSS_STRONG_TYPE(uint8_t, PhyAddr);
FBOSS_STRONG_TYPE(uint16_t, GlobalXphyID);
FBOSS_STRONG_TYPE(uint16_t, XphyId);
FBOSS_STRONG_TYPE(uint32_t, LaneID);
FBOSS_STRONG_TYPE(int32_t, LabelID);
FBOSS_STRONG_TYPE(int64_t, SystemPortID);
FBOSS_STRONG_TYPE(uint16_t, IngressPriorityGroupID);
FBOSS_STRONG_TYPE(uint16_t, SwitchIndex);

/*
 * Unique ID for a NextHop
 */
FBOSS_STRONG_TYPE(int64_t, NextHopID)

/*
 * Unique ID for a set of NextHopIDs
 */
FBOSS_STRONG_TYPE(int64_t, NextHopSetID)

/*
 * Timestamp of a stat
 */
FBOSS_STRONG_TYPE(int64_t, StatTimestamp)

namespace facebook::fboss {

using LoadBalancerID = cfg::LoadBalancerID;

// The top 4 bits of a NextHopID/NextHopSetID name the allocator that owns it,
// so a bare id is never ambiguous. Every allocator gets its own type here.
// Values must stay below 0b1000; bit 63 is the sign bit.
enum class NextHopIdType : int64_t {
  NEXTHOP = 0b0000,
  // 0100 rather than 0001: persisted from 2^62 before types existed.
  NEXTHOP_SET = 0b0100,
  // EcmpResourceManager override groups.
  OVERRIDE_SET = 0b0101,
};

constexpr int kNextHopIdTypeShift = 60;

constexpr NextHopIdType nextHopIdType(int64_t id) {
  return static_cast<NextHopIdType>(id >> kNextHopIdTypeShift);
}

constexpr int64_t nextHopIdTypeStart(NextHopIdType type) {
  return static_cast<int64_t>(type) << kNextHopIdTypeShift;
}

constexpr int64_t nextHopIdTypeEnd(NextHopIdType type) {
  return (static_cast<int64_t>(type) + 1) << kNextHopIdTypeShift;
}

namespace cfg {

std::ostream& operator<<(std::ostream& out, LoadBalancerID id);

} // namespace cfg
} // namespace facebook::fboss
