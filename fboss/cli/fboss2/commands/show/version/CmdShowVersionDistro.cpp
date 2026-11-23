/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/version/CmdShowVersionDistro.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/json.h>

#include <cerrno>
#include <set>
#include <stdexcept>
#include <utility>

namespace facebook::fboss {
CmdShowVersionDistro::RetType CmdShowVersionDistro::queryClient(
    const HostInfo& /* hostInfo */) {
  return readVersionFile("/etc/fboss-distro-version.json");
}

CmdShowVersionDistro::RetType CmdShowVersionDistro::readVersionFile(
    const std::string& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    if (errno == ENOENT) {
      return {
          {"Distro version",
           "Unavailable: " + path +
               " is missing (older images do not contain this record)."}};
    }
    throw std::runtime_error("Unable to read distro version record: " + path);
  }
  const auto record = folly::parseJson(contents);
  if (!record.isObject() || record.getDefault("schema") != 1) {
    throw std::runtime_error(
        "Unsupported distro version record schema: " + path);
  }
  RetType result{{"record", folly::toPrettyJson(record)}};
  for (const auto& [label, section] : {
           std::pair{"bsp", "versions"},
           std::pair{"kernel", "versions"},
           std::pair{"native_sdk", "versions"},
           std::pair{"sai", "versions"},
           std::pair{"manifest", "image"},
           std::pair{"source_revision", "image"},
       }) {
    if (const auto* fields = record.get_ptr(section)) {
      if (!fields->isObject()) {
        throw std::runtime_error(
            "Invalid distro version record field " + std::string(section) +
            ": " + path);
      }
      if (const auto* value = fields->get_ptr(label)) {
        if (!value->isString()) {
          throw std::runtime_error(
              "Invalid distro version record field " + std::string(label) +
              ": " + path);
        }
        result[label] = value->asString();
      }
    }
  }
  const auto* components = record.get_ptr("components");
  if (!components) {
    return result;
  }
  if (!components->isObject()) {
    throw std::runtime_error(
        "Invalid distro version record field components: " + path);
  }
  std::set<std::pair<std::string, std::string>> serviceVersions;
  for (const auto& component : components->items()) {
    if (!component.second.isObject()) {
      continue;
    }
    const auto* provenance = component.second.get_ptr("provenance");
    if (!provenance || !provenance->isObject()) {
      continue;
    }
    const auto* buildInfo = provenance->get_ptr("build_info");
    if (!buildInfo || !buildInfo->isObject()) {
      continue;
    }
    const auto* binaries = buildInfo->get_ptr("binaries");
    if (!binaries || !binaries->isObject()) {
      continue;
    }
    for (const auto& binary : binaries->items()) {
      if (!binary.second.isObject()) {
        continue;
      }
      const auto* version = binary.second.get_ptr("build_version");
      serviceVersions.emplace(
          binary.first.asString(),
          version && version->isString() && !version->asString().empty()
              ? version->asString()
              : "Unavailable");
    }
  }
  if (!serviceVersions.empty()) {
    std::string summary = "\nService versions:\n";
    for (const auto& [binary, version] : serviceVersions) {
      summary += fmt::format("  {:<32}{}\n", binary, version);
    }
    result["service_versions"] = std::move(summary);
  }
  return result;
}

void CmdShowVersionDistro::printOutput(const RetType& versions) {
  if (versions.find("Distro version") != versions.end()) {
    for (const auto& [key, value] : versions) {
      fmt::print("{}: {}\n", key, value);
    }
    return;
  }

  if (detailed_) {
    fmt::print("{}\n", versions.at("record"));
    return;
  }
  for (const auto& label :
       {"bsp", "kernel", "native_sdk", "sai", "manifest", "source_revision"}) {
    if (const auto entry = versions.find(label); entry != versions.end()) {
      fmt::print("{:<16}{}\n", label, entry->second);
    }
  }
  if (const auto entry = versions.find("service_versions");
      entry != versions.end()) {
    fmt::print("{}", entry->second);
  }
}

template void CmdHandler<CmdShowVersionDistro, CmdShowVersionTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowVersionDistro, CmdShowVersionTraits>::getValidFilters();

} // namespace facebook::fboss
