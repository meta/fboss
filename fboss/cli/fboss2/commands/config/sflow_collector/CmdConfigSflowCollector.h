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

#include <folly/IPAddress.h>
#include <cstdint>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

// Parses the <ip> <port> tuple shared by config/delete sflow-collector.
// cfg::SflowCollector.port is an i16, so the largest positive port that can be
// represented by the current schema is 32767.
class SflowCollectorArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ SflowCollectorArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const folly::IPAddress& getIpAddress() const {
    return ipAddress_;
  }

  const std::string& getCanonicalIp() const {
    return canonicalIp_;
  }

  int16_t getPort() const {
    return port_;
  }

 private:
  folly::IPAddress ipAddress_;
  std::string canonicalIp_;
  int16_t port_{0};
};

struct CmdConfigSflowCollectorTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "ip_and_port",
           args,
           "Collector IP address followed by UDP port (1-32767)")
        ->expected(2);
  }
  using ObjectArgType = SflowCollectorArg;
  using RetType = std::string;
};

class CmdConfigSflowCollector : public CmdHandler<
                                    CmdConfigSflowCollector,
                                    CmdConfigSflowCollectorTraits> {
 public:
  using ObjectArgType = CmdConfigSflowCollectorTraits::ObjectArgType;
  using RetType = CmdConfigSflowCollectorTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& collector);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
