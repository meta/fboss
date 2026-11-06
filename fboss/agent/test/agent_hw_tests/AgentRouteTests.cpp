/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/RouteTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "folly/IPAddressV4.h"
#include "folly/IPAddressV6.h"

#include <string>

using facebook::network::toBinaryAddress;

DECLARE_bool(classid_for_unresolved_routes);

namespace {
template <typename AddrT>
bool verifyProgrammedStack(
    typename facebook::fboss::Route<AddrT>::Prefix routePrefix,
    const facebook::fboss::InterfaceID& intfID,
    const facebook::fboss::LabelForwardingAction::LabelStack& stack,
    long refCount,
    facebook::fboss::AgentEnsemble& ensemble) {
  auto switchId = ensemble.getSw()
                      ->getScopeResolver()
                      ->scope(ensemble.masterLogicalPortIds())
                      .switchId();
  facebook::fboss::IpPrefix prefix;
  prefix.ip() = toBinaryAddress(routePrefix.network());
  prefix.prefixLength() = routePrefix.mask();
  auto client = ensemble.getHwAgentTestClient(switchId);
  return client->sync_isProgrammedInHw(intfID, prefix, stack, refCount);
}
} // namespace

namespace facebook::fboss {

class AgentRouteTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }

  RouterID kRouterID() const {
    return RouterID(0);
  }

  cfg::AclLookupClass kLookupClass() const {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2;
  }

  std::vector<PortDescriptor> portDescs() const {
    std::vector<PortDescriptor> ports;
    for (auto i = 0; i < 4; ++i) {
      if (FLAGS_hyper_port) {
        ports.emplace_back(masterLogicalHyperPortIds()[i]);
      } else {
        ports.emplace_back(masterLogicalInterfacePortIds()[i]);
      }
    }
    return ports;
  }

  template <typename AddrT>
  RoutePrefix<AddrT> getSubnetIpForInterface() const {
    auto state = this->getProgrammedState();
    InterfaceID intfID = firstInterfaceIDWithPortsForTesting(state);
    auto interface = state->getInterfaces()->getNodeIf(intfID);
    if (interface) {
      for (auto iter : std::as_const(*interface->getAddresses())) {
        std::pair<folly::IPAddress, uint8_t> address(
            folly::IPAddress(iter.first), iter.second->ref());
        if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
          if (address.first.isV4()) {
            return RoutePrefix<folly::IPAddressV4>{
                address.first.asV4(), address.second};
          }
        } else {
          if (address.first.isV6()) {
            return RoutePrefix<folly::IPAddressV6>{
                address.first.asV6(), address.second};
          }
        }
      }
    }
    XLOG(FATAL) << "No interface found";
  }

  template <typename AddrT>
  const std::vector<RoutePrefix<AddrT>> kGetRoutePrefixes() const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      static const std::vector<RoutePrefix<AddrT>> routePrefixes = {
          RoutePrefix<folly::IPAddressV4>{folly::IPAddressV4{"10.10.1.0"}, 24},
          RoutePrefix<folly::IPAddressV4>{folly::IPAddressV4{"10.20.1.0"}, 24},
          RoutePrefix<folly::IPAddressV4>{folly::IPAddressV4{"10.30.1.0"}, 24},
          RoutePrefix<folly::IPAddressV4>{folly::IPAddressV4{"10.40.1.1"}, 32}};

      return routePrefixes;
    } else {
      static const std::vector<RoutePrefix<AddrT>> routePrefixes = {
          RoutePrefix<folly::IPAddressV6>{
              folly::IPAddressV6{"2803:6080:d038:3063::"}, 64},
          RoutePrefix<folly::IPAddressV6>{
              folly::IPAddressV6{"2803:6080:d038:3064::"}, 64},
          RoutePrefix<folly::IPAddressV6>{
              folly::IPAddressV6{"2803:6080:d038:3065::"}, 64},
          RoutePrefix<folly::IPAddressV6>{
              folly::IPAddressV6{"2803:6080:d038:3065::1"}, 128}};

      return routePrefixes;
    }
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kDefaultPrefix() const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return RoutePrefix<folly::IPAddressV4>{folly::IPAddressV4{"0.0.0.0"}, 0};

    } else {
      return RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6{"::"}, 0};
    }
  }

  template <typename AddrT>
  const AddrT kStaticIp2MplsNextHop() const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return folly::IPAddressV4{"10.10.1.1"};
    } else {
      return folly::IPAddressV6{"2803:6080:d038:3063::1"};
    }
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kGetRoutePrefix0() const {
    return kGetRoutePrefixes<AddrT>()[0];
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kGetRoutePrefix1() const {
    return kGetRoutePrefixes<AddrT>()[1];
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kGetRoutePrefix2() const {
    return kGetRoutePrefixes<AddrT>()[2];
  }

  template <typename AddrT>
  RoutePrefix<AddrT> kGetRoutePrefix3() const {
    return kGetRoutePrefixes<AddrT>()[3];
  }

  template <typename AddrT>
  std::shared_ptr<SwitchState> addRoutes(
      const std::shared_ptr<SwitchState>& inState,
      const std::vector<RoutePrefix<AddrT>>& routePrefixes) {
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        inState, getSw()->needL2EntryForNeighbor(), kRouterID());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, kDefaultEcmpWidth);
      return newState;
    });
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&wrapper, kDefaultEcmpWidth, routePrefixes);
    return getProgrammedState();
  }

  template <typename AddrT>
  void verifyClassIDHelper(
      RoutePrefix<AddrT> routePrefix,
      std::optional<cfg::AclLookupClass> classID) {
    auto routeInfo = utility::getRouteInfo(
        routePrefix.network(), routePrefix.mask(), *this->getAgentEnsemble());
    std::optional<cfg::AclLookupClass> classIDFromHw;
    if (routeInfo.classId().has_value()) {
      classIDFromHw =
          facebook::fboss::cfg::AclLookupClass(routeInfo.classId().value());
    }
    EXPECT_EQ(classIDFromHw, classID);
  }

  template <typename AddrT>
  void verifyClassIDSetup() {
    // 3 routes r0, r1, r2. r0 & r1 have classID, r2 does not.
    this->addRoutes<AddrT>(
        this->getProgrammedState(),
        {this->kGetRoutePrefix0<AddrT>(),
         this->kGetRoutePrefix1<AddrT>(),
         this->kGetRoutePrefix2<AddrT>()});
    auto updater = this->getSw()->getRouteUpdater();
    updater.programClassID(
        this->kRouterID(),
        {this->kGetRoutePrefix0<AddrT>().toCidrNetwork(),
         this->kGetRoutePrefix1<AddrT>().toCidrNetwork()},
        this->kLookupClass(),
        false /*sync*/);

    // verify classID programming
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix0<AddrT>(), this->kLookupClass());
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix1<AddrT>(), this->kLookupClass());
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix2<AddrT>(), std::nullopt);

    // remove r1's classID, add classID for r2
    updater.programClassID(
        this->kRouterID(),
        {this->kGetRoutePrefix1<AddrT>().toCidrNetwork()},
        std::nullopt,
        false /*sync*/);
    updater.programClassID(
        this->kRouterID(),
        {this->kGetRoutePrefix2<AddrT>().toCidrNetwork()},
        this->kLookupClass(),
        false /*sync*/);
  }

  template <typename AddrT>
  void verifyClassIDVerify() {
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix0<AddrT>(), this->kLookupClass());
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix1<AddrT>(), std::nullopt);
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix2<AddrT>(), this->kLookupClass());
  }

  template <typename AddrT>
  void verifyClassIdWithNhopResolutionFlapSetup() {
    this->addRoutes<AddrT>(
        this->getProgrammedState(), {this->kGetRoutePrefix0<AddrT>()});
    auto updater = this->getSw()->getRouteUpdater();
    updater.programClassID(
        this->kRouterID(),
        {this->kGetRoutePrefix0<AddrT>().toCidrNetwork()},
        this->kLookupClass(),
        false /*sync*/);

    // verify classID programming
    this->verifyClassIDHelper<AddrT>(
        this->kGetRoutePrefix0<AddrT>(), this->kLookupClass());
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    // Unresolve nhop
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.unresolveNextHops(in, kDefaultEcmpWidth);
      return newState;
    });
    // Resolve nhop
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, kDefaultEcmpWidth);
      return newState;
    });
  }

  template <typename AddrT>
  void unresolvedAndResolvedNextHopSetup(
      const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[0]}, {this->kGetRoutePrefix0<AddrT>()});

    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[1]});
      return newState;
    });
    ecmpHelper.programRoutes(
        &wrapper, {ports[1]}, {this->kGetRoutePrefix1<AddrT>()});
  }

  template <typename AddrT>
  void unresolvedAndResolvedNextHopVerify(
      const std::vector<PortDescriptor>& ports) {
    auto routePrefix0 = this->kGetRoutePrefix0<AddrT>();
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    auto routeInfo = utility::getRouteInfo(
        routePrefix0.network(), routePrefix0.mask(), *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.isProgrammedToCpu());
    EXPECT_FALSE(*routeInfo.isMultiPath());

    auto routePrefix1 = this->kGetRoutePrefix1<AddrT>();
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix1.network(),
            routePrefix1.mask(),
            ecmpHelper.nhop(ports[1]).ip,
            *this->getAgentEnsemble()));

    auto routeInfo1 = utility::getRouteInfo(
        routePrefix1.network(), routePrefix1.mask(), *this->getAgentEnsemble());
    EXPECT_FALSE(*routeInfo1.isMultiPath());
  }

  template <typename AddrT>
  void unresolveResolvedNextHopSetup() {
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, 1);
      return newState;
    });

    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&wrapper, 1, {this->kGetRoutePrefix0<AddrT>()});
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.unresolveNextHops(in, 1);
      return newState;
    });
  }

  template <typename AddrT>
  void unresolveResolvedNextHopVerify() {
    auto routePrefix = this->kGetRoutePrefix0<AddrT>();
    auto routeInfo = utility::getRouteInfo(
        routePrefix.network(), routePrefix.mask(), *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.isProgrammedToCpu());
    EXPECT_FALSE(*routeInfo.isMultiPath());
  }

  template <typename AddrT>
  void unresolvedAndResolvedMultiNextHopSetup(
      const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[0], ports[1]}, {this->kGetRoutePrefix0<AddrT>()});

    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[2], ports[3]});
      return newState;
    });
    ecmpHelper.programRoutes(
        &wrapper, {ports[2], ports[3]}, {this->kGetRoutePrefix1<AddrT>()});
  }

  template <typename AddrT>
  void unresolvedAndResolvedMultiNextHopVerify(
      const std::vector<PortDescriptor>& ports) {
    auto routePrefix0 = this->kGetRoutePrefix0<AddrT>();
    auto routeInfo = utility::getRouteInfo(
        routePrefix0.network(), routePrefix0.mask(), *this->getAgentEnsemble());
    EXPECT_FALSE(*routeInfo.isProgrammedToCpu());
    EXPECT_TRUE(*routeInfo.isMultiPath());
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    EXPECT_FALSE(
        utility::isRouteToNexthop(
            routePrefix0.network(),
            routePrefix0.mask(),
            ecmpHelper.nhop(ports[0]).ip,
            *this->getAgentEnsemble()));
    EXPECT_FALSE(
        utility::isRouteToNexthop(
            routePrefix0.network(),
            routePrefix0.mask(),
            ecmpHelper.nhop(ports[1]).ip,
            *this->getAgentEnsemble()));

    auto routePrefix1 = this->kGetRoutePrefix1<AddrT>();
    auto routeInfo1 = utility::getRouteInfo(
        routePrefix1.network(), routePrefix1.mask(), *this->getAgentEnsemble());
    EXPECT_FALSE(*routeInfo1.isProgrammedToCpu());
    EXPECT_TRUE(*routeInfo1.isMultiPath());
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix1.network(),
            routePrefix1.mask(),
            ecmpHelper.nhop(ports[2]).ip,
            *this->getAgentEnsemble()));
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix1.network(),
            routePrefix1.mask(),
            ecmpHelper.nhop(ports[3]).ip,
            *this->getAgentEnsemble()));
  }

  template <typename AddrT>
  void resolvedMultiNexthopToUnresolvedSingleNexthopVerify(
      const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[0], ports[1]});
      return newState;
    });
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[0], ports[1]}, {this->kGetRoutePrefix0<AddrT>()});
    auto routePrefix0 = this->kGetRoutePrefix0<AddrT>();
    auto routeInfo = utility::getRouteInfo(
        routePrefix0.network(), routePrefix0.mask(), *this->getAgentEnsemble());
    EXPECT_FALSE(*routeInfo.isProgrammedToCpu());
    EXPECT_TRUE(*routeInfo.isMultiPath());
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix0.network(),
            routePrefix0.mask(),
            ecmpHelper.nhop(ports[0]).ip,
            *this->getAgentEnsemble()));
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix0.network(),
            routePrefix0.mask(),
            ecmpHelper.nhop(ports[1]).ip,
            *this->getAgentEnsemble()));

    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.unresolveNextHops(in, {ports[0], ports[1]});
      return newState;
    });
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[0]});
      return newState;
    });
    ecmpHelper.programRoutes(
        &wrapper, {ports[0]}, {this->kGetRoutePrefix0<AddrT>()});
  }

  template <typename AddrT>
  void verifyRoutingSetup(const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());

    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[0]});
      return newState;
    });

    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[0]}, {this->kDefaultPrefix<AddrT>()});
  }

  template <typename AddrT>
  void verifyRoutingVerify(const std::vector<PortDescriptor>& ports) {
    auto constexpr isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
    const auto egressPort = ports[0].phyPortID();
    auto vlanId = this->getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
    this->learnL2EntryIfPending(intfMac, ports[1].phyPortID());

    auto beforeOutPkts =
        *this->getLatestPortStats(egressPort).outUnicastPkts__ref();
    auto v4TxPkt = utility::makeUDPTxPacket(
        this->getSw(),
        vlanId,
        intfMac,
        intfMac,
        folly::IPAddressV4("101.101.0.1"),
        folly::IPAddressV4("201.101.0.1"),
        1234,
        4321);

    auto v6TxPkt = utility::makeUDPTxPacket(
        this->getSw(),
        vlanId,
        intfMac,
        intfMac,
        folly::IPAddressV6("1001::1"),
        folly::IPAddressV6("2001::1"),
        1234,
        4321);

    if (isV4) {
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(v4TxPkt), ports[1].phyPortID());
    } else {
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(v6TxPkt), ports[1].phyPortID());
    }
    WITH_RETRIES({
      auto afterOutPkts =
          *this->getLatestPortStats(egressPort).outUnicastPkts__ref();
      XLOG(DBG2) << "Stats:: beforeOutPkts: " << beforeOutPkts
                 << " afterOutPkts: " << afterOutPkts;
      EXPECT_EVENTUALLY_EQ(afterOutPkts - 1, beforeOutPkts);
    });
  }

  template <typename AddrT>
  void verifyHostRouteChangeSetup(const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[0], ports[1]});
      return newState;
    });
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[1]}, {this->kGetRoutePrefix3<AddrT>()});
    ecmpHelper.programRoutes(
        &wrapper, {ports[0], ports[1]}, {this->kGetRoutePrefix3<AddrT>()});
  }

  template <typename AddrT>
  void verifyHostRouteChangeVerify(const std::vector<PortDescriptor>& ports) {
    auto routePrefix = this->kGetRoutePrefix3<AddrT>();
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix.network(),
            routePrefix.mask(),
            ecmpHelper.nhop(ports[0]).ip,
            *this->getAgentEnsemble()));
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            routePrefix.network(),
            routePrefix.mask(),
            ecmpHelper.nhop(ports[1]).ip,
            *this->getAgentEnsemble()));
  }

  template <typename AddrT>
  void verifyCpuRouteChangeSetup(const std::vector<PortDescriptor>& ports) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    // Next hops unresolved - route should point to CPU
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper, {ports[1]}, {this->kGetRoutePrefix3<AddrT>()});
  }

  template <typename AddrT>
  void verifyCpuRouteChangeVerify(const std::vector<PortDescriptor>& ports) {
    auto routePrefix = this->kGetRoutePrefix3<AddrT>();
    auto routeInfo = utility::getRouteInfo(
        routePrefix.network(), routePrefix.mask(), *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.isProgrammedToCpu());
    if (FLAGS_classid_for_unresolved_routes) {
      EXPECT_TRUE(*routeInfo.isRouteUnresolvedToClassId());
    }

    // Resolve next hops
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[1]});
      return newState;
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          utility::isRouteToNexthop(
              routePrefix.network(),
              routePrefix.mask(),
              ecmpHelper.nhop(ports[1]).ip,
              *this->getAgentEnsemble()));
      if (FLAGS_classid_for_unresolved_routes) {
        auto routeInfo2 = utility::getRouteInfo(
            routePrefix.network(),
            routePrefix.mask(),
            *this->getAgentEnsemble());
        EXPECT_EVENTUALLY_FALSE(*routeInfo2.isRouteUnresolvedToClassId());
      }
    });

    // Verify routing
    const auto egressPort = ports[1].phyPortID();
    auto vlanId = this->getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
    this->learnL2EntryIfPending(intfMac, ports[0].phyPortID());
    auto beforeOutPkts =
        *this->getLatestPortStats(egressPort).outUnicastPkts__ref();
    auto v6TxPkt = utility::makeUDPTxPacket(
        this->getSw(),
        vlanId,
        intfMac,
        intfMac,
        this->kGetRoutePrefix0<AddrT>().network(), // Randomly pick src IP
        routePrefix.network(),
        1234,
        4321);
    this->getAgentEnsemble()->ensureSendPacketOutOfPort(
        std::move(v6TxPkt), ports[0].phyPortID());
    WITH_RETRIES({
      auto afterOutPkts =
          *this->getLatestPortStats(egressPort).outUnicastPkts__ref();
      XLOG(DBG2) << "Stats:: beforeOutPkts: " << beforeOutPkts
                 << " afterOutPkts: " << afterOutPkts;
      EXPECT_EVENTUALLY_EQ(afterOutPkts - 1, beforeOutPkts);
    });

    // Unresolve next hops
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.unresolveNextHops(in, {ports[1]});
      return newState;
    });
    WITH_RETRIES({
      auto routeInfo3 = utility::getRouteInfo(
          routePrefix.network(), routePrefix.mask(), *this->getAgentEnsemble());
      EXPECT_EVENTUALLY_TRUE(*routeInfo3.isProgrammedToCpu());
      if (FLAGS_classid_for_unresolved_routes) {
        EXPECT_EVENTUALLY_TRUE(*routeInfo3.isRouteUnresolvedToClassId());
      }
    });

    // Resolve next hops
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(in, {ports[1]});
      return newState;
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          utility::isRouteToNexthop(
              routePrefix.network(),
              routePrefix.mask(),
              ecmpHelper.nhop(ports[1]).ip,
              *this->getAgentEnsemble()));
      if (FLAGS_classid_for_unresolved_routes) {
        auto routeInfo4 = utility::getRouteInfo(
            routePrefix.network(),
            routePrefix.mask(),
            *this->getAgentEnsemble());
        EXPECT_EVENTUALLY_FALSE(*routeInfo4.isRouteUnresolvedToClassId());
      }
    });

    // Unresolve next hops
    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.unresolveNextHops(in, {ports[1]});
      return newState;
    });
    WITH_RETRIES({
      auto routeInfo5 = utility::getRouteInfo(
          routePrefix.network(), routePrefix.mask(), *this->getAgentEnsemble());
      EXPECT_EVENTUALLY_TRUE(*routeInfo5.isProgrammedToCpu());
      if (FLAGS_classid_for_unresolved_routes) {
        EXPECT_EVENTUALLY_TRUE(*routeInfo5.isRouteUnresolvedToClassId());
      }
    });
  }

  template <typename AddrT>
  void addStaticIp2MplsRoute(cfg::SwitchConfig& config) {
    cfg::StaticIp2MplsRoute staticRoute;
    staticRoute.prefix() = this->kGetRoutePrefix1<AddrT>().str();

    NextHopThrift nexthop;
    nexthop.address() = toBinaryAddress(
        folly::IPAddress(
            this->kStaticIp2MplsNextHop<AddrT>().str())); // in prefix 0 subnet
    MplsAction action;
    action.action() = MplsActionCode::PUSH;
    action.pushLabels() = {1001, 1002};
    nexthop.mplsAction() = action;

    staticRoute.nexthops()->resize(1);
    staticRoute.nexthops()[0] = nexthop;
    config.staticIp2MplsRoutes()->push_back(staticRoute);
  }

  template <typename AddrT>
  void staticIp2MplsRoutesResolve() {
    // resolve prefix 0 subnet
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        this->kRouterID());
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &wrapper,
        {this->portDescs()[0], this->portDescs()[1]},
        {this->kGetRoutePrefix0<AddrT>()});

    this->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper.resolveNextHops(
          in, {this->portDescs()[0], this->portDescs()[1]});
      return newState;
    });
  }

  template <typename AddrT>
  void staticIp2MplsRoutesVerify() {
    // prefix 1 subnet reachable via prefix 0 with mpls stack over this stack
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          verifyProgrammedStack<AddrT>(
              this->kGetRoutePrefix1<AddrT>(),
              InterfaceID(utility::kBaseVlanId),
              {1001, 1002},
              1,
              *this->getAgentEnsemble()));
      EXPECT_EVENTUALLY_TRUE(
          verifyProgrammedStack<AddrT>(
              this->kGetRoutePrefix1<AddrT>(),
              InterfaceID(utility::kBaseVlanId + 1),
              {1001, 1002},
              1,
              *this->getAgentEnsemble()));
    });
  }
};

