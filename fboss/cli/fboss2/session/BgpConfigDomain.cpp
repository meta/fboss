// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/BgpConfigDomain.h"

#include <fmt/format.h>
#include <glog/logging.h>
#include <exception>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
// Needed by the explicit instantiation below, which include-cleaner does not
// count as a use.
#include "fboss/cli/fboss2/session/TypedConfigDomain-defs.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

template class TypedConfigDomain<bgp::thrift::BgpConfig>;

BgpConfigDomain::BgpConfigDomain(
    const std::string& sessionConfigDir,
    const std::string& systemConfigDir)
    : TypedConfigDomain(
          ConfigDomainInfo{
              cli::ServiceType::BGP,
              "BGP",
              fmt::format("{}/bgp_config.json", sessionConfigDir),
              "bgpcpp/bgpcpp.conf",
              fmt::format("{}/bgpcpp/bgpcpp.conf", systemConfigDir),
              // bgpd has no reload: a rollback restarts it.
              cli::ConfigActionLevel::SERVICE_RESTART,
          }) {}

std::string BgpConfigDomain::queryCurrentPath(const HostInfo& hostInfo) const {
  std::string configPath;
  auto client = utils::createBgpClient(hostInfo);
  client->sync_getOption(configPath, "config");
  return configPath;
}

void BgpConfigDomain::load(const std::string& json) {
  if (!json.empty()) {
    try {
      setConfig(parse(json));
      return;
    } catch (const std::exception& ex) {
      LOG(WARNING) << "Failed to parse BGP config, starting from defaults: "
                   << ex.what();
    }
  }
  setConfig(bgp::thrift::BgpConfig());
}

} // namespace facebook::fboss
