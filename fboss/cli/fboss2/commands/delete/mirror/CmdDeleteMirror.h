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

namespace facebook::fboss {

class MirrorNameArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ MirrorNameArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getName() const {
    return name_;
  }

 private:
  std::string name_;
};

struct CmdDeleteMirrorTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("mirror_name", args, "<name> - mirror name to delete")
        ->expected(1);
  }
  using ObjectArgType = MirrorNameArg;
  using RetType = std::string;
};

class CmdDeleteMirror
    : public CmdHandler<CmdDeleteMirror, CmdDeleteMirrorTraits> {
 public:
  using ObjectArgType = CmdDeleteMirrorTraits::ObjectArgType;
  using RetType = CmdDeleteMirrorTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& mirror);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
