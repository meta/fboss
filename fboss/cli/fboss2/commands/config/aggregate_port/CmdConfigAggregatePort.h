// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

class AggregatePortConfigArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AggregatePortConfigArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getName() const {
    return name_;
  }
  const std::map<std::string, std::string>& getAttributes() const {
    return attributes_;
  }

 private:
  std::string name_;
  std::map<std::string, std::string> attributes_;
};

class AggregatePortMemberConfigArg
    : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */
  AggregatePortMemberConfigArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::vector<std::string>& getPorts() const {
    return ports_;
  }
  const std::map<std::string, std::string>& getAttributes() const {
    return attributes_;
  }

 private:
  std::vector<std::string> ports_;
  std::map<std::string, std::string> attributes_;
};

struct CmdConfigAggregatePortTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "aggregate_port_config",
        args,
        "<name> [<attribute> <value> ...]; attributes: key, description, "
        "type, minimum-capacity-link-count, "
        "minimum-capacity-to-up-link-count");
  }
  using ObjectArgType = AggregatePortConfigArg;
  using RetType = std::string;
};

class CmdConfigAggregatePort
    : public CmdHandler<CmdConfigAggregatePort, CmdConfigAggregatePortTraits> {
 public:
  using ObjectArgType = CmdConfigAggregatePortTraits::ObjectArgType;
  using RetType = CmdConfigAggregatePortTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& aggregatePort);
  void printOutput(const RetType& output);
};

struct CmdConfigAggregatePortMemberTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigAggregatePort;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "aggregate_port_member_config",
        args,
        "<port> [<port> ...] [<attribute> <value> ...]; attributes: "
        "activity, rate, priority, hold-timer-multiplier");
  }
  using ObjectArgType = AggregatePortMemberConfigArg;
  using RetType = std::string;
};

class CmdConfigAggregatePortMember : public CmdHandler<
                                         CmdConfigAggregatePortMember,
                                         CmdConfigAggregatePortMemberTraits> {
 public:
  using ObjectArgType = CmdConfigAggregatePortMemberTraits::ObjectArgType;
  using RetType = CmdConfigAggregatePortMemberTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const AggregatePortConfigArg& aggregatePort,
      const ObjectArgType& members);
  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
