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

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

/**
 * Argument type for VLAN attribute configuration.
 *
 * Each concrete subclass encodes one attribute name (e.g. "name") so the
 * handler can dispatch on it.  Adding a new attribute requires only:
 *   1. A new subclass below with the appropriate getAttrName() value.
 *   2. A new else-if branch in CmdConfigVlanConfig::queryClient().
 *   3. A new subcommand entry in CmdListConfig.cpp pointing to the same
 *      handler with the new arg class as ObjectArgType.
 */
class VlanConfigArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ VlanConfigArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v) {
    if (v.size() != 1) {
      throw std::invalid_argument(
          "Expected <value>, got " + std::to_string(v.size()) + " argument(s)");
    }
    value_ = std::move(v[0]);
  }

  virtual std::string getAttrName() const = 0;

  const std::string& getValue() const {
    return value_;
  }

 private:
  std::string value_;
};

/** Argument type for 'config vlan <id> name <value>'. */
class VlanNameArg : public VlanConfigArg {
 public:
  /* implicit */ VlanNameArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v)
      : VlanConfigArg(std::move(v)) {}

  std::string getAttrName() const override {
    return "name";
  }
};

struct CmdConfigVlanConfigTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigVlan;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "vlan_config_value", args, "New value for the VLAN attribute");
  }
  using ObjectArgType = VlanNameArg;
  using RetType = std::string;
};

class CmdConfigVlanConfig
    : public CmdHandler<CmdConfigVlanConfig, CmdConfigVlanConfigTraits> {
 public:
  using ObjectArgType = CmdConfigVlanConfigTraits::ObjectArgType;
  using RetType = CmdConfigVlanConfigTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const VlanId& vlanId,
      const ObjectArgType& arg);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
