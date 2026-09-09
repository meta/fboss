/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss::srv6_tunnel_delete_utils {

void parseTunnelDeleteArgs(
    const std::vector<std::string>& values,
    std::string& tunnelId,
    std::vector<std::string>& attrs);

std::string applyTunnelDelete(
    cfg::SwitchConfig& swConfig,
    TunnelType expectedType,
    const std::string& tunnelId,
    const std::vector<std::string>& attrs,
    bool& changed);

std::string deleteTunnel(
    TunnelType expectedType,
    const std::string& tunnelId,
    const std::vector<std::string>& attrs);

class TunnelSrv6DeleteArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ TunnelSrv6DeleteArgs(std::vector<std::string> values) {
    parseTunnelDeleteArgs(values, tunnelId_, attrs_);
    data_ = std::move(values);
  }

  const std::string& getTunnelId() const {
    return tunnelId_;
  }

  const std::vector<std::string>& getAttrs() const {
    return attrs_;
  }

 private:
  std::string tunnelId_;
  std::vector<std::string> attrs_;
};

} // namespace facebook::fboss::srv6_tunnel_delete_utils
