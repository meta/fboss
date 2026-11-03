/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"

namespace {
const static int kVlanID = 2000;
const static int kInterfaceID = 2000;
} // namespace

namespace facebook::fboss {

class AgentQueuePerHostRouteTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto switchId = getCurrentSwitchIdForTesting();
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(switchId);
    auto cfg = utility::oneL3IntfTwoPortConfig(
        ensemble.getSw()->getPlatformMapping(),
        asic,
        ensemble.masterLogicalPortIds()[0],
        ensemble.masterLogicalPortIds()[1],
        ensemble.getSw()->getPlatformSupportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        ensemble.getSw()->getPlatformType());
    utility::addQueuePerHostQueueConfig(&cfg);
    utility::addQueuePerHostAcls(&cfg, ensemble.isSai());
    // Disabled so warm boot doesn't bring up an L2 loop with the first port.
    for (auto& port : *cfg.ports()) {
      if (PortID(*port.logicalID()) == ensemble.masterLogicalPortIds()[1]) {
        port.state() = cfg::PortState::DISABLED;
      }
    }
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::QUEUE_PER_HOST};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Enable neighbor cache so that class id is set
    FLAGS_disable_neighbor_updates = false;
  }

  RouterID kRouterID() const {
    return RouterID(0);
  }

  cfg::AclLookupClass kLookupClass() const {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2;
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kGetRoutePrefix() const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return RoutePrefix<folly::IPAddressV4>{
          folly::IPAddressV4{"10.10.1.0"}, 24};
    } else {
      return RoutePrefix<folly::IPAddressV6>{
          folly::IPAddressV6{"2803:6080:d038:3063::"}, 64};
    }
  }

  template <typename AddrT>
  void addRoutes(const std::vector<RoutePrefix<AddrT>>& routePrefixes) {
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), kRouterID());
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&wrapper, kDefaultEcmpWidth, routePrefixes);
  }

  template <typename AddrT>
  void resolveNeighborAndVerifyClassID(
      AddrT ipAddress,
      MacAddress macAddress,
      PortID port) {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      getSw()->getNeighborUpdater()->receivedArpMineForIntf(
          InterfaceID(kInterfaceID),
          ipAddress,
          macAddress,
          PortDescriptor(port),
          ArpOpCode::ARP_OP_REPLY);

      WITH_RETRIES({
        std::list<facebook::fboss::ArpEntryThrift> entries;
        entries = getSw()->getNeighborUpdater()->getArpCacheDataForIntf().get();
        bool found = false;
        for (const auto& entry : entries) {
          if (entry.ip() == network::toBinaryAddress(ipAddress)) {
            found = true;
            EXPECT_EVENTUALLY_TRUE(*entry.classID());
          }
        }
        EXPECT_EVENTUALLY_TRUE(found);
      });
    } else {
      getSw()->getNeighborUpdater()->receivedNdpMineForIntf(
          InterfaceID(kInterfaceID),
          ipAddress,
          macAddress,
          PortDescriptor(port),
          ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
          0);

      WITH_RETRIES({
        std::list<facebook::fboss::NdpEntryThrift> entries;
        entries = getSw()->getNeighborUpdater()->getNdpCacheDataForIntf().get();
        bool found = false;
        for (const auto& entry : entries) {
          if (entry.ip() == network::toBinaryAddress(ipAddress)) {
            found = true;
            EXPECT_EVENTUALLY_TRUE(*entry.classID());
          }
        }
        EXPECT_EVENTUALLY_TRUE(found);
      });
    }
  }

  template <typename AddrT>
  AddrT kSrcIP() {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      return folly::IPAddressV4("1.0.0.1");
    } else {
      return folly::IPAddressV6("1::1");
    }
  }

  template <typename AddrT>
  AddrT kDstIP() {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      return folly::IPAddressV4("10.10.1.2");
    } else {
      return folly::IPAddressV6("2803:6080:d038:3063::2");
    }
  }

  template <typename AddrT>
  std::vector<std::pair<AddrT, MacAddress>> kNeighborIPs() {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      return {
          {folly::IPAddressV4("1.1.1.4"), MacAddress("06:00:00:00:00:03")},
          {folly::IPAddressV4("1.1.1.3"), MacAddress("06:00:00:00:00:02")},
          {folly::IPAddressV4("1.1.1.2"), MacAddress("06:00:00:00:00:01")}};
    } else {
      return {
          {folly::IPAddressV6("1::3"), MacAddress("06:00:00:00:00:03")},
          {folly::IPAddressV6("1::2"), MacAddress("06:00:00:00:00:02")},
          {folly::IPAddressV6("1::1"), MacAddress("06:00:00:00:00:01")}};
    }
  }

  void setMacAddrsToBlock() {
    auto cfgMacAddrsToBlock = std::make_unique<std::vector<cfg::MacAndVlan>>();
    auto addMacs = [&](const auto& neighborIPs) {
      for (const auto& ipAndMac : neighborIPs) {
        cfg::MacAndVlan macAndVlan;
        macAndVlan.vlanID() = kVlanID;
        macAndVlan.macAddress() = ipAndMac.second.toString();
        cfgMacAddrsToBlock->emplace_back(macAndVlan);
      }
    };
    // v4 and v6 neighbors share the same MAC addresses; block both families.
    addMacs(kNeighborIPs<folly::IPAddressV4>());
    addMacs(kNeighborIPs<folly::IPAddressV6>());
    ThriftHandler handler(getSw());
    handler.setMacAddrsToBlock(std::move(cfgMacAddrsToBlock));
  }

  template <typename AddrT>
  void resolveNeighborsAndAddRoutes() {
    for (const auto& neighborIpAndMac : kNeighborIPs<AddrT>()) {
      resolveNeighborAndVerifyClassID<AddrT>(
          neighborIpAndMac.first,
          neighborIpAndMac.second,
          masterLogicalPortIds()[0]);
    }
    addRoutes<AddrT>({kGetRoutePrefix<AddrT>()});
  }

  void setupHelper(bool blockNeighbor) {
    // Disable neighbor updates to prevent stats increment
    FLAGS_disable_neighbor_updates = true;
    getAgentEnsemble()->bringDownPort(masterLogicalPortIds()[1]);
    if (blockNeighbor) {
      setMacAddrsToBlock();
    }
    resolveNeighborsAndAddRoutes<folly::IPAddressV4>();
    resolveNeighborsAndAddRoutes<folly::IPAddressV6>();
  }

  template <typename AddrT>
  void verifyHelper(bool useFrontPanel, bool blockNeighbor) {
    // Disable neighbor updates to prevent stats increment
    FLAGS_disable_neighbor_updates = true;
    getNextUpdatedPortStats(masterLogicalPortIds()[0]);
    XLOG(DBG2) << "verify send packets "
               << (useFrontPanel ? "out of port" : "switched");
    auto vlanId =
        VlanID(*initialConfig(*getAgentEnsemble()).vlanPorts()[0].vlanID());
    auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    if (useFrontPanel) {
      learnL2EntryIfPending(srcMac, masterLogicalPortIds()[1]);
    }

    utility::verifyQueuePerHostMapping(
        getAgentEnsemble(),
        vlanId,
        srcMac,
        intfMac,
        kSrcIP<AddrT>(),
        kDstIP<AddrT>(),
        useFrontPanel,
        blockNeighbor,
        std::nullopt, /* l4SrcPort */
        std::nullopt, /* l4DstPort */
        std::nullopt); /* dscp */
  }

  void setFrontPanelPortAdminState(cfg::PortState state) {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = in->clone();
      newState->getPorts()
          ->getNodeIf(masterLogicalPortIds()[1])
          ->modify(&newState)
          ->setAdminState(state);
      return newState;
    });
  }

  void verifyAllFamilies(bool useFrontPanel, bool blockNeighbor) {
    {
      SCOPED_TRACE("v4");
      verifyHelper<folly::IPAddressV4>(useFrontPanel, blockNeighbor);
    }
    {
      SCOPED_TRACE("v6");
      verifyHelper<folly::IPAddressV6>(useFrontPanel, blockNeighbor);
    }
  }
};

