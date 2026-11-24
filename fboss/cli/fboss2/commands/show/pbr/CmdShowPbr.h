// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/pbr/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss {

struct CmdShowPbrTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = utils::NoneArgType;
  using RetType = cli::ShowPbrModel;

  static std::string_view description();
};

class CmdShowPbr : public CmdHandler<CmdShowPbr, CmdShowPbrTraits> {
 public:
  using RetType = CmdShowPbrTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  RetType createModel(
      const std::vector<Policy>& policies,
      const AclTableThrift& aclTables);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  static RetType sampleModel();
};

} // namespace facebook::fboss
