// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

// Member definitions of RestartOnlyConfigDomain<T>, and through
// TypedConfigDomain-defs.h those of its base. Only the .cpp of a domain
// includes this file, with the full definition of its config type, and then
// explicitly instantiates both templates for that type, e.g.:
//
//   template class TypedConfigDomain<bgp::thrift::BgpConfig>;
//   template class RestartOnlyConfigDomain<bgp::thrift::BgpConfig>;

#include <fmt/format.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/RestartOnlyConfigDomain.h"
#include "fboss/cli/fboss2/session/RestartOnlyServiceController.h"
// Brings in the base class's definitions for the includer's explicit
// instantiation; include-cleaner does not count that as a use.
#include "fboss/cli/fboss2/session/TypedConfigDomain-defs.h" // NOLINT(misc-include-cleaner)

namespace facebook::fboss {

template <class T>
RestartOnlyConfigDomain<T>::RestartOnlyConfigDomain(
    ConfigDomainInfo info,
    std::string daemon)
    : TypedConfigDomain<T>(std::move(info)), daemon_(std::move(daemon)) {}

template <class T>
RestartOnlyConfigDomain<T>::~RestartOnlyConfigDomain() = default;

template <class T>
bool RestartOnlyConfigDomain<T>::requiresConfig() const {
  return false;
}

template <class T>
void RestartOnlyConfigDomain<T>::prepare(
    cli::ConfigActionLevel level,
    const HostInfo& /*hostInfo*/) {
  if (level == cli::ConfigActionLevel::HITLESS) {
    throw std::runtime_error(
        fmt::format(
            "{} does not support config reload; it must be restarted",
            daemon_));
  }
}

template <class T>
std::vector<std::string> RestartOnlyConfigDomain<T>::apply(
    cli::ConfigActionLevel level,
    const HostInfo& hostInfo) {
  prepare(level, hostInfo);
  if (!controller_) {
    controller_ = makeController();
  }
  return controller_->restart();
}

template <class T>
std::string RestartOnlyConfigDomain<T>::actionName(
    cli::ConfigActionLevel level) const {
  // HITLESS is named for what the level means, as before this class existed,
  // although prepare() rejects it: this service is never reloaded.
  return level == cli::ConfigActionLevel::HITLESS ? "reload" : "restart";
}

} // namespace facebook::fboss
