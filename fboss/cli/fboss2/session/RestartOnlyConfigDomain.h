// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/session/TypedConfigDomain.h"

namespace facebook::fboss {

class RestartOnlyServiceController;

/**
 * A config domain whose service has no config reload: every restart level
 * restarts it and HITLESS is an error. A service supplies its paths, how to
 * ask it for its config path, and its controller.
 *
 * Like TypedConfigDomain, this header only declares the template. The member
 * definitions are in RestartOnlyConfigDomain-defs.h, which a domain's .cpp
 * includes before explicitly instantiating the template for its T.
 */
template <class T>
class RestartOnlyConfigDomain : public TypedConfigDomain<T> {
 public:
  // daemon names the service in error messages, e.g. "bgpd".
  RestartOnlyConfigDomain(ConfigDomainInfo info, std::string daemon);
  ~RestartOnlyConfigDomain() override;

  bool requiresConfig() const override;
  void prepare(cli::ConfigActionLevel level, const HostInfo& hostInfo) override;
  std::vector<std::string> apply(
      cli::ConfigActionLevel level,
      const HostInfo& hostInfo) override;
  std::string actionName(cli::ConfigActionLevel level) const override;

 protected:
  virtual std::unique_ptr<RestartOnlyServiceController> makeController()
      const = 0;

  std::unique_ptr<RestartOnlyServiceController> controller_;

 private:
  std::string daemon_;
};

} // namespace facebook::fboss
