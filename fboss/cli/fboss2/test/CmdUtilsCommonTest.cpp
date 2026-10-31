// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <fboss/cli/fboss2/utils/CmdUtilsCommon.h>
#include <gtest/gtest.h>

#include <chrono>
#include <regex>
#include <string>

using namespace ::testing;

namespace facebook::fboss::utils {
namespace {

int64_t nowEpochSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

} // namespace

TEST(FormatBandwidthTest, belowOneByteIsNotApplicable) {
  // Anything that floors to less than a single byte/second is "N/A".
  EXPECT_EQ(formatBandwidth(0.0f), "N/A");
  EXPECT_EQ(formatBandwidth(0.5f), "N/A");
  EXPECT_EQ(formatBandwidth(0.999f), "N/A");
}

TEST(FormatBandwidthTest, scalesAcrossUnits) {
  // The input is bytes/second; the output is bits/second (x8).
  EXPECT_EQ(formatBandwidth(100.0f), "800bps"); // 100 * 8 = 800
  EXPECT_EQ(formatBandwidth(125.0f), "1Kbps"); // 1,000 bits
  EXPECT_EQ(formatBandwidth(125000.0f), "1Mbps"); // 1,000,000 bits
  EXPECT_EQ(formatBandwidth(125000000.0f), "1Gbps"); // 1,000,000,000 bits
}

TEST(FormatBandwidthTest, truncatesRatherThanRounds) {
  // The implementation deliberately does not round up (integer division).
  EXPECT_EQ(formatBandwidth(249.0f), "1Kbps"); // 1,992 bits -> 1Kbps
  EXPECT_EQ(formatBandwidth(124.0f), "992bps"); // 992 bits, still < 1000
}

TEST(SplitFractionalSecondsTest, splitsSecondsAndMilliseconds) {
  auto tv = splitFractionalSecondsFromTimer(6475);
  EXPECT_EQ(tv.tv_sec, 6);
  EXPECT_EQ(tv.tv_usec, 475);
}

TEST(SplitFractionalSecondsTest, handlesSmallAndZeroFraction) {
  auto small = splitFractionalSecondsFromTimer(6005);
  EXPECT_EQ(small.tv_sec, 6);
  EXPECT_EQ(small.tv_usec, 5);

  auto whole = splitFractionalSecondsFromTimer(7000);
  EXPECT_EQ(whole.tv_sec, 7);
  EXPECT_EQ(whole.tv_usec, 0);
}

TEST(GetEpochFromDurationTest, zeroDurationIsCurrentEpoch) {
  auto before = nowEpochSeconds();
  auto epoch = getEpochFromDuration(0);
  auto after = nowEpochSeconds();
  EXPECT_GE(epoch, before);
  EXPECT_LE(epoch, after);
}

TEST(GetEpochFromDurationTest, subtractsDurationInMilliseconds) {
  // 10,000 ms in the past is 10 seconds earlier than "now". Allow a one
  // second slack for the two separate now() reads straddling a boundary.
  auto now = getEpochFromDuration(0);
  auto past = getEpochFromDuration(10000);
  EXPECT_GE(now - past, 9);
  EXPECT_LE(now - past, 11);
}

TEST(ParseTimeToTimeStampTest, formatsDateTimeWithFraction) {
  // Use a fractional part >= 100 ms so the assertion is independent of the
  // local timezone and of millisecond zero-padding. The seconds portion
  // varies with the local timezone, so match structurally with a regex.
  const auto formatted = parseTimeToTimeStamp(1635292806475);
  EXPECT_TRUE(
      std::regex_search(
          formatted,
          std::regex(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.475)")))
      << "unexpected timestamp format: " << formatted;
}

} // namespace facebook::fboss::utils
