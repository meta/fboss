/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"

namespace facebook::fboss {

class CmdShowVersionDistro
    : public CmdHandler<CmdShowVersionDistro, CmdShowVersionTraits> {
 public:
  using RetType = CmdShowVersionTraits::RetType;

  explicit CmdShowVersionDistro(bool detailed = false) : detailed_(detailed) {}

  RetType queryClient(const HostInfo& hostInfo);
  static RetType readVersionFile(const std::string& path);
  void printOutput(const RetType& versions);

 private:
  bool detailed_;
};

class CmdShowVersionDistroDetail : public CmdShowVersionDistro {
 public:
  CmdShowVersionDistroDetail() : CmdShowVersionDistro(true) {}
};

} // namespace facebook::fboss
