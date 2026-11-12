// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

namespace {
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
constexpr auto kSpan = "local";
constexpr auto kErspan = "mirror0";
constexpr auto kSflow = "mirror1";
constexpr auto kDscpDefault =
    cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_;
using TestTypes = ::testing::Types<folly::IPAddressV4, folly::IPAddressV6>;

template <typename AddrT>
struct TestParams {
  std::array<AddrT, 4> ipAddrs;
  std::array<MacAddress, 4> macAddrs;
  TestParams();
};

template <>
TestParams<IPAddressV4>::TestParams()
    : ipAddrs{IPAddressV4("1.1.1.1"), IPAddressV4("1.1.1.10"), IPAddressV4("2.2.2.2"), IPAddressV4("2.2.2.10")},
      macAddrs{
          MacAddress("1:1:1:1:1:1"),
          MacAddress("1:1:1:1:1:10"),
          MacAddress("2:2:2:2:2:2"),
          MacAddress("2:2:2:2:2:10")} {}

template <>
TestParams<IPAddressV6>::TestParams()
    : ipAddrs{IPAddressV6("1::1"), IPAddressV6("1:1::1:10"), IPAddressV6("2::1"), IPAddressV6("2:2::2:10")},
      macAddrs{
          MacAddress("1:1:1:1:1:1"),
          MacAddress("1:1:1:1:1:10"),
          MacAddress("2:2:2:2:2:2"),
          MacAddress("2:2:2:2:2:10")} {}

// Flag values matching the thrift handler in HwTestMirrorUtilsThriftHandler.cpp
constexpr int32_t kMirrorPortIngress = 0x00000001;
constexpr int32_t kMirrorPortEgress = 0x00000002;
constexpr int32_t kMirrorPortSflow = 0x00000004;

} // namespace

