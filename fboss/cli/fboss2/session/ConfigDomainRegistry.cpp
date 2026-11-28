// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/ConfigDomainRegistry.h"

#include <fmt/format.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <stdexcept>
#include <utility>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/BgpConfigDomain.h"

namespace facebook::fboss {

void ConfigDomainRegistry::add(std::unique_ptr<ConfigDomain> domain) {
  const auto service = domain->info().service;
  for (const auto& existing : domains_) {
    if (existing->info().service == service) {
      throw std::runtime_error(
          fmt::format(
              "A config domain is already registered for service {}",
              apache::thrift::util::enumNameSafe(service)));
    }
  }
  domains_.push_back(std::move(domain));
}

ConfigDomain* ConfigDomainRegistry::find(cli::ServiceType service) const {
  for (const auto& domain : domains_) {
    if (domain->info().service == service) {
      return domain.get();
    }
  }
  throw std::runtime_error(
      fmt::format(
          "No config domain for service {}",
          apache::thrift::util::enumNameSafe(service)));
}

ConfigDomain& ConfigDomainRegistry::get(cli::ServiceType service) {
  return *find(service);
}

const ConfigDomain& ConfigDomainRegistry::get(cli::ServiceType service) const {
  return *find(service);
}

ConfigDomainRegistry makeDefaultConfigDomains(
    const std::string& sessionConfigDir,
    const std::string& systemConfigDir) {
  ConfigDomainRegistry domains;
  domains.add(
      std::make_unique<AgentConfigDomain>(sessionConfigDir, systemConfigDir));
  domains.add(
      std::make_unique<BgpConfigDomain>(sessionConfigDir, systemConfigDir));
  return domains;
}

} // namespace facebook::fboss
