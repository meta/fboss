/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/interface/ipv6/ndp/CmdDeleteInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/ipv6/ndp/CmdConfigInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

// ---------------------------------------------------------------------------
// NdpDeleteAttrs constructor — validates token list against known attr names
// ---------------------------------------------------------------------------

NdpDeleteAttrs::NdpDeleteAttrs(const std::vector<std::string>& v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "No NDP attribute provided. Valid attributes: " +
        folly::join(", ", ndpAttrNames()));
  }

  std::unordered_set<std::string> seen;
  for (const auto& attr : v) {
    std::string attrLower = attr;
    std::transform(
        attrLower.begin(), attrLower.end(), attrLower.begin(), ::tolower);

    if (ndpAttrNames().find(attrLower) == ndpAttrNames().end()) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown ndp attribute '{}'. Valid attributes: {}",
              attr,
              folly::join(", ", ndpAttrNames())));
    }

    if (!seen.insert(attrLower).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate ndp attribute '{}'", attrLower));
    }

    attributes_.push_back(attrLower);
  }
}

// ---------------------------------------------------------------------------
// CmdDeleteInterfaceIpv6Ndp::queryClient
// ---------------------------------------------------------------------------

CmdDeleteInterfaceIpv6NdpTraits::RetType CmdDeleteInterfaceIpv6Ndp::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& ndpAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  // NDP is an L3-interface property; reject names that resolved to a port
  // with no associated L3 interface rather than silently skipping them.
  std::vector<std::string> noL3Interface;
  for (const utils::Intf& intf : interfaces) {
    if (!intf.getInterface()) {
      noL3Interface.push_back(intf.name());
    }
  }
  if (!noL3Interface.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No L3 interface in configuration for: {}. "
            "NDP is configured on L3 interfaces.",
            folly::join(", ", noL3Interface)));
  }

  const cfg::NdpConfig kDefaultNdp;

  bool modified = false;
  for (const utils::Intf& intf : interfaces) {
    cfg::Interface* iface = intf.getInterface();
    if (!iface->ndp().has_value()) {
      // Idempotent: nothing to reset on interfaces with no NDP config
      continue;
    }

    for (const auto& attr : ndpAttrs.getAttributes()) {
      if (attr == "ra-interval") {
        iface->ndp()->routerAdvertisementSeconds() =
            *kDefaultNdp.routerAdvertisementSeconds();
      } else if (attr == "hop-limit") {
        iface->ndp()->curHopLimit() = *kDefaultNdp.curHopLimit();
      } else if (attr == "ra-lifetime") {
        iface->ndp()->routerLifetime() = *kDefaultNdp.routerLifetime();
      } else if (attr == "prefix-valid-lifetime") {
        iface->ndp()->prefixValidLifetimeSeconds() =
            *kDefaultNdp.prefixValidLifetimeSeconds();
      } else if (attr == "prefix-preferred-lifetime") {
        iface->ndp()->prefixPreferredLifetimeSeconds() =
            *kDefaultNdp.prefixPreferredLifetimeSeconds();
      } else if (attr == "managed-config-flag") {
        iface->ndp()->routerAdvertisementManagedBit() =
            *kDefaultNdp.routerAdvertisementManagedBit();
      } else if (attr == "other-config-flag") {
        iface->ndp()->routerAdvertisementOtherBit() =
            *kDefaultNdp.routerAdvertisementOtherBit();
      } else if (attr == "ra-address") {
        iface->ndp()->routerAddress().reset();
      }
    }
    modified = true;
  }

  if (!modified) {
    return fmt::format(
        "No NDP configuration present on interface(s) {}; nothing to reset",
        folly::join(", ", interfaces.getNames()));
  }

  ConfigSession::getInstance().saveConfig();

  return fmt::format(
      "Successfully reset interface(s) {}: {}",
      folly::join(", ", interfaces.getNames()),
      folly::join(", ", ndpAttrs.getAttributes()));
}

void CmdDeleteInterfaceIpv6Ndp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteInterfaceIpv6Ndp, CmdDeleteInterfaceIpv6NdpTraits>::run();

std::vector<std::string> CmdDeleteInterfaceIpv6NdpTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completeAttrList(
      typed, utils::completion::toWords(ndpAttrNames()));
}

} // namespace facebook::fboss
