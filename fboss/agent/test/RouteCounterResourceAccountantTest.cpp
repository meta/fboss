// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fmt/core.h>
#include <gtest/gtest.h>

#include "fboss/agent/RouteCounterResourceAccountant.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

class RouteCounterResourceAccountantTest : public ResourceAccountantTestBase {
 public:
  void SetUp() override {
    ResourceAccountantTestBase::SetUp();
    accountant_ =
        std::make_unique<RouteCounterResourceAccountant>(asicTable_.get());
  }

  std::unique_ptr<RouteCounterResourceAccountant> accountant_;
};

TEST_F(RouteCounterResourceAccountantTest, checkAndUpdateRouteCounterResource) {
  RouteNextHopSet nhops1{
      ResolvedNextHop(folly::IPAddress("1.1.1.1"), InterfaceID(1), ecmpWeight)};
  RouteNextHopSet nhops2{
      ResolvedNextHop(folly::IPAddress("2.2.2.2"), InterfaceID(2), ecmpWeight)};

  RouteCounterID counter1("route.counter.0");
  RouteCounterID counter2("route.counter.1");

  auto makeEntryWithCounter =
      [](const RouteNextHopSet& nhops,
         const std::optional<RouteCounterID>& counterID) {
        RouteNextHopEntry base(nhops, AdminDistance::EBGP);
        auto thrift = base.toThrift();
        if (counterID.has_value()) {
          thrift.counterID() = *counterID;
        }
        RouteNextHopEntry entry;
        entry.fromThrift(thrift);
        return entry;
      };

  // Route A with counter1
  auto entry1 = makeEntryWithCounter(nhops1, counter1);
  auto routeA =
      makeV6Route({folly::IPAddressV6("2401::1"), 128}, entry1, nhops1);
  accountant_->checkAndUpdateRouteCounterResource(routeA, true);
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 1);
  EXPECT_EQ(accountant_->routeCounterRefMap_[counter1], 1);

  // Route B also uses counter1 — refcount goes to 2
  auto entry2 = makeEntryWithCounter(nhops2, counter1);
  auto routeB =
      makeV6Route({folly::IPAddressV6("2401::2"), 128}, entry2, nhops2);
  accountant_->checkAndUpdateRouteCounterResource(routeB, true);
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 1);
  EXPECT_EQ(accountant_->routeCounterRefMap_[counter1], 2);

  // Route C with counter2
  auto entry3 = makeEntryWithCounter(nhops1, counter2);
  auto routeC =
      makeV6Route({folly::IPAddressV6("2401::3"), 128}, entry3, nhops1);
  accountant_->checkAndUpdateRouteCounterResource(routeC, true);
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 2);
  EXPECT_EQ(accountant_->routeCounterRefMap_[counter2], 1);

  // Remove route A — counter1 refcount drops to 1
  accountant_->checkAndUpdateRouteCounterResource(routeA, false);
  EXPECT_EQ(accountant_->routeCounterRefMap_[counter1], 1);

  // Remove route B — counter1 refcount drops to 0, entry erased
  accountant_->checkAndUpdateRouteCounterResource(routeB, false);
  EXPECT_EQ(accountant_->routeCounterRefMap_.count(counter1), 0);
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 1);

  // Route without counter — no-op
  auto entryNoCounter = makeEntryWithCounter(nhops1, std::nullopt);
  auto routeD =
      makeV6Route({folly::IPAddressV6("2401::4"), 128}, entryNoCounter, nhops1);
  accountant_->checkAndUpdateRouteCounterResource(routeD, true);
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 1);

  // Remove route C — counter2 erased, map empty
  accountant_->checkAndUpdateRouteCounterResource(routeC, false);
  EXPECT_TRUE(accountant_->routeCounterRefMap_.empty());
}

TEST_F(RouteCounterResourceAccountantTest, routeCounterResourceExceeded) {
  RouteNextHopSet nhops{
      ResolvedNextHop(folly::IPAddress("1.1.1.1"), InterfaceID(1), ecmpWeight)};

  auto makeEntryWithCounter = [](const RouteNextHopSet& nhops,
                                 const RouteCounterID& counterID) {
    RouteNextHopEntry base(nhops, AdminDistance::EBGP);
    auto thrift = base.toThrift();
    thrift.counterID() = counterID;
    RouteNextHopEntry entry;
    entry.fromThrift(thrift);
    return entry;
  };

  // Fill up to the ASIC limit (4096 unique counters for Yuba/G202x)
  // Use the ref map directly to simulate near-capacity
  for (uint32_t i = 0; i < 4096; i++) {
    accountant_->routeCounterRefMap_[fmt::format("counter.{}", i)] = 1;
  }

  // At 100% (intermediate) — should pass since we're at exactly the limit
  EXPECT_TRUE(
      accountant_->checkRouteCounterResource(true /* intermediateState */));

  // At 75% (final) — should fail since 4096 > 3072 (75% of 4096)
  EXPECT_FALSE(
      accountant_->checkRouteCounterResource(false /* intermediateState */));

  // Adding one more unique counter should fail intermediate check too
  auto entry = makeEntryWithCounter(nhops, "counter.4096");
  auto route = makeV6Route({folly::IPAddressV6("2401::1"), 128}, entry, nhops);
  EXPECT_FALSE(accountant_->checkAndUpdateRouteCounterResource(route, true));
  EXPECT_EQ(accountant_->routeCounterRefMap_.size(), 4097);

  // Adding a route that shares an existing counter should pass (no new counter)
  auto entryShared = makeEntryWithCounter(nhops, "counter.0");
  auto routeShared =
      makeV6Route({folly::IPAddressV6("2401::2"), 128}, entryShared, nhops);
  EXPECT_TRUE(
      accountant_->checkAndUpdateRouteCounterResource(routeShared, true));
  EXPECT_EQ(accountant_->routeCounterRefMap_["counter.0"], 2);
}

} // namespace facebook::fboss
