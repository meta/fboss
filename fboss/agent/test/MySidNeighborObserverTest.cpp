// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <atomic>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/ndp/IPv6RouteAdvertiser.h"

using namespace facebook::fboss;

class MySidNeighborObserverTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    auto config = testConfigA();
    // Set ingressVlan on port1 so adjacency entries can resolve
    config.ports()[0].ingressVlan() = 1;
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    baseConfig_ = config;
  }

  void TearDown() override {
    FLAGS_enable_nexthop_id_manager = false;
  }

  void applyConfig(const cfg::SwitchConfig& config) {
    sw_->applyConfig("test", config);
  }

  std::shared_ptr<MySid> getMySid(const std::string& key) {
    auto mySids = sw_->getState()->getMySids();
    for (const auto& [_, mySidMap] : std::as_const(*mySids)) {
      auto node = mySidMap->getNodeIf(key);
      if (node) {
        return node;
      }
    }
    return nullptr;
  }

  // Add a config uA MySid entry via config apply
  void addConfigAdjacencyMySid(
      const std::string& locator = "3001:db8::/32",
      int16_t funcId = 1,
      const std::string& portName = "port1",
      bool isV6 = true) {
    auto config = baseConfig_;
    cfg::MySidConfig mySidConfig;
    mySidConfig.locatorPrefix() = locator;
    cfg::MySidEntryConfig entry;
    cfg::AdjacencyMySidConfig adjConfig;
    adjConfig.portName() = portName;
    adjConfig.isV6() = isV6;
    entry.adjacency() = adjConfig;
    mySidConfig.entries()[funcId] = entry;
    config.mySidConfig() = mySidConfig;
    applyConfig(config);
  }

  // Resolve an NDP neighbor via NeighborUpdater (triggers state observer)
  void resolveNdpNeighbor(
      const std::string& ip,
      InterfaceID intfId,
      const std::string& mac = "00:11:22:33:44:55") {
    sw_->getNeighborUpdater()->receivedNdpMineForIntf(
        intfId,
        folly::IPAddressV6(ip),
        folly::MacAddress(mac),
        PortDescriptor(PortID(1)),
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION,
        0);
    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForStateUpdates(sw_);
  }

  // Poll until the MySid at `key` reaches the expected resolution state.
  // Replaces a fixed multi-step thread drain (which silently goes from "fails
  // deterministically" to "races" any time the resolution pipeline gains an
  // extra hop) with condition-based waiting.
  void waitForMySidResolution(const std::string& key, bool resolved) {
    WITH_RETRIES({
      auto mySid = getMySid(key);
      ASSERT_EVENTUALLY_TRUE(mySid != nullptr);
      if (resolved) {
        ASSERT_EVENTUALLY_TRUE(mySid->getUnresolveNextHopsId().has_value());
      } else {
        ASSERT_EVENTUALLY_FALSE(mySid->getUnresolveNextHopsId().has_value());
      }
    });
  }

  SwSwitch* sw_{};
  std::unique_ptr<HwTestHandle> handle_;
  cfg::SwitchConfig baseConfig_;
};

TEST_F(MySidNeighborObserverTest, NewMySidWithExistingNeighbor) {
  // First resolve a neighbor, then add a uA MySid pointing to the interface
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));

  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  auto mySid = getMySid("3001:db8:1::/48");
  EXPECT_EQ(mySid->getType(), MySidType::ADJACENCY_MICRO_SID);
  EXPECT_EQ(mySid->getClientId(), ClientID::STATIC_ROUTE);
}

// Pin the same-id invariant: a bound uA MySid must have
// unresolveNextHopsId == resolvedNextHopsId. The observer constructs the
// resolve set with intf already populated (queueResolve →
// `ResolvedNextHop(neighborIP, intf)`), which makes
// RibMySidUpdater::resolveNhop short-circuit and lets NextHopIDManager's
// content-keyed lookup return the same id for both sides. If a future RIB
// change ever breaks this, MySidNeighborObserver::needsResolve will CHECK
// in production — this test catches that drift in CI first.
TEST_F(MySidNeighborObserverTest, BoundMySidHasSameUnresolvedAndResolvedId) {
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  auto mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  ASSERT_TRUE(mySid->getUnresolveNextHopsId().has_value());
  ASSERT_TRUE(mySid->getResolvedNextHopsId().has_value());
  EXPECT_EQ(*mySid->getUnresolveNextHopsId(), *mySid->getResolvedNextHopsId());
}

