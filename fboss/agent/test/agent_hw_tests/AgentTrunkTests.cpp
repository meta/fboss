// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/Utils.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/TrunkTestUtils.h"

#include <fmt/format.h>
#include <folly/container/F14Map.h>
#include <gtest/gtest.h>

using namespace ::testing;

namespace facebook::fboss {
class AgentTrunkTest : public AgentHwTest {
 protected:
  AggregatePortID kAggPort1() const {
    return AggregatePortID(1);
  }
  AggregatePortID kAggPortMax() const {
    return AggregatePortID(std::numeric_limits<AggregatePortID>::max());
  }
  std::vector<PortID> getAggPortMembers(
      AggregatePortID aggPort,
      const AgentEnsemble& ensemble) const {
    auto ports = ensemble.masterLogicalPortIds();
    if (aggPort == kAggPort1()) {
      return {ports[0], ports[1]};
    }
    CHECK(aggPort == kAggPortMax());
    return {ports[2], ports[3]};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    auto ports = ensemble.masterLogicalPortIds();
    return utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getPlatformMapping(),
        asic,
        {ports[0], ports[1], ports[2], ports[3]},
        ensemble.getSw()->getPlatformSupportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        {{kAggPort1(), getAggPortMembers(kAggPort1(), ensemble)},
         {kAggPortMax(), getAggPortMembers(kAggPortMax(), ensemble)}},
        cfg::InterfaceType::VLAN);
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::LAG};
  }

  void applyConfigAndEnableTrunks(const cfg::SwitchConfig& config) {
    applyNewConfig(config);
    applyNewState(
        [](const std::shared_ptr<SwitchState> state) {
          return utility::enableTrunkPorts(state);
        },
        "enable trunk ports");
  }
};

