// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/Thrifty.h"

namespace facebook::fboss {

USE_THRIFT_COW(Policer);

class Policer : public ThriftStructNode<Policer, cfg::Policer> {
 public:
  using BaseT = ThriftStructNode<Policer, cfg::Policer>;

  Policer() = default; // Required by Thrift COW map patching.
  explicit Policer(const cfg::Policer& policer);

  std::string getID() const {
    return getLabel();
  }
  void validate() const;
  std::string getLabel() const;
  cfg::PolicerMeterType getMeterType() const;
  int64_t getCommittedInformationRate() const;
  int64_t getCommittedBurstSize() const;

 private:
  explicit Policer(const Policer* orig) : BaseT(orig) {}
  friend class CloneAllocator;
};

extern template struct ThriftStructNode<Policer, cfg::Policer>;

} // namespace facebook::fboss
