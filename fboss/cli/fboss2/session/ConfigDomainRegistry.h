// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types_fwd.h"
#include "fboss/cli/fboss2/session/ConfigDomain.h"

namespace facebook::fboss {

// The config domains a session manages, in the order they were added.
class ConfigDomainRegistry {
 public:
  // Throws if a domain is already registered for the same service.
  void add(std::unique_ptr<ConfigDomain> domain);

  // Throws if no domain is registered for the service.
  ConfigDomain& get(cli::ServiceType service);
  const ConfigDomain& get(cli::ServiceType service) const;

  // The registered domain of concrete type D. Throws if there is none.
  template <class D>
  D& get() {
    return *find<D>();
  }
  template <class D>
  const D& get() const {
    return *find<D>();
  }

  const std::vector<std::unique_ptr<ConfigDomain>>& all() const {
    return domains_;
  }

 private:
  ConfigDomain* find(cli::ServiceType service) const;

  template <class D>
  D* find() const {
    for (const auto& domain : domains_) {
      if (auto* typed = dynamic_cast<D*>(domain.get())) {
        return typed;
      }
    }
    throw std::runtime_error("No config domain of the requested type");
  }

  std::vector<std::unique_ptr<ConfigDomain>> domains_;
};

// Agent first, then BGP. This is the only place that lists the services.
ConfigDomainRegistry makeDefaultConfigDomains(
    const std::string& sessionConfigDir,
    const std::string& systemConfigDir);

} // namespace facebook::fboss