TEST_F(AgentTrunkTest, TrunkCreateHighLowKeyIds) {
  auto setup = [=, this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
  };
  auto verify = [=, this]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getAggregatePortCount(*getAgentEnsemble()), 2);
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyAggregatePort(*getAgentEnsemble(), kAggPort1()));
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyAggregatePort(*getAgentEnsemble(), kAggPortMax()));
      auto aggIDs = {kAggPort1(), kAggPortMax()};
      for (auto aggId : aggIDs) {
        EXPECT_EVENTUALLY_TRUE(
            utility::verifyAggregatePortMemberCount(
                *getAgentEnsemble(), aggId, 2));
      }
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTrunkTest, TrunkCheckIngressPktAggPort) {
  auto setup = [=, this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
    for (auto member : getAggPortMembers(kAggPort1(), *getAgentEnsemble())) {
      bringDownPort(member);
    }
  };
  auto verify = [=, this]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyPktFromAggregatePort(
              *getAgentEnsemble(), kAggPortMax()));
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTrunkTest, TrunkMemberPortDownMinLinksViolated) {
  auto setup = [=, this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
    for (auto member : getAggPortMembers(kAggPort1(), *getAgentEnsemble())) {
      bringDownPort(member);
    }
  };
  auto verify = [=, this]() {
    // Member port count should drop to 1 now.
    bringDownPort(getAggPortMembers(kAggPortMax(), *getAgentEnsemble())[0]);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getAggregatePortCount(*getAgentEnsemble()), 2);
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyAggregatePort(*getAgentEnsemble(), kAggPortMax()));
      EXPECT_EVENTUALLY_TRUE(
          utility::verifyAggregatePortMemberCount(
              *getAgentEnsemble(), kAggPortMax(), 1));
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTrunkTest, TrunkPortStats) {
  auto setup = [=, this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
    auto ecmpHelper = utility::EcmpSetupTargetedPorts6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper.resolveNextHops(in, {PortDescriptor(kAggPort1())});
        },
        "resolve next hops");
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&routeUpdater, {PortDescriptor(kAggPort1())});
    bringDownPort(getAggPortMembers(kAggPortMax(), *getAgentEnsemble())[0]);
  };
  auto verify = [=, this]() {
    const std::string kTrunkName = "AGG-1";
    for (auto throughPort : {false, true}) {
      // Tag for the ingress port's own interface: out-of-port injection on
      // Agg-max member, the trunk member's interface for switched packets.
      auto injectPort = throughPort
          ? getAggPortMembers(kAggPortMax(), *getAgentEnsemble())[1]
          : getAggPortMembers(kAggPort1(), *getAgentEnsemble())[1];
      auto state = getProgrammedState();
      auto intf = state->getInterfaces()->getNodeIf(
          getInterfaceIDForPort(injectPort, state));
      CHECK(intf != nullptr);
      auto vlanId = getSw()->getVlanIDForTx(intf);
      auto intfMac = intf->getMac();
      auto portPkts0 = int64_t{0};
      for (auto member : getAggPortMembers(kAggPort1(), *getAgentEnsemble())) {
        auto portStats = getLatestPortStats(member);
        portPkts0 += *portStats.outUnicastPkts_() +
            *portStats.outMulticastPkts_() + *portStats.outBroadcastPkts_();
      }

      auto getTrunkOutPkts = [&](const std::string& trunkName) -> int64_t {
        auto hwStats = getAllHwSwitchStats();
        for (const auto& [_, switchStats] : hwStats) {
          auto it = switchStats.hwTrunkStats()->find(trunkName);
          if (it != switchStats.hwTrunkStats()->end()) {
            return *it->second.outUnicastPkts_() +
                *it->second.outMulticastPkts_() +
                *it->second.outBroadcastPkts_();
          }
        }
        return 0;
      };

      int64_t trunkPkts0 = getTrunkOutPkts(kTrunkName);

      auto pkt = utility::makeUDPTxPacket(
          getSw(),
          vlanId,
          intfMac,
          intfMac,
          folly::IPAddress("2401::1"),
          folly::IPAddress("2401::2"),
          10001,
          20001);
      throughPort
          ? getAgentEnsemble()->ensureSendPacketOutOfPort(
                std::move(pkt),
                getAggPortMembers(kAggPortMax(), *getAgentEnsemble())[1])
          : getAgentEnsemble()->ensureSendPacketSwitched(std::move(pkt));

      auto portPkts1 = int64_t{0};
      for (auto member : getAggPortMembers(kAggPort1(), *getAgentEnsemble())) {
        auto portStats = getLatestPortStats(member);
        portPkts1 += *portStats.outUnicastPkts_() +
            *portStats.outMulticastPkts_() + *portStats.outBroadcastPkts_();
      }

      int64_t trunkPkts1 = getTrunkOutPkts(kTrunkName);

      EXPECT_GT(portPkts1, portPkts0);
      EXPECT_GT(trunkPkts1, trunkPkts0);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}
TEST_F(AgentTrunkTest, TrunkCapacityUpdatesOnMemberDown) {
  auto setup = [=, this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
    for (auto member : getAggPortMembers(kAggPortMax(), *getAgentEnsemble())) {
      bringDownPort(member);
    }
  };
  auto verify = [=, this]() {
    const std::string kTrunkName = "AGG-1";

    auto getTrunkCapacity = [&]() -> int64_t {
      auto hwStats = getAllHwSwitchStats();
      for (const auto& [_, switchStats] : hwStats) {
        auto it = switchStats.hwTrunkStats()->find(kTrunkName);
        if (it != switchStats.hwTrunkStats()->end()) {
          return *it->second.capacity_();
        }
      }
      return -1;
    };

    auto state = getProgrammedState();
    auto member0Speed = static_cast<int64_t>(
        state->getPorts()
            ->getNodeIf(getAggPortMembers(kAggPort1(), *getAgentEnsemble())[0])
            ->getSpeed());
    auto member1Speed = static_cast<int64_t>(
        state->getPorts()
            ->getNodeIf(getAggPortMembers(kAggPort1(), *getAgentEnsemble())[1])
            ->getSpeed());

    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(getTrunkCapacity(), member0Speed + member1Speed);
    });

    bringDownPort(getAggPortMembers(kAggPort1(), *getAgentEnsemble())[0]);

    WITH_RETRIES({ EXPECT_EVENTUALLY_EQ(getTrunkCapacity(), member1Speed); });
    this->applyNewState(
        [](const std::shared_ptr<SwitchState> state) {
          return utility::disableTrunkPorts(state);
        },
        "disable trunk ports to sync with LACP state");

    WITH_RETRIES({ EXPECT_EVENTUALLY_EQ(getTrunkCapacity(), 0); });
    bringUpPort(getAggPortMembers(kAggPort1(), *getAgentEnsemble())[0]);
    applyNewState(
        [](const std::shared_ptr<SwitchState>& state) {
          return utility::enableTrunkPorts(state);
        },
        "re-enable trunk member port");

    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(getTrunkCapacity(), member0Speed + member1Speed);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Long haul port speed layout on morgan800cc: OSFPs 1-32 run four 200G ports
// each (eth1/N/{1,3,5,7}, two lanes per port) and OSFPs 33-64 run one 800G
// port each (eth1/N/1). eth1/N/{1,3,5} of every 200G OSFP form a LAG and
// eth1/N/7 stays standalone. Everything is programmed in the initial config,
// so no port is recreated by a profile change. The test verifies that all
// ports and LAGs come up.
class AgentTrunkMixedSpeedTest : public AgentTrunkTest {
 protected:
  static constexpr int kNum200gOsfps = 32;
  static constexpr int kNumOsfps = 64;
  static constexpr uint32_t kNumLagMembers = 3;
  static constexpr auto k200gProfile =
      cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL;
  static constexpr auto k800gProfile =
      cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL;

  struct PortLayout {
    std::vector<PortID> ports200g;
    std::vector<PortID> ports800g;
    std::vector<utility::AggregatePortInfo> aggPorts;
  };

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  // Resolves the layout by port name. Returns nullopt when the platform
  // mapping lacks these ports or profiles.
  static std::optional<PortLayout> getPortLayout(
      const AgentEnsemble& ensemble) {
    const auto& platformPorts =
        ensemble.getPlatformMapping()->getPlatformPorts();
    folly::F14FastMap<std::string, int32_t> nameToPort;
    for (const auto& [portId, entry] : platformPorts) {
      nameToPort.emplace(*entry.mapping()->name(), portId);
    }
    auto findPort = [&](int osfp,
                        int subport,
                        cfg::PortProfileID profile) -> std::optional<PortID> {
      auto it = nameToPort.find(fmt::format("eth1/{}/{}", osfp, subport));
      if (it == nameToPort.end() ||
          !platformPorts.at(it->second)
               .supportedProfiles()
               ->contains(profile)) {
        return std::nullopt;
      }
      return PortID(it->second);
    };

    PortLayout layout;
    for (int osfp = 1; osfp <= kNum200gOsfps; ++osfp) {
      std::vector<PortID> members;
      for (int subport : {1, 3, 5, 7}) {
        auto port = findPort(osfp, subport, k200gProfile);
        if (!port) {
          return std::nullopt;
        }
        layout.ports200g.push_back(*port);
        if (subport != 7) {
          members.push_back(*port);
        }
      }
      layout.aggPorts.push_back({AggregatePortID(osfp), std::move(members)});
    }
    for (int osfp = kNum200gOsfps + 1; osfp <= kNumOsfps; ++osfp) {
      auto port = findPort(osfp, 1, k800gProfile);
      if (!port) {
        return std::nullopt;
      }
      layout.ports800g.push_back(*port);
    }
    return layout;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto layout = getPortLayout(ensemble);
    if (!layout) {
      // The test skips on platforms without this layout.
      return AgentHwTest::initialConfig(ensemble);
    }
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    auto supportsAddRemovePort =
        ensemble.getSw()->getPlatformSupportsAddRemovePort();
    auto ports = layout->ports200g;
    ports.insert(
        ports.end(), layout->ports800g.begin(), layout->ports800g.end());
    auto config = utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getPlatformMapping(),
        asic,
        ports,
        supportsAddRemovePort,
        asic->desiredLoopbackModes(),
        layout->aggPorts,
        cfg::InterfaceType::VLAN);
    auto setProfile = [&](PortID port, cfg::PortProfileID profile) {
      utility::configurePortProfile(
          ensemble.getPlatformMapping(),
          supportsAddRemovePort,
          config,
          profile,
          {port},
          port);
    };
    for (const auto& port : layout->ports200g) {
      setProfile(port, k200gProfile);
    }
    for (const auto& port : layout->ports800g) {
      setProfile(port, k800gProfile);
    }
    return config;
  }

  static std::optional<int64_t> getLagCapacity(
      const std::map<uint16_t, multiswitch::HwSwitchStats>& hwStats,
      const std::string& lagName) {
    for (const auto& [_, switchStats] : hwStats) {
      auto it = switchStats.hwTrunkStats()->find(lagName);
      if (it != switchStats.hwTrunkStats()->end()) {
        return *it->second.capacity_();
      }
    }
    return std::nullopt;
  }

  void verifyPortsUp(const PortLayout& layout) const {
    WITH_RETRIES({
      auto state = getProgrammedState();
      auto isUp = [&](PortID portId) {
        auto port = state->getPorts()->getNodeIf(portId);
        return port && port->isUp();
      };
      for (auto portId : layout.ports200g) {
        EXPECT_EVENTUALLY_TRUE(isUp(portId)) << "Port " << portId << " not up";
      }
      for (auto portId : layout.ports800g) {
        EXPECT_EVENTUALLY_TRUE(isUp(portId)) << "Port " << portId << " not up";
      }
    });
  }

  // A LAG is up when it is up in the switch state and its HW capacity covers
  // all members. HW capacity only counts members that are link up and
  // forwarding, while the state only tracks the forwarding flag.
  void verifyLagsUp(const PortLayout& layout) const {
    const auto expectedCapacity =
        kNumLagMembers * static_cast<int64_t>(cfg::PortSpeed::TWOHUNDREDG);
    WITH_RETRIES({
      auto state = getProgrammedState();
      auto hwStats = getAllHwSwitchStats();
      for (const auto& aggInfo : layout.aggPorts) {
        auto aggPort = state->getAggregatePorts()->getNodeIf(aggInfo.id);
        auto lagName = fmt::format("AGG-{}", static_cast<int>(aggInfo.id));
        EXPECT_EVENTUALLY_TRUE(aggPort && aggPort->isUp()) << lagName;
        EXPECT_EVENTUALLY_EQ(getLagCapacity(hwStats, lagName), expectedCapacity)
            << lagName;
      }
    });
  }
};

TEST_F(AgentTrunkMixedSpeedTest, LongHaulPortSpeedsAndLags) {
  auto layout = getPortLayout(*getAgentEnsemble());
  if (!layout) {
    GTEST_SKIP() << "Platform mapping lacks the long haul port layout";
  }
  auto setup = [this]() {
    applyConfigAndEnableTrunks(initialConfig(*getAgentEnsemble()));
  };
  auto verify = [this, layout]() {
    verifyPortsUp(*layout);
    verifyLagsUp(*layout);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
