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

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss::utils {

// DSCP is a 6-bit field (RFC 2474): valid codepoints are 0..63. Shared so the
// config and delete qos-policy-map commands validate against one definition
// rather than each carrying their own copy.
constexpr int16_t kMinDscp = 0;
constexpr int16_t kMaxDscp = 63;

// Locates the QosPolicy named `name` in `qosPolicies`, or end() when absent.
// sw.qosPolicies is a list keyed only by the name field, so every command that
// touches a policy has to do this scan; sharing it keeps `config qos policy`
// and `delete qos policy` agreeing on what "the policy called X" means.
//
// Returns an iterator rather than a pointer so callers can erase.
std::vector<cfg::QosPolicy>::iterator findQosPolicy(
    std::vector<cfg::QosPolicy>& qosPolicies,
    const std::string& name);

// Like findQosPolicy, but throws std::runtime_error with a uniform
// "No QoS policy named '<name>' exists" message when absent, so every delete
// command reports the same error instead of repeating the find-then-throw.
std::vector<cfg::QosPolicy>::iterator findQosPolicyOrThrow(
    std::vector<cfg::QosPolicy>& qosPolicies,
    const std::string& name);

// Removes `codepoint` from every map entry's ingress list except the entry
// for `keepTrafficClass`. An ingress codepoint classifies to exactly one
// traffic class, so both adding a codepoint (which must first detach it from
// its old class) and deleting one go through here. An entry left with no
// ingress codepoints and no egress rewrite carries no information and is
// dropped. Returns whether anything was removed.
template <typename MapEntry, typename GetIngressList, typename GetEgressValue>
bool eraseQosMapCodepoint(
    std::vector<MapEntry>& maps,
    int16_t codepoint,
    GetIngressList getIngressList,
    GetEgressValue getEgressValue,
    std::optional<int16_t> keepTrafficClass = std::nullopt) {
  auto byteVal = static_cast<int8_t>(codepoint);
  bool erased = false;

  for (auto& entry : maps) {
    if (keepTrafficClass.has_value() &&
        *entry.internalTrafficClass() == *keepTrafficClass) {
      continue;
    }
    auto& fromList = getIngressList(entry);
    auto it = std::find(fromList.begin(), fromList.end(), byteVal);
    if (it != fromList.end()) {
      fromList.erase(it);
      erased = true;
    }
  }

  if (erased) {
    maps.erase(
        std::remove_if(
            maps.begin(),
            maps.end(),
            [&](const MapEntry& entry) {
              return getIngressList(const_cast<MapEntry&>(entry)).empty() &&
                  !getEgressValue(entry).has_value();
            }),
        maps.end());
  }

  return erased;
}

bool eraseDscp(std::vector<cfg::DscpQosMap>& dscpMaps, int16_t dscp);
bool eraseExp(std::vector<cfg::ExpQosMap>& expMaps, int16_t exp);
bool erasePcp(std::vector<cfg::PcpQosMap>& pcpMaps, int16_t pcp);

} // namespace facebook::fboss::utils
