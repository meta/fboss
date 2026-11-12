// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/AddressUtil.h"

namespace facebook::fboss {

class AgentSrv6ResourceUsageTest : public AgentHwTest {
 protected:
  const folly::IPAddressV6 kDecapMySidAddr{"3001:db8:efff::"};
  static constexpr uint8_t kDecapMySidPrefixLen{48};

  // SID offset for the adjacency (uA) mySid: 3001:db8:1::/48, kept clear of the
  // decap SID above. Matches utility::makeAdjacencyMySidEntries' SID layout.
  static constexpr int kAdjSidOffset{1};

  // On Yuba (GR2) the two SID types cost differently.
  //
  // DECAPSULATE_AND_LOOKUP maps to ..._BEHAVIOR_UDT46. Per the Leaba SDK
  // SAI adapter (sai_srv6.cpp, create_my_sid_entry_internal):
  //
  //   "for uDT, we need to detect SRv6 packets with "Sids" after the uDT
  //   and drop those packets. To achieve this behavior, we install 2
  //   endpoints: one for exact match and one for miss action."
  //
  // ADJACENCY_MICRO_SID maps to ..._BEHAVIOR_UA and takes the
  // single-endpoint path. Each endpoint is one tunnel_attributes_ipv6_table
  // entry = one slot.
  static constexpr int32_t kDecapMySidSlotCost{2};

  // TH5/TH6 decap and adjacency entries each consume one slot.
  static constexpr int32_t kAdjacencyMySidSlotCost{1};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::SRV6_DECAP, ProductionFeature::SRV6_MIDPOINT};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // SRv6 SID scale uses all interface ports as ECMP next-hops.
    return std::nullopt;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  utility::EcmpSetupAnyNPorts<folly::IPAddressV6> makeEcmpHelper() {
    return utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getLocalMacAddress());
  }

  void removeDecapMySidEntry() {
    IpPrefix ipPrefix;
    ipPrefix.ip() = facebook::network::toBinaryAddress(kDecapMySidAddr);
    ipPrefix.prefixLength() = kDecapMySidPrefixLen;
    auto sw = getSw();
    auto rib = sw->getRib();
    auto ribMySidToSwitchStateFunc =
        createRibMySidToSwitchStateFunction(std::nullopt);
    rib->update(
        sw->getScopeResolver(),
        std::vector<MySidEntry>{} /* toAdd */,
        {ipPrefix},
        "removeDecapMySidEntry",
        ribMySidToSwitchStateFunc,
        sw);
  }

  int32_t getMySidEntriesFree() {
    auto switchId = getCurrentSwitchIdForTesting();
    getLatestPortStats(masterLogicalPortIds());
    auto stats = getSw()->getHwSwitchStatsExpensive(switchId);
    return stats.hwResourceStats()->my_sid_entries_free().value();
  }
};

TEST_F(AgentSrv6ResourceUsageTest, verifyMySidResourceUsage) {
  auto setup = []() {};

  auto verify = [this]() {
    const auto switchId = getCurrentSwitchIdForTesting();
    const auto asicType =
        getSw()->getHwAsicTable()->getHwAsic(switchId)->getAsicType();
    std::optional<int32_t> expectedDecapMySidSlotCost;
    if (asicType == cfg::AsicType::ASIC_TYPE_YUBA) {
      expectedDecapMySidSlotCost = kDecapMySidSlotCost;
    } else if (
        asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
        asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
      expectedDecapMySidSlotCost = 1;
    }

    const auto mySidFreeBaseline = getMySidEntriesFree();

    // Add decap mySid.
    utility::addDecapMySidEntry(getSw(), kDecapMySidAddr, kDecapMySidPrefixLen);
    int32_t decapMySidSlotCost{0};
    WITH_RETRIES({
      decapMySidSlotCost = mySidFreeBaseline - getMySidEntriesFree();
      if (expectedDecapMySidSlotCost.has_value()) {
        EXPECT_EVENTUALLY_EQ(decapMySidSlotCost, *expectedDecapMySidSlotCost);
      } else {
        EXPECT_EVENTUALLY_GT(decapMySidSlotCost, 0);
      }
    });
    const auto mySidFreeWithDecap = mySidFreeBaseline - decapMySidSlotCost;

    // Add adjacency (uA) mySid. The interface is baked into the resolved next
    // hop so the unresolved/resolved next-hop-set IDs coincide.
    auto ecmpHelper = makeEcmpHelper();
    resolveNeighbors(ecmpHelper, 1);
    utility::programMySidEntries(
        getSw(),
        utility::makeAdjacencyMySidEntries(
            ecmpHelper, 1 /*numNhops*/, 1 /*numEntries*/, kAdjSidOffset));
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          getMySidEntriesFree(), mySidFreeWithDecap - kAdjacencyMySidSlotCost);
    });

    // Remove adjacency mySid, verify only the decap SID's slots stay consumed.
    utility::deleteScaleMySidEntries(getSw(), 1 /*numEntries*/, kAdjSidOffset);
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(getMySidEntriesFree(), mySidFreeWithDecap); });

    // Remove decap mySid, verify the pool returns to baseline.
    removeDecapMySidEntry();
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(getMySidEntriesFree(), mySidFreeBaseline); });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentSrv6ResourceUsageTest, verifyDualLocatorMySidResourceUsage) {
  auto setup = []() {};

  auto verify = [this]() {
    const auto mySidFreeBaseline = getMySidEntriesFree();
    auto config = initialConfig(*getAgentEnsemble());

    // 1. The decap SID under the legacy locator alone gives the per-SID cost,
    // which differs by ASIC.
    config.mySidConfig() =
        utility::makeDecapMySidConfig({utility::kSrv6LegacyLocatorPrefix});
    applyNewConfig(config);
    int32_t decapSlotCost{0};
    WITH_RETRIES({
      decapSlotCost = mySidFreeBaseline - getMySidEntriesFree();
      EXPECT_EVENTUALLY_GT(decapSlotCost, 0);
    });

    // 2. Adding the plane locator costs one more decap SID's slots.
    config.mySidConfig() = utility::makeDecapMySidConfig(
        {utility::kSrv6LegacyLocatorPrefix, utility::kSrv6PlaneLocatorPrefix});
    applyNewConfig(config);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          getMySidEntriesFree(), mySidFreeBaseline - 2 * decapSlotCost);
    });

    // 3. Dropping the plane locator frees them again.
    config.mySidConfig() =
        utility::makeDecapMySidConfig({utility::kSrv6LegacyLocatorPrefix});
    applyNewConfig(config);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          getMySidEntriesFree(), mySidFreeBaseline - decapSlotCost);
    });

    // 4. Removing mySidConfig returns the pool to baseline.
    config.mySidConfig().reset();
    applyNewConfig(config);
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(getMySidEntriesFree(), mySidFreeBaseline); });
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
