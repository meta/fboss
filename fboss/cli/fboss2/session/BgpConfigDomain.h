// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include "fboss/cli/fboss2/session/TypedConfigDomain.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types_fwd.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types_fwd.h>
#endif

namespace facebook::fboss {

class BgpConfigDomain : public TypedConfigDomain<bgp::thrift::BgpConfig> {
 public:
  BgpConfigDomain(
      const std::string& sessionConfigDir,
      const std::string& systemConfigDir);

  std::string queryCurrentPath(const HostInfo& hostInfo) const override;
  bool requiresConfig() const override {
    return false;
  }

  // Empty or unparseable text starts from the schema defaults.
  void load(const std::string& json) override;
};

} // namespace facebook::fboss
