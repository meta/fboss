// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <variant>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

struct CmdConfigInitTraits : public WriteCommandTraits {
  using ObjectArgType = std::monostate; // no arg
  using RetType = std::string;
};

// Commits every service config on the box that the config history does not
// match yet (see ConfigSession::initializeHistory()). Run at boot by
// fboss_init.sh, and safe to rerun, e.g. after a new service is installed.
class CmdConfigInit : public CmdHandler<CmdConfigInit, CmdConfigInitTraits> {
 public:
  using ObjectArgType = CmdConfigInitTraits::ObjectArgType;
  using RetType = CmdConfigInitTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
