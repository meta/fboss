// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>

namespace facebook::fboss {

class StateDelta;

class L2ResourceAccountant {
 public:
  void stateChanged(const StateDelta& delta);
  bool isValidUpdate(const StateDelta& delta);
  uint32_t getUsage() const;

 private:
  uint32_t l2Entries_{0};
};

} // namespace facebook::fboss
