/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigComparisonUtils.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <folly/json/json.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <yaml-cpp/yaml.h>

#include "fboss/agent/gen-cpp2/agent_config_types.h"

namespace facebook::fboss::configgen {
namespace {

const std::set<std::string> kUniversalIgnoredPaths{"sw.sdkVersion"};

std::string frame(std::string_view type, std::string_view value) {
  return std::string(type) + std::to_string(value.size()) + ":" +
      std::string(value);
}

std::string canonicalizeYamlNode(const YAML::Node& node) {
  switch (node.Type()) {
    case YAML::NodeType::Undefined:
      return "undefined";
    case YAML::NodeType::Null:
      return "null";
    case YAML::NodeType::Scalar:
      return frame("scalar", node.Tag()) + frame("value", node.Scalar());
    case YAML::NodeType::Sequence: {
      std::string result = "sequence";
      for (const auto& child : node) {
        result += frame("item", canonicalizeYamlNode(child));
      }
      return result;
    }
    case YAML::NodeType::Map: {
      std::vector<std::string> entries;
      entries.reserve(node.size());
      for (const auto& entry : node) {
        entries.push_back(
            frame("key", canonicalizeYamlNode(entry.first)) +
            frame("value", canonicalizeYamlNode(entry.second)));
      }
      std::sort(entries.begin(), entries.end());
      std::string result = "map";
      for (const auto& entry : entries) {
        result += frame("entry", entry);
      }
      return result;
    }
  }
  throw std::runtime_error("Unsupported YAML node type");
}

std::string canonicalizeYaml(std::string_view contents) {
  std::string result;
  for (const auto& document : YAML::LoadAll(std::string(contents))) {
    result += frame("document", canonicalizeYamlNode(document));
  }
  return result;
}

folly::dynamic canonicalizeEmbeddedConfigs(
    const folly::dynamic& value,
    std::string_view fieldName = {}) {
  if (value.isObject()) {
    folly::dynamic result = folly::dynamic::object;
    for (const auto& [key, child] : value.items()) {
      const auto keyString = key.asString();
      result[key] = canonicalizeEmbeddedConfigs(child, keyString);
    }
    return result;
  }
  if (value.isArray()) {
    folly::dynamic result = folly::dynamic::array;
    for (const auto& child : value) {
      result.push_back(canonicalizeEmbeddedConfigs(child));
    }
    return result;
  }
  if (value.isString() && fieldName == "yamlConfig") {
    return canonicalizeYaml(value.asString());
  }
  if (value.isString() && fieldName == "jsonConfig") {
    return folly::parseJson(value.asString());
  }
  return value;
}

std::vector<std::string> splitPath(std::string_view path) {
  std::vector<std::string> components;
  size_t begin = 0;
  while (begin <= path.size()) {
    const auto end = path.find('.', begin);
    components.emplace_back(path.substr(begin, end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return components;
}

bool erasePath(folly::dynamic& root, std::string_view path) {
  auto components = splitPath(path);
  if (components.empty() || components.front().empty()) {
    throw std::invalid_argument("Ignored AgentConfig path must not be empty");
  }

  auto* current = &root;
  for (size_t index = 0; index + 1 < components.size(); ++index) {
    if (!current->isObject() || !current->count(components[index])) {
      return false;
    }
    current = &(*current)[components[index]];
  }
  if (!current->isObject() || !current->count(components.back())) {
    return false;
  }
  current->erase(components.back());
  return true;
}

folly::dynamic deserializeAgentConfig(const std::string& contents) {
  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize<cfg::AgentConfig>(
      contents, config);
  return folly::parseJson(
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(config));
}

bool isJqIdentifier(std::string_view value) {
  if (value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(value.front())) ||
        value.front() == '_')) {
    return false;
  }
  return std::all_of(
      value.begin(), value.end(), [](const unsigned char character) {
        return std::isalnum(character) || character == '_';
      });
}

std::string childPath(std::string_view parent, std::string_view child) {
  auto path = std::string(parent);
  if (isJqIdentifier(child)) {
    return path + "." + std::string(child);
  }
  return path + "[" + folly::toJson(folly::dynamic(std::string(child))) + "]";
}

std::string dottedPathToJq(std::string_view path) {
  std::string jqPath;
  for (const auto& component : splitPath(path)) {
    jqPath = childPath(jqPath, component);
  }
  return jqPath;
}

void collectDifferingPaths(
    const folly::dynamic& generated,
    const folly::dynamic& reference,
    std::string_view path,
    std::vector<std::string>& differences) {
  if (generated.type() != reference.type()) {
    differences.emplace_back(path.empty() ? "." : path);
    return;
  }
  if (generated.isObject()) {
    std::set<std::string> keys;
    for (const auto& [key, value] : generated.items()) {
      keys.insert(key.asString());
    }
    for (const auto& [key, value] : reference.items()) {
      keys.insert(key.asString());
    }
    for (const auto& key : keys) {
      const auto nextPath = childPath(path, key);
      if (!generated.count(key) || !reference.count(key)) {
        differences.push_back(nextPath);
        continue;
      }
      collectDifferingPaths(
          generated[key], reference[key], nextPath, differences);
    }
    return;
  }
  if (generated.isArray()) {
    if (generated.size() != reference.size()) {
      differences.emplace_back(path.empty() ? "." : path);
    }
    const auto commonSize = std::min(generated.size(), reference.size());
    for (size_t index = 0; index < commonSize; ++index) {
      collectDifferingPaths(
          generated[index],
          reference[index],
          (path.empty() ? std::string(".") : std::string(path)) + "[" +
              std::to_string(index) + "]",
          differences);
    }
    return;
  }
  if (generated != reference) {
    differences.emplace_back(path.empty() ? "." : path);
  }
}

bool isAtOrBelowPath(std::string_view path, std::string_view parent) {
  return path == parent ||
      (path.size() > parent.size() && path.starts_with(parent) &&
       path[parent.size()] == '.');
}

std::vector<std::string> describeSemanticDifferences(
    const folly::dynamic& generated,
    const folly::dynamic& reference,
    const std::set<std::string>& ignoredPaths) {
  std::vector<std::string> differingPaths;
  collectDifferingPaths(generated, reference, "", differingPaths);
  if (differingPaths.empty()) {
    return {"JSON whitespace or object key ordering"};
  }

  std::vector<std::string> descriptions;
  descriptions.reserve(differingPaths.size());
  for (const auto& path : differingPaths) {
    const auto ignored = std::any_of(
        ignoredPaths.begin(), ignoredPaths.end(), [&](const auto& ignoredPath) {
          return isAtOrBelowPath(path, dottedPathToJq(ignoredPath));
        });
    if (ignored) {
      descriptions.push_back(path + ": ignored by comparison policy");
    } else if (std::string_view(path).ends_with("yamlConfig")) {
      descriptions.push_back(path + ": YAML formatting or map ordering");
    } else if (std::string_view(path).ends_with("jsonConfig")) {
      descriptions.push_back(
          path + ": embedded JSON formatting or key ordering");
    } else {
      descriptions.push_back(
          path + ": normalized by the current Thrift schema");
    }
  }
  return descriptions;
}

} // namespace

std::string_view getAgentConfigComparisonStatusName(
    AgentConfigComparisonStatus status) {
  switch (status) {
    case AgentConfigComparisonStatus::BYTE_MATCH:
      return "BYTE_MATCH";
    case AgentConfigComparisonStatus::SEMANTIC_MATCH:
      return "SEMANTIC_MATCH";
    case AgentConfigComparisonStatus::CONTENT_MISMATCH:
      return "CONTENT_MISMATCH";
  }
  throw std::runtime_error("Unknown AgentConfig comparison status");
}

AgentConfigComparisonResult compareAgentConfigContents(
    const std::string& generated,
    const std::string& reference,
    const AgentConfigComparisonOptions& options) {
  std::set<std::string> ignoredPaths = kUniversalIgnoredPaths;
  ignoredPaths.insert(options.ignoredPaths.begin(), options.ignoredPaths.end());
  if (generated == reference && options.ignoredPaths.empty()) {
    return {
        .status = AgentConfigComparisonStatus::BYTE_MATCH,
        .normalizedGenerated = generated,
        .normalizedReference = reference,
        .ignoredPaths = {ignoredPaths.begin(), ignoredPaths.end()},
        .differingPaths = {},
        .semanticDifferences = {},
    };
  }

  const auto rawGeneratedConfig = folly::parseJson(generated);
  const auto rawReferenceConfig = folly::parseJson(reference);
  auto generatedConfig = deserializeAgentConfig(generated);
  auto referenceConfig = deserializeAgentConfig(reference);
  for (const auto& path : ignoredPaths) {
    const auto erasedGenerated = erasePath(generatedConfig, path);
    const auto erasedReference = erasePath(referenceConfig, path);
    if (!erasedGenerated && !erasedReference &&
        !kUniversalIgnoredPaths.contains(path)) {
      throw std::invalid_argument(
          "Ignored AgentConfig path does not exist in either input: " + path);
    }
  }

  generatedConfig = canonicalizeEmbeddedConfigs(generatedConfig);
  referenceConfig = canonicalizeEmbeddedConfigs(referenceConfig);
  const auto normalizedGenerated = folly::toPrettyJson(generatedConfig);
  const auto normalizedReference = folly::toPrettyJson(referenceConfig);
  std::vector<std::string> differingPaths;
  collectDifferingPaths(generatedConfig, referenceConfig, "", differingPaths);
  const auto status = generatedConfig == referenceConfig
      ? AgentConfigComparisonStatus::SEMANTIC_MATCH
      : AgentConfigComparisonStatus::CONTENT_MISMATCH;
  return {
      .status = status,
      .normalizedGenerated = normalizedGenerated,
      .normalizedReference = normalizedReference,
      .ignoredPaths = {ignoredPaths.begin(), ignoredPaths.end()},
      .differingPaths = std::move(differingPaths),
      .semanticDifferences =
          status == AgentConfigComparisonStatus::SEMANTIC_MATCH
          ? describeSemanticDifferences(
                rawGeneratedConfig, rawReferenceConfig, ignoredPaths)
          : std::vector<std::string>{},
  };
}

} // namespace facebook::fboss::configgen