TEST_F(MySidNeighborObserverTest, NewMySidWithoutNeighbor) {
  // Add uA MySid when no neighbor exists on the interface
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);

  auto mySid = getMySid("3001:db8:1::/48");
  EXPECT_EQ(mySid->getType(), MySidType::ADJACENCY_MICRO_SID);
}

TEST_F(MySidNeighborObserverTest, NeighborAddedResolvesMySid) {
  // First add uA MySid (no neighbor yet)
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);

  // Now add a neighbor — observer should resolve the MySid
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
}

TEST_F(MySidNeighborObserverTest, LinkLocalNeighborResolved) {
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);

  // Add a link-local neighbor — observer should resolve the MySid using
  // the neighbor's link-local IP scoped by the MySid's adjacency intf.
  resolveNdpNeighbor("fe80::1", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  // Deep-verify the resolved nhop carries the expected LL IP scoped by
  // the MySid's adjacencyInterfaceId. The intf scoping proves the
  // RouteNextHop link-local-needs-interface invariant (RouteNextHop.cpp)
  // is satisfied — without it, a bare LL UnresolvedNextHop would throw.
  auto mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  ASSERT_TRUE(mySid->getAdjacencyInterfaceId().has_value());
  EXPECT_EQ(*mySid->getAdjacencyInterfaceId(), 1);
  ASSERT_TRUE(mySid->getResolvedNextHopsId().has_value());
  auto nhopMgr = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(nhopMgr, nullptr);
  auto resolvedSet = nhopMgr->getNextHops(*mySid->getResolvedNextHopsId());
  ASSERT_EQ(resolvedSet.size(), 1);
  const auto& nhop = *resolvedSet.begin();
  EXPECT_EQ(nhop.addr(), folly::IPAddress("fe80::1"));
  ASSERT_TRUE(nhop.intfID().has_value());
  EXPECT_EQ(*nhop.intfID(), InterfaceID(1));
}

TEST_F(MySidNeighborObserverTest, NeighborRemovedUnresolvesMySid) {
  // Add uA MySid and resolve a neighbor
  addConfigAdjacencyMySid();
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  // Remove the neighbor — observer should unresolve the MySid
  sw_->getNeighborUpdater()->flushEntryForIntf(
      InterfaceID(1), folly::IPAddress("2401:db00:2110:3001::2"));
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);
}

TEST_F(MySidNeighborObserverTest, RpcMySidNotAffectedByNeighbor) {
  // Add a dynamic MySid via ThriftHandler
  ThriftHandler handler(sw_);
  auto entries = std::make_unique<std::vector<MySidEntry>>();
  MySidEntry entry;
  entry.type() = MySidType::BINDING_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::1"));
  prefix.prefixLength() = 64;
  entry.mySid() = prefix;
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = "tunnel1";
  entry.nextHops() = {nhop};
  entries->push_back(entry);
  handler.addMySidEntries(std::move(entries));

  // Neighbor changes should NOT affect this RPC-owned entry
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  WITH_RETRIES({
    auto mySid = getMySid("2001:db8::1/64");
    ASSERT_EVENTUALLY_TRUE(mySid != nullptr);
    EXPECT_EVENTUALLY_EQ(mySid->getClientId(), ClientID::TE_AGENT);
  });
}

TEST_F(MySidNeighborObserverTest, AdjacencyChangedToDecapClearsResolution) {
  // Add adjacency entry and resolve it
  addConfigAdjacencyMySid();
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  // Change the same function ID from adjacency to decap (no
  // adjacencyInterfaceId)
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  entry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[1] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);

  auto mySid = getMySid("3001:db8:1::/48");
  EXPECT_EQ(mySid->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
}

TEST_F(MySidNeighborObserverTest, AdjacencyInterfaceIdChangedReResolves) {
  // Add adjacency on port1 (intfId=1) and resolve
  addConfigAdjacencyMySid();
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);

  // Change adjacencyInterfaceId to a different port (port5 → intfId=55)
  // Interface 55 exists in testConfigA but has no neighbors
  auto config = baseConfig_;
  config.ports()[0].ingressVlan() = 1;
  config.ports()[4].ingressVlan() = 55;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "port5";
  entry.adjacency() = adjConfig;
  mySidConfig.entries()[1] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  // No neighbor on port5/intf55 — should be unresolved
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);
}

