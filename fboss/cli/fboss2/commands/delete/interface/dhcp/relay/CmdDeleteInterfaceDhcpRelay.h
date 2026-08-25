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
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/interface/dhcp/CmdDeleteInterfaceDhcp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

/*
 * DhcpRelayDeleteAttrs captures the attributes to clear for the
 * `delete interface <intf> dhcp relay` command.
 *
 * Supported attributes:
 *   ip-address [<A.B.C.D>]   - Clear the IPv4 DHCP relay destination
 *   ipv6-address [<A:B::C>]  - Clear the IPv6 DHCP relay destination
 *
 * The address is optional; when given, it must match the currently
 * configured relay destination.
 */
class DhcpRelayDeleteAttrs : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ DhcpRelayDeleteAttrs(const std::vector<std::string>& v);

  // (attr, expected-address-or-empty) pairs
  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> attributes_;
};

struct CmdDeleteInterfaceDhcpRelayTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteInterfaceDhcp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "dhcp_relay_delete_attrs", args, "<attr> [<address>] [<attr> ...]");
  }
  using ObjectArgType = DhcpRelayDeleteAttrs;
  using RetType = std::string;
};

class CmdDeleteInterfaceDhcpRelay : public CmdHandler<
                                        CmdDeleteInterfaceDhcpRelay,
                                        CmdDeleteInterfaceDhcpRelayTraits> {
 public:
  using ObjectArgType = CmdDeleteInterfaceDhcpRelayTraits::ObjectArgType;
  using RetType = CmdDeleteInterfaceDhcpRelayTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& relayAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
