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

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::fboss::configgen {

enum class AgentConfigComparisonStatus {
  BYTE_MATCH,
  SEMANTIC_MATCH,
  CONTENT_MISMATCH,
};

struct AgentConfigComparisonOptions {
  std::set<std::string> ignoredPaths;
};

struct AgentConfigComparisonResult {
  AgentConfigComparisonStatus status;
  std::string normalizedGenerated;
  std::string normalizedReference;
  std::vector<std::string> ignoredPaths;
  std::vector<std::string> differingPaths;
  std::vector<std::string> semanticDifferences;
};

std::string_view getAgentConfigComparisonStatusName(
    AgentConfigComparisonStatus status);

// Compares serialized AgentConfig values using the current Thrift schema.
// Dotted ignored paths are removed from both inputs before comparison.
// Embedded YAML and JSON ASIC configurations are compared structurally.
AgentConfigComparisonResult compareAgentConfigContents(
    const std::string& generated,
    const std::string& reference,
    const AgentConfigComparisonOptions& options = {});

} // namespace facebook::fboss::configgen
