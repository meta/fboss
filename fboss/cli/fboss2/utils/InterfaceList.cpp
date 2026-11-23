/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/InterfaceList.h"
#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/PortMap.h"

namespace facebook::fboss::utils {

namespace {

constexpr std::string_view kLoopbackPrefix = "loopback";

// Resolves a loopback token against the conventional layout: an interface
// named "fbossLoopback<N>" (Meta-style configs), else the virtual interface
// at intfID kLoopbackIntfIdBase + N (bootstrap configs leave it unnamed).
// The literal name "loopback<N>" is covered by the caller's ordinary
// name lookup before this runs.
cfg::Interface* findLoopbackInterface(const PortMap& portMap, int32_t index) {
  if (cfg::Interface* named =
          portMap.getInterfaceByName(loopbackVlanName(index))) {
    return named;
  }
  cfg::Interface* byId =
      portMap.getInterface(InterfaceID(kLoopbackIntfIdBase + index));
  if (byId != nullptr && *byId->isVirtual()) {
    return byId;
  }
  return nullptr;
}

} // namespace

std::optional<int32_t> parseLoopbackIndex(const std::string& name) {
  if (name.size() <= kLoopbackPrefix.size() ||
      name.size() > kLoopbackPrefix.size() + 2) {
    return std::nullopt;
  }
  for (size_t i = 0; i < kLoopbackPrefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(name[i])) !=
        kLoopbackPrefix[i]) {
      return std::nullopt;
    }
  }
  const std::string digits = name.substr(kLoopbackPrefix.size());
  auto parsed = folly::tryTo<int32_t>(digits);
  if (!parsed.hasValue() || parsed.value() < 0 ||
      parsed.value() > kMaxLoopbackIndex) {
    return std::nullopt;
  }
  return parsed.value();
}

std::string loopbackVlanName(int32_t index) {
  return fmt::format("fbossLoopback{}", index);
}

InterfaceList::InterfaceList(std::vector<std::string> names, bool allowMissing)
    : names_(std::move(names)) {
  // Get the PortMap from the session
  auto& portMap = ConfigSession::getInstance().getPortMap();

  // Resolve names to Intf objects
  std::vector<std::string> notFound;

  for (const auto& name : names_) {
    Intf intf(name);

    // First try to look up as a port name
    cfg::Port* port = portMap.getPort(name);
    if (port) {
      intf.setPort(port);
      // Also try to get the associated interface
      auto interfaceId = portMap.getInterfaceIdForPort(name);
      if (interfaceId) {
        cfg::Interface* interface = portMap.getInterface(*interfaceId);
        if (interface) {
          intf.setInterface(interface);
        }
      }
    } else {
      // If not found as a port, try as an interface name, then as an
      // interface ID, then as a loopback token against the conventional
      // loopback layout.
      cfg::Interface* interface = portMap.getInterfaceByName(name);
      if (!interface) {
        // A purely-numeric name may be an interface ID.
        auto parsedInterfaceId = folly::tryTo<int32_t>(name);
        if (parsedInterfaceId.hasValue() && *parsedInterfaceId >= 0) {
          interface = portMap.getInterface(InterfaceID(*parsedInterfaceId));
        }
      }
      if (!interface) {
        if (auto loopbackIndex = parseLoopbackIndex(name)) {
          interface = findLoopbackInterface(portMap, *loopbackIndex);
        }
      }
      if (interface) {
        intf.setInterface(interface);
      }
    }

    if (!intf.isValid() && !allowMissing) {
      notFound.push_back(name);
    } else {
      data_.push_back(intf);
    }
  }

  if (!notFound.empty()) {
    throw std::invalid_argument(
        "Port(s) or interface(s) not found in configuration: " +
        folly::join(", ", notFound) +
        ". Ports must exist in the hardware platform mapping and be defined "
        "in the configuration before they can be configured.");
  }
}

} // namespace facebook::fboss::utils
