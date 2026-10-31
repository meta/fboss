/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowVersionBgp.h"

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h" // NOLINT(misc-include-cleaner)
#include "thrift/lib/cpp/TApplicationException.h"

namespace facebook::fboss {

CmdShowVersionBgp::RetType CmdShowVersionBgp::queryClient(
    const HostInfo& hostInfo) {
  RetType retVal;

#ifndef IS_OSS
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  client->sync_getRegexExportedValues(retVal, "build_.*");
#else
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
  try {
    client->sync_getBuildInfo(retVal);
  } catch (const apache::thrift::TApplicationException& ex) {
    if (ex.getType() != apache::thrift::TApplicationException::UNKNOWN_METHOD) {
      throw;
    }
    throw std::runtime_error(
        "The running BGP daemon does not support getBuildInfo; upgrade the "
        "BGP package and restart the service before using this command");
  }
#endif

  return retVal;
}

void CmdShowVersionBgp::printOutput(RetType& bgpVer) {
  cmdShowVersion(bgpVer);
}

} // namespace facebook::fboss
