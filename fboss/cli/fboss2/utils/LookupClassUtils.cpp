/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/LookupClassUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss::lookup_class {

// Only the queue-per-host classes are an operator's to use; the rest are
// assigned by the agent itself, so naming one would either tag neighbors with
// an agent-reserved class or write an ACL matching a class the operator does
// not control.
bool isQueuePerHostClass(cfg::AclLookupClass lookupClass) {
  return lookupClass >= cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 &&
      lookupClass <= cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_9;
}

// Human-readable list of every configurable lookup class as "<id> (<name>)".
std::string validLookupClasses() {
  std::vector<std::string> entries;
  for (const auto value :
       apache::thrift::TEnumTraits<cfg::AclLookupClass>::values) {
    if (!isQueuePerHostClass(value)) {
      continue;
    }
    entries.push_back(
        fmt::format(
            "{} ({})",
            static_cast<int>(value),
            apache::thrift::util::enumNameSafe(value)));
  }
  return folly::join(", ", entries);
}

// Parses a single lookup-class token: a numeric id (e.g. "10") or an enum
// name (e.g. "CLASS_QUEUE_PER_HOST_QUEUE_0", case-insensitive). Only
// queue-per-host classes are accepted.
cfg::AclLookupClass parseLookupClassId(const std::string& token) {
  cfg::AclLookupClass lookupClass{
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0};
  int32_t classId = 0;
  if (folly::tryTo<int32_t>(token).hasValue()) {
    classId = folly::to<int32_t>(token);
    lookupClass = static_cast<cfg::AclLookupClass>(classId);
    if (apache::thrift::TEnumTraits<cfg::AclLookupClass>::findName(
            lookupClass) == nullptr) {
      throw std::invalid_argument(
          fmt::format(
              "Invalid lookup-class value '{}'. Valid values: {}",
              token,
              validLookupClasses()));
    }
  } else {
    std::string tokenUpper = token;
    std::transform(
        tokenUpper.begin(),
        tokenUpper.end(),
        tokenUpper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (!apache::thrift::TEnumTraits<cfg::AclLookupClass>::findValue(
            tokenUpper, &lookupClass)) {
      throw std::invalid_argument(
          fmt::format(
              "Invalid lookup-class value '{}': must be a numeric id or class "
              "name. Valid values: {}",
              token,
              validLookupClasses()));
    }
  }

  if (!isQueuePerHostClass(lookupClass)) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid lookup-class value '{}': {} is reserved for agent use. "
            "Valid values: {}",
            token,
            apache::thrift::util::enumNameSafe(lookupClass),
            validLookupClasses()));
  }
  return lookupClass;
}

} // namespace facebook::fboss::lookup_class
