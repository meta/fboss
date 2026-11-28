// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

// Member definitions of TypedConfigDomain<T>. Only the .cpp of a domain
// includes this file, with the full definition of its config type, and then
// explicitly instantiates the template for that type, e.g.:
//
//   template class TypedConfigDomain<cfg::AgentConfig>;
//
// Everything else includes TypedConfigDomain.h, which needs only a forward
// declaration of T.

#include <fmt/format.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "fboss/cli/fboss2/session/TypedConfigDomain.h"
#include "fboss/cli/fboss2/utils/ConfigFileUtils.h"

namespace facebook::fboss {

template <class T>
TypedConfigDomain<T>::TypedConfigDomain(ConfigDomainInfo info)
    : info_(std::move(info)) {}

template <class T>
TypedConfigDomain<T>::~TypedConfigDomain() = default;

template <class T>
const ConfigDomainInfo& TypedConfigDomain<T>::info() const {
  return info_;
}

template <class T>
T& TypedConfigDomain<T>::config() {
  requireLoaded();
  return *config_;
}

template <class T>
const T& TypedConfigDomain<T>::config() const {
  requireLoaded();
  return *config_;
}

template <class T>
std::string TypedConfigDomain<T>::normalize(const std::string& json) const {
  return utils::serializeToPrettyJson(parse(json));
}

template <class T>
bool TypedConfigDomain<T>::equal(std::string_view a, std::string_view b) const {
  return parse(a) == parse(b);
}

template <class T>
bool TypedConfigDomain<T>::loaded() const {
  return config_ != nullptr;
}

template <class T>
void TypedConfigDomain<T>::load(const std::string& json) {
  config_ = std::make_unique<T>(parse(json));
}

template <class T>
void TypedConfigDomain<T>::unload() {
  config_.reset();
}

template <class T>
std::string TypedConfigDomain<T>::serialize() const {
  return utils::serializeToPrettyJson(config());
}

template <class T>
T TypedConfigDomain<T>::parse(std::string_view json) const {
  T config;
  apache::thrift::SimpleJSONSerializer::deserialize<T>(json, config);
  return config;
}

template <class T>
void TypedConfigDomain<T>::setConfig(T config) {
  config_ = std::make_unique<T>(std::move(config));
}

template <class T>
void TypedConfigDomain<T>::requireLoaded() const {
  if (!config_) {
    throw std::runtime_error(
        fmt::format("{} config is not loaded", info_.name));
  }
}

} // namespace facebook::fboss
