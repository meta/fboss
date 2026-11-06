// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <boost/container/flat_set.hpp>
#include <folly/Conv.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include <array>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTest.h"
#include "fboss/agent/test/utils/AgentMPLSDataplaneTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/types.h"

#include <gtest/gtest.h>

namespace {

namespace mpls_test = facebook::fboss::utility::mpls_dataplane_test;
using mpls_test::MplsTrapPacketMechanism;

const facebook::fboss::LabelForwardingAction::Label kSwapLabel{201};
constexpr uint32_t kSinglePushedLabelBase = 101;
constexpr uint32_t kMaxPushedLabelBase = 1001;
constexpr auto kUdpPayloadSize = 256;

using MplsHeadEndPortTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;

} // namespace

namespace facebook::fboss {

template <typename AddrT>
RoutePrefix<AddrT> headEndIp2MplsRoutePrefix();

template <>
RoutePrefix<folly::IPAddressV4> headEndIp2MplsRoutePrefix() {
  return {folly::IPAddressV4{"200.1.1.0"}, 24};
}

template <>
RoutePrefix<folly::IPAddressV6> headEndIp2MplsRoutePrefix() {
  return {folly::IPAddressV6{"2001::"}, 64};
}

template <typename AddrT>
AddrT headEndIngressPacketSrcIp();

template <>
folly::IPAddressV4 headEndIngressPacketSrcIp() {
  return folly::IPAddressV4{"100.1.1.1"};
}

template <>
folly::IPAddressV6 headEndIngressPacketSrcIp() {
  return folly::IPAddressV6{"1001::1"};
}

template <typename AddrT>
AddrT headEndIngressPacketDstIp();

template <>
folly::IPAddressV4 headEndIngressPacketDstIp() {
  return folly::IPAddressV4{"200.1.1.1"};
}

template <>
folly::IPAddressV6 headEndIngressPacketDstIp() {
  return folly::IPAddressV6{"2001::1"};
}

template <typename PortType>
class AgentMPLSHeadEndTest : public AgentMPLSDataplaneTest<PortType> {
 protected:
  using BaseT = AgentMPLSDataplaneTest<PortType>;
  using MplsEcmpSetupHelper =
      utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>;

  using BaseT::applyConfigAndEnableTrunks;
  using BaseT::egressPort;
  using BaseT::egressPortDescriptor;
  using BaseT::getAgentEnsemble;
  using BaseT::getProgrammedState;
  using BaseT::getSw;
  using BaseT::getVlanIDForTx;
  using BaseT::ingressPort;
  using BaseT::initialConfig;
  using BaseT::maxPushedLabelStack;
  using BaseT::pushedLabelStack;
  using BaseT::pushedTopLabel;
  using BaseT::routerMac;
  using BaseT::secondPassEgressPort;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (BaseT::kIsTrunk) {
      return {ProductionFeature::MPLS_HEADEND, ProductionFeature::LAG};
    }
    return {ProductionFeature::MPLS_HEADEND};
  }

  MplsTrapPacketMechanism trapPacketMechanism() const {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    return asic->isSupported(HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER)
        ? MplsTrapPacketMechanism::SrcPortAcl
        : MplsTrapPacketMechanism::TtlExpiry;
  }

  LabelForwardingAction::LabelStack singlePushedLabelStack() const {
    return pushedLabelStack(kSinglePushedLabelBase, 1);
  }

  LabelForwardingAction::LabelStack maxPushedLabelStack() const {
    return maxPushedLabelStack(kMaxPushedLabelBase);
  }

  std::unique_ptr<utility::EcmpSetupTargetedPorts6> setupIpEcmpHelper() const {
    return std::make_unique<utility::EcmpSetupTargetedPorts6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
  }

