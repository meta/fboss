/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/configs/platforms/generic/forwarding_stack/utils/ThriftConfigUtils.h"

#include <algorithm>
#include <string>

#include <folly/json/json.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss::configgen {
namespace {

void validateNoUnknownFields(
    const folly::dynamic& input,
    const folly::dynamic& normalized,
    const std::string& path) {
  if (input.isObject() && normalized.isObject()) {
    for (const auto& [name, value] : input.items()) {
      const auto normalizedIt = normalized.find(name);
      if (normalizedIt == normalized.items().end()) {
        throw FbossError("Unknown field '", name.asString(), "' at ", path);
      }
      validateNoUnknownFields(
          value, normalizedIt->second, path + "." + name.asString());
    }
    return;
  }
  if (input.isArray() && normalized.isArray()) {
    const auto commonSize = std::min(input.size(), normalized.size());
    for (size_t index = 0; index < commonSize; ++index) {
      validateNoUnknownFields(
          input[index],
          normalized[index],
          path + "[" + std::to_string(index) + "]");
    }
  }
}

} // namespace

void validateNoUnknownThriftFields(
    std::string_view inputContents,
    std::string_view normalizedContents) {
  validateNoUnknownFields(
      folly::parseJson(inputContents),
      folly::parseJson(normalizedContents),
      "$");
}

} // namespace facebook::fboss::configgen
