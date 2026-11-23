/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.h"

#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/small_vector.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss::configgen {
namespace {

enum Column : size_t {
  kPortAColumn,
  kPortBColumn,
  kProfileColumn,
  kHasTransceiverColumn,
  kColumnCount,
};

constexpr std::array<std::string_view, kColumnCount> kHeader{
    "portA",
    "portB",
    "profile",
    "hasTransceiver"};
constexpr std::string_view kLinkTopologyDirectoryName = "link_topology";

[[noreturn]] void
throwRowError(std::string_view sourceName, size_t row, std::string_view error) {
  throw FbossError(sourceName, " row ", row, ": ", error);
}

folly::small_vector<folly::StringPiece, kColumnCount>
parseColumns(folly::StringPiece line, std::string_view sourceName, size_t row) {
  folly::small_vector<folly::StringPiece, kColumnCount> columns;
  folly::split(',', line, columns, /*ignoreEmpty=*/false);
  if (columns.size() != kHeader.size()) {
    throwRowError(sourceName, row, "expected exactly four columns");
  }
  for (auto& column : columns) {
    column = folly::trimWhitespace(column);
  }
  return columns;
}

} // namespace

LinkTopology parseLinkTopologyCsv(
    std::string_view contents,
    std::string_view sourceName) {
  std::vector<folly::StringPiece> lines;
  folly::split(
      '\n', folly::StringPiece(contents), lines, /*ignoreEmpty=*/false);
  if (lines.empty() || folly::trimWhitespace(lines.front()).empty()) {
    throw FbossError(sourceName, ": missing link-topology CSV header");
  }

  const auto header = parseColumns(lines.front(), sourceName, 1);
  for (size_t column = 0; column < kHeader.size(); ++column) {
    if (header.at(column) != kHeader.at(column)) {
      throwRowError(sourceName, 1, "unexpected link-topology CSV header");
    }
  }

  LinkTopology topology;
  std::set<std::string> ports;
  for (size_t line = 1; line < lines.size(); ++line) {
    if (folly::trimWhitespace(lines.at(line)).empty()) {
      continue;
    }
    const auto row = line + 1;
    const auto columns = parseColumns(lines.at(line), sourceName, row);
    if (columns.at(kPortAColumn).empty() ||
        columns.at(kProfileColumn).empty() ||
        columns.at(kHasTransceiverColumn).empty()) {
      throwRowError(
          sourceName,
          row,
          "portA, profile, and hasTransceiver must not be empty");
    }

    cfg::PortProfileID profile;
    if (!apache::thrift::util::tryParseEnum(
            columns.at(kProfileColumn), &profile)) {
      throwRowError(sourceName, row, "profile must be a PortProfileID name");
    }
    if (profile == cfg::PortProfileID::PROFILE_DEFAULT) {
      throwRowError(
          sourceName, row, "profile must be a concrete PortProfileID");
    }

    bool hasTransceiver;
    if (columns.at(kHasTransceiverColumn) == "true") {
      hasTransceiver = true;
    } else if (columns.at(kHasTransceiverColumn) == "false") {
      hasTransceiver = false;
    } else {
      throwRowError(sourceName, row, "hasTransceiver must be true or false");
    }

    auto portA = columns.at(kPortAColumn).str();
    if (!ports.insert(portA).second) {
      throwRowError(sourceName, row, "portA appears in more than one link");
    }
    std::optional<std::string> portB;
    if (!columns.at(kPortBColumn).empty()) {
      portB = columns.at(kPortBColumn).str();
      if (*portB != portA && !ports.insert(*portB).second) {
        throwRowError(sourceName, row, "portB appears in more than one link");
      }
    }

    topology.push_back(
        {.portA = std::move(portA),
         .portB = std::move(portB),
         .profile = profile,
         .hasTransceiver = hasTransceiver});
  }
  if (topology.empty()) {
    throw FbossError(
        sourceName, ": link topology must contain at least one link");
  }
  return topology;
}

LinkTopology loadLinkTopologyCsv(const std::filesystem::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw FbossError("Unable to read link topology from ", path.string());
  }
  return parseLinkTopologyCsv(contents, path.string());
}

LinkTopology loadLinkTopologyVariant(
    const std::filesystem::path& platformDirectory,
    std::string_view variant) {
  const std::filesystem::path component{variant};
  if (component.empty() || component.has_parent_path() || component == "." ||
      component == "..") {
    throw FbossError("Invalid link topology variant: ", variant);
  }

  auto fileName = std::string(variant);
  fileName += ".csv";
  const auto path = platformDirectory / "forwarding_stack" /
      kLinkTopologyDirectoryName / fileName;
  if (!std::filesystem::is_regular_file(path)) {
    throw FbossError(
        "Link topology variant '",
        variant,
        "' does not exist for platform '",
        platformDirectory.filename().string(),
        "': ",
        path.string());
  }
  return loadLinkTopologyCsv(path);
}

} // namespace facebook::fboss::configgen
