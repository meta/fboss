// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/DropReasonUtils.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <span>

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {
// Made up names rather than real vendor enumerators. These helpers take the
// prefix and suffix as parameters and have no SDK knowledge, so a real name
// exercises exactly the same code as a synthetic one. What matters is the
// shape a stringified X-macro entry has: "<PREFIX><NAME><SUFFIX> = <value>,".
constexpr std::string_view kPrefix = "TEST_DROP_CAUSE_";
constexpr std::string_view kSuffix = "_X";

struct DropReasonLogParams {
  std::string_view stage;
  std::optional<uint64_t> bitmap;
  std::string header;
};

class DropReasonLogTest : public testing::TestWithParam<DropReasonLogParams> {};

INSTANTIATE_TEST_SUITE_P(
    Unified,
    DropReasonLogTest,
    testing::Values(
        DropReasonLogParams{"ingress", std::nullopt, "DROP reasons ingress: "},
        DropReasonLogParams{"egress", std::nullopt, "DROP reasons egress: "},
        DropReasonLogParams{"CGM", 0x80001, "DROP reasons CGM: (0x80001) "},
        DropReasonLogParams{
            "pipelineIpv4",
            0x80001,
            "DROP reasons pipelineIpv4: (0x80001) "}));

const char* const kEnums[] = {
    "TEST_DROP_CAUSE_ALPHA_X = 0,",
    "TEST_DROP_CAUSE_BRAVO_X = 1,",
    "TEST_DROP_CAUSE_CHARLIE_X = 2,",
    "TEST_DROP_CAUSE_DELTA_X = 3,",
    "TEST_DROP_CAUSE_ECHO_X = 4,",
};

std::vector<std::string> namesOnLine(const std::string& line) {
  std::vector<std::string> names;
  size_t pos = 0;
  while (true) {
    auto next = line.find(", ", pos);
    if (next == std::string::npos) {
      names.push_back(line.substr(pos));
      return names;
    }
    names.push_back(line.substr(pos, next - pos));
    pos = next + 2;
  }
}

// Print the lines as the HwAgent logs them. To see them:
//   buck2 run <this target> -- --gtest_filter='*Split*'
void showLoggedLines(const std::vector<std::string>& lines) {
  std::cout << lines.size() << " log line(s):" << std::endl;
  for (const auto& line : lines) {
    std::cout << "  DROP reasons ingress: " << line << std::endl;
    std::cout << "  ^ " << line.size() << " chars" << std::endl;
  }
}

std::vector<std::string> namesAcrossLines(
    const std::vector<std::string>& lines) {
  std::vector<std::string> names;
  for (const auto& line : lines) {
    auto onLine = namesOnLine(line);
    names.insert(names.end(), onLine.begin(), onLine.end());
  }
  return names;
}
} // namespace

TEST(DropReasonUtilsTest, extractStripsPrefixSuffixAndValue) {
  EXPECT_EQ(extractDropReasonName(kEnums[0], kPrefix, kSuffix), "ALPHA");
}

TEST(DropReasonUtilsTest, extractStripsTrailingEnumValue) {
  EXPECT_EQ(extractDropReasonName(kEnums[4], kPrefix, kSuffix), "ECHO");
}

TEST(DropReasonUtilsTest, extractHandlesMissingValueTail) {
  EXPECT_EQ(
      extractDropReasonName("TEST_DROP_CAUSE_FOXTROT_X", kPrefix, kSuffix),
      "FOXTROT");
}

TEST(DropReasonUtilsTest, extractHandlesMissingPrefix) {
  EXPECT_EQ(
      extractDropReasonName("OTHER_ENUM_X = 5,", kPrefix, kSuffix),
      "OTHER_ENUM");
}

TEST(DropReasonUtilsTest, extractHandlesMissingSuffix) {
  EXPECT_EQ(
      extractDropReasonName("TEST_DROP_CAUSE_GOLF", kPrefix, kSuffix), "GOLF");
}

TEST(DropReasonUtilsTest, extractHandlesInputShorterThanSuffix) {
  EXPECT_EQ(extractDropReasonName("A", kPrefix, kSuffix), "A");
}

TEST(DropReasonUtilsTest, extractHandlesNull) {
  EXPECT_TRUE(extractDropReasonName(nullptr, kPrefix, kSuffix).empty());
}

TEST(DropReasonUtilsTest, joinSeparatesWithComma) {
  EXPECT_EQ(
      formatDropReasonLines(std::vector<std::string>{"ONE", "TWO", "THREE"}),
      std::vector<std::string>{"ONE, TWO, THREE"});
}

TEST(DropReasonUtilsTest, joinSkipsEmptyNames) {
  EXPECT_EQ(
      formatDropReasonLines(std::vector<std::string>{"ONE", "", "TWO"}),
      std::vector<std::string>{"ONE, TWO"});
}

TEST(DropReasonUtilsTest, joinOfNothingIsNoLines) {
  EXPECT_TRUE(formatDropReasonLines(std::vector<std::string>{}).empty());
}

