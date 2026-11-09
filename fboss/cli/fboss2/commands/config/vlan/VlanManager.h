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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"

#include <utility>

namespace facebook::fboss {

/**
 * VlanManager provides utilities for managing VLANs in SwitchConfig.
 *
 * This class is used by config vlan subcommands to automatically create VLANs
 * when they do not yet exist, so users do not need to create VLANs explicitly
 * before configuring them.
 */
class VlanManager {
 public:
  // Creates a VLAN in swConfig if it doesn't exist.
  // Returns a pair of (created, vlan_pointer) where:
  //   - created: true if the VLAN was created, false if it already existed
  //   - vlan_pointer: pointer to the VLAN (newly created or existing)
  // Auto-generates a name "Vlan<id>" (e.g., "Vlan100") for new VLANs.
  // Does NOT call saveConfig() — callers save after their own mutations.
  //
  // WARNING: the returned reference is into swConfig.vlans() (a std::vector).
  // Calling push_back / createVlan on the same vector after this call will
  // invalidate the reference.  Use the reference immediately and do not store
  // it across further mutations of swConfig.vlans(), including other calls to
  // this function.
  static std::pair<bool, cfg::Vlan*> createVlan(
      cfg::SwitchConfig& swConfig,
      const VlanID& vlanId);

  // Returns a pointer to the VLAN with the given ID, or nullptr if not found.
  // Use this for read-only lookups where you don't want auto-creation.
  static const cfg::Vlan* findVlan(
      const cfg::SwitchConfig& swConfig,
      const VlanID& vlanId);

  // Removes the VLAN with the given ID from swConfig, along with the child
  // objects that cannot outlive it:
  //   - interfaces bound to it (Interface.vlanID), including routed SVIs with
  //     IP addresses — an interface's vlanID must reference an existing VLAN
  //   - switchport membership rows naming it (VlanPort.vlanID), dropping the
  //     VLAN from every member port
  //   - static MAC entries scoped to it (StaticMacEntry.vlanID)
  //
  // Refuses (throws FbossError) rather than silently orphaning references or
  // changing a sibling object's meaning when the VLAN is still in use. Each
  // referrer must be cleared first:
  //   - it is the global default VLAN (SwitchConfig.defaultVlan)
  //       -> point the default elsewhere: config vlan default <other-id>
  //   - a port lists it as its untagged ingress VLAN (Port.ingressVlan); the
  //     only fallback value is 0, which means routed port, so clearing it here
  //     would change the port's L2 mode
  //       -> move the port: config interface <port> switchport access vlan
  //          <other-id>
  // Throws FbossError if no VLAN with the given ID exists.
  //
  // Does NOT call saveConfig() — callers save after this returns.
  static void deleteVlan(cfg::SwitchConfig& swConfig, const VlanID& vlanId);
};

} // namespace facebook::fboss
