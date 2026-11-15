// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace facebook::fboss {

class HwAsicTable;
class StateDelta;

class AclResourceAccountant {
 public:
  explicit AclResourceAccountant(const HwAsicTable* asicTable);

  /**
   * False if the new state's class-based policies need more PBR ACL rules than
   * the steady share of an asic's limit, or its PBR ACL table holds more
   * entries than the limit itself.
   */
  bool isValidUpdate(const StateDelta& delta) const;

 private:
  const HwAsicTable* asicTable_;
};

} // namespace facebook::fboss
