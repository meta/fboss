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

#include "fboss/agent/if/gen-cpp2/common_types.h"

#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss {

class AclEntry;
class ClassBasedPolicyNode;

// A make-before-break swap holds a rule under a temporary copy of its name, so
// a policy name may not start with this prefix.
inline constexpr char kPbrTempAclEntryPrefix = '#';

// "<policyName>_tc<N>", or "#<policyName>_tc<N>" for the temporary copy.
std::string makePbrAclEntryName(
    const std::string& policyName,
    ForwardingClass trafficClass,
    bool temporary);

bool isPbrTempAclEntry(const std::string& entryName);

std::string makePbrCounterName(
    ForwardingClass trafficClass,
    const std::string& redirectNhgName);

std::shared_ptr<AclEntry> makePbrAclEntry(
    const std::shared_ptr<ClassBasedPolicyNode>& policy,
    ForwardingClass trafficClass,
    bool temporary = false);

std::vector<std::shared_ptr<AclEntry>> createAclEntriesFromPolicy(
    const std::shared_ptr<ClassBasedPolicyNode>& policy,
    bool temporary = false);

} // namespace facebook::fboss
