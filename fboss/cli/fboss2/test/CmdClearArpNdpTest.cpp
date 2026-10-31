// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddress.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearArp.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearNdp.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

template <typename NeighborEntry>
NeighborEntry
makeEntry(const std::string& ip, int32_t intfID, const std::string& state) {
  NeighborEntry entry;
  entry.ip() = facebook::network::toBinaryAddress(folly::IPAddress(ip));
  entry.interfaceID() = intfID;
  entry.state() = state;
  return entry;
}

} // namespace

class CmdClearArpNdpTestFixture : public CmdHandlerTestBase {
 public:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    setupMockedAgentServer();
  }

  // The agent returns how many entries it removed; capture what was sent.
  void expectFlush(int32_t numFlushed, size_t* numSent) {
    EXPECT_CALL(getMockAgent(), flushNeighborEntries(_))
        .WillOnce(Invoke([=](std::unique_ptr<std::vector<IfAndIP>> entries) {
          *numSent = entries->size();
          return numFlushed;
        }));
  }
};

// A full table with 2 INCOMPLETE + 3 REACHABLE, agent flushes 3.
// Only the resolved entries are announced as deletions; the unresolved ones
// are still sent to the agent but reported in a separate note.
TEST_F(CmdClearArpNdpTestFixture, arpMixedResolvedAndIncomplete) {
  std::vector<ArpEntryThrift> arpEntries = {
      makeEntry<ArpEntryThrift>("10.100.1.1", 2001, "INCOMPLETE"),
      makeEntry<ArpEntryThrift>("10.100.5.1", 2005, "INCOMPLETE"),
      makeEntry<ArpEntryThrift>("10.100.30.1", 2030, "REACHABLE"),
      makeEntry<ArpEntryThrift>("10.100.31.1", 2031, "REACHABLE"),
      makeEntry<ArpEntryThrift>("10.100.100.1", 2900, "REACHABLE"),
  };
  EXPECT_CALL(getMockAgent(), getArpTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries = arpEntries; }));
  size_t numSent = 0;
  expectFlush(3, &numSent);

  auto result = CmdClearArp().queryClient(localhost(), {});

  EXPECT_EQ(numSent, 5);
  EXPECT_EQ(
      result,
      "Deleting Arp entry ip: 10.100.30.1 interfaceID: 2030\n"
      "Deleting Arp entry ip: 10.100.31.1 interfaceID: 2031\n"
      "Deleting Arp entry ip: 10.100.100.1 interfaceID: 2900\n"
      "Flushed 3 entries\n"
      "Note: 2 unresolved (INCOMPLETE) Arp entries were also sent for flush; "
      "counted above only if programmed in hardware:\n"
      "  ip: 10.100.1.1 interfaceID: 2001\n"
      "  ip: 10.100.5.1 interfaceID: 2005");
}

TEST_F(CmdClearArpNdpTestFixture, arpSingleEntryIsSingular) {
  std::vector<ArpEntryThrift> arpEntries = {
      makeEntry<ArpEntryThrift>("10.100.30.1", 2030, "REACHABLE"),
      makeEntry<ArpEntryThrift>("10.100.31.1", 2031, "REACHABLE"),
  };
  EXPECT_CALL(getMockAgent(), getArpTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries = arpEntries; }));
  size_t numSent = 0;
  expectFlush(1, &numSent);

  auto result = CmdClearArp().queryClient(
      localhost(), std::vector<std::string>{"10.100.30.0/31"});

  EXPECT_EQ(numSent, 1);
  EXPECT_EQ(
      result,
      "Deleting Arp entry ip: 10.100.30.1 interfaceID: 2030\n"
      "Flushed 1 entry");
}

TEST_F(CmdClearArpNdpTestFixture, ndpSingleIncomplete) {
  std::vector<NdpEntryThrift> ndpEntries = {
      makeEntry<NdpEntryThrift>("fd00:100:5::1", 2005, "INCOMPLETE"),
  };
  EXPECT_CALL(getMockAgent(), getNdpTable(_))
      .WillOnce(Invoke([&](auto& entries) { entries = ndpEntries; }));
  size_t numSent = 0;
  expectFlush(0, &numSent);

  auto result = CmdClearNdp().queryClient(
      localhost(), std::vector<std::string>{"fd00:100:5::/127"});

  EXPECT_EQ(numSent, 1);
  EXPECT_EQ(
      result,
      "Flushed 0 entries\n"
      "Note: 1 unresolved (INCOMPLETE) Ndp entry was also sent for flush; "
      "counted above only if programmed in hardware:\n"
      "  ip: fd00:100:5::1 interfaceID: 2005");
}

} // namespace facebook::fboss
