// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/switch/CmdConfigSwitch.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Attribute-name vocabulary for `config switch resource-limit`. These map onto
// gflags in AgentConfig.defaultCommandLineArgs that drive the SW-agent
// ResourceAccountant:
//   l2-max        -> max_l2_entries              (non-negative int)
//   arp-max       -> max_arp_entries             (non-negative int)
//   ndp-max       -> max_ndp_entries             (non-negative int)
//   hw-protection -> enable_hw_update_protection (bool: true/false)
// The accountant only enforces the three *-max limits while hw-protection is
// on, so they are refused unless the session config has it enabled. With
// enforce_resource_hw_limits (the default) the agent also caps arp-max and
// ndp-max at the ASIC's neighbor table size.
namespace resource_limit_attrs {
constexpr std::string_view kL2Max = "l2-max";
constexpr std::string_view kArpMax = "arp-max";
constexpr std::string_view kNdpMax = "ndp-max";
constexpr std::string_view kHwProtection = "hw-protection";

// Alphabetical order so folly::join produces a stable, sorted error message.
constexpr auto kValidAttrs = std::to_array<std::string_view>({
    kArpMax,
    kHwProtection,
    kL2Max,
    kNdpMax,
});
} // namespace resource_limit_attrs

// Argument for `config switch resource-limit <attr> <value>`.
// Validates that v[0] is a known attribute and that v[1] is the right type for
// that attribute: a non-negative int32 for the three *-max limits, or a
// boolean (true/false) for hw-protection. The normalized gflag string value is
// exposed via getGflagValue(), and the target gflag name via getGflagName().
class ResourceLimitConfigArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ ResourceLimitConfigArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getAttribute() const {
    return attribute_;
  }

  const std::string& getGflagName() const {
    return gflagName_;
  }

  const std::string& getGflagValue() const {
    return gflagValue_;
  }

  // True for the *-max attributes, which need hw-protection to be enforced.
  bool isLimit() const {
    return isLimit_;
  }

 private:
  std::string attribute_;
  std::string gflagName_;
  std::string gflagValue_;
  bool isLimit_{false};
};

struct CmdConfigResourceLimitTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigSwitch;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "resource_limit_attr_value",
           args,
           "<attr> <value> where <attr> is one of: "
           "arp-max, hw-protection, l2-max, ndp-max. "
           "The *-max attrs take a non-negative integer and require "
           "hw-protection to be true; hw-protection takes true or false. "
           "The agent caps arp-max and ndp-max at the ASIC table size.")
        ->required()
        ->expected(2);
  }
  using ObjectArgType = ResourceLimitConfigArgs;
  using RetType = std::string;
};

class CmdConfigResourceLimit
    : public CmdHandler<CmdConfigResourceLimit, CmdConfigResourceLimitTraits> {
 public:
  using ObjectArgType = CmdConfigResourceLimitTraits::ObjectArgType;
  using RetType = CmdConfigResourceLimitTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