// Regression: an RPC-owned (TE_AGENT) MySid added with explicit next hops
// must keep those next hops. The observer's MySid-delta path must not
// queue an unresolve for non-STATIC_ROUTE entries — otherwise it would
// wipe the TE_AGENT-provided unresolveNextHopsId via rib->updateAsync.
TEST_F(MySidNeighborObserverTest, RpcMySidWithNextHopsNotWipedByObserver) {
  // ThriftHandler rejects NODE_MICRO_SID entries, so call rib->update
  // directly with a NODE_MICRO_SID + next hops; mySidFromEntry produces a
  // MySid with the default clientId=TE_AGENT.
  auto* rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  std::vector<MySidEntry> entries;
  MySidEntry entry;
  entry.type() = MySidType::NODE_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::1"));
  prefix.prefixLength() = 64;
  entry.mySid() = prefix;
  NextHopThrift nh;
  nh.address() = facebook::network::toBinaryAddress(
      folly::IPAddress("2401:db00:2110:3001::2"));
  entry.nextHops() = {nh};
  entries.push_back(entry);

  rib->update(
      sw_->getScopeResolver(),
      entries,
      {} /* toDelete */,
      "RpcMySidWithNextHopsNotWipedByObserver",
      createRibMySidToSwitchStateFunction(std::nullopt),
      sw_);

  WITH_RETRIES({
    auto mySid = getMySid("2001:db8::1/64");
    ASSERT_EVENTUALLY_TRUE(mySid != nullptr);
    EXPECT_EVENTUALLY_EQ(mySid->getClientId(), ClientID::TE_AGENT);
    EXPECT_EVENTUALLY_TRUE(mySid->getUnresolveNextHopsId().has_value());
  });
}

// Warm-boot replay: every preserved MySid shows up as "added" in the first
// state delta after restart, with its unresolveNextHopsId carried over from
// before WB. The observer must skip re-resolution for these — otherwise it
// would dispatch a rib update per MySid on every WB.
TEST_F(MySidNeighborObserverTest, WarmBootReplaySkipsResolveForExistingEntry) {
  // Have a real neighbor on intf 1 so the observer would have something
  // valid to resolve to if it failed to skip.
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));

  // Sentinel id distinct from anything the rib would allocate naturally.
  // If the observer skips, the state keeps this id; if it re-resolves,
  // the rib's callback overwrites it with a freshly-allocated id.
  // Set both unresolveNextHopsId and resolvedNextHopsId to the same
  // value to match the post-resolve invariant for uA SIDs (see
  // MySidNeighborObserver::needsResolve).
  constexpr int64_t kPreservedNhopId = 99999;

  // Inject a uA mySid directly into SwitchState (bypassing the rib) as if
  // it had just been re-published from the WB-preserved state.
  sw_->updateState(
      "simulate-WB-mysid-add", [kPreservedNhopId](const auto& oldState) {
        auto newState = oldState->clone();
        auto mySids = newState->getMySids()->modify(&newState);

        state::MySidFields fields;
        fields.type() = MySidType::ADJACENCY_MICRO_SID;
        facebook::network::thrift::IPPrefix prefix;
        prefix.prefixAddress() = facebook::network::toBinaryAddress(
            folly::IPAddress("3001:db8:1::"));
        prefix.prefixLength() = 48;
        fields.mySid() = prefix;
        fields.adjacencyInterfaceId() = 1;
        fields.isV6() = true;
        fields.clientId() = ClientID::STATIC_ROUTE;
        fields.unresolveNextHopsId() = kPreservedNhopId;
        fields.resolvedNextHopsId() = kPreservedNhopId;

        auto mySid = std::make_shared<MySid>(fields);
        mySids->addNode(
            mySid, HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)}));
        return newState;
      });

  waitForBackgroundThread(sw_);
  waitForStateUpdates(sw_);

  auto mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  ASSERT_TRUE(mySid->getUnresolveNextHopsId().has_value());
  EXPECT_EQ(*mySid->getUnresolveNextHopsId(), kPreservedNhopId)
      << "Observer re-resolved a WB-preserved MySid; should have skipped";
}

// Two reachable neighbors on the same intf — MySid binds to one. Removing
// the OTHER neighbor must leave the MySid's binding alone (RIB's match
// check sees no match and no-ops).
TEST_F(
    MySidNeighborObserverTest,
    AddingSecondNeighborDoesNotRebindAlreadyResolvedMySid) {
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
  const auto idBefore = *getMySid("3001:db8:1::/48")->getUnresolveNextHopsId();

  // Add a second reachable neighbor — already-bound MySid stays put.
  resolveNdpNeighbor(
      "2401:db00:2110:3001::3", InterfaceID(1), "00:11:22:33:44:66" /* mac */);

  WITH_RETRIES({
    auto mySid = getMySid("3001:db8:1::/48");
    ASSERT_EVENTUALLY_TRUE(mySid != nullptr);
    ASSERT_EVENTUALLY_TRUE(mySid->getUnresolveNextHopsId().has_value());
    EXPECT_EVENTUALLY_EQ(*mySid->getUnresolveNextHopsId(), idBefore);
  });
}

