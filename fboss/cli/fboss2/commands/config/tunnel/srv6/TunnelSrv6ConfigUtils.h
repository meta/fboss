/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss::srv6_tunnel_utils {

inline constexpr std::string_view kAttrSource = "source";
inline constexpr std::string_view kAttrTtlMode = "ttl-mode";
inline constexpr std::string_view kAttrDscpMode = "dscp-mode";
inline constexpr std::string_view kAttrEcnMode = "ecn-mode";
inline constexpr std::string_view kAttrTerminationType = "termination-type";
inline constexpr std::string_view kAttrUnderlayIntfId = "underlay-intf-id";

std::string toLower(const std::string& value);
std::string directionName(TunnelType type);
cfg::TunnelMode parseTunnelMode(const std::string& value);
cfg::TunnelTerminationType parseTerminationType(const std::string& value);

void parseTunnelConfigArgs(
    const std::vector<std::string>& values,
    const std::unordered_set<std::string>& allowedAttrs,
    std::string& tunnelId,
    std::map<std::string, std::string>& attrs);

std::vector<std::string> applyTunnelConfig(
    cfg::SwitchConfig& swConfig,
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs);

std::string configureTunnel(
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs);

class TunnelSrv6ConfigArgsBase : public utils::BaseObjectArgType<std::string> {
 public:
  TunnelSrv6ConfigArgsBase(
      std::vector<std::string> values,
      const std::unordered_set<std::string>& allowedAttrs) {
    parseTunnelConfigArgs(values, allowedAttrs, tunnelId_, attrs_);
    data_ = std::move(values);
  }

  const std::string& getTunnelId() const {
    return tunnelId_;
  }

  const std::map<std::string, std::string>& getAttrs() const {
    return attrs_;
  }

  bool hasAttrs() const {
    return !attrs_.empty();
  }

 private:
  std::string tunnelId_;
  std::map<std::string, std::string> attrs_;
};

} // namespace facebook::fboss::srv6_tunnel_utils
