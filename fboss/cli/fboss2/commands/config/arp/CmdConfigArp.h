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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Attribute-name vocabulary shared between `config arp` and `delete arp`
// so the two command trees can never diverge.
namespace arp_attrs {
constexpr std::string_view kTimeout = "timeout";
constexpr std::string_view kAgeInterval = "age-interval";
constexpr std::string_view kMaxProbes = "max-probes";
constexpr std::string_view kStaleInterval = "stale-interval";
/* arpRefreshSeconds is defined in switch_config.thrift yet not implemented
constexpr std::string_view kRefresh = "refresh";
*/

// Alphabetical order so folly::join produces a stable, sorted error message.
constexpr auto kValidAttrs = std::to_array<std::string_view>({
    kAgeInterval,
    kMaxProbes,
    kStaleInterval,
    kTimeout,
});
} // namespace arp_attrs

// Argument for `config arp <attr> <value>`.
// Validates that v[0] is one of the valid ARP attribute names and v[1] is a
// non-negative int32.
class ArpConfigArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ ArpConfigArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getAttribute() const {
    return attribute_;
  }

  int32_t getValue() const {
    return value_;
  }

 private:
  std::string attribute_;
  int32_t value_ = 0;
};

struct CmdConfigArpTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "arp_attr_value",
        args,
        "<attr> <value> where <attr> is one of: "
        "timeout, age-interval, max-probes, stale-interval");
  }
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed) {
    return utils::completion::completeAttrGrammar(
        typed, {.attrs = utils::completion::toWords(arp_attrs::kValidAttrs)});
  }
  using ObjectArgType = ArpConfigArgs;
  using RetType = std::string;
};

class CmdConfigArp : public CmdHandler<CmdConfigArp, CmdConfigArpTraits> {
 public:
  using ObjectArgType = CmdConfigArpTraits::ObjectArgType;
  using RetType = CmdConfigArpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
