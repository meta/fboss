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

#include <cstdint>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/acl/CmdConfigAcl.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Argument for `config acl table <table-name> group <group-name> priority
// <value>`.
//
// <table-name>  the table being configured; created in <group-name> if absent.
// <group-name>  must already exist -- creating a group is `config acl
//               table-group`'s job.
// <value>       is a non-negative int16. The agent stores it but today's
//               parallel ACL groups ignore it for hardware ordering, and
//               changing it on an existing table recreates the table.
class AclTableConfigArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AclTableConfigArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getGroupName() const {
    return groupName_;
  }

  const std::string& getTableName() const {
    return tableName_;
  }

  int16_t getPriority() const {
    return priority_;
  }

 private:
  std::string groupName_;
  std::string tableName_;
  int16_t priority_ = 0;
};

struct CmdConfigAclTableTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigAcl;
  using ObjectArgType = AclTableConfigArgs;
  using RetType = std::string;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // required() + expected(5) prevents CLI11 from treating the first
    // positional (the table name) as a subcommand name.
    cmd.add_option(
           "acl_table_config",
           args,
           "<table-name> group <group-name> priority <value>")
        ->required()
        ->expected(5);
  }
};

class CmdConfigAclTable
    : public CmdHandler<CmdConfigAclTable, CmdConfigAclTableTraits> {
 public:
  using ObjectArgType = CmdConfigAclTableTraits::ObjectArgType;
  using RetType = CmdConfigAclTableTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
