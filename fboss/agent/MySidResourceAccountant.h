// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest_prod.h>

#include <cstdint>

namespace facebook::fboss {

class HwAsicTable;
class StateDelta;

class MySidResourceAccountant {
 public:
  explicit MySidResourceAccountant(const HwAsicTable* asicTable);
  void stateChanged(const StateDelta& delta);
  bool isValidUpdate(const StateDelta& delta);

 private:
  bool checkMySidResource(bool intermediateState) const;

  const HwAsicTable* asicTable_;
  uint32_t mySidUsage_{0};

  FRIEND_TEST(MySidResourceAccountantTest, checkMySidResource);
  FRIEND_TEST(MySidResourceAccountantTest, mySidStateChanged);
};

} // namespace facebook::fboss
