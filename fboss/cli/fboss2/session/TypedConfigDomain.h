// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "fboss/cli/fboss2/session/ConfigDomain.h"

namespace facebook::fboss {

/**
 * A ConfigDomain whose config is the thrift struct T.
 *
 * This header only declares the template, so including it (or the header of a
 * domain derived from it) needs no more than a forward declaration of T. The
 * member definitions are in TypedConfigDomain-defs.h, which a domain's .cpp
 * includes before explicitly instantiating the template for its T.
 */
template <class T>
class TypedConfigDomain : public ConfigDomain {
 public:
  explicit TypedConfigDomain(ConfigDomainInfo info);
  ~TypedConfigDomain() override;

  const ConfigDomainInfo& info() const override;

  T& config();
  const T& config() const;

  std::string normalize(const std::string& json) const override;
  bool equal(std::string_view a, std::string_view b) const override;

  bool loaded() const override;
  void load(const std::string& json) override;
  void unload() override;
  std::string serialize() const override;

 protected:
  virtual T parse(std::string_view json) const;

  void setConfig(T config);

 private:
  void requireLoaded() const;

  ConfigDomainInfo info_;
  std::unique_ptr<T> config_;
};

} // namespace facebook::fboss
