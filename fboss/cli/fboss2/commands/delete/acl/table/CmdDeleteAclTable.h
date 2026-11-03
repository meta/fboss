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
#include "fboss/cli/fboss2/commands/delete/acl/CmdDeleteAcl.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

// Argument for `delete acl table <table-name>`.
//
// The group is not named: table names are unique across groups (config acl
// table refuses a duplicate), so the table resolves on its own.
class AclTableDeleteArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AclTableDeleteArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getTableName() const {
    return tableName_;
  }

 private:
  std::string tableName_;
};

struct CmdDeleteAclTableTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteAcl;
  using ObjectArgType = AclTableDeleteArgs;
  using RetType = std::string;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "acl_table_delete",
           args,
           "<table-name>: removes the AclTable, its entries, and any "
           "traffic-policy actions attached to those entries")
        ->required()
        ->expected(1);
  }
};

class CmdDeleteAclTable
    : public CmdHandler<CmdDeleteAclTable, CmdDeleteAclTableTraits> {
 public:
  using ObjectArgType = CmdDeleteAclTableTraits::ObjectArgType;
  using RetType = CmdDeleteAclTableTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);
  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
