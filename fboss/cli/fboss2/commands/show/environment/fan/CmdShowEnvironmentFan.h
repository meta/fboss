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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_types.h"

namespace facebook::fboss {

/*
 Define the traits of this command. This will include the inputs and output
 types
*/
struct CmdShowEnvironmentFanTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::map<std::string, platform::fan_service::FanStatus>;
};

class CmdShowEnvironmentFan
    : public CmdHandler<CmdShowEnvironmentFan, CmdShowEnvironmentFanTraits> {
 public:
  using ObjectArgType = CmdShowEnvironmentFanTraits::ObjectArgType;
  using RetType = CmdShowEnvironmentFanTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(const RetType& fanStatuses, std::ostream& out = std::cout);
};

} // namespace facebook::fboss
