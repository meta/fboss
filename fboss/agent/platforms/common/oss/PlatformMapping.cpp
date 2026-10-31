// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/platforms/common/PlatformMapping.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/agent/FbossError.h"

DEFINE_string(
    transceiver_vendor_override_map_path,
    "",
    "Thrift JSON TransceiverVendorOverrideMap resolving placeholder vendors in "
    "platform mapping overrides to transceiver vendor and part number "
    "patterns. Empty means no vendor override ever matches.");

namespace facebook::fboss {

namespace {

struct VendorPattern {
  VendorPattern(const std::string& vendorName, const std::string& partNumber)
      : vendor(vendorName), partNumber(partNumber) {}
  re2::RE2 vendor;
  re2::RE2 partNumber;
};

using VendorOverrideMap = std::map<
    std::pair<std::string, std::string>,
    std::vector<std::unique_ptr<VendorPattern>>>;

VendorOverrideMap loadVendorOverrideMap(const std::string& path) {
  VendorOverrideMap map;
  if (path.empty()) {
    XLOG(WARNING) << "No transceiver vendor override map configured, vendor "
                  << "port config overrides will never match";
    return map;
  }
  std::string json;
  if (!folly::readFile(path.c_str(), json)) {
    throw FbossError("Unable to read transceiver vendor override map ", path);
  }
  cfg::TransceiverVendorOverrideMap config;
  try {
    config = apache::thrift::SimpleJSONSerializer::deserialize<
        cfg::TransceiverVendorOverrideMap>(json);
  } catch (const std::exception& ex) {
    throw FbossError(
        "Unable to parse transceiver vendor override map ",
        path,
        ": ",
        ex.what());
  }
  for (const auto& entry : *config.entries()) {
    auto& patterns = map[std::make_pair(
        *entry.placeholder()->name(), *entry.placeholder()->partNumber())];
    for (const auto& pattern : *entry.patterns()) {
      auto compiled = std::make_unique<VendorPattern>(
          *pattern.vendorName(), *pattern.partNumber());
      for (const auto* re : {&compiled->vendor, &compiled->partNumber}) {
        if (!re->ok()) {
          throw FbossError(
              "Invalid pattern in transceiver vendor override map ",
              path,
              ": '",
              re->pattern(),
              "': ",
              re->error());
        }
      }
      patterns.push_back(std::move(compiled));
    }
  }
  XLOG(INFO) << "Loaded " << map.size() << " placeholder vendors from " << path;
  return map;
}

const VendorOverrideMap& vendorOverrideMap() {
  static const VendorOverrideMap kMap =
      loadVendorOverrideMap(FLAGS_transceiver_vendor_override_map_path);
  return kMap;
}

} // namespace

bool PlatformPortProfileConfigMatcher::isTransceiverVendorOverrideMatch(
    const std::string& overrideVendorName,
    const std::string& overridePartNum,
    const std::string& currentVendorName,
    const std::string& currentPartNum) const {
  const auto& map = vendorOverrideMap();
  auto it = map.find(std::make_pair(overrideVendorName, overridePartNum));
  if (it == map.end()) {
    return false;
  }
  for (const auto& pattern : it->second) {
    if (re2::RE2::FullMatch(currentVendorName, pattern->vendor) &&
        re2::RE2::FullMatch(currentPartNum, pattern->partNumber)) {
      return true;
    }
  }
  return false;
}

} // namespace facebook::fboss