TEST_F(MySidNeighborObserverTest, RemovingNonBoundNeighborLeavesMySidResolved) {
  // Bind MySid to the first reachable neighbor (X).
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
  const auto idBefore = *getMySid("3001:db8:1::/48")->getUnresolveNextHopsId();

  // Add a second neighbor (Y). MySid stays bound to X.
  resolveNdpNeighbor(
      "2401:db00:2110:3001::3", InterfaceID(1), "00:11:22:33:44:66" /* mac */);

  // Remove Y — RIB's match check on X.ip != Y.ip must no-op.
  sw_->getNeighborUpdater()->flushEntryForIntf(
      InterfaceID(1), folly::IPAddress("2401:db00:2110:3001::3"));

  // MySid id should still equal idBefore — no churn.
  WITH_RETRIES({
    auto mySid = getMySid("3001:db8:1::/48");
    ASSERT_EVENTUALLY_TRUE(mySid != nullptr);
    ASSERT_EVENTUALLY_TRUE(mySid->getUnresolveNextHopsId().has_value());
    EXPECT_EVENTUALLY_EQ(*mySid->getUnresolveNextHopsId(), idBefore);
  });
}

TEST_F(
    MySidNeighborObserverTest,
    RemovingBoundNeighborReResolvesToRemainingNeighbor) {
  // Bind MySid to X; also have Y reachable on the same intf.
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
  resolveNdpNeighbor(
      "2401:db00:2110:3001::3", InterfaceID(1), "00:11:22:33:44:66" /* mac */);

  // Remove X (the bound neighbor). 2-step convergence: pass 1 clears,
  // pass 2's MySid-changed handler picks up Y.
  sw_->getNeighborUpdater()->flushEntryForIntf(
      InterfaceID(1), folly::IPAddress("2401:db00:2110:3001::2"));

  // MySid eventually settles on a non-empty unresolveNextHopsId again.
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
}

TEST_F(
    MySidNeighborObserverTest,
    ReapplyIdenticalConfigDoesNotTouchBoundMySid) {
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
  const auto boundMySid = getMySid("3001:db8:1::/48");

  std::atomic<int> numMySidDeltas{0};
  WaitForSwitchState mySidDeltaCounter(
      sw_,
      [&numMySidDeltas](const StateDelta& delta) {
        if (!DeltaFunctions::isEmpty(delta.getMySidsDelta())) {
          ++numMySidDeltas;
        }
        return false;
      },
      "MySidDeltaCounter");
  addConfigAdjacencyMySid();
  waitForStateUpdates(sw_);
  sw_->getRib()->waitForRibUpdates();
  waitForStateUpdates(sw_);

  EXPECT_EQ(numMySidDeltas.load(), 0);
  EXPECT_EQ(getMySid("3001:db8:1::/48"), boundMySid);
}

TEST_F(
    MySidNeighborObserverTest,
    NeighborChangesStillHandledAfterIdenticalReapply) {
  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  addConfigAdjacencyMySid();
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
  addConfigAdjacencyMySid();

  sw_->getNeighborUpdater()->flushEntryForIntf(
      InterfaceID(1), folly::IPAddress("2401:db00:2110:3001::2"));
  waitForMySidResolution("3001:db8:1::/48", false /* unresolved */);

  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));
  waitForMySidResolution("3001:db8:1::/48", true /* resolved */);
}

TEST_F(MySidNeighborObserverTest, NeighborAddedResolvesUASidUnderEveryLocator) {
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "fdad:ffff::/32";
  mySidConfig.locatorPrefixes() = {"fdad:ffff::/32", "fdad:20d::/32"};
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "port1";
  mySidConfig.entries()[1].adjacency() = adjConfig;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  waitForMySidResolution("fdad:ffff:1::/48", false /* unresolved */);
  waitForMySidResolution("fdad:20d:1::/48", false /* unresolved */);

  resolveNdpNeighbor("2401:db00:2110:3001::2", InterfaceID(1));

  waitForMySidResolution("fdad:ffff:1::/48", true /* resolved */);
  waitForMySidResolution("fdad:20d:1::/48", true /* resolved */);
  EXPECT_EQ(
      getMySid("fdad:ffff:1::/48")->getResolvedNextHopsId(),
      getMySid("fdad:20d:1::/48")->getResolvedNextHopsId());
}
