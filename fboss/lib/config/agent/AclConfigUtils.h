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

#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {
class HwAsic;
namespace utility {

/*
 * Utilities for generating the ACL-related portions of an agent SwitchConfig.
 * This library is shared by FBOSS hardware tests and the fboss2 config CLI so
 * both paths use the same ASIC-aware configuration logic.
 */

// Returns the qualifiers used by the default ACL table for the given ASIC.
std::vector<cfg::AclTableQualifier> genAclQualifiersConfig(
    cfg::AsicType asicType);

// Returns the actions used by the default ACL table.
std::vector<cfg::AclTableActionType> genAclActionTypesConfig();

// Adds the ASIC-specific default ACL table to an existing ingress table group.
void addDefaultAclTable(
    cfg::SwitchConfig& config,
    const HwAsic& asic,
    const std::vector<std::string>& udfGroups = {});

// Adds the default ACL table groups supported by the ASIC. Existing groups are
// preserved, and any flat ingress ACLs are moved into the default table.
// Returns true when ACL table groups are supported and should be enabled.
bool setupDefaultAclTableGroups(cfg::SwitchConfig& config, const HwAsic& asic);

// Adds an ACL entry directly to the default table for the requested stage.
// The table group must already have been created by
// setupDefaultAclTableGroups(). This avoids using the legacy flat ACL list as
// an intermediate representation.
void addAclEntryToDefaultAclTable(
    cfg::SwitchConfig& config,
    cfg::AclEntry acl,
    cfg::AclStage stage = cfg::AclStage::INGRESS);

} // namespace utility
} // namespace facebook::fboss
