// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>

#include <folly/IPAddressV6.h>
#include "fboss/cli/fboss2/commands/show/mysid/CmdShowMySid.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

namespace facebook::fboss {

namespace {

using ::testing::HasSubstr;

MySidEntry makeEntryWithPrimaryAndBackup() {
  MySidEntry entry;
  entry.type() = MySidType::BINDING_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  prefix.prefixLength() = 64;
  entry.mySid() = prefix;
  NextHopThrift primary;
  primary.address() =
      network::toBinaryAddress(folly::IPAddressV6("2001:db8::aa"));
  entry.resolvedNextHops()->push_back(std::move(primary));
  NextHopThrift backup;
  backup.address() =
      network::toBinaryAddress(folly::IPAddressV6("2001:db8::bb"));
  entry.backupResolvedNextHops()->push_back(std::move(backup));
  return entry;
}

} // namespace

class CmdShowMySidTestFixture : public CmdHandlerTestBase {};

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowMySidTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowMySidTraits::description().empty());
  EXPECT_FALSE(CmdShowMySid::sampleModel().mySidEntries()->empty());
}

TEST_F(CmdShowMySidTestFixture, createModelSeparatesBackupResolvedNextHops) {
  const auto model = CmdShowMySid().createModel(
      {makeEntryWithPrimaryAndBackup()}, {}, {}, {}, {});
  ASSERT_EQ(model.mySidEntries()->size(), 1);
  const auto& entry = model.mySidEntries()->at(0);
  ASSERT_EQ(entry.resolvedNextHops()->size(), 1);
  EXPECT_THAT(entry.resolvedNextHops()->at(0), HasSubstr("2001:db8::aa"));
  ASSERT_EQ(entry.backupResolvedNextHops()->size(), 1);
  EXPECT_THAT(entry.backupResolvedNextHops()->at(0), HasSubstr("2001:db8::bb"));
}

TEST_F(CmdShowMySidTestFixture, printOutputShowsBackupSectionSeparately) {
  const auto model = CmdShowMySid().createModel(
      {makeEntryWithPrimaryAndBackup()}, {}, {}, {}, {});
  std::ostringstream output;
  CmdShowMySid().printOutput(model, output);
  const auto& str = output.str();
  const auto primaryPos = str.find("\tResolved via:\n");
  const auto backupPos = str.find("\tBackup resolved via:\n");
  ASSERT_NE(primaryPos, std::string::npos);
  ASSERT_NE(backupPos, std::string::npos);
  EXPECT_LT(primaryPos, backupPos);
  // Each nexthop renders only in its own section.
  EXPECT_LT(str.find("2001:db8::aa", primaryPos), backupPos);
  EXPECT_EQ(str.find("2001:db8::bb"), str.find("2001:db8::bb", backupPos));
  EXPECT_NE(str.find("2001:db8::bb", backupPos), std::string::npos);
}

} // namespace facebook::fboss
