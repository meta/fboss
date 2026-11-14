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

#include <folly/FileUtil.h>
#include <folly/json/json.h>
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/qsfp_service/SdkDumpPath.h"

namespace facebook::fboss {

CmdShowQsfpSdkDump::RetType CmdShowQsfpSdkDump::queryClient(
    const HostInfo& hostInfo) {
  auto client = utils::createQsfpClient(hostInfo);

  // qsfp_service confines the SDK dump to its own directory (kSdkDumpDir) and
  // rejects absolute/traversing paths, using only the basename of the request.
  // Send a plain basename and read the result back from the confined location.
  std::string fileName = "fboss2_sdk_dump";
  sdkDumpPath = std::string(kSdkDumpDir) + fileName;

  bool rc = client->sync_getSdkState(fileName);
  return rc;
}

void CmdShowQsfpSdkDump::printOutput(const RetType& rc, std::ostream& out)
    const {
  std::string outputLine;

  if (!rc) {
    out << "Getting SDK state failed" << std::endl;
    return;
  }
  out << "Printing SDK state:" << std::endl;

  // Read the SDK dump from the service-owned dump directory.
  if (!folly::readFile(sdkDumpPath.c_str(), outputLine)) {
    out << "Reading dump file " << sdkDumpPath << " failed" << std::endl;
    return;
  }
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

void CmdShowAgentSdkDump::printOutput(const RetType& rc, std::ostream& out)
    const {
  out << "Printing Agent SDK state:" << std::endl;
  out << rc;

  // getHwDebugDump can succeed but return no SDK state (e.g. Broadcom SAI may
  // produce an empty dump), which would otherwise be silently printed as
  // nothing. Surface the empty case so the user knows no data was captured.
  if (rc.empty()) {
    out << std::endl << "Warning: no SDK state was captured." << std::endl;
    return;
  }

  // In multi-switch mode the payload is a JSON object keyed by switch id whose
  // values are the per-switch dumps; warn for any switch with an empty dump.
  try {
    auto parsed = folly::parseJson(rc);
    if (parsed.isObject()) {
      bool allEmpty = true;
      for (const auto& [switchId, dump] : parsed.items()) {
        if (dump.isString() && dump.asString().empty()) {
          out << std::endl
              << "Warning: no SDK state was captured for switch "
              << switchId.asString() << "." << std::endl;
        } else {
          allEmpty = false;
        }
      }
      if (allEmpty && !parsed.empty()) {
        out << "Warning: no SDK state was captured for any switch."
            << std::endl;
      }
    }
  } catch (const std::exception&) {
    // Monolithic mode returns a raw (non-JSON) dump; the emptiness check above
    // already covers it.
  }
}

// Explicit template instantiation
template void CmdHandler<CmdShowQsfpSdkDump, CmdShowQsfpSdkDumpTraits>::run();
template void CmdHandler<CmdShowAgentSdkDump, CmdShowAgentSdkDumpTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowQsfpSdkDump, CmdShowQsfpSdkDumpTraits>::getValidFilters();
template const ValidFilterMapType
CmdHandler<CmdShowAgentSdkDump, CmdShowAgentSdkDumpTraits>::getValidFilters();

} // namespace facebook::fboss
