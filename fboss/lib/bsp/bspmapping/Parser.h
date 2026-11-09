// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/Range.h>
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace facebook::fboss {

inline const int HEADER_OFFSET = 1;
inline const int MIN_LANE_ID = 1;
inline const int MAX_LANE_ID = 8;
inline constexpr folly::StringPiece kInputConfigPrefix{
    "fboss/lib/bsp/bspmapping/input/"};
// Only ladakh800bcls and leh800bcls remain on the legacy CSV flow for their
// MDIO retimer (Phy) mappings; all other platforms are constructed dynamically
// via XcvrLib from platform_manager.json.
inline constexpr folly::StringPiece kPortMappingLadakh800bclsCsv{
    "Ladakh800bcls_BspMapping.csv"};
inline constexpr folly::StringPiece kPortMappingLeh800bclsCsv{
    "Leh800bcls_BspMapping.csv"};

class Parser {
 public:
  static TransceiverConfigRow getTransceiverConfigRowFromCsvLine(
      const std::string_view& line);
  static std::vector<TransceiverConfigRow> getTransceiverConfigRowsFromCsv(
      folly::StringPiece csv);
  static PhyConfigRow getPhyConfigRowFromCsvLine(const std::string_view& line);
  static std::vector<PhyConfigRow> getPhyConfigRowsFromCsv(
      folly::StringPiece csv);
  static BspPlatformMappingThrift getBspPlatformMappingFromCsv(
      folly::StringPiece csv);
  static BspPlatformMappingThrift getBspPlatformMappingFromCsv(
      folly::StringPiece csv,
      folly::StringPiece phyCsv);

 private:
  static std::vector<int> getTransceiverLaneIdList(
      const std::string_view& entry);
  static ResetAndPresenceAccessType getAccessCtrlTypeFromString(
      const std::string_view& entry);
  static TransceiverIOType getTransceiverIOTypeFromString(
      const std::string_view& entry);
  static PhyIOType getPhyIOTypeFromString(const std::string_view& entry);
};

} // namespace facebook::fboss
