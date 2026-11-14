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
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

/*
 * ArpDeleteAttrs captures the list of ARP attribute names to reset to their
 * switch_config.thrift defaults for the `delete arp` command.
 *
 * Supported attributes (all valueless — only names are provided):
 *   timeout        - Reset ARP/NDP neighbor entry timeout to 60
 *   age-interval   - Reset ARP/NDP ager interval to 5
 *   max-probes     - Reset max neighbor probes to 300
 *   stale-interval - Reset stale entry interval to 10
 */
class ArpDeleteAttrs : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ ArpDeleteAttrs(const std::vector<std::string>& v);

  const std::vector<std::string>& getAttributes() const {
    return attributes_;
  }

 private:
  std::vector<std::string> attributes_;
};

struct CmdDeleteArpTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "arp_delete_attrs",
        args,
        "<attr> [<attr> ...] where <attr> is one of: "
        "age-interval, max-probes, stale-interval, timeout");
  }
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed) {
    return utils::completion::completeAttrList(
        typed, utils::completion::toWords(arp_attrs::kValidAttrs));
  }
  using ObjectArgType = ArpDeleteAttrs;
  using RetType = std::string;
};

class CmdDeleteArp : public CmdHandler<CmdDeleteArp, CmdDeleteArpTraits> {
 public:
  using ObjectArgType = CmdDeleteArpTraits::ObjectArgType;
  using RetType = CmdDeleteArpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
