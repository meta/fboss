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
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types_fwd.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/interface/dhcp/CmdConfigInterfaceDhcp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

/*
 * Canonical DHCP relay attribute names, shared by
 * `config interface <intf> dhcp relay` and
 * `delete interface <intf> dhcp relay` so the two commands can't drift.
 */
namespace dhcp_relay_attrs {
constexpr std::string_view kIpAddress = "ip-address";
constexpr std::string_view kIpv6Address = "ipv6-address";
} // namespace dhcp_relay_attrs

const std::unordered_set<std::string>& dhcpRelayAttrNames();

/* Case-insensitive membership test against dhcpRelayAttrNames(). */
bool isKnownDhcpRelayAttr(const std::string& s);

/*
 * The agent reads the DHCP relay destination from the Interface config or
 * from the Vlan config depending on whether interface neighbor tables are
 * enabled. Return the VLAN-type interface's VLAN (nullptr when there is
 * none) so callers can mirror the value and the relay works in either mode.
 */
cfg::Vlan* findVlanForInterface(
    cfg::SwitchConfig& swConfig,
    const cfg::Interface& iface);

/*
 * DhcpRelayConfigAttrs captures the attribute-value pairs for the
 * `config interface <intf> dhcp relay` command.
 *
 * Supported attributes (valueful):
 *   ip-address <A.B.C.D>    - IPv4 DHCP relay (helper) destination
 *   ipv6-address <A:B::C>   - IPv6 DHCP relay (helper) destination
 */
class DhcpRelayConfigAttrs : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ DhcpRelayConfigAttrs(const std::vector<std::string>& v);

  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> attributes_;
};

struct CmdConfigInterfaceDhcpRelayTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigInterfaceDhcp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "dhcp_relay_config_attrs", args, "<attr> <address> [<attr> <address>]");
  }
  using ObjectArgType = DhcpRelayConfigAttrs;
  using RetType = std::string;
};

class CmdConfigInterfaceDhcpRelay : public CmdHandler<
                                        CmdConfigInterfaceDhcpRelay,
                                        CmdConfigInterfaceDhcpRelayTraits> {
 public:
  using ObjectArgType = CmdConfigInterfaceDhcpRelayTraits::ObjectArgType;
  using RetType = CmdConfigInterfaceDhcpRelayTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& relayAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
