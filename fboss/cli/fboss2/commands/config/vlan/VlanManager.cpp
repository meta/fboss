/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

namespace {

// Returns the iterator at which an entry with the given id should be inserted
// to keep `list` sorted in ascending order by getId().
//
// The list is treated as ascending, with one tolerated exception: a single
// leading entry that is out of order (e.g. VLAN 4094, which by convention is
// often kept first). That entry is left pinned at the front and the remainder
// of the list is treated as the sorted run; the new entry is placed within that
// run. This avoids inserting a mid-range id ahead of the pinned sentinel.
template <typename List, typename GetId>
auto sortedInsertPos(List& list, int32_t id, const GetId& getId) {
  auto searchBegin = list.begin();
  // If the first entry is greater than the second, treat it as a pinned
  // out-of-order sentinel and start the sorted search after it.
  if (searchBegin != list.end() && std::next(searchBegin) != list.end() &&
      getId(*searchBegin) > getId(*std::next(searchBegin))) {
    ++searchBegin;
  }
  return std::find_if(searchBegin, list.end(), [id, &getId](const auto& entry) {
    return getId(entry) > id;
  });
}

} // namespace

const cfg::Vlan* VlanManager::findVlan(
    const cfg::SwitchConfig& swConfig,
    const VlanID& vlanId) {
  auto it = std::find_if(
      swConfig.vlans()->cbegin(),
      swConfig.vlans()->cend(),
      [vlanId](const auto& vlan) {
        return *vlan.id() == static_cast<int32_t>(vlanId);
      });
  return it != swConfig.vlans()->cend() ? &*it : nullptr;
}

std::pair<bool, cfg::Vlan*> VlanManager::createVlan(
    cfg::SwitchConfig& swConfig,
    const VlanID& vlanId) {
  // Check if VLAN already exists
  const cfg::Vlan* existingVlan = findVlan(swConfig, vlanId);
  if (existingVlan != nullptr) {
    return {false, const_cast<cfg::Vlan*>(existingVlan)};
  }

  cfg::Vlan newVlan;
  newVlan.id() = static_cast<int32_t>(vlanId);
  newVlan.name() = fmt::format("Vlan{}", static_cast<uint16_t>(vlanId));
  newVlan.routable() = false;

  // Insert the new VLAN keeping the list sorted by ID (tolerating a pinned
  // leading sentinel such as VLAN 4094).
  auto insertPos = sortedInsertPos(
      *swConfig.vlans(),
      static_cast<int32_t>(vlanId),
      [](const cfg::Vlan& vlan) { return *vlan.id(); });
  auto insertedIt = swConfig.vlans()->insert(insertPos, newVlan);

  // Create a corresponding barebone interface — the agent requires every
  // VLAN to have at least one cfg::Interface entry.
  // First check if there's already an interface for this VLAN.
  auto intfIt = std::find_if(
      swConfig.interfaces()->cbegin(),
      swConfig.interfaces()->cend(),
      [vlanId](const auto& intf) {
        return *intf.vlanID() == static_cast<int32_t>(vlanId);
      });

  if (intfIt == swConfig.interfaces()->cend()) {
    // No interface exists for this VLAN yet. We need to create one.
    // Ideally, we use intfID == vlanId, but we need to handle the case where
    // an interface with that ID already exists and belongs to a different VLAN.

    // Find a suitable interface ID
    auto intfId = static_cast<int32_t>(vlanId);
    auto existingIntfIt = std::find_if(
        swConfig.interfaces()->cbegin(),
        swConfig.interfaces()->cend(),
        [intfId](const auto& intf) { return *intf.intfID() == intfId; });

    if (existingIntfIt != swConfig.interfaces()->cend()) {
      // An interface with this ID already exists but belongs to a different
      // VLAN. To avoid future conflicts, we use a separate namespace for
      // conflict resolution: 5000 + vlanId. VLANs are limited to < 4095, so
      // this ensures we don't conflict with normal VLAN IDs.
      intfId = 5000 + static_cast<int32_t>(vlanId);

      // Verify that the conflict-resolution ID is also available
      auto conflictIntfIt = std::find_if(
          swConfig.interfaces()->cbegin(),
          swConfig.interfaces()->cend(),
          [intfId](const auto& intf) { return *intf.intfID() == intfId; });

      if (conflictIntfIt != swConfig.interfaces()->cend()) {
        throw FbossError(
            "Cannot find a suitable interface ID for VLAN ",
            static_cast<uint16_t>(vlanId));
      }
    }

    cfg::Interface newIntf;
    newIntf.intfID() = intfId;
    newIntf.vlanID() = static_cast<int32_t>(vlanId);
    newIntf.name() = fmt::format("fboss{}", static_cast<uint16_t>(vlanId));

    // Insert the new interface keeping the list sorted by intfID, the same way
    // we insert VLANs.
    auto intfInsertPos = sortedInsertPos(
        *swConfig.interfaces(), intfId, [](const cfg::Interface& intf) {
          return *intf.intfID();
        });
    swConfig.interfaces()->insert(intfInsertPos, newIntf);
  }

  // Return pointer to the newly inserted VLAN
  return {true, &(*insertedIt)};
}