TEST(DropReasonUtilsTest, joinAcceptsStringViews) {
  EXPECT_EQ(
      formatDropReasonLines(std::vector<std::string_view>{"ONE", "", "TWO"}),
      std::vector<std::string>{"ONE, TWO"});
}

TEST(DropReasonUtilsTest, joinSplitsPastTheCap) {
  // Distinct 50 char names; four fit per line (206), a fifth would need 258.
  std::vector<std::string> names;
  names.reserve(10);
  for (size_t i = 0; i < 10; i++) {
    names.push_back(
        std::string(49, static_cast<char>('A' + i)) + std::to_string(i));
  }

  auto lines = formatDropReasonLines(names);
  showLoggedLines(lines);

  ASSERT_EQ(lines.size(), 3);
  EXPECT_EQ(namesOnLine(lines[0]).size(), 4);
  EXPECT_EQ(namesOnLine(lines[1]).size(), 4);
  EXPECT_EQ(namesOnLine(lines[2]).size(), 2);
  for (const auto& line : lines) {
    EXPECT_LE(line.size(), 256);
  }
  EXPECT_EQ(namesAcrossLines(lines), names);
}

TEST(DropReasonUtilsTest, joinSplitsRealisticReasonListWithoutLoss) {
  // As many reasons as the ingress table holds; none may be lost at a wrap.
  std::vector<std::string> names;
  names.reserve(235);
  for (size_t i = 0; i < 235; i++) {
    names.push_back("SOME_DROP_REASON_" + std::to_string(i));
  }

  auto lines = formatDropReasonLines(names);
  showLoggedLines(lines);

  EXPECT_GT(lines.size(), 1);
  for (const auto& line : lines) {
    EXPECT_LE(line.size(), 256);
    EXPECT_FALSE(line.starts_with(", "));
    EXPECT_FALSE(line.ends_with(","));
  }
  EXPECT_EQ(namesAcrossLines(lines), names);
}

TEST(DropReasonUtilsTest, joinKeepsNameLongerThanTheCap) {
  // Nothing to wrap onto, so the line runs over rather than losing the name.
  const std::string huge(300, 'X');
  EXPECT_EQ(
      formatDropReasonLines(std::vector<std::string>{huge}),
      std::vector<std::string>{huge});
}

// The split at the cap. Names are one letter repeated to a width chosen so
// six of them land on exactly 256, putting the boundary itself under test.
TEST(DropReasonUtilsTest, joinSplitsAtTheCapBoundary) {
  // 6 * 41 + 5 * 2 = 256 exactly; a seventh would need 299.
  constexpr size_t kNameLen = 41;
  std::vector<std::string> names;
  names.reserve(26);
  for (char c = 'A'; c <= 'Z'; c++) {
    names.emplace_back(kNameLen, c);
  }

  auto lines = formatDropReasonLines(names);
  showLoggedLines(lines);

  auto expectedLine = [](char first, char last) {
    std::string out;
    for (char c = first; c <= last; c++) {
      if (!out.empty()) {
        out += ", ";
      }
      out += std::string(kNameLen, c);
    }
    return out;
  };
  EXPECT_EQ(
      lines,
      (std::vector<std::string>{
          expectedLine('A', 'F'),
          expectedLine('G', 'L'),
          expectedLine('M', 'R'),
          expectedLine('S', 'X'),
          expectedLine('Y', 'Z')}));

  // Inclusive cap: full lines sit on exactly 256, packed as full as it allows.
  for (size_t i = 0; i + 1 < lines.size(); i++) {
    EXPECT_EQ(lines[i].size(), 256);
    EXPECT_GT(
        lines[i].size() + 2 + namesOnLine(lines[i + 1]).front().size(), 256ul);
  }
  EXPECT_EQ(namesAcrossLines(lines), names);
}

TEST(DropReasonUtilsTest, decodeOfZeroBitmapIsEmpty) {
  EXPECT_TRUE(decodeDropBitmap(0, kEnums, kPrefix, kSuffix).empty());
}

TEST_P(DropReasonLogTest, reasonsPackedWithHeader) {
  const auto& [stage, bitmap, header] = GetParam();
  EXPECT_EQ(
      formatDropReasonLogLines(stage, {"ALPHA", "BRAVO"}, bitmap),
      (std::vector<std::string>{header + "ALPHA, BRAVO"}));
}

TEST_P(DropReasonLogTest, reasonsWrapWithRepeatedHeaders) {
  const auto& [stage, bitmap, header] = GetParam();
  const std::vector<std::string> names{
      std::string(127, 'A'), std::string(127, 'B'), std::string(127, 'C')};
  auto lines = formatDropReasonLogLines(stage, names, bitmap);
  EXPECT_EQ(
      lines,
      (std::vector<std::string>{
          header + names[0] + ", " + names[1], header + names[2]}));

  std::vector<std::string> payloads;
  for (const auto& line : lines) {
    ASSERT_TRUE(line.starts_with(header));
    payloads.push_back(line.substr(header.size()));
    EXPECT_LE(payloads.back().size(), 256);
  }
  EXPECT_EQ(namesAcrossLines(payloads), names);
}

