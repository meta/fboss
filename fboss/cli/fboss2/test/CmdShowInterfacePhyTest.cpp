// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/String.h>

#include "fboss/cli/fboss2/commands/show/interface/phy/CmdShowInterfacePhy.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

using TableCells = std::vector<std::vector<std::string>>;

// Every table cell is rendered with a single space of padding on each side, so
// dropping the leading pad and splitting on runs of two or more spaces recovers
// the cell values, including the empty leading cell of a data row.
std::vector<std::string> splitCells(const std::string& line) {
  std::string row = folly::rtrimWhitespace(line).str();
  if (!row.empty() && row[0] == ' ') {
    row = row.substr(1);
  }
  std::vector<std::string> cells;
  size_t pos = 0;
  while (pos <= row.size()) {
    auto sep = row.find("  ", pos);
    if (sep == std::string::npos) {
      cells.push_back(row.substr(pos));
      break;
    }
    cells.push_back(row.substr(pos, sep - pos));
    pos = row.find_first_not_of(' ', sep);
  }
  return cells;
}

// Parses a printed table into its header row followed by its data rows. The
// dashed separator tabulate draws under the header is dropped.
TableCells parseTable(const std::string& output) {
  std::vector<std::string> lines;
  folly::split('\n', output, lines);
  TableCells rows;
  for (const auto& line : lines) {
    auto trimmed = folly::trimWhitespace(line).str();
    if (trimmed.empty() ||
        trimmed.find_first_not_of('-') == std::string::npos) {
      continue;
    }
    rows.push_back(splitCells(line));
  }
  return rows;
}

phy::LaneState makeRxLaneState(int16_t lane, bool signalDetectLive) {
  phy::LaneState laneState;
  laneState.lane() = lane;
  laneState.signalDetectLive() = signalDetectLive;
  laneState.cdrLockLive() = true;
  return laneState;
}

phy::LaneStats makeRxLaneStats(
    int16_t lane,
    std::optional<int32_t> signalDetectChangedCount) {
  phy::LaneStats laneStats;
  laneStats.lane() = lane;
  if (signalDetectChangedCount.has_value()) {
    laneStats.signalDetectChangedCount() = *signalDetectChangedCount;
  }
  return laneStats;
}

phy::LaneState makeTxLaneState(int16_t lane, std::optional<int32_t> precoding) {
  phy::TxSettings txSettings;
  txSettings.pre3() = 1;
  txSettings.pre2() = 2;
  txSettings.pre() = 3;
  txSettings.main() = 4;
  txSettings.post() = 5;
  txSettings.post2() = 6;
  txSettings.post3() = 7;
  if (precoding.has_value()) {
    txSettings.precoding() = *precoding;
  }
  // Cisco-native TX fields: set a representative subset to values and leave the
  // rest unset so both the value and N/A rendering paths are exercised.
  txSettings.driverSwing() = 20;

  phy::LaneState laneState;
  laneState.lane() = lane;
  laneState.txSettings() = txSettings;
  return laneState;
}

phy::LaneState makeSerdesLaneState(
    int16_t lane,
    std::optional<phy::RxReach> rxReach,
    std::optional<int32_t> rxPrecoding) {
  phy::SerdesParameters serdesParams;
  serdesParams.lane() = lane;
  if (rxReach.has_value()) {
    serdesParams.rxReach() = *rxReach;
  }
  if (rxPrecoding.has_value()) {
    serdesParams.rxPrecoding() = *rxPrecoding;
  }
  // Cisco-native RX fields: set a representative subset to values and leave the
  // rest unset so both the value and N/A rendering paths are exercised.
  serdesParams.rxCtleCode() = 30;
  serdesParams.rxInstgBoost1Start() = 40;

  phy::LaneState laneState;
  laneState.lane() = lane;
  laneState.serdesParameters() = serdesParams;
  return laneState;
}

} // namespace

class CmdShowInterfacePhyTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowInterfacePhyTestFixture, printPmdLaneRxInfoOmitsUnreportedStats) {
  phy::PhySideState sideState;
  sideState.pmd()->lanes() = {
      {0, makeRxLaneState(0, true)}, {1, makeRxLaneState(1, false)}};
  phy::PhySideStats sideStats;
  sideStats.pmd()->lanes() = {
      {0, makeRxLaneStats(0, 3)}, {1, makeRxLaneStats(1, std::nullopt)}};