template <typename AddrT>
class AgentHwMirrorTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
      return {
          ProductionFeature::INGRESS_MIRRORING,
          ProductionFeature::ERSPANV6_MIRRORING};
    }
    return {ProductionFeature::INGRESS_MIRRORING};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_check_wb_handles = false;
  }

  void updateMirror(const std::shared_ptr<Mirror>& mirror) {
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = in->clone();
      auto mnpuMirrors = in->getMirrors()->modify(&newState);
      mnpuMirrors->updateNode(mirror, scopeResolver().scope(mirror));
      return newState;
    });
  }

  void updateMirrors(const std::vector<std::shared_ptr<Mirror>>& mirrors) {
    for (const auto& mirror : mirrors) {
      updateMirror(mirror);
    }
  }

  cfg::Mirror getSpanMirror(
      const std::string& mirrorName = kSpan,
      const uint8_t dscp = kDscpDefault,
      const bool truncate = false) const {
    cfg::MirrorDestination destination;
    destination.egressPort() = cfg::MirrorEgressPort();
    destination.egressPort()->logicalID() = masterLogicalInterfacePortIds()[0];

    cfg::Mirror mirrorConfig;
    mirrorConfig.name() = mirrorName;
    mirrorConfig.destination() = destination;
    mirrorConfig.dscp() = dscp;
    mirrorConfig.truncate() = truncate;
    return mirrorConfig;
  }

  cfg::Mirror getErspanMirror(
      const std::string& mirrorName = kErspan,
      const uint8_t dscp = kDscpDefault) const {
    auto params = testParams();
    auto destinationIp = params.ipAddrs[1];

    cfg::MirrorDestination destination;
    cfg::MirrorTunnel tunnel;
    cfg::GreTunnel greTunnel;
    greTunnel.ip() = destinationIp.str();
    tunnel.greTunnel() = greTunnel;
    destination.tunnel() = tunnel;

    cfg::Mirror mirrorConfig;
    mirrorConfig.name() = mirrorName;
    mirrorConfig.destination() = destination;
    mirrorConfig.dscp() = dscp;
    return mirrorConfig;
  }

  cfg::Mirror getSflowMirror(
      const std::string& mirrorName = kSflow,
      const uint8_t dscp = kDscpDefault) const {
    auto params = testParams();
    auto destinationIp = params.ipAddrs[1];

    cfg::MirrorDestination destination;
    cfg::MirrorTunnel tunnel;
    cfg::SflowTunnel sflowTunnel;
    sflowTunnel.ip() = destinationIp.str();
    sflowTunnel.udpSrcPort() = 6545;
    sflowTunnel.udpDstPort() = 6343;
    tunnel.sflowTunnel() = sflowTunnel;
    destination.tunnel() = tunnel;

    cfg::Mirror mirrorConfig;
    mirrorConfig.name() = mirrorName;
    mirrorConfig.destination() = destination;
    mirrorConfig.dscp() = dscp;
    mirrorConfig.truncate() = true;
    return mirrorConfig;
  }

  TestParams<AddrT> testParams() const {
    return TestParams<AddrT>();
  }

  SwitchID getSwitchId() {
    return scopeResolver().scope(masterLogicalInterfacePortIds()[0]).switchId();
  }

  auto getClient() {
    return getAgentEnsemble()->getHwAgentTestClient(getSwitchId());
  }

  // Other switches' copies may stay unresolved on multi-NPU.
  std::vector<std::shared_ptr<Mirror>> getMirrorsOnSwitchUnderTest() {
    std::vector<std::shared_ptr<Mirror>> result;
    auto mirrors = this->getProgrammedState()->getMirrors()->getMapNodeIf(
        HwSwitchMatcher(std::unordered_set<SwitchID>({getSwitchId()})));
    if (mirrors) {
      for (const auto& [_, mirror] : std::as_const(*mirrors)) {
        result.push_back(mirror);
      }
    }
    return result;
  }

  std::shared_ptr<Mirror> getMirrorOnSwitchUnderTest(
      const std::string& mirrorName) {
    auto mirrors = this->getProgrammedState()->getMirrors()->getMapNodeIf(
        HwSwitchMatcher(std::unordered_set<SwitchID>({getSwitchId()})));
    return mirrors ? mirrors->getNodeIf(mirrorName) : nullptr;
  }

  std::optional<HwResourceStats> getMirrorStats() {
    try {
      auto switchIndex =
          getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
              getSwitchId());
      auto stats = getSw()->getHwSwitchStatsExpensive(switchIndex);
      return *(stats.hwResourceStats());
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  void programMirrorRoute(
      utility::EcmpSetupAnyNPorts<AddrT>& ecmpHelper,
      const folly::IPAddress& dip,
      const PortDescriptor& mirrorToPort) {
    auto nhopPorts = boost::container::flat_set<PortDescriptor>{mirrorToPort};
    auto wrapper = getSw()->getRouteUpdater();
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ecmpHelper.programRoutes(
          &wrapper, nhopPorts, {RoutePrefixV4{dip.asV4(), 32}});
    } else {
      ecmpHelper.programRoutes(
          &wrapper, nhopPorts, {RoutePrefixV6{dip.asV6(), 128}});
    }
  }

  std::set<cfg::PortType> getEcmpPortTypes() const {
    if (FLAGS_hyper_port) {
      return {cfg::PortType::HYPER_PORT};
    }
    return {cfg::PortType::INTERFACE_PORT};
  }

  void resolveMirror(const std::string& mirrorName, PortID mirrorToPort) {
    resolveMirror(mirrorName, PortDescriptor(mirrorToPort));
  }

  void resolveMirror(
      const std::string& mirrorName,
      const PortDescriptor& mirrorToPort) {
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        RouterID(0),
        getEcmpPortTypes());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(
          in, boost::container::flat_set<PortDescriptor>{mirrorToPort});
    });
    auto mirror =
        this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
    if (mirror && mirror->getDestinationIp().has_value()) {
      programMirrorRoute(
          ecmpHelper, mirror->getDestinationIp().value(), mirrorToPort);
    }
  }

  void resolveAllMirrors(PortID mirrorToPort) {
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        RouterID(0),
        getEcmpPortTypes());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(
          in,
          boost::container::flat_set<PortDescriptor>{
              PortDescriptor(mirrorToPort)});
    });
    auto mirrors = this->getProgrammedState()->getMirrors();
    for (auto mniter = mirrors->cbegin(); mniter != mirrors->cend(); ++mniter) {
      for (auto iter : std::as_const(*mniter->second)) {
        auto mirror = iter.second;
        if (mirror->getDestinationIp().has_value()) {
          programMirrorRoute(
              ecmpHelper,
              mirror->getDestinationIp().value(),
              PortDescriptor(mirrorToPort));
        }
      }
    }
  }

  void unresolveMirror(const std::string& mirrorName) {
    auto mirror =
        this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
    if (mirror && mirror->getDestinationIp().has_value()) {
      utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
          getProgrammedState(), getSw()->needL2EntryForNeighbor(), RouterID(0));
      auto wrapper = getSw()->getRouteUpdater();
      auto dip = mirror->getDestinationIp().value();
      if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
        ecmpHelper.unprogramRoutes(&wrapper, {RoutePrefixV4{dip.asV4(), 32}});
      } else {
        ecmpHelper.unprogramRoutes(&wrapper, {RoutePrefixV6{dip.asV6(), 128}});
      }
    }
  }

  static int32_t getMirrorPortIngressFlags() {
    return kMirrorPortIngress;
  }

  static int32_t getMirrorPortEgressFlags() {
    return kMirrorPortEgress;
  }

  static int32_t getMirrorPortIngressAndSflowFlags() {
    return kMirrorPortIngress | kMirrorPortSflow;
  }
};

