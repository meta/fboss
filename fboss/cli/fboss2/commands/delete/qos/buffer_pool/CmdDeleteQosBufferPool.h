// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <folly/String.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/qos/CmdDeleteQos.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

class BufferPoolName : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BufferPoolName(std::vector<std::string> v)
      : BaseObjectArgType(std::move(v)) {
    if (data_.size() != 1) {
      throw std::invalid_argument(
          data_.empty() ? "Buffer pool name is required"
                        : "Expected single buffer pool name, got: " +
                  folly::join(", ", data_));
    }
  }

  const std::string& getName() const {
    return data_[0];
  }
};

// `delete qos buffer-pool <name>` removes an entry from sw.bufferPoolConfigs.
// It refuses while any port queue or priority group still names the pool, so
// the config never carries a dangling bufferPoolName.
struct CmdDeleteQosBufferPoolTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteQos;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("buffer_pool_name", args, "Buffer pool to remove")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = BufferPoolName;
  using RetType = std::string;
};

class CmdDeleteQosBufferPool
    : public CmdHandler<CmdDeleteQosBufferPool, CmdDeleteQosBufferPoolTraits> {
 public:
  using ObjectArgType = CmdDeleteQosBufferPoolTraits::ObjectArgType;
  using RetType = CmdDeleteQosBufferPoolTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& name);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
