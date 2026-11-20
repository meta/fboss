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

#include <folly/testing/TestUtil.h>
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

  CmdShowQsfpSdkDump();
  explicit CmdShowQsfpSdkDump(folly::fs::path sdkDumpDir);

  std::unique_ptr<folly::test::TemporaryFile> tempSdkFile;

  std::string prepareSdkDumpFile();
  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& rc, std::ostream& out = std::cout);

 private:
  folly::fs::path sdkDumpDir_;
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
  std::unique_ptr<folly::test::TemporaryFile> tempSdkFile;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& rc, std::ostream& out = std::cout);
};

} // namespace facebook::fboss
