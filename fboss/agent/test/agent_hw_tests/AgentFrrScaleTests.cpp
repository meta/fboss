/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include <gtest/gtest.h>

namespace facebook::fboss {

/*
 * Scale shape for FRR protection groups with split horizon on.
 *
 * Every port is the primary of exactly one group, and a backup member of all
 * the others, so with N interface ports each group carries 1 primary and N-1
 * backups.
 * That is the widest fan-out the port count allows and the densest sharing of
 * next hops between groups, which is what makes it worth programming: the
 * backup sets overlap almost completely, so any per-group duplication in how
 * the SDK lays out secondary members shows up here and nowhere in the
 * functional tests, which use four to six members.
 *
 * No traffic is sent, so the loopback mode does not matter and the default MAC
 * loopback is used. That also lifts the 800G-only restriction the prune tests
 * carry, since that exists solely to make link state observable.
 */
class AgentFrrScaleTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ARS_SOURCE_PORT_PRUNE,
        ProductionFeature::ADJACENCY_FRR};
  }

 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_flowletSwitchingEnable = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentHwTest::initialConfig(ensemble);
    // Split horizon is CREATE_ONLY, so it has to be in the config the groups
    // are created from. Both FRR halves are named: ApplyThriftConfig rejects a
    // config that enables only one.
    cfg::EcmpGroupSettings settings;
    settings.enableSplitHorizon() = true;
    cfg.switchSettings()->ecmpGroupSettings() = {
        {cfg::EcmpGroupType::FRR_PRIMARY, settings},
        {cfg::EcmpGroupType::FRR_BACKUP, settings}};
    return cfg;
  }

  // AgentHwTest caps tests at kDefaultMaxTestInterfacePorts (8) front panel
  // ports. This test needs one per group, so opt out and take the whole set.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  std::vector<PortID> scalePorts() const {
    return masterLogicalInterfacePortIds();
  }

  // Group i: port[i] is the primary, every other port is a backup.
  void programFrrGroups() {
    const auto ports = scalePorts();
    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());

    boost::container::flat_set<PortDescriptor> allPorts;
    for (auto port : ports) {
      allPorts.emplace(PortDescriptor(port));
    }
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, allPorts);
    });

    const auto makeNextHop = [&ecmpHelper](PortID port, NextHopRole role) {
      return UnresolvedNextHop(
          ecmpHelper.ip(PortDescriptor(port)),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          {},
          std::nullopt,
          std::nullopt,
          std::nullopt,
          role);
    };

    auto routeUpdater = getSw()->getRouteUpdater();
    for (size_t i = 0; i < ports.size(); ++i) {
      RouteNextHopSet nextHops;
      nextHops.emplace(makeNextHop(ports[i], NextHopRole::PRIMARY));
      for (size_t j = 0; j < ports.size(); ++j) {
        if (j != i) {
          nextHops.emplace(makeNextHop(ports[j], NextHopRole::BACKUP));
        }
      }
      // A distinct /128 per group, so each gets its own protection group
      // rather than sharing one.
      routeUpdater.addRoute(
          RouterID(0),
          folly::IPAddressV6(folly::to<std::string>("2001::", i + 1)),
          128,
          ClientID::BGPD,
          RouteNextHopEntry(nextHops, AdminDistance::EBGP));
    }
    routeUpdater.program();
  }
};

// Programs the full set and checks it survives a warm boot. There is no traffic
// to assert on; what this catches is the agent or the SDK failing to lay out
// the protection pairs -- a resource exhaustion, a programming error, or a
// crash on replay. Route presence is asserted so a silently dropped update
// fails here rather than looking like a pass.
TEST_F(AgentFrrScaleTest, ProgramFrrGroupsWithSplitHorizon) {
  auto setup = [this]() { programFrrGroups(); };

  auto verify = [this]() {
    const auto state = getProgrammedState();
    const auto numGroups = scalePorts().size();
    size_t found = 0;
    for (size_t i = 0; i < numGroups; ++i) {
      const auto prefix =
          folly::IPAddressV6(folly::to<std::string>("2001::", i + 1));
      auto route =
          findRoute<folly::IPAddressV6>(RouterID(0), {prefix, 128}, state);
      if (route) {
        ++found;
      }
    }
    EXPECT_EQ(numGroups, found);
    XLOG(INFO) << "FRR scale: " << found << " of " << numGroups
               << " protection groups programmed, " << numGroups - 1
               << " backups each";
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
