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

#include <map>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

class MirrorConfigArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ MirrorConfigArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getName() const {
    return name_;
  }

  const std::map<std::string, std::string>& getAttrs() const {
    return attrs_;
  }

 private:
  std::string name_;
  std::map<std::string, std::string> attrs_;
};

struct CmdConfigMirrorTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "mirror_config",
        args,
        "<name> <attr> <value> ...; egress-port accepts a port name or "
        "logical ID; attributes: egress-port, "
        "destination-ip, source-ip, udp-src-port, udp-dst-port, dscp, "
        "truncate");
  }
  using ObjectArgType = MirrorConfigArg;
  using RetType = std::string;
};

class CmdConfigMirror
    : public CmdHandler<CmdConfigMirror, CmdConfigMirrorTraits> {
 public:
  using ObjectArgType = CmdConfigMirrorTraits::ObjectArgType;
  using RetType = CmdConfigMirrorTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& mirror);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
