/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/interface/capabilities/CmdShowInterfaceCapabilities.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

namespace facebook::fboss {

CmdShowInterfaceCapabilities::RetType CmdShowInterfaceCapabilities::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::map<std::string, std::vector<cfg::PortProfileID>> portProfiles;
  client->sync_getAllPortSupportedProfiles(portProfiles, true);

  return createModel(portProfiles, queriedIfs);
}

CmdShowInterfaceCapabilities::RetType CmdShowInterfaceCapabilities::createModel(
    const std::map<std::string, std::vector<cfg::PortProfileID>>& portProfiles,
    const std::vector<std::string>& queriedIfs) {
  RetType model;

  if (queriedIfs.empty()) {
    model.portProfiles() = portProfiles;
  } else {
    for (auto& interface : queriedIfs) {
      if (portProfiles.find(interface) != portProfiles.end()) {
        model.portProfiles()[interface] = portProfiles.at(interface);
      }
    }
  }

  return model;
}

void CmdShowInterfaceCapabilities::printOutput(
    const RetType& model,
    std::ostream& out) {
  Table table;
  table.setHeader({"Interface", "Profiles"});

  for (auto& [portName, profileList] : model.portProfiles().value()) {
    std::string profileListStr;
    for (auto& profile : profileList) {
      profileListStr +=
          apache::thrift::util::enumNameSafe(profile) + "\n               ";
    }
    table.addRow({portName, profileListStr});
  }
  out << table << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdShowInterfaceCapabilities,
    CmdShowInterfaceCapabilitiesTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCapabilities,
    CmdShowInterfaceCapabilitiesTraits>::getValidFilters();

} // namespace facebook::fboss