TYPED_TEST_SUITE(AgentHwMirrorTest, TestTypes);

template <typename AddrT>
class AgentHwSflowMirrorTest : public AgentHwMirrorTest<AddrT> {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
      return {
          ProductionFeature::INGRESS_MIRRORING,
          ProductionFeature::ERSPANV6_MIRRORING,
          ProductionFeature::SFLOWv6_SAMPLING};
    }
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::SFLOWv4_SAMPLING};
  }
};

TYPED_TEST_SUITE(AgentHwSflowMirrorTest, TestTypes);

template <typename AddrT>
class AgentHwMirrorTrunkTest : public AgentHwMirrorTest<AddrT> {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        {utility::AggregatePortInfo(
            AggregatePortID(1),
            {ensemble.masterLogicalInterfacePortIds()[0]})});
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
      return {
          ProductionFeature::INGRESS_MIRRORING,
          ProductionFeature::ERSPANV6_MIRRORING,
          ProductionFeature::LAG_MIRRORING};
    }
    return {
        ProductionFeature::INGRESS_MIRRORING, ProductionFeature::LAG_MIRRORING};
  }
};

TYPED_TEST_SUITE(AgentHwMirrorTrunkTest, TestTypes);

TYPED_TEST(AgentHwMirrorTest, UnresolvedToUnresolvedUpdate) {
  auto setup = [=, this]() {
    auto params = this->testParams();
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSpanMirror());
    cfg.mirrors()->push_back(this->getErspanMirror());
    this->applyNewConfig(cfg);
    auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
    auto newMirror = std::make_shared<Mirror>(
        mirror->getID(),
        mirror->getEgressPortDesc(),
        std::optional<folly::IPAddress>(folly::IPAddress(params.ipAddrs[3])));
    this->updateMirror(newMirror);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    auto local = this->getProgrammedState()->getMirrors()->getNodeIf(kSpan);
    auto localFields = local->toThrift();
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(localFields));
    });
    auto erspan = this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
    auto erspanFields = erspan->toThrift();
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyUnResolvedMirror(erspanFields));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, ResolvedToResolvedUpdate) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getErspanMirror());
    this->applyNewConfig(cfg);
    this->resolveMirror(kErspan, this->masterLogicalInterfacePortIds()[0]);
    this->resolveMirror(kErspan, this->masterLogicalInterfacePortIds()[1]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, ResolvedToUnresolvedUpdate) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getErspanMirror());
    this->applyNewConfig(cfg);
    this->resolveMirror(kErspan, this->masterLogicalInterfacePortIds()[0]);
    this->unresolveMirror(kErspan);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyUnResolvedMirror(fields));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, NoPortMirroringIfUnResolved) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getErspanMirror());
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[0]);
    portCfg->ingressMirror() = kErspan;
    portCfg->egressMirror() = kErspan;
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
      EXPECT_EVENTUALLY_TRUE(mirror != nullptr);
      if (!mirror) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyUnResolvedMirror(fields));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[0]),
          this->getMirrorPortIngressFlags()));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[0]),
          this->getMirrorPortEgressFlags()));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, PortMirrorUpdateIfMirrorUpdate) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getErspanMirror());
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[0]);
    portCfg->ingressMirror() = kErspan;
    portCfg->egressMirror() = kErspan;
    this->applyNewConfig(cfg);
    this->resolveMirror(kErspan, this->masterLogicalInterfacePortIds()[1]);
    this->resolveMirror(kErspan, this->masterLogicalInterfacePortIds()[1]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    ASSERT_EQ(destinations.size(), 1);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[0]),
          this->getMirrorPortIngressFlags(),
          destinations[0]));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, HwMirrorStat) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSpanMirror());
    cfg.mirrors()->push_back(this->getErspanMirror());
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    WITH_RETRIES({
      auto mirrorStatsOpt = this->getMirrorStats();
      EXPECT_EVENTUALLY_TRUE(mirrorStatsOpt.has_value());
      if (!mirrorStatsOpt) {
        continue;
      }
      auto mirrorStats = mirrorStatsOpt.value();
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_used(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_span(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_erspan(), 0);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, HwResolvedMirrorStat) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->resize(3);
    cfg.mirrors()[0] = this->getSpanMirror();
    cfg.mirrors()[1] = this->getErspanMirror();
    cfg.mirrors()[2] = this->getSflowMirror();
    this->applyNewConfig(cfg);
    this->resolveAllMirrors(this->masterLogicalInterfacePortIds()[1]);
  };
  auto verify = [=, this]() {
    WITH_RETRIES({
      auto span = this->getMirrorOnSwitchUnderTest(kSpan);
      auto erspan = this->getMirrorOnSwitchUnderTest(kErspan);
      auto sflowMirror = this->getMirrorOnSwitchUnderTest(kSflow);
      ASSERT_EVENTUALLY_NE(span, nullptr);
      ASSERT_EVENTUALLY_NE(erspan, nullptr);
      ASSERT_EVENTUALLY_NE(sflowMirror, nullptr);
      EXPECT_EVENTUALLY_TRUE(span->isResolved());
      EXPECT_EVENTUALLY_TRUE(erspan->isResolved());
      EXPECT_EVENTUALLY_TRUE(sflowMirror->isResolved());
      auto mirrorStatsOpt = this->getMirrorStats();
      EXPECT_EVENTUALLY_TRUE(mirrorStatsOpt.has_value());
      if (!mirrorStatsOpt) {
        continue;
      }
      auto mirrorStats = mirrorStatsOpt.value();
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_used(), 3);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_span(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_erspan(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_sflow(), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, HwUnresolvedMirrorStat) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->resize(3);
    cfg.mirrors()[0] = this->getSpanMirror();
    cfg.mirrors()[1] = this->getErspanMirror();
    cfg.mirrors()[2] = this->getSflowMirror();
    this->applyNewConfig(cfg);
    this->resolveAllMirrors(this->masterLogicalInterfacePortIds()[1]);
    this->unresolveMirror(kErspan);
    this->unresolveMirror(kSflow);
  };
  auto verify = [=, this]() {
    WITH_RETRIES({
      auto span = this->getMirrorOnSwitchUnderTest(kSpan);
      auto erspan = this->getMirrorOnSwitchUnderTest(kErspan);
      auto sflowMirror = this->getMirrorOnSwitchUnderTest(kSflow);
      EXPECT_EVENTUALLY_TRUE(span && span->isResolved());
      EXPECT_EVENTUALLY_TRUE(erspan && !erspan->isResolved());
      EXPECT_EVENTUALLY_TRUE(sflowMirror && !sflowMirror->isResolved());
      auto mirrorStatsOpt = this->getMirrorStats();
      EXPECT_EVENTUALLY_TRUE(mirrorStatsOpt.has_value());
      if (!mirrorStatsOpt) {
        continue;
      }
      auto mirrorStats = mirrorStatsOpt.value();
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_used(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_span(), 1);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_erspan(), 0);
      EXPECT_EVENTUALLY_EQ(*mirrorStats.mirrors_sflow(), 0);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTest, HwMirrorLimitExceeded) {
  auto cfg = this->initialConfig(*this->getAgentEnsemble());
  this->applyNewConfig(cfg);

  cfg.mirrors()->resize(5);
  cfg.mirrors()[0] = this->getErspanMirror("mirror0");
  cfg.mirrors()[1] = this->getErspanMirror("mirror1");
  cfg.mirrors()[2] = this->getErspanMirror("mirror2");
  cfg.mirrors()[3] = this->getErspanMirror("mirror3");
  cfg.mirrors()[4] = this->getErspanMirror("mirror4");

  EXPECT_THROW(this->applyNewConfig(cfg), FbossError);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleOnePort) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSflowMirror());
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[1]);
    portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
    *portCfg->sFlowIngressRate() = 90000;
    this->applyNewConfig(cfg);
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    ASSERT_EQ(destinations.size(), 1);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[1]),
          this->getMirrorPortIngressAndSflowFlags(),
          destinations[0]));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleAllPorts) {
  /* Setup sample destination to mirror for all ports, and mirror only one port
  this will ensure all port traffic is sampled and sent to that mirror */
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    ASSERT_EQ(destinations.size(), 1);
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            destinations[0]));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SflowMirrorWithErspanMirror) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSflowMirror());
    cfg.mirrors()->push_back(this->getErspanMirror());

    for (auto i = 0; i < 2; i++) {
      auto portId = this->masterLogicalInterfacePortIds()[i];
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[2]);
    portCfg->ingressMirror() = *cfg.mirrors()[1].name();
    portCfg->egressMirror() = *cfg.mirrors()[1].name();
    this->applyNewConfig(cfg);
    // resolve both mirrors
    this->resolveAllMirrors(this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirrors = this->getMirrorsOnSwitchUnderTest();
      bool allResolved = !mirrors.empty();
      for (const auto& mirror : mirrors) {
        if (!mirror->isResolved()) {
          allResolved = false;
        }
      }
      EXPECT_EVENTUALLY_TRUE(allResolved);
      if (!allResolved) {
        continue;
      }
      for (const auto& mirror : mirrors) {
        EXPECT_EVENTUALLY_TRUE(
            client->sync_verifyResolvedMirror(mirror->toThrift()));
      }
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 2);
    });
    ASSERT_EQ(destinations.size(), 2);
    int64_t sflow = 0;
    int64_t erspan = 0;
    for (auto destination : destinations) {
      if (client->sync_isMirrorSflowTunnelEnabled(destination)) {
        sflow = destination;
      } else {
        erspan = destination;
      }
    }
    for (auto i = 0; i < 2; i++) {
      auto port = this->masterLogicalInterfacePortIds()[i];
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            sflow));
      });
    }
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortIngressFlags(),
          erspan));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortEgressFlags(),
          erspan));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SflowMirrorWithErspanMirrorOnePortSflow) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSflowMirror());
    cfg.mirrors()->push_back(this->getErspanMirror());

    for (auto i = 0; i < 2; i++) {
      auto portId = this->masterLogicalInterfacePortIds()[i];
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[2]);
    portCfg->ingressMirror() = *cfg.mirrors()[1].name();
    portCfg->egressMirror() = *cfg.mirrors()[1].name();
    this->applyNewConfig(cfg);
    // resolve both mirrors
    this->resolveAllMirrors(this->masterLogicalInterfacePortIds()[0]);

    portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[1]);
    portCfg->sampleDest() = cfg::SampleDestination::CPU;
    *portCfg->sFlowIngressRate() = 0;
    portCfg->ingressMirror().reset();
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirrors = this->getMirrorsOnSwitchUnderTest();
      bool allResolved = !mirrors.empty();
      for (const auto& mirror : mirrors) {
        if (!mirror->isResolved()) {
          allResolved = false;
        }
      }
      EXPECT_EVENTUALLY_TRUE(allResolved);
      if (!allResolved) {
        continue;
      }
      for (const auto& mirror : mirrors) {
        EXPECT_EVENTUALLY_TRUE(
            client->sync_verifyResolvedMirror(mirror->toThrift()));
      }
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 2);
    });
    ASSERT_EQ(destinations.size(), 2);
    int64_t sflow = 0;
    int64_t erspan = 0;
    for (auto destination : destinations) {
      if (client->sync_isMirrorSflowTunnelEnabled(destination)) {
        sflow = destination;
      } else {
        erspan = destination;
      }
    }
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[0]),
          this->getMirrorPortIngressAndSflowFlags(),
          sflow));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[1]),
          this->getMirrorPortIngressAndSflowFlags()));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortIngressFlags(),
          erspan));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortEgressFlags(),
          erspan));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SflowMirrorWithErspanMirrorNoPortSflow) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSflowMirror());
    cfg.mirrors()->push_back(this->getErspanMirror());

    for (auto i = 0; i < 2; i++) {
      auto portId = this->masterLogicalInterfacePortIds()[i];
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    auto portCfg =
        utility::findCfgPort(cfg, this->masterLogicalInterfacePortIds()[2]);
    portCfg->ingressMirror() = *cfg.mirrors()[1].name();
    portCfg->egressMirror() = *cfg.mirrors()[1].name();
    this->applyNewConfig(cfg);
    // resolve both mirrors
    this->resolveAllMirrors(this->masterLogicalInterfacePortIds()[0]);
    for (auto i = 0; i < 2; i++) {
      auto portId = this->masterLogicalInterfacePortIds()[i];
      auto portConfig = utility::findCfgPort(cfg, portId);
      portConfig->sampleDest() = cfg::SampleDestination::CPU;
      *portConfig->sFlowIngressRate() = 0;
      portConfig->ingressMirror().reset();
    }
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirrors = this->getMirrorsOnSwitchUnderTest();
      bool allResolved = !mirrors.empty();
      for (const auto& mirror : mirrors) {
        if (!mirror->isResolved()) {
          allResolved = false;
        }
      }
      EXPECT_EVENTUALLY_TRUE(allResolved);
      if (!allResolved) {
        continue;
      }
      for (const auto& mirror : mirrors) {
        EXPECT_EVENTUALLY_TRUE(
            client->sync_verifyResolvedMirror(mirror->toThrift()));
      }
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 2);
    });
    ASSERT_EQ(destinations.size(), 2);
    int64_t erspan = 0;
    for (auto destination : destinations) {
      if (client->sync_isMirrorSflowTunnelEnabled(destination)) {
        std::ignore = destination;
      } else {
        erspan = destination;
      }
    }
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags()));
      });
    }
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortIngressFlags(),
          erspan));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
          static_cast<int32_t>(this->masterLogicalInterfacePortIds()[2]),
          this->getMirrorPortEgressFlags(),
          erspan));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleAllPortsMirrorUnresolved) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
    this->unresolveMirror(kSflow);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror != nullptr);
      if (!mirror) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyUnResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      EXPECT_EVENTUALLY_EQ(destinations.size(), 0);
    });
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags()));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleAllPortsMirrorUnresolvedResolved) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
    // unresolve
    this->unresolveMirror(kSflow);
    // reresolve
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    ASSERT_EQ(destinations.size(), 1);
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            destinations[0]));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleAllPortsMirrorUpdate) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    ASSERT_EQ(destinations.size(), 1);
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            destinations[0]));
      });
    }
  };
  auto setupPostWb = [=, this] {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    cfg.mirrors()->push_back(this->getSflowMirror());
    // update to truncate if supported
    *cfg.mirrors()[0].truncate() =
        this->isSupportedOnAllAsics(HwAsic::Feature::MIRROR_PACKET_TRUNCATION);
    // update destination port now
    cfg.mirrors()[0].destination()->tunnel()->sflowTunnel()->udpDstPort() =
        9898;
    /* sampling all ports and send traffic to sflow mirror */
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  this->verifyAcrossWarmBoots(setup, verify, setupPostWb, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, RemoveSampleAllPorts) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (auto portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);

    /* reset all config */
    cfg = this->initialConfig(*this->getAgentEnsemble());
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror == nullptr);
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      EXPECT_EVENTUALLY_EQ(destinations.size(), 0);
    });
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags()));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwSflowMirrorTest, RemoveSampleAllPortsAfterWarmBoot) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (const auto& portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    for (const auto& port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            destinations[0]));
      });
    }
  };
  auto setupPostWb = [=, this]() {
    /* reset all config */
    this->applyNewConfig(this->initialConfig(*this->getAgentEnsemble()));
  };
  auto verifyPostWb = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror == nullptr);
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      EXPECT_EVENTUALLY_EQ(destinations.size(), 0);
    });
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortNoMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags()));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify, setupPostWb, verifyPostWb);
}

