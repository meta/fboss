/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/sdk/dump/CmdShowSdkDump.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <boost/algorithm/string.hpp>
#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <fstream>
#include <utility>
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/qsfp_service/SdkDumpPath.h"

namespace facebook::fboss {

CmdShowQsfpSdkDump::CmdShowQsfpSdkDump() : CmdShowQsfpSdkDump(kSdkDumpDir) {}

CmdShowQsfpSdkDump::CmdShowQsfpSdkDump(folly::fs::path sdkDumpDir)
    : sdkDumpDir_(std::move(sdkDumpDir)) {}

std::string CmdShowQsfpSdkDump::prepareSdkDumpFile() {
  if (!tempSdkFile) {
    // The service confines SDK dumps to kSdkDumpDir. Create the temporary file
    // there so the CLI reads the same file that the service writes, but send
    // only the basename over Thrift to preserve the server's path boundary.
    tempSdkFile = std::make_unique<folly::test::TemporaryFile>(
        folly::StringPiece() /* namePrefix */,
        sdkDumpDir_,
        folly::test::TemporaryFile::Scope::UNLINK_ON_DESTRUCTION /* scope */);
  }
  return tempSdkFile->path().filename().string();
}

CmdShowQsfpSdkDump::RetType CmdShowQsfpSdkDump::queryClient(
    const HostInfo& hostInfo) {
  auto client =
      utils::createClient<facebook::fboss::QsfpServiceAsyncClient>(hostInfo);

  bool rc = client->sync_getSdkState(prepareSdkDumpFile());
  return rc;
}

void CmdShowQsfpSdkDump::printOutput(const RetType& rc, std::ostream& out) {
  std::ifstream sdkDumpStream;
  std::string outputLine;

  if (!rc) {
    out << "Getting SDK state failed" << std::endl;
    return;
  }
  out << "Printing SDK state:" << std::endl;

  // Read the temp file with SDK dump
  if (!folly::readFile(tempSdkFile->path().string().c_str(), outputLine)) {
    out << "Reading temp file " << tempSdkFile->path().string() << " failed"
        << std::endl;
    return;
  }
  // Print temp file
  out << outputLine << std::endl;
}

CmdShowAgentSdkDump::RetType CmdShowAgentSdkDump::queryClient(
    const HostInfo& hostInfo) {
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  std::string debugDump{};
  client->sync_getHwDebugDump(debugDump);
  return debugDump;
}

void CmdShowAgentSdkDump::printOutput(const RetType& rc, std::ostream& out) {
  std::ifstream sdkDumpStream;
  std::string outputLine;

  out << "Printing Agent SDK state:" << std::endl;
  out << rc;
}

// Explicit template instantiation
template void CmdHandler<CmdShowQsfpSdkDump, CmdShowQsfpSdkDumpTraits>::run();
template void CmdHandler<CmdShowAgentSdkDump, CmdShowAgentSdkDumpTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowQsfpSdkDump, CmdShowQsfpSdkDumpTraits>::getValidFilters();
template const ValidFilterMapType
CmdHandler<CmdShowAgentSdkDump, CmdShowAgentSdkDumpTraits>::getValidFilters();

} // namespace facebook::fboss
