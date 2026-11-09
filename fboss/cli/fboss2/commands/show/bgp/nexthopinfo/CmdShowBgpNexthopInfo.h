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

#include <folly/IPAddress.h>
#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {

using facebook::neteng::fboss::bgp::thrift::TNexthopInfo;
using facebook::neteng::fboss::bgp::thrift::TNexthopInfoQueryResult;

struct CmdShowBgpNexthopInfoTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  // The view mode (list vs. per-nexthop detail) and the queried addresses are
  // carried in the result itself, so printOutput is a pure function of its
  // input rather than depending on out-of-band state on the command object.
  using RetType = TNexthopInfoQueryResult;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowBgpNexthopInfo
    : public CmdHandler<CmdShowBgpNexthopInfo, CmdShowBgpNexthopInfoTraits> {
 public:
  using RetType = CmdShowBgpNexthopInfoTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedIps);

  void printOutput(const RetType& data, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
