// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

class AggregatePortNameArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AggregatePortNameArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::string& getName() const {
    return name_;
  }

 private:
  std::string name_;
};

class AggregatePortMemberDeleteArg
    : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */
  AggregatePortMemberDeleteArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::vector<std::string>& getPorts() const {
    return ports_;
  }

 private:
  std::vector<std::string> ports_;
};

struct CmdDeleteAggregatePortTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "aggregate_port_name", args, "<name> - aggregate port to delete")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = AggregatePortNameArg;
  using RetType = std::string;
};

class CmdDeleteAggregatePort
    : public CmdHandler<CmdDeleteAggregatePort, CmdDeleteAggregatePortTraits> {
 public:
  using ObjectArgType = CmdDeleteAggregatePortTraits::ObjectArgType;
  using RetType = CmdDeleteAggregatePortTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& aggregatePort);
  void printOutput(const RetType& output);
};

struct CmdDeleteAggregatePortMemberTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteAggregatePort;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "aggregate_port_members", args, "<port> [<port> ...] to remove")
        ->required()
        ->expected(1, -1);
  }
  using ObjectArgType = AggregatePortMemberDeleteArg;
  using RetType = std::string;
};

class CmdDeleteAggregatePortMember : public CmdHandler<
                                         CmdDeleteAggregatePortMember,
                                         CmdDeleteAggregatePortMemberTraits> {
 public:
  using ObjectArgType = CmdDeleteAggregatePortMemberTraits::ObjectArgType;
  using RetType = CmdDeleteAggregatePortMemberTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const AggregatePortNameArg& aggregatePort,
      const ObjectArgType& members);
  void printOutput(const RetType& output);
};

struct CmdDeleteAggregatePortMinimumCapacityToUpTraits
    : public WriteCommandTraits {
  using ParentCmd = CmdDeleteAggregatePort;
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdDeleteAggregatePortMinimumCapacityToUp
    : public CmdHandler<
          CmdDeleteAggregatePortMinimumCapacityToUp,
          CmdDeleteAggregatePortMinimumCapacityToUpTraits> {
 public:
  using RetType = CmdDeleteAggregatePortMinimumCapacityToUpTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const AggregatePortNameArg& aggregatePort);
  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
