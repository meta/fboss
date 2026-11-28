// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <string_view>
#include "fboss/agent/gen-cpp2/agent_config_types_fwd.h"
#include "fboss/cli/fboss2/session/TypedConfigDomain.h"

namespace facebook::fboss {

class AgentConfigDomain : public TypedConfigDomain<cfg::AgentConfig> {
 public:
  AgentConfigDomain(
      const std::string& sessionConfigDir,
      const std::string& systemConfigDir);

  std::string queryCurrentPath(const HostInfo& hostInfo) const override;
  bool requiresConfig() const override {
    return true;
  }

 protected:
  // Accepts an AgentConfig, or a bare SwitchConfig as its "sw".
  cfg::AgentConfig parse(std::string_view json) const override;
};

} // namespace facebook::fboss
