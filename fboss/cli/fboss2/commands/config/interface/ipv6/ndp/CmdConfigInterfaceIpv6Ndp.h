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

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/interface/ipv6/CmdConfigInterfaceIpv6.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

/*
 * Canonical NDP attribute names, shared by `config interface <intf> ipv6 ndp`
 * and `delete interface <intf> ipv6 ndp` so the two commands can't drift.
 */
const std::unordered_set<std::string>& ndpAttrNames();

/*
 * NdpConfigAttrs captures the attribute-value pairs for the
 * `config interface <intf> ipv6 ndp` command.
 *
 * Supported attributes (valueful):
 *   ra-interval <seconds>          - Router Advertisement interval (>= 0; 0 =
 * disabled) ra-lifetime <seconds>          - Router lifetime in RA messages (>=
 * 0) hop-limit <0-255>              - IPv6 hop limit advertised in RA
 *   prefix-valid-lifetime <sec>    - Prefix valid lifetime
 *   prefix-preferred-lifetime <sec>- Prefix preferred lifetime
 *   ra-address <ip>                - Source IP used in RA
 *
 * Supported attributes (valueless):
 *   managed-config-flag            - Set M-bit in RA (DHCPv6 addresses)
 *   other-config-flag              - Set O-bit in RA (DHCPv6 other info)
 */
class NdpConfigAttrs : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ NdpConfigAttrs(std::vector<std::string> v);

  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> attributes_;
};

struct CmdConfigInterfaceIpv6NdpTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigInterfaceIpv6;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "ndp_config_attrs", args, "<attr> <value> [<attr> <value> ...]");
  }
  using ObjectArgType = NdpConfigAttrs;
  using RetType = std::string;
};

class CmdConfigInterfaceIpv6Ndp : public CmdHandler<
                                      CmdConfigInterfaceIpv6Ndp,
                                      CmdConfigInterfaceIpv6NdpTraits> {
 public:
  using ObjectArgType = CmdConfigInterfaceIpv6NdpTraits::ObjectArgType;
  using RetType = CmdConfigInterfaceIpv6NdpTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& ndpAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