void VlanManager::checkDeletable(
    const cfg::SwitchConfig& swConfig,
    const VlanID& vlanId) {
  const auto id = static_cast<int32_t>(vlanId);

  if (findVlan(swConfig, vlanId) == nullptr) {
    throw FbossError("VLAN ", static_cast<uint16_t>(vlanId), " does not exist");
  }

  if (*swConfig.defaultVlan() == id) {
    throw FbossError(
        "Cannot delete VLAN ",
        static_cast<uint16_t>(vlanId),
        ": it is the global default VLAN");
  }

  // Untagged ingress VLAN on a port (Port.ingressVlan).
  std::vector<std::string> ingressPorts;
  for (const auto& port : *swConfig.ports()) {
    if (*port.ingressVlan() == id) {
      ingressPorts.push_back(
          port.name().has_value() ? *port.name()
                                  : std::to_string(*port.logicalID()));
    }
  }
  if (!ingressPorts.empty()) {
    throw FbossError(
        "Cannot delete VLAN ",
        static_cast<uint16_t>(vlanId),
        ": it is the ingress VLAN for port(s): ",
        folly::join(", ", ingressPorts));
  }
}

void VlanManager::deleteVlan(
    cfg::SwitchConfig& swConfig,
    const VlanID& vlanId) {
  checkDeletable(swConfig, vlanId);
  const auto id = static_cast<int32_t>(vlanId);

  // Safe to remove. Drop the VLAN entry, the interface(s) bound to it
  // (including routed SVIs with IP addresses — an interface's vlanID must
  // reference an existing VLAN, so it cannot outlive it), the switchport
  // membership rows naming it, and any static MAC entries scoped to it.
  //
  // Membership rows are cascaded rather than refused: a VlanPort row carries no
  // configuration the user would have to re-supply, and dropping it leaves the
  // port valid in its remaining VLANs. Equivalent to running
  // `config interface <port> switchport trunk allowed vlan remove <id>` on
  // every member port. Access ports are not silently retagged — a port with
  // ingressVlan == id is refused above, before this point.
  auto& vlanPorts = *swConfig.vlanPorts();
  vlanPorts.erase(
      std::remove_if(
          vlanPorts.begin(),
          vlanPorts.end(),
          [id](const cfg::VlanPort& vp) { return *vp.vlanID() == id; }),
      vlanPorts.end());

  if (swConfig.staticMacAddrs().has_value()) {
    auto& macs = *swConfig.staticMacAddrs();
    macs.erase(
        std::remove_if(
            macs.begin(),
            macs.end(),
            [id](const cfg::StaticMacEntry& e) { return *e.vlanID() == id; }),
        macs.end());
  }

  auto& vlans = *swConfig.vlans();
  vlans.erase(
      std::remove_if(
          vlans.begin(),
          vlans.end(),
          [id](const cfg::Vlan& vlan) { return *vlan.id() == id; }),
      vlans.end());

  auto& interfaces = *swConfig.interfaces();
  interfaces.erase(
      std::remove_if(
          interfaces.begin(),
          interfaces.end(),
          [id](const cfg::Interface& intf) { return *intf.vlanID() == id; }),
      interfaces.end());
}

} // namespace facebook::fboss
