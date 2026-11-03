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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

/*
 * Only compiled in OSS: callers include this in place of the non-open-sourced
 * neteng/fboss/bgp/cpp/lib headers (see the IS_OSS switch in
 * commands/show/bgp/CmdShowUtils.cpp).
 *
 * Mirrors the internal library's namespace, facebook::nettools::bgplib. It
 * cannot sit at global scope: callers are inside facebook::fboss and spell
 * these unqualified as nettools::bgplib::X, which only reached a global stub
 * while facebook::nettools did not exist in the translation unit. Any header
 * pulling in the generated BgpStructs types now introduces it, and unqualified
 * lookup stops at facebook::nettools without ever reaching global scope.
 *
 * BgpAttrOrigin is deliberately absent: BgpStructs.thrift declares it in this
 * same namespace, so a stub copy would be a redefinition. OSS gets the real
 * generated enum.
 */
namespace facebook {
namespace nettools {
namespace bgplib {

// Stub for BGP community attribute
class BgpAttrCommunityC {
 public:
  // Convert 32-bit community value to asn:value format
  explicit BgpAttrCommunityC(const std::string& commStr) : commStr_(commStr) {}

  static std::shared_ptr<BgpAttrCommunityC> createBgpAttrCommunity(
      const std::string& commStr) {
    return std::make_shared<BgpAttrCommunityC>(commStr);
  }

  std::string to_string() const {
    // Convert 32-bit community to asn:value format
    // BGP communities are encoded as: (asn << 16) | value
    try {
      uint32_t comm32 = std::stoul(commStr_);
      uint16_t asn = (comm32 >> 16) & 0xFFFF;
      uint16_t value = comm32 & 0xFFFF;
      return std::to_string(asn) + ":" + std::to_string(value);
    } catch (...) {
      // If conversion fails, return original string
      return commStr_;
    }
  }

 private:
  std::string commStr_;
};

// Keep unresolved aliases consistent with the internal BGP utility.
constexpr const char* kNullMessage = "(NA)";

// Well-known BGP communities (subset)
// Format: "asn:value" -> "ALIAS"
inline const std::map<std::string, std::string>& getWellKnownCommunities() {
  static const std::map<std::string, std::string> wellKnown = {
      {"65000:1", "NO_EXPORT"},
      {"65000:2", "NO_ADVERTISE"},
      {"65000:3", "NO_EXPORT_SUBCONFED"},
      // Add more as needed
  };
  return wellKnown;
}

// Stub for finding communities in a set
// Returns a map of community sets to their aliases
inline std::map<std::set<std::string>, std::string> findCommunities(
    const std::vector<std::string>& communities,
    const std::map<std::set<std::string>, std::string>& communitySet) {
  std::map<std::set<std::string>, std::string> result;
  const auto& wellKnown = getWellKnownCommunities();

  for (const auto& comm : communities) {
    std::set<std::string> commSet;
    commSet.insert(comm);

    // Check config-sourced community names first, then well-known aliases
    auto configIt = communitySet.find(commSet);
    if (configIt != communitySet.end()) {
      result[commSet] = configIt->second;
    } else {
      auto wellKnownIt = wellKnown.find(comm);
      result[commSet] =
          (wellKnownIt != wellKnown.end()) ? wellKnownIt->second : kNullMessage;
    }
  }
  return result;
}

// Stub for BgpPathC topology info conversion
class BgpPathC {
 public:
  // Accept any type for topoInfoToStr - we don't know the exact type
  template <typename T>
  static std::string topoInfoToStr(const T& /* values */) {
    // Return placeholder - tests will show what's expected
    return "TopoInfo(stub)";
  }
};

// Stub for device type check
inline bool isAristaDevice() {
  // Return false - not an Arista device
  return false;
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
