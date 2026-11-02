/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdSubcommands.h"
#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/utils/CLIParserUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

#include <CLI/App.hpp>
#include <fmt/format.h>
#include <folly/Singleton.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct singleton_tag_type {};

const std::map<std::string, std::string>& kSupportedVerbs() {
  static const std::map<std::string, std::string> supportedVerbs = {
      {"show", "Show object info"},
      {"clear", "Clear object info"},
      {"create", "Create object"},
      {"delete", "Delete object"},
      {"debug", "Debug object"},
      {"set", "Set object"},
      {"bounce", "Disable/Enable object"},
      {"stream", "Continuously stream"},
      {"start", "Start event"},
      {"stop", "Stop event"},
      {"get", "Get object"},
      {"reload", "Reload object"},
      {"clear_and_override",
       "Clear and override object to prevent external writers from re-setting"},
      // Config generation is available in fboss2. Mutating config commands
      // remain limited to fboss2-dev.
      {"config", "Configuration commands"},
  };

  return supportedVerbs;
};

} // namespace

using facebook::fboss::CmdSubcommands;
static folly::Singleton<CmdSubcommands, singleton_tag_type> cmdSubcommands{};
std::shared_ptr<CmdSubcommands> CmdSubcommands::getInstance() {
  return cmdSubcommands.try_get();
}

