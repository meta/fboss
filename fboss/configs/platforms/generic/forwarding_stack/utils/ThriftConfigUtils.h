/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss::configgen {

void validateNoUnknownThriftFields(
    std::string_view inputContents,
    std::string_view normalizedContents);

template <typename Enum>
Enum parseThriftEnumName(
    std::string_view enumName,
    std::string_view fieldName) {
  Enum value;
  if (!apache::thrift::util::tryParseEnum(enumName, &value)) {
    throw FbossError("Unknown ", fieldName, " enum name: ", enumName);
  }
  return value;
}

template <typename Enum>
int64_t thriftEnumToInt(Enum value) {
  return static_cast<int64_t>(static_cast<std::underlying_type_t<Enum>>(value));
}

template <typename Enum>
void validateThriftEnum(Enum value, std::string_view fieldName) {
  if (!apache::thrift::util::tryGetEnumName(value)) {
    throw FbossError(fieldName, " is missing or invalid");
  }
}

template <typename Enum>
bool thriftEnumNameMatches(std::string_view enumName, Enum expected) {
  Enum value;
  return apache::thrift::util::tryParseEnum(enumName, &value) &&
      value == expected;
}

template <typename ThriftType>
ThriftType parseStrictSimpleJson(std::string_view contents) {
  auto value =
      apache::thrift::SimpleJSONSerializer::deserialize<ThriftType>(contents);
  validateNoUnknownThriftFields(
      contents,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(value));
  return value;
}

} // namespace facebook::fboss::configgen
