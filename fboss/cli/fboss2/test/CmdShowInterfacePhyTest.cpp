// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
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

  phy::LaneState laneState;
  laneState.lane() = lane;
  laneState.serdesParameters() = serdesParams;
  return laneState;
}

} // namespace

class CmdShowInterfacePhyTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowInterfacePhyTestFixture, printPmdLaneTxInfoShowsPrecoding) {
  phy::PhySideState sideState;
  sideState.pmd()->lanes() = {
      {0, makeTxLaneState(0, 1)}, {1, makeTxLaneState(1, std::nullopt)}};

  std::stringstream ss;
  CmdShowInterfacePhy().printPmdLaneTxInfo(ss, sideState, {0, 1}, "Line ");

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
       "Precoding"},
      {"", "0", "1", "2", "3", "4", "5", "6", "7", "1"},
      {"", "1", "1", "2", "3", "4", "5", "6", "7", "N/A"},
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

  // Only the two new trailing columns are populated here; every other serdes
  // parameter is left unset and renders as N/A.
  const TableCells expected = {
      {"Line Serdes Parameters",
       "Lane",
       "RVga",
       "Dco",
       "TpChn0",
       "TpChn1",
       "TpChn2",
       "RxPf",
       "RxPfLfq",
       "RxPfHfq",
       "RxFltM",
       "RxFltS",
       "RxTap1",
       "RxTap2",
       "RxEq3",
       "RxEq2",
       "RxEq1",
       "RxEqM",
       "RxEqP1",
       "RxEqP2",
       "RxReach",
       "RxPrecoding"},
      {"",    "0",   "N/A", "N/A", "N/A", "N/A", "N/A",
       "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "N/A",
       "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "RX_EXTENDED_REACH",
       "1"},
      {"",    "1",   "N/A", "N/A", "N/A", "N/A", "N/A", "N/A",
       "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "N/A",
       "N/A", "N/A", "N/A", "N/A", "N/A", "N/A"},
  };
  EXPECT_EQ(parseTable(ss.str()), expected);
}

TEST_F(CmdShowInterfacePhyTestFixture, printSideStateAndStatShowsPcsCounters) {
  phy::PhySideState sideState;
  phy::PcsState pcsState;
  pcsState.pcsRxStatusLive() = true;
  pcsState.pcsRxStatusLatched() = false;
  sideState.pcs() = pcsState;

  phy::PhySideStats sideStats;
  phy::PcsStats pcsStats;
  pcsStats.erroredBlocks() = 12;
  pcsStats.berCount() = 34;
  sideStats.pcs() = pcsStats;

  std::stringstream ss;
  CmdShowInterfacePhy().printSideStateAndStat(
      ss, sideState, sideStats, "Line ");

  // The PCS table is printed first; its empty header cell is trimmed.
  const TableCells expected = {
      {"Line PCS"},
      {"Line PCS RX Link Status Live", "True"},
      {"Line PCS RX Link Status Changed", "0"},
      {"Line PCS Errored Blocks", "12"},
      {"Line PCS BER Count", "34"},
  };
  auto rows = parseTable(ss.str());
  ASSERT_GE(rows.size(), expected.size());
  EXPECT_EQ(TableCells(rows.begin(), rows.begin() + expected.size()), expected);
}

TEST_F(
    CmdShowInterfacePhyTestFixture,
    printSideStateAndStatShowsPcsCountersWithoutRxStatus) {
  phy::PhySideState sideState;
  phy::PhySideStats sideStats;
  phy::PcsStats pcsStats;
  pcsStats.erroredBlocks() = 5;
  sideStats.pcs() = pcsStats;

  std::stringstream ss;
  CmdShowInterfacePhy().printSideStateAndStat(
      ss, sideState, sideStats, "Line ");

  const TableCells expected = {
      {"Line PCS"},
      {"Line PCS Errored Blocks", "5"},
  };
  auto rows = parseTable(ss.str());
  ASSERT_GE(rows.size(), expected.size());
  EXPECT_EQ(TableCells(rows.begin(), rows.begin() + expected.size()), expected);
  EXPECT_THAT(rows, Not(Contains(ElementsAre("Line PCS BER Count", _))));
}

} // namespace facebook::fboss