TYPED_TEST(AgentHwSflowMirrorTest, SampleAllPortsReloadConfig) {
  /* Setup sample destination to mirror for all ports, and mirror only one port
  this will ensure all port traffic is sampled and sent to that mirror */
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    /* sampling all ports and send traffic to sflow mirror */
    cfg.mirrors()->push_back(this->getSflowMirror());
    for (const auto& portId : this->masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
      *portCfg->sFlowIngressRate() = 90000;
      portCfg->ingressMirror() = *cfg.mirrors()[0].name();
    }
    this->applyNewConfig(cfg);
    // resolve mirror
    this->resolveMirror(kSflow, this->masterLogicalInterfacePortIds()[0]);

    // reload config and verify mirror stays resolved
    this->applyNewConfig(cfg);
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(kSflow);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
    std::vector<int64_t> destinations;
    WITH_RETRIES({
      destinations.clear();
      client->sync_getAllMirrorDestinations(destinations);
      ASSERT_EVENTUALLY_EQ(destinations.size(), 1);
    });
    for (auto port : this->masterLogicalInterfacePortIds()) {
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_verifyPortMirrorDestination(
            static_cast<int32_t>(port),
            this->getMirrorPortIngressAndSflowFlags(),
            destinations[0]));
      });
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentHwMirrorTrunkTest, ResolvedErspanMirrorOnTrunk) {
  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());

    cfg.mirrors()->push_back(this->getErspanMirror());
    auto state = this->applyNewConfig(cfg);
    this->applyNewState([=](const std::shared_ptr<SwitchState>&) {
      return utility::enableTrunkPorts(state);
    });

    // Egress the mirror over the LAG: the member port carries no RIF, so
    // next-hop resolution must target the aggregate.
    this->resolveMirror(kErspan, PortDescriptor(AggregatePortID(1)));
  };
  auto verify = [=, this]() {
    auto client = this->getClient();
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(kErspan);
      EXPECT_EVENTUALLY_TRUE(mirror && mirror->isResolved());
      if (!mirror || !mirror->isResolved()) {
        continue;
      }
      auto fields = mirror->toThrift();
      EXPECT_EVENTUALLY_TRUE(client->sync_verifyResolvedMirror(fields));
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
