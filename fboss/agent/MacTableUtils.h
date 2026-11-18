/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/L2Entry.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

class SwitchState;
class Vlan;

class MacTableUtils {
 public:
  static std::shared_ptr<SwitchState> updateMacTable(
      const std::shared_ptr<SwitchState>& state,
      L2Entry l2Entry,
      L2EntryUpdateType l2EntryUpdateType);

  static std::shared_ptr<SwitchState> updateOrAddEntryWithClassID(
      const std::shared_ptr<SwitchState>& state,
      VlanID vlanID,
      const std::shared_ptr<MacEntry>& macEntry,
      cfg::AclLookupClass classID);

  static std::shared_ptr<SwitchState> removeClassIDForEntry(
      const std::shared_ptr<SwitchState>& state,
      VlanID vlanID,
      const std::shared_ptr<MacEntry>& macEntry);

  static std::shared_ptr<SwitchState> updateOrAddStaticEntry(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& port,
      VlanID vlan,
      folly::MacAddress mac,
      std::optional<bool> configured = std::nullopt);

  static std::shared_ptr<SwitchState> removeEntry(
      const std::shared_ptr<SwitchState>& state,
      VlanID vlan,
      folly::MacAddress mac);

  // Removes learned and neighbor-created MACs; static MACs from config stay.
  static std::shared_ptr<SwitchState> removeUnconfiguredEntriesForPort(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& port);

  static bool isMacLimitErrorDisabled(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& port);

  // Learned and neighbor-created MACs; static MACs from config don't count.
  static uint32_t countUnconfiguredEntriesForPort(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& port);

  // Whether adding `mac` on `port` would take the port over `limit` MACs. A MAC
  // already on the port doesn't add to the count.
  static bool exceedsMacLimit(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& port,
      VlanID vlan,
      folly::MacAddress mac,
      int32_t limit);

  static std::shared_ptr<SwitchState> updateOrAddStaticEntryIfNbrExists(
      const std::shared_ptr<SwitchState>& state,
      VlanID vlan,
      folly::MacAddress mac,
      std::optional<cfg::AclLookupClass> classID = std::nullopt);
};

} // namespace facebook::fboss