TEST_F(AgentQueuePerHostRouteTest, VerifyHostToQueueMappingClassID) {
  auto setup = [=, this]() { setupHelper(false /* blockNeighbor */); };
  auto verify = [=, this]() {
    // No ARP/NDP broadcasts while both looped ports are up.
    FLAGS_disable_neighbor_updates = true;
    setFrontPanelPortAdminState(cfg::PortState::ENABLED);
    getAgentEnsemble()->bringUpPort(masterLogicalPortIds()[1]);
    verifyAllFamilies(true /* front panel port */, false /* block neighbor */);
    getAgentEnsemble()->bringDownPort(masterLogicalPortIds()[1]);
    setFrontPanelPortAdminState(cfg::PortState::DISABLED);
    verifyAllFamilies(false /* cpu port */, false /* block neighbor */);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentQueuePerHostRouteTest, VerifyHostToQueueMappingClassIDBlock) {
  auto setup = [=, this]() { setupHelper(true /* blockNeighbor */); };
  auto verify = [=, this]() {
    // No ARP/NDP broadcasts while both looped ports are up.
    FLAGS_disable_neighbor_updates = true;
    setFrontPanelPortAdminState(cfg::PortState::ENABLED);
    getAgentEnsemble()->bringUpPort(masterLogicalPortIds()[1]);
    verifyAllFamilies(true /* front panel port */, true /* block neighbor */);
    getAgentEnsemble()->bringDownPort(masterLogicalPortIds()[1]);
    setFrontPanelPortAdminState(cfg::PortState::DISABLED);
    verifyAllFamilies(false /* cpu port */, true /* block neighbor */);
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