namespace facebook::fboss {

CLI::App* CmdSubcommands::addCommand(
    CLI::App& app,
    const Command& cmd,
    std::string& fullCmd,
    int depth) {
  if (fullCmd.empty()) {
    fullCmd += cmd.name;
  } else {
    fullCmd += fmt::format("_{}", cmd.name);
  }
  auto* subCmd = app.add_subcommand(cmd.name, cmd.help);
  if (auto& commandHandler = cmd.commandHandler) {
    // Wrap the handler to only execute if this is the leaf command
    // (i.e., no subcommands were parsed). This is needed because CLI11
    // invokes callbacks for all commands in the chain, but we only want
    // the target leaf command to execute.
    subCmd->callback([commandHandler, subCmd]() {
      if (subCmd->get_subcommands().empty()) {
        (*commandHandler)();
      }
    });

    if (auto& localOptionsHandler = cmd.localOptionsHandler) {
      auto& localOptionMap =
          CmdLocalOptions::getInstance()->getLocalOptionMap(fullCmd);
      auto& getLocalOptions = *localOptionsHandler;
      for (const auto& localOption : getLocalOptions()) {
        if (localOption.isFlag) {
          subCmd->add_flag(
              localOption.name,
              localOptionMap[localOption.name],
              localOption.helpMsg);
          continue;
        }
        auto* cliOpt = subCmd->add_option(
            localOption.name,
            localOptionMap[localOption.name],
            localOption.helpMsg);
        if (localOption.defaultValue.has_value()) {
          cliOpt->default_val(*localOption.defaultValue);
        }
      }
    }
    if (auto& argRegistrar = cmd.argRegistrar) {
      // Config commands: direct dispatch via addCliArg
      auto& args = CmdArgsLists::getInstance()->refAt(depth);
      (*argRegistrar)(*subCmd, args);
    } else if (auto& argTypeHandler = cmd.argTypeHandler) {
      // Non-config commands: enum-based dispatch via switch
      auto& args = CmdArgsLists::getInstance()->refAt(depth);
      auto& argType = *argTypeHandler;
      switch (argType()) {
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_COMMUNITY_LIST: {
          subCmd->add_option("communities", args, "BGP community(s)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_DEBUG_LEVEL: {
          subCmd->add_option("level", args, "Debug level");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST: {
          subCmd->add_option("ipv6Addrs", args, "IPv6 addr(s)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST: {
          subCmd->add_option("ipAddrs", args, "IPv4 or IPv6 addr(s)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_CIDR_NETWORK: {
          subCmd->add_option(
              "cidrNetwork", args, "CIDR network in the form <ip>/<mask>");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PEERID_LIST: {
          subCmd->add_option("peerID", args, "BGP remote peer ID (int)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST: {
          subCmd->add_option("ports", args, "Port(s)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE: {
          subCmd->add_option("msg", args, "Message");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_AREA_LIST: {
          subCmd->add_option("areas", args, "Openr areas");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NODE_LIST: {
          subCmd->add_option("nodes", args, "Openr node names");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_COMPONENT: {
          subCmd->add_option(
              "component",
              args,
              "name(s) of PRBS component, one or multiple of "
              "asic/xphy_system/xphy_line/transceiver_system/transceiver_line");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_PHY_CHIP_TYPE: {
          subCmd->add_option(
              "phyChipType",
              args,
              "phy chip type(s), one or multiple of asic/xphy");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_AS_SEQUENCE: {
          subCmd->add_option("sequence", args, "AS sequence");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_LOCAL_PREFERENCE: {
          subCmd->add_option("preference", args, "Local preference num");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_STATE: {
          subCmd->add_option(
              "state",
              args,
              "PRBS state to set - some examples \n"
              "'off' -> Disables PRBS on both generator and checker\n"
              "'off generator checker' -> Disables PRBS on both generator and checker\n"
              "'off generator' -> Disables PRBS on the generator only\n"
              "'off checker' -> Disables PRBS on the checker only\n"
              "'prbs31 generator' -> Enables PRBS31 only on the generator\n"
              "'prbs31q generator checker' -> Enables PRBS31Q on both generator and checker\n");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_PORT_STATE: {
          subCmd->add_option(
              "state",
              args,
              "Port state to set - some examples \n"
              "'enable' -> Enables port\n"
              "'disable' -> Disables port\n");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_LINK_DIRECTION: {
          subCmd->add_option(
              "direction", args, "Link direction - system|line\n");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_PATH: {
          subCmd
              ->add_option(
                  "fsdb_path",
                  args,
                  "Path in fsdb model, delimited and prefixed by slashes (/). Slashes inside keys like port name \n"
                  "need to be escaped (eth2/1/1 -> eth2\\/1\\/1). Ex:\n"
                  "\tstats path - \"/agent/hwPortStats/eth2\\/1\\/1\" -- Note the quotes, otherwise the shell will drop the escape (can double escape without quotes)\n"
                  "\tstate path - /agent/switchState\n")
              ->required()
              ->expected(1)
              ->check(
                  CLI::Validator(
                      [](std::string& path) {
                        return path.empty() || path.front() != '/' ||
                                path.find_first_not_of('/') == std::string::npos
                            ? "FSDB path must start with / and include a publisher root, such as /agent"
                            : "";
                      },
                      "FSDB path"));
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_VIP_INJECTOR_ID: {
          subCmd->add_option("injector", args, "Injector");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_CLIENT_ID: {
          subCmd->add_option(
              "fsdb_client_id",
              args,
              "Client ID representing fsdb publishers/subscribers IDs e.g.\n"
              "'agent'\n");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_HW_OBJECT_LIST: {
          subCmd->add_option(
              "hw_object_type", args, "Hardware (HW) object type(s)");
          break;
        }
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_SYSTEM_PORT_LIST:
          subCmd->add_option(
              "sys_ports",
              args,
              "System port in the format switch:moduleNum/port/subport\n"
              "e.g. rdsw001.n001.z004.snc1:eth1/5/3 will be parsed to five"
              "parts: rdsw001.n001.z004.snc1 (switch name), eth(module name)"
              "1(module number), 5(port number), 3(subport number)\n");
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MIRROR_LIST:
          subCmd->add_option("mirrors", args, "Mirror(s)");
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_SWITCH_NAME_LIST:
          subCmd->add_option("switch_name_list", args, "Switch Name(s)");
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_FAN_PWM:
          subCmd->add_option("pwm", args, "Fan PWM (0..100) or 'disable'");
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_MTU:
          subCmd->add_option(
              "mtu",
              args,
              fmt::format("MTU value ({}-{})", utils::kMtuMin, utils::kMtuMax));
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_INTERFACE_LIST:
          subCmd->add_option("interfaces", args, "Interface(s)");
          break;
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_UNINITIALIZE:
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE:
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_REVISION_LIST:
        case utils::ObjectArgTypeId::OBJECT_ARG_TYPE_VLAN_ID:
          break;
      }
    }
  } else {
    subCmd->require_subcommand();
  }
  return subCmd;
}

void CmdSubcommands::addCommandBranch(
    CLI::App& app,
    const Command& cmd,
    std::string& fullCmd,
    int depth) {
  if (auto* existing = utils::getSubcommandIf(app, cmd.name)) {
    // Command already exists. Allow merging when either side has subcommands
    // to contribute. Reject only when both the existing and incoming commands
    // are leaf nodes (no children) — that means the exact same path from root
    // to leaf exists in multiple trees, which is a true conflict.
    if (cmd.subcommands.empty() &&
        existing->get_subcommands([](CLI::App*) { return true; }).empty()) {
      throw std::runtime_error(
          fmt::format(
              "Command `{}` already exists, command tree must be invalid",
              cmd.name));
    }
    if (fullCmd.empty()) {
      fullCmd += cmd.name;
    } else {
      fullCmd += fmt::format("_{}", cmd.name);
    }
    for (const auto& child : cmd.subcommands) {
      std::string newFullCmd = fullCmd;
      addCommandBranch(*existing, child, newFullCmd, depth);
    }
    return;
  }
  auto* subCmd = addCommand(app, cmd, fullCmd, depth);
  if (cmd.commandHandler) {
    // Do not increase depth for pass through commands that don't have arguments
    depth++;
  }
  for (const auto& child : cmd.subcommands) {
    std::string newFullCmd = fullCmd;
    addCommandBranch(*subCmd, child, newFullCmd, depth);
  }
}

void CmdSubcommands::initCommandTree(
    CLI::App& app,
    const CommandTree& cmdTree) {
  for (const auto& cmd : cmdTree) {
    auto verb = cmd.verb;
    auto* verbCmd = utils::getSubcommandIf(app, verb);
    // TODO explore moving this check to a compile time check
    if (!verbCmd) {
      throw std::runtime_error("Unsupported verb " + verb);
    }

    addCommandBranch(*verbCmd, cmd, verb);
  }
}

void CmdSubcommands::init(
    CLI::App& app,
    const CommandTree& cmdTree,
    const CommandTree& additionalCmdTree,
    const std::vector<Command>& specialCmds) {
  for (const auto& [verb, helpMsg] : kSupportedVerbs()) {
    app.add_subcommand(verb, helpMsg);
  }

  initCommandTree(app, cmdTree);
  initCommandTree(app, additionalCmdTree);
  for (const auto& cmd : specialCmds) {
    std::string fullCmd;
    addCommand(app, cmd, fullCmd, 0);
  }
}

} // namespace facebook::fboss