  std::stringstream ss;
  CmdShowInterfacePhy().printPmdLaneRxInfo(
      ss, sideState, sideStats, {0, 1}, "Line ");

  // Nothing reports eyes, PPM, SNR or a CDR lock change here, so those columns
  // are left out; signal detect changed stays because one lane reports it.
  const TableCells expected = {
      {"Line RX PMD",
       "Lane",
       "RX Signal Detect Live",
       "RX Signal Detect Changed",
       "RX CDR Lock Live"},
      {"", "0", "True", "3", "True"},
      {"", "1", "False", "N/A", "True"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

TEST_F(CmdShowInterfacePhyTestFixture, printPmdLaneTxInfoShowsPrecoding) {
  phy::PhySideState sideState;
  sideState.pmd()->lanes() = {
      {0, makeTxLaneState(0, 1)}, {1, makeTxLaneState(1, std::nullopt)}};

  std::stringstream ss;
  CmdShowInterfacePhy().printPmdLaneTxInfo(ss, sideState, {0, 1}, "Line ");

  // Precoding is printed because one lane has it; the TX fields that no lane
  // populates (DigGain, DiffEncoderEn, LdoBypass) are dropped entirely. No lane
  // sets a fir* tap, as on a non-Tajo platform, so the i16 fields are printed.
  const TableCells expected = {
      {"Line TX PMD",
       "Lane",
       "Pre3",
       "Pre2",
       "Pre1",
       "Main",
       "Post1",
       "Post2",
       "Post3",
       "Precoding",
       "DriverSwing"},
      {"", "0", "1", "2", "3", "4", "5", "6", "7", "1", "20"},
      {"", "1", "1", "2", "3", "4", "5", "6", "7", "N/A", "20"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

TEST_F(
    CmdShowInterfacePhyTestFixture,
    printPmdLaneTxInfoPrefersUntruncatedFirTaps) {
  phy::TxSettings txSettings;
  // A Cisco SiliconOne main tap of 52428 as it lands in both fields: wrapped in
  // the i16, intact in the i32 read-back of the same register.
  txSettings.main() = -13108;
  txSettings.firMain() = 52428;
  txSettings.ldoBypass() = 1;

  phy::LaneState laneState;
  laneState.lane() = 0;
  laneState.txSettings() = txSettings;

  phy::PhySideState sideState;
  sideState.pmd()->lanes() = {{0, laneState}};

  std::stringstream ss;
  CmdShowInterfacePhy().printPmdLaneTxInfo(ss, sideState, {0}, "Line ");

  const TableCells expected = {
      {"Line TX PMD",
       "Lane",
       "Pre2",
       "Pre1",
       "Main",
       "Post1",
       "Post2",
       "Post3",
       "LdoBypass"},
      {"", "0", "0", "0", "52428", "0", "0", "0", "1"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

TEST_F(
    CmdShowInterfacePhyTestFixture,
    printSerdesParametersInfoShowsRxReachAndRxPrecoding) {
  phy::PmdState pmdState;
  pmdState.lanes() = {
      {0, makeSerdesLaneState(0, phy::RxReach::RX_EXTENDED_REACH, 1)},
      {1, makeSerdesLaneState(1, std::nullopt, std::nullopt)}};

  std::stringstream ss;
  CmdShowInterfacePhy().printSerdesParametersInfo(ss, pmdState, "Line ");

  // Only the four columns that at least one lane populates are printed; the
  // Broadcom-only parameters (RVga, Dco, ...) are not applicable here and are
  // left out rather than printed as a wall of N/A.
  const TableCells expected = {
      {"Line Serdes Parameters",
       "Lane",
       "RxReach",
       "RxPrecoding",
       "RxCtleCode",
       "RxInstgBoost1Start"},
      {"", "0", "RX_EXTENDED_REACH", "1", "30", "40"},
      {"", "1", "N/A", "N/A", "30", "40"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

TEST_F(CmdShowInterfacePhyTestFixture, printSerdesParametersInfoOmitsAllNa) {
  phy::PmdState pmdState;
  phy::LaneState laneState;
  laneState.lane() = 0;
  laneState.serdesParameters()->lane() = 0;
  pmdState.lanes() = {{0, laneState}};

  std::stringstream ss;
  CmdShowInterfacePhy().printSerdesParametersInfo(ss, pmdState, "Line ");

  // Nothing is populated, so only the lane identity columns remain.
  const TableCells expected = {
      {"Line Serdes Parameters", "Lane"},
      {"", "0"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

} // namespace facebook::fboss
