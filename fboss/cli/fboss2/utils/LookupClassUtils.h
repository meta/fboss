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

#include "fboss/agent/gen-cpp2/switch_config_types.h"

/*
 * Shared vocabulary for AclLookupClass values, used by `config interface
 * <intf> lookup-class` (which hands class IDs out on a port) and by the
 * `config acl rule ... lookup-class-*` attributes (which match on them). One
 * concept, so one parser and one notion of which classes an operator may name.
 */
namespace facebook::fboss::lookup_class {

/*
 * Only the CLASS_QUEUE_PER_HOST_QUEUE_* members are configurable. The rest
 * (CLASS_DROP, DST_CLASS_L3_LOCAL_*, ...) are assigned by the agent itself, so
 * naming one would either tag neighbors with an agent-reserved class or write
 * an ACL matching a class the operator does not control.
 */
bool isQueuePerHostClass(cfg::AclLookupClass lookupClass);

// Human-readable list of every configurable class as "<id> (<name>)", for
// help and error text.
std::string validLookupClasses();

/*
 * Parse one lookup-class token: a numeric id ("10") or an enum name
 * ("CLASS_QUEUE_PER_HOST_QUEUE_0", case-insensitive). Throws
 * std::invalid_argument for an unknown value or for a class reserved to the
 * agent.
 */
cfg::AclLookupClass parseLookupClassId(const std::string& token);

} // namespace facebook::fboss::lookup_class
