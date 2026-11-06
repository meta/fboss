// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <sstream>
#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/commands/show/nexthopgroups/CmdShowNextHopGroups.h"
#include "fboss/cli/fboss2/commands/show/nexthopgroups/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
constexpr auto kSharedNhIp = "2401:db00::1";
constexpr std::array kGroupNames{"nhgA", "nhgB"};
constexpr int64_t kSharedSetId = 42;

NextHopThrift makeNextHop(const std::string& ip) {
  NextHopThrift nh;
  nh.address() = facebook::network::toBinaryAddress(folly::IPAddress(ip));
  return nh;
}

// Two distinct named groups that share the SAME next-hop set. On hardware they
// dedup to a single NextHopSetId; the name-keyed getNamedNextHopGroups API
// still returns both, one entry per name.
std::vector<NextHopGroup> createSameSetNamedGroups() {
  std::vector<NextHopGroup> groups;
  for (const auto& name : kGroupNames) {
    NextHopGroup group;
    group.name() = name;
    group.isProgrammed() = true;
    group.id() = kSharedSetId;
    group.nexthops() = {makeNextHop(kSharedNhIp)};
    groups.push_back(std::move(group));
  }
  return groups;
}

// Expected CLI rows for createSameSetNamedGroups(): a full-object expectation
// covering every displayed attribute (name, isNamed, programmed state, id, and
// the formatted nexthop string), so a mismatch pinpoints the offending field. A
// bare nexthop (weight 0, no interface/cost/SRv6/backup) displays weight 1.
std::vector<cli::NextHopGroupEntry> expectedNamedEntries() {
  std::vector<cli::NextHopGroupEntry> entries;
  for (const auto& name : kGroupNames) {
    cli::NextHopGroupEntry entry;
    entry.name() = name;
    entry.isNamed() = true;
    entry.programmed() = "yes";
    entry.id() = kSharedSetId;
    entry.nextHops() = {fmt::format("{} weight 1", kSharedNhIp)};
    entries.push_back(std::move(entry));
  }
  return entries;
}
} // namespace

class CmdShowNamedNextHopGroupsTestFixture : public CmdHandlerTestBase {};

// Two named groups sharing one next-hop set must both appear in the listing.
// This is the regression guard for the collapse bug: the command must query the
// name-keyed getNamedNextHopGroups API rather than the set-id-keyed
// getNextHopGroups API (which returns only one entry per shared NextHopSetId).
TEST_F(CmdShowNamedNextHopGroupsTestFixture, queryClientReturnsBothSharedSet) {
  setupMockedAgentServer();
  auto groups = createSameSetNamedGroups();
  EXPECT_CALL(getMockAgent(), getNamedNextHopGroups(_, _, _))
      .WillOnce(Invoke([&](auto& result, auto /*names*/, auto /*replicate*/) {
        result = groups;
      }));

  auto cmd = CmdShowNamedNextHopGroups();
  auto model = cmd.queryClient(localhost());

  EXPECT_THAT(
      model.nextHopGroups().value(),
      UnorderedElementsAreArray(expectedNamedEntries()));
}

// An agent that predates NextHopGroup.id leaves it unset; print "--" for it.
TEST_F(CmdShowNamedNextHopGroupsTestFixture, printOutputShowsGroupId) {
  auto groups = createSameSetNamedGroups();
  groups[1].id().reset();

  auto cmd = CmdShowNamedNextHopGroups();
  std::stringstream out;
  cmd.printOutput(cmd.createModel(groups), out);

  const std::string expected = fmt::format(
      "NextHopGroup: nhgA  Id: {}  Programmed: yes\n"
      "  {} weight 1\n"
      "NextHopGroup: nhgB  Id: --  Programmed: yes\n"
      "  {} weight 1\n",
      kSharedSetId,
      kSharedNhIp,
      kSharedNhIp);
  EXPECT_EQ(out.str(), expected);
}

TEST_F(CmdShowNamedNextHopGroupsTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowNamedNextHopGroupsTraits::description().empty());
  EXPECT_FALSE(
      CmdShowNamedNextHopGroups::sampleModel().nextHopGroups()->empty());
}

} // namespace facebook::fboss