  std::unique_ptr<MplsEcmpSetupHelper> setupMplsECMPHelper(
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) const {
    return std::make_unique<MplsEcmpSetupHelper>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        topLabel,
        actionType);
  }

  template <typename AddrT>
  void configureStaticIp2MplsPushRoute(
      cfg::SwitchConfig& config,
      const LabelForwardingAction::LabelStack& pushStack) const {
    config.staticIp2MplsRoutes()->emplace_back();
    auto& route = config.staticIp2MplsRoutes()->back();
    auto prefix = headEndIp2MplsRoutePrefix<AddrT>().str();
    route.prefix() = prefix;

    auto helper = setupIpEcmpHelper();
    auto nhop = helper->nhop(egressPortDescriptor());

    NextHopThrift nextHopThrift;
    CHECK(nhop.linkLocalNhopIp.has_value());
    nextHopThrift.address() =
        network::toBinaryAddress(folly::IPAddress(*nhop.linkLocalNhopIp));
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() =
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::PUSH, pushStack)
            .toThrift();
    XLOG(INFO) << "MPLS head-end IP-to-MPLS route prefix " << prefix
               << " uses link-local nexthop " << *nhop.linkLocalNhopIp
               << " on interface " << nhop.intf;
    route.nexthops()->push_back(nextHopThrift);
  }

  void configureStaticMplsRoute(
      cfg::SwitchConfig& config,
      Label ingressLabel,
      const LabelForwardingAction& action,
      PortDescriptor nextHop) const {
    config.staticMplsRoutesWithNhops()->emplace_back();
    auto& route = config.staticMplsRoutesWithNhops()->back();
    route.ingressLabel() = ingressLabel.value();

    auto helper = setupMplsECMPHelper(ingressLabel, action.type());
    auto nhop = helper->nhop(std::move(nextHop));

    NextHopThrift nextHopThrift;
    CHECK(nhop.linkLocalNhopIp.has_value());
    nextHopThrift.address() =
        network::toBinaryAddress(folly::IPAddress(*nhop.linkLocalNhopIp));
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() = action.toThrift();
    XLOG(INFO) << "MPLS head-end trap MPLS route ingress label "
               << ingressLabel.value() << " uses link-local nexthop "
               << *nhop.linkLocalNhopIp << " on interface " << nhop.intf;
    route.nexthops()->push_back(nextHopThrift);
  }

  void configureStaticMplsSwapRoute(
      cfg::SwitchConfig& config,
      Label ingressLabel,
      LabelForwardingAction::Label swapLabel,
      PortDescriptor nextHop) const {
    configureStaticMplsRoute(
        config,
        ingressLabel,
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::SWAP, swapLabel),
        std::move(nextHop));
  }

  void configureTrapPacketMechanism(
      cfg::SwitchConfig& config,
      MplsTrapPacketMechanism mechanism,
      const LabelForwardingAction::LabelStack& pushStack) const {
    switch (mechanism) {
      case MplsTrapPacketMechanism::SrcPortAcl: {
        auto asic =
            checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
        utility::addTrapPacketAcl(asic, &config, egressPort());
        break;
      }
      case MplsTrapPacketMechanism::TtlExpiry:
        configureStaticMplsSwapRoute(
            config,
            pushedTopLabel(pushStack),
            kSwapLabel,
            PortDescriptor(secondPassEgressPort()));
        break;
    }
  }

  void resolveIpNextHopForPortWithMac(
      const PortDescriptor& nextHop,
      folly::MacAddress nextHopMac) {
    this->applyNewState(
        [this, nextHop, nextHopMac](const std::shared_ptr<SwitchState>& state) {
          utility::EcmpSetupTargetedPorts6 helper(
              state, getSw()->needL2EntryForNeighbor(), nextHopMac);
          return helper.resolveNextHops(
              state,
              boost::container::flat_set<PortDescriptor>{nextHop},
              true /* useLinkLocal */);
        },
        "resolve head-end IP nexthop with explicit MAC");
  }

  void resolveMplsNextHopForPort(
      const PortDescriptor& nextHop,
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) {
    this->applyNewState(
        [this, nextHop, topLabel, actionType](
            const std::shared_ptr<SwitchState>& state) {
          auto helper = MplsEcmpSetupHelper(
              state, getSw()->needL2EntryForNeighbor(), topLabel, actionType);
          return helper.resolveNextHops(
              state,
              boost::container::flat_set<PortDescriptor>{nextHop},
              true /* useLinkLocal */);
        },
        "resolve head-end MPLS nexthop");
  }

  template <typename AddrT>
  utility::EthFrame makeIpIngressFrame(uint8_t ttlOrHopLimit) const {
    // nullopt on port based router interface platforms; frame goes out untagged
    auto vlan = getVlanIDForTx();

    constexpr auto isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
    constexpr auto etherType =
        isV4 ? ETHERTYPE::ETHERTYPE_IPV4 : ETHERTYPE::ETHERTYPE_IPV6;
    EthHdr::VlanTags_t tags;
    if (vlan.has_value()) {
      tags.push_back(VlanTag(*vlan, 0x8100));
    }
    EthHdr ethHdr{
        utility::kLocalCpuMac(),
        folly::MacAddress{"02:00:00:00:00:02"},
        {tags},
        static_cast<uint16_t>(etherType)};

    std::conditional_t<isV4, IPv4Hdr, IPv6Hdr> ipHdr;
    ipHdr.srcAddr = headEndIngressPacketSrcIp<AddrT>();
    ipHdr.dstAddr = headEndIngressPacketDstIp<AddrT>();
    ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));
    if constexpr (isV4) {
      ipHdr.ttl = ttlOrHopLimit;
    } else {
      ipHdr.hopLimit = ttlOrHopLimit;
    }

    UDPHeader udpHdr;
    udpHdr.srcPort = 10000;
    udpHdr.dstPort = 20000;

    return utility::EthFrame(
        ethHdr,
        utility::IPPacket<AddrT>(
            ipHdr,
            utility::UDPDatagram(
                udpHdr, std::vector<uint8_t>(kUdpPayloadSize, 0xff))));
  }

  template <typename AddrT>
  std::unique_ptr<TxPacket> makeIpIngressPacket(uint8_t ttlOrHopLimit) const {
    auto frame = makeIpIngressFrame<AddrT>(ttlOrHopLimit);
    return frame.getTxPacket(
        [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
  }

  template <typename AddrT>
  void sendIpIngressPacket(
      uint8_t ttlOrHopLimit,
      std::optional<PortID> injectPort) {
    auto pkt = makeIpIngressPacket<AddrT>(ttlOrHopLimit);
    constexpr bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
    XLOG(INFO) << "MPLS head-end injected packet hexdump ttlOrHopLimit "
               << static_cast<int>(ttlOrHopLimit) << " payload "
               << (isV4 ? "IPv4" : "IPv6") << " send "
               << (injectPort.has_value() ? "front-panel" : "cpu") << ":\n"
               << folly::hexDump(pkt->buf()->data(), pkt->buf()->length());
    if (injectPort.has_value()) {
      EXPECT_TRUE(
          getAgentEnsemble()->ensureSendPacketOutOfPort(
              std::move(pkt), *injectPort));
    } else {
      EXPECT_TRUE(getAgentEnsemble()->ensureSendPacketSwitched(std::move(pkt)));
    }
  }

  template <typename AddrT>
  void setupStaticIp2MplsRoutePush(
      const LabelForwardingAction::LabelStack& pushStack) {
    auto mechanism = trapPacketMechanism();
    auto config = initialConfig(*getAgentEnsemble());
    configureStaticIp2MplsPushRoute<AddrT>(config, pushStack);
    configureTrapPacketMechanism(config, mechanism, pushStack);
    applyConfigAndEnableTrunks(config);

    resolveIpNextHopForPortWithMac(egressPortDescriptor(), routerMac());
    if (mechanism == MplsTrapPacketMechanism::TtlExpiry) {
      resolveMplsNextHopForPort(
          PortDescriptor(secondPassEgressPort()),
          pushedTopLabel(pushStack),
          LabelForwardingAction::LabelForwardingType::SWAP);
    }
  }

  void setupStaticIp2MplsRoutePush(
      const LabelForwardingAction::LabelStack& pushStack) {
    auto mechanism = trapPacketMechanism();
    auto config = initialConfig(*getAgentEnsemble());
    configureStaticIp2MplsPushRoute<folly::IPAddressV4>(config, pushStack);
    configureStaticIp2MplsPushRoute<folly::IPAddressV6>(config, pushStack);
    configureTrapPacketMechanism(config, mechanism, pushStack);
    applyConfigAndEnableTrunks(config);

    resolveIpNextHopForPortWithMac(egressPortDescriptor(), routerMac());
    if (mechanism == MplsTrapPacketMechanism::TtlExpiry) {
      resolveMplsNextHopForPort(
          PortDescriptor(secondPassEgressPort()),
          pushedTopLabel(pushStack),
          LabelForwardingAction::LabelForwardingType::SWAP);
    }
  }

  template <typename AddrT>
  void verifyIp2MplsPushAndTrapPacket(
      std::optional<PortID> injectPort,
      const LabelForwardingAction::LabelStack& expectedPushStack) {
    auto mechanism = trapPacketMechanism();
    constexpr bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
    BaseT::verifyMplsPushAndTrapPacket(
        "mpls-head-end-push-verifier",
        isV4,
        injectPort,
        mechanism,
        expectedPushStack,
        [this, injectPort](uint8_t ttlOrHopLimit) {
          sendIpIngressPacket<AddrT>(ttlOrHopLimit, injectPort);
        });
  }

  void verifyIp2MplsPush(
      const LabelForwardingAction::LabelStack& expectedPushStack) {
    const std::array<std::optional<PortID>, 2> injectPorts{
        std::nullopt,
        std::optional<PortID>{ingressPort()},
    };
    for (auto injectPort : injectPorts) {
      verifyIp2MplsPushAndTrapPacket<folly::IPAddressV4>(
          injectPort, expectedPushStack);
      verifyIp2MplsPushAndTrapPacket<folly::IPAddressV6>(
          injectPort, expectedPushStack);
    }
  }
};

TYPED_TEST_SUITE(AgentMPLSHeadEndTest, MplsHeadEndPortTypes);

// PushLabel verifies MPLS head-end PUSH behavior across:
// - IPv4 and IPv6 IP packets injected into the switch.
// - Front-panel and CPU-switched injection paths.
// - Physical-port and single-port LAG nexthops.
// - First-pass IP forwarding to prove the IP-to-MPLS route imposed a label.
// - Trapped packet label-stack inspection:
//   - If src-port ACL is supported, trap packets looped back from the egress
//     port because those packets have already completed the PUSH pass.
//   - Otherwise, use MPLS TTL expiry on the second pass: the looped packet
//     routes again with router MAC, matches the pushed label, expires TTL, and
//     reaches CPU through the MPLS TTL trap.
// Note: the TTL-expiry fallback is needed because programming a simple "trap
// MPLS packet to CPU" inSegEntry does not work and needs an SAI SDK fix.
TYPED_TEST(AgentMPLSHeadEndTest, PushLabel) {
  auto setup = [this]() {
    this->setupStaticIp2MplsRoutePush(this->singlePushedLabelStack());
  };

  auto verify = [this]() {
    this->verifyIp2MplsPush(this->singlePushedLabelStack());
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// PushMaxLabelStack verifies that head-end PUSH can impose the maximum label
// depth reported by the ASIC for IP-to-MPLS routes and that the captured packet
// carries the full stack in wire/top-first order.
TYPED_TEST(AgentMPLSHeadEndTest, PushMaxLabelStack) {
  auto setup = [this]() {
    this->setupStaticIp2MplsRoutePush(this->maxPushedLabelStack());
  };

  auto verify = [this]() {
    this->verifyIp2MplsPush(this->maxPushedLabelStack());
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