TEST_P(DropReasonLogTest, reasonsWrapAtPayloadBoundary) {
  const auto& [stage, bitmap, header] = GetParam();
  const std::string name(256, 'A');
  EXPECT_EQ(
      formatDropReasonLogLines(stage, {name, "B"}, bitmap),
      (std::vector<std::string>{header + name, header + "B"}));
}

TEST(DropReasonUtilsTest, logReasonsKeepHighBitmapBit) {
  EXPECT_EQ(
      formatDropReasonLogLines("stage", {"ALPHA"}, 1ULL << 63),
      (std::vector<std::string>{
          "DROP reasons stage: (0x8000000000000000) ALPHA"}));
}

TEST_P(DropReasonLogTest, emptyReasonsProduceNoLines) {
  const auto& [stage, bitmap, header] = GetParam();
  EXPECT_TRUE(formatDropReasonLogLines(stage, {}, bitmap).empty());
}

TEST(DropReasonUtilsTest, logReasonsWithDecodedBitmap) {
  EXPECT_EQ(
      formatDropReasonLogLines(
          "stage", decodeDropBitmap(5, kEnums, kPrefix, kSuffix), 5),
      (std::vector<std::string>{"DROP reasons stage: (0x5) ALPHA, CHARLIE"}));
}

TEST(DropReasonUtilsTest, decodeSingleBit) {
  EXPECT_EQ(
      decodeDropBitmap(1 << 0, kEnums, kPrefix, kSuffix),
      std::vector<std::string>{"ALPHA"});
}

TEST(DropReasonUtilsTest, decodeHighestBitInTable) {
  EXPECT_EQ(
      decodeDropBitmap(1 << 4, kEnums, kPrefix, kSuffix),
      std::vector<std::string>{"ECHO"});
}

TEST(DropReasonUtilsTest, decodeHighBitmapBitInOrder) {
  std::vector<const char*> enumNames(64, nullptr);
  enumNames[0] = kEnums[0];
  enumNames[63] = "TEST_DROP_CAUSE_HIGH_X = 63,";
  const auto bitmap = std::numeric_limits<int64_t>::min() | 1;
  EXPECT_EQ(
      decodeDropBitmap(bitmap, enumNames, kPrefix, kSuffix),
      (std::vector<std::string>{"ALPHA", "HIGH"}));
}

TEST(DropReasonUtilsTest, decodeReturnsOnlySetBits) {
  // Bits 0 and 2 set.
  EXPECT_EQ(
      decodeDropBitmap(0b101, kEnums, kPrefix, kSuffix),
      (std::vector<std::string>{"ALPHA", "CHARLIE"}));
}

TEST(DropReasonUtilsTest, decodeAllBitsInTable) {
  EXPECT_EQ(
      decodeDropBitmap(0b11111, kEnums, kPrefix, kSuffix),
      (std::vector<std::string>{"ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO"}));
}

TEST(DropReasonUtilsTest, decodeIgnoresBitsPastTheTable) {
  // Bit 5 has no table entry; bit 1 does.
  EXPECT_EQ(
      decodeDropBitmap(0b100010, kEnums, kPrefix, kSuffix),
      std::vector<std::string>{"BRAVO"});
}

TEST(DropReasonUtilsTest, decodeIgnoresAllBitsPastTheTable) {
  // Every one of the 64 bits set, but only the table entries have names: the
  // rest must be dropped rather than read off the end of the table.
  EXPECT_EQ(
      decodeDropBitmap(-1, kEnums, kPrefix, kSuffix),
      decodeDropBitmap(0b11111, kEnums, kPrefix, kSuffix));
}

TEST(DropReasonUtilsTest, decodeSkipsNullTableEntries) {
  const char* const sparse[] = {
      "TEST_DROP_CAUSE_FIRST_X = 0,",
      nullptr,
      "TEST_DROP_CAUSE_THIRD_X = 2,",
  };
  EXPECT_EQ(
      decodeDropBitmap(0b111, sparse, kPrefix, kSuffix),
      (std::vector<std::string>{"FIRST", "THIRD"}));
}

TEST(DropReasonUtilsTest, decodeSkipsNullAtBitZero) {
  const char* const sparse[] = {
      nullptr,
      "TEST_DROP_CAUSE_SECOND_X = 1,",
  };
  EXPECT_EQ(
      decodeDropBitmap(0b11, sparse, kPrefix, kSuffix),
      std::vector<std::string>{"SECOND"});
}

TEST(DropReasonUtilsTest, decodeRejectsBadTable) {
  EXPECT_TRUE(
      decodeDropBitmap(0b1, std::span<const char* const>{}, kPrefix, kSuffix)
          .empty());
}
