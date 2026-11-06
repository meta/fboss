// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/MySidMapUpdater.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(10)}};
}

std::map<int64_t, cfg::SwitchInfo> getTestSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> map;
  cfg::SwitchInfo info;
  info.switchType() = cfg::SwitchType::NPU;
  info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  info.switchIndex() = 0;
  map.emplace(10, info);
  return map;
}

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kSwitchIdScopeResolver(
      getTestSwitchInfo());
  return &kSwitchIdScopeResolver;
}

folly::CIDRNetworkV6 makeSidPrefix(
    const std::string& addr = "fc00:100::1",
    uint8_t len = 48) {
  return std::make_pair(folly::IPAddressV6(addr), len);
}

std::shared_ptr<MySid> makeMySid(
    const folly::CIDRNetworkV6& prefix = makeSidPrefix(),
    MySidType type = MySidType::NODE_MICRO_SID) {
  state::MySidFields fields;
  fields.type() = type;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(prefix.first);
  thriftPrefix.prefixLength() = prefix.second;
  fields.mySid() = thriftPrefix;
  auto mySid = std::make_shared<MySid>(fields);

  if (type == MySidType::NODE_MICRO_SID) {
    mySid->setUnresolveNextHopsId(
        std::optional<NextHopSetID>(NextHopSetID(100)));
    mySid->setResolvedNextHopsId(
        std::optional<NextHopSetID>(NextHopSetID(200)));
  } else {
    // DECAPSULATE_AND_LOOKUP: no next hops
    mySid->setUnresolveNextHopsId(std::nullopt);
    mySid->setResolvedNextHopsId(std::nullopt);
  }

  return mySid;
}

} // namespace

TEST(MySidMapUpdater, EmptyRibNoChange) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  MySidTable emptyTable;
  MySidMapUpdater updater(scopeResolver(), emptyTable);
  auto newState = updater(state);

  EXPECT_EQ(newState, state);
}

TEST(MySidMapUpdater, AddEntries) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySid1 = makeMySid(makeSidPrefix("fc00:200::1", 64));
  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;
  ribTable[makeSidPrefix("fc00:200::1", 64)] = mySid1;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST(MySidMapUpdater, NoChangeWhenSameEntries) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  state->publish();

  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  // Same pointer means no change
  EXPECT_EQ(newState, state);
}

TEST(MySidMapUpdater, NoChangeWhenEqualEntries) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  state->publish();

  // Create a different object with the same content
  auto mySid0Copy = makeMySid(makeSidPrefix("fc00:100::1", 48));
  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:100::1", 48)] = mySid0Copy;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  EXPECT_EQ(newState, state);
}

TEST(MySidMapUpdater, DetectModifiedEntry) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  state->publish();

  // Create updated entry with different type
  auto updatedMySid = makeMySid(
      makeSidPrefix("fc00:100::1", 48), MySidType::DECAPSULATE_AND_LOOKUP);
  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:100::1", 48)] = updatedMySid;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);
  auto result = newState->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
}

TEST(MySidMapUpdater, DetectDeletedEntries) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySid1 = makeMySid(makeSidPrefix("fc00:200::1", 64));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  mySids->addNode(mySid1, scope());
  state->publish();

  // RIB only has mySid0, mySid1 should be deleted
  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_EQ(newState->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST(MySidMapUpdater, DeleteAllEntries) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  state->publish();

  MySidTable emptyTable;
  MySidMapUpdater updater(scopeResolver(), emptyTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);
  EXPECT_EQ(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
}

TEST(MySidMapUpdater, MixedAddDeleteModify) {
  auto state = std::make_shared<SwitchState>();
  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySid1 = makeMySid(makeSidPrefix("fc00:200::1", 64));
  auto mySids = state->getMySids()->modify(&state);
  mySids->addNode(mySid0, scope());
  mySids->addNode(mySid1, scope());
  state->publish();

  // mySid0: deleted (not in RIB)
  // mySid1: modified (different type)
  // mySid2: added (new entry)
  auto updatedMySid1 = makeMySid(
      makeSidPrefix("fc00:200::1", 64), MySidType::DECAPSULATE_AND_LOOKUP);
  auto mySid2 = makeMySid(makeSidPrefix("fc00:300::1", 128));

  MySidTable ribTable;
  ribTable[makeSidPrefix("fc00:200::1", 64)] = updatedMySid1;
  ribTable[makeSidPrefix("fc00:300::1", 128)] = mySid2;

  MySidMapUpdater updater(scopeResolver(), ribTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);
  EXPECT_EQ(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  auto result1 = newState->getMySids()->getNodeIf("fc00:200::1/64");
  ASSERT_NE(result1, nullptr);
  EXPECT_EQ(result1->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:300::1/128"), nullptr);
}