class AgentClassIDRouteTest : public AgentRouteTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentRouteTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::CLASS_ID_FOR_CONNECTED_ROUTE);
    return features;
  }

  template <typename AddrT>
  void verifyClassIDForConnectedRouteVerify() {
    auto ipAddr = this->getSubnetIpForInterface<AddrT>();
    // verify if the connected route of the interface is present
    auto routeInfo = utility::getRouteInfo(
        ipAddr.network(), ipAddr.mask(), *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.exists());
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    if (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
      if (FLAGS_set_classid_for_my_subnet_and_ip_routes) {
        this->verifyClassIDHelper<AddrT>(
            ipAddr, cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);
      }
    }
  }
};

class AgentMplsRouteTest : public AgentRouteTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentRouteTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::MPLS);
    return features;
  }
};

TEST_F(AgentRouteTest, VerifyClassID) {
  auto setup = [=, this]() {
    this->verifyClassIDSetup<folly::IPAddressV4>();
    this->verifyClassIDSetup<folly::IPAddressV6>();
  };

  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyClassIDVerify<folly::IPAddressV4>();
    }
    {
      SCOPED_TRACE("v6");
      this->verifyClassIDVerify<folly::IPAddressV6>();
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, VerifyClassIdWithNhopResolutionFlap) {
  auto setup = [=, this]() {
    this->verifyClassIdWithNhopResolutionFlapSetup<folly::IPAddressV4>();
    this->verifyClassIdWithNhopResolutionFlapSetup<folly::IPAddressV6>();
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyClassIDHelper<folly::IPAddressV4>(
          this->kGetRoutePrefix0<folly::IPAddressV4>(), this->kLookupClass());
    }
    {
      SCOPED_TRACE("v6");
      this->verifyClassIDHelper<folly::IPAddressV6>(
          this->kGetRoutePrefix0<folly::IPAddressV6>(), this->kLookupClass());
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, UnresolvedAndResolvedNextHop) {
  auto ports = this->portDescs();
  auto setup = [=, this]() {
    this->unresolvedAndResolvedNextHopSetup<folly::IPAddressV4>(ports);
    this->unresolvedAndResolvedNextHopSetup<folly::IPAddressV6>(ports);
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->unresolvedAndResolvedNextHopVerify<folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->unresolvedAndResolvedNextHopVerify<folly::IPAddressV6>(ports);
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, UnresolveResolvedNextHop) {
  auto setup = [=, this]() {
    this->unresolveResolvedNextHopSetup<folly::IPAddressV4>();
    this->unresolveResolvedNextHopSetup<folly::IPAddressV6>();
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->unresolveResolvedNextHopVerify<folly::IPAddressV4>();
    }
    {
      SCOPED_TRACE("v6");
      this->unresolveResolvedNextHopVerify<folly::IPAddressV6>();
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, UnresolvedAndResolvedMultiNextHop) {
  auto ports = this->portDescs();
  auto setup = [=, this]() {
    this->unresolvedAndResolvedMultiNextHopSetup<folly::IPAddressV4>(ports);
    this->unresolvedAndResolvedMultiNextHopSetup<folly::IPAddressV6>(ports);
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->unresolvedAndResolvedMultiNextHopVerify<folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->unresolvedAndResolvedMultiNextHopVerify<folly::IPAddressV6>(ports);
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, ResolvedMultiNexthopToUnresolvedSingleNexthop) {
  auto ports = this->portDescs();
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->resolvedMultiNexthopToUnresolvedSingleNexthopVerify<
          folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->resolvedMultiNexthopToUnresolvedSingleNexthopVerify<
          folly::IPAddressV6>(ports);
    }
  };
  this->verifyAcrossWarmBoots(verify);
}

TEST_F(AgentRouteTest, VerifyRouting) {
  auto ports = this->portDescs();
  auto setup = [=, this]() {
    this->verifyRoutingSetup<folly::IPAddressV4>(ports);
    this->verifyRoutingSetup<folly::IPAddressV6>(ports);
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyRoutingVerify<folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->verifyRoutingVerify<folly::IPAddressV6>(ports);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, verifyHostRouteChange) {
  auto ports = this->portDescs();

  auto setup = [=, this]() {
    this->verifyHostRouteChangeSetup<folly::IPAddressV4>(ports);
    this->verifyHostRouteChangeSetup<folly::IPAddressV6>(ports);
  };

  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyHostRouteChangeVerify<folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->verifyHostRouteChangeVerify<folly::IPAddressV6>(ports);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, verifyCpuRouteChange) {
  auto ports = this->portDescs();

  auto setup = [=, this]() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    auto ensemble = this->getAgentEnsemble();
    utility::addOlympicQosMaps(cfg, ensemble->getL3Asics());
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble->getL3Asics(), ensemble->isSai());
    utility::addCpuQueueConfig(cfg, ensemble->getL3Asics(), ensemble->isSai());
    this->applyNewConfig(cfg);

    this->verifyCpuRouteChangeSetup<folly::IPAddressV4>(ports);
    this->verifyCpuRouteChangeSetup<folly::IPAddressV6>(ports);
  };

  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyCpuRouteChangeVerify<folly::IPAddressV4>(ports);
    }
    {
      SCOPED_TRACE("v6");
      this->verifyCpuRouteChangeVerify<folly::IPAddressV6>(ports);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentRouteTest, VerifyDefaultRoute) {
  auto verify = [=, this]() {
    // default routes should exist always.
    auto routeInfo = utility::getRouteInfo(
        folly::IPAddress("::"), 0, *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.exists());
    auto routeInfo1 = utility::getRouteInfo(
        folly::IPAddress("0.0.0.0"), 0, *this->getAgentEnsemble());
    EXPECT_TRUE(*routeInfo1.exists());
  };
  this->verifyAcrossWarmBoots(verify);
}

TEST_F(AgentClassIDRouteTest, VerifyClassIDForConnectedRoute) {
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->verifyClassIDForConnectedRouteVerify<folly::IPAddressV4>();
    }
    {
      SCOPED_TRACE("v6");
      this->verifyClassIDForConnectedRouteVerify<folly::IPAddressV6>();
    }
  };

  this->verifyAcrossWarmBoots(verify);
}

TEST_F(AgentMplsRouteTest, StaticIp2MplsRoutes) {
  auto setup = [=, this]() {
    // Program both families' static IP2MPLS routes via a single config so the
    // v6 config does not clobber the v4 static route.
    auto config = this->initialConfig(*this->getAgentEnsemble());
    this->addStaticIp2MplsRoute<folly::IPAddressV4>(config);
    this->addStaticIp2MplsRoute<folly::IPAddressV6>(config);
    this->applyNewConfig(config);

    this->staticIp2MplsRoutesResolve<folly::IPAddressV4>();
    this->staticIp2MplsRoutesResolve<folly::IPAddressV6>();
  };
  auto verify = [=, this]() {
    {
      SCOPED_TRACE("v4");
      this->staticIp2MplsRoutesVerify<folly::IPAddressV4>();
    }
    {
      SCOPED_TRACE("v6");
      this->staticIp2MplsRoutesVerify<folly::IPAddressV6>();
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
