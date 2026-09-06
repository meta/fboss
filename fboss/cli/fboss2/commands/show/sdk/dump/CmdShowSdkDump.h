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

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

struct CmdShowQsfpSdkDumpTraits : public ReadCommandTraits,
                                  public CliDocsExempt {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using RetType = bool;
};

class CmdShowQsfpSdkDump
    : public CmdHandler<CmdShowQsfpSdkDump, CmdShowQsfpSdkDumpTraits> {
 public:
  using RetType = CmdShowQsfpSdkDumpTraits::RetType;
  // Path under the service-owned dump directory (kSdkDumpDir) that
  // qsfp_service writes the SDK state to and that printOutput reads back.
  std::string sdkDumpPath;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& rc, std::ostream& out = std::cout) const;
};

struct CmdShowAgentSdkDumpTraits : public ReadCommandTraits,
                                   public CliDocsExempt {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using RetType = std::string;
};

class CmdShowAgentSdkDump
    : public CmdHandler<CmdShowAgentSdkDump, CmdShowAgentSdkDumpTraits> {
 public:
  using RetType = CmdShowAgentSdkDumpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& rc, std::ostream& out = std::cout) const;
};

} // namespace facebook::fboss
