// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest_prod.h>

#include <cstdint>

namespace facebook::fboss {

class HwAsicTable;
class StateDelta;

class VrfResourceAccountant {
 public:
  explicit VrfResourceAccountant(const HwAsicTable* asicTable);
  void stateChanged(const StateDelta& delta);
  bool isValidUpdate(const StateDelta& delta);

 private:
  bool checkVrfResource() const;

  const HwAsicTable* asicTable_;
  // Configured VRFs, excluding the default VRF
  uint32_t vrfUsage_{0};

  FRIEND_TEST(VrfResourceAccountantTest, vrfResourceExceeded);
};

} // namespace facebook::fboss
