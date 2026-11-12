// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <boost/container/flat_set.hpp>
#include <folly/Conv.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTest.h"
#include "fboss/agent/test/utils/AgentMPLSDataplaneTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortStatsTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/types.h"

#include <fb303/ServiceData.h>
#include <gtest/gtest.h>

namespace {

namespace mpls_test = facebook::fboss::utility::mpls_dataplane_test;
using mpls_test::MplsTrapPacketMechanism;

const facebook::fboss::Label kTopLabel{1101};
const facebook::fboss::LabelForwardingAction::Label kSwapLabel{201};
constexpr uint32_t kSinglePushedLabelBase = 101;
constexpr uint32_t kMaxPushedLabelBase = 1001;
constexpr uint32_t kPopAndForwardInnerLabelBase = 2202;
constexpr uint8_t kDefaultPopAndForwardLabelTtlForTtlExpiryTrap = 2;
constexpr uint8_t kDefaultPopAndForwardLabelTtlForSrcPortAclTrap = 128;
constexpr std::array<size_t, 5> kPopAndForwardLabelStackDepths{
    2,
    11,
    14,
    16,
    32};

const facebook::fboss::Label kTtlTrapIngressLabel{3101};
const facebook::fboss::LabelForwardingAction::Label kTtlTrapSwapLabel{3201};
const std::string kMplsTtlExceededCounter =
    facebook::fboss::SwitchStats::kCounterPrefix + "mpls.ttl_exceeded.sum";

using MplsMidpointPortTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;

} // namespace

namespace facebook::fboss {

template <typename PortType>
class AgentMPLSMidpointTest : public AgentMPLSDataplaneTest<PortType> {
 protected:
  using BaseT = AgentMPLSDataplaneTest<PortType>;
  using EcmpSetupHelper =
      utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>;

  using BaseT::applyConfigAndEnableTrunks;
  using BaseT::egressPort;
  using BaseT::egressPortDescriptor;
  using BaseT::getAgentEnsemble;
  using BaseT::getLatestPortStats;
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
  using BaseT::switchIdForPort;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (BaseT::kIsTrunk) {
      return {ProductionFeature::MPLS_MIDPOINT, ProductionFeature::LAG};
    }
    return {ProductionFeature::MPLS_MIDPOINT};
  }

  MplsTrapPacketMechanism trapPacketMechanism() const {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    return asic->isSupported(HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER)
        ? MplsTrapPacketMechanism::SrcPortAcl
        : MplsTrapPacketMechanism::TtlExpiry;
  }

  std::unique_ptr<EcmpSetupHelper> setupECMPHelper(
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) const {
    return std::make_unique<EcmpSetupHelper>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        topLabel,
        actionType);
  }

  LabelForwardingAction::LabelStack singlePushedLabelStack() const {
    return pushedLabelStack(kSinglePushedLabelBase, 1);
  }

  LabelForwardingAction::LabelStack maxPushedLabelStack() const {
    return maxPushedLabelStack(kMaxPushedLabelBase);
  }

  void configureStaticMplsRoute(
      cfg::SwitchConfig& config,
      Label ingressLabel,
      const LabelForwardingAction& action,
      PortDescriptor nextHop) const {
    config.staticMplsRoutesWithNhops()->emplace_back();
    auto& route = config.staticMplsRoutesWithNhops()->back();
    route.ingressLabel() = ingressLabel.value();

    auto helper = setupECMPHelper(ingressLabel, action.type());
    auto nhop = helper->nhop(std::move(nextHop));

    NextHopThrift nextHopThrift;
    CHECK(nhop.linkLocalNhopIp.has_value());
    nextHopThrift.address() =
        network::toBinaryAddress(folly::IPAddress(*nhop.linkLocalNhopIp));
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() = action.toThrift();
    XLOG(INFO) << "MPLS midpoint route ingress label " << ingressLabel.value()
               << " uses link-local nexthop " << *nhop.linkLocalNhopIp
               << " on interface " << nhop.intf;
    route.nexthops()->push_back(nextHopThrift);
  }

  void configureStaticMplsPushRoute(
      cfg::SwitchConfig& config,
      const LabelForwardingAction::LabelStack& pushStack) const {
    configureStaticMplsRoute(
        config,
        kTopLabel,
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::PUSH, pushStack),
        egressPortDescriptor());
  }

  void configureStaticMplsPopAndForwardRoute(cfg::SwitchConfig& config) const {
    configureStaticMplsRoute(
        config,
        kTopLabel,
        LabelForwardingAction(LabelForwardingAction::LabelForwardingType::PHP),
        egressPortDescriptor());
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
      Label secondPassIngressLabel) {
    switch (mechanism) {
      case MplsTrapPacketMechanism::SrcPortAcl: {
        auto asic =
            checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
        utility::addTrapPacketAcl(asic, &config, egressPort());
        break;
      }
      case MplsTrapPacketMechanism::TtlExpiry:
        resolveNextHopForPort(
            PortDescriptor(secondPassEgressPort()),
            secondPassIngressLabel,
            LabelForwardingAction::LabelForwardingType::SWAP);
        configureStaticMplsSwapRoute(
            config,
            secondPassIngressLabel,
            kSwapLabel,
            PortDescriptor(secondPassEgressPort()));
        break;
    }
  }

  void resolveNextHopForPort(
      const PortDescriptor& nextHop,
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) {
    this->applyNewState(
        [this, nextHop, topLabel, actionType](
            const std::shared_ptr<SwitchState>& state) {
          auto helper = EcmpSetupHelper(
              state, getSw()->needL2EntryForNeighbor(), topLabel, actionType);
          return helper.resolveNextHops(
              state,
              boost::container::flat_set<PortDescriptor>{nextHop},
              true /* useLinkLocal */);
        },
        "resolve midpoint MPLS nexthop");
  }

  void resolveNextHop() {
    resolveNextHopForPort(
        egressPortDescriptor(),
        kTopLabel,
        LabelForwardingAction::LabelForwardingType::PUSH);
  }

  void resolveNextHopForPortWithMac(
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
        "resolve midpoint MPLS nexthop with explicit MAC");
  }

  void unresolveNextHopForPortWithMac(
      const PortDescriptor& nextHop,
      folly::MacAddress nextHopMac) {
    this->applyNewState(
        [this, nextHop, nextHopMac](const std::shared_ptr<SwitchState>& state) {
          utility::EcmpSetupTargetedPorts6 helper(
              state, getSw()->needL2EntryForNeighbor(), nextHopMac);
          return helper.unresolveNextHops(
              state,
              boost::container::flat_set<PortDescriptor>{nextHop},
              true /* useLinkLocal */);
        },
        "unresolve midpoint MPLS nexthop with explicit MAC");
  }

  void flapEgressPortAndReresolveNextHop() {
    this->bringDownPort(egressPort());
    unresolveNextHopForPortWithMac(egressPortDescriptor(), routerMac());
    this->bringUpPort(egressPort());

    // After the SAI fast-path link-down handler disables the single LAG member,
    // tests need a switch state delta to re-enable the member because LACP is
    // not running.
    if constexpr (BaseT::kIsTrunk) {
      this->applyNewState(
          [](const std::shared_ptr<SwitchState>& state) {
            return utility::disableTrunkPorts(state);
          },
          "disable trunk ports to sync with SAI state");
      this->applyNewState(
          [](const std::shared_ptr<SwitchState>& state) {
            return utility::enableTrunkPorts(state);
          },
          "re-enable trunk ports after link flap");
    }

    resolveNextHopForPortWithMac(egressPortDescriptor(), routerMac());
  }

  std::unique_ptr<TxPacket>
  makeMplsIngressPacket(Label label, uint8_t ttl, bool isV4) const {
    // nullopt on port based router interface platforms; frame goes out untagged
    auto vlan = getVlanIDForTx();

    MPLSHdr::Label mplsLabel{
        static_cast<uint32_t>(label.value()), 0, true, ttl};
    std::unique_ptr<TxPacket> pkt;
    if (isV4) {
      auto frame = utility::getEthFrame(
          utility::kLocalCpuMac(),
          folly::MacAddress{"02:00:00:00:00:02"},
          {mplsLabel},
          folly::IPAddressV4{"100.1.1.1"},
          folly::IPAddressV4{"200.1.1.1"},
          10000,
          20000,
          vlan);
      pkt = frame.getTxPacket(
          [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
    } else {
      auto frame = utility::getEthFrame(
          utility::kLocalCpuMac(),
          folly::MacAddress{"02:00:00:00:00:02"},
          {mplsLabel},
          folly::IPAddressV6{"1001::1"},
          folly::IPAddressV6{"2001::1"},
          10000,
          20000,
          vlan);
      pkt = frame.getTxPacket(
          [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
    }
    return pkt;
  }

  std::unique_ptr<TxPacket> makeMplsLabelStackIngressPacket(
      size_t labelStackDepth,
      uint8_t ttl) const {
    // nullopt on port based router interface platforms; frame goes out untagged
    auto vlan = getVlanIDForTx();

    std::vector<MPLSHdr::Label> labels;
    labels.reserve(labelStackDepth);
    labels.emplace_back(
        static_cast<uint32_t>(kTopLabel.value()), 0, false, ttl);
    for (size_t i = 1; i < labelStackDepth; ++i) {
      labels.emplace_back(
          kPopAndForwardInnerLabelBase + i - 1,
          0,
          i + 1 == labelStackDepth,
          ttl);
    }

    auto frame = utility::getEthFrame(
        utility::kLocalCpuMac(),
        routerMac(),
        labels,
        folly::IPAddressV6{"1001::1"},
        folly::IPAddressV6{"2001::1"},
        10000,
        20000,
        vlan);
    return frame.getTxPacket(
        [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
  }

  std::vector<uint32_t> expectedPopAndForwardLabels(
      size_t labelStackDepth) const {
    std::vector<uint32_t> expectedLabels;
    expectedLabels.reserve(labelStackDepth);
    for (size_t i = 1; i < labelStackDepth; ++i) {
      expectedLabels.push_back(kPopAndForwardInnerLabelBase + i - 1);
    }
    return expectedLabels;
  }

  void sendMplsIngressPacket(
      Label label,
      uint8_t ttl,
      bool isV4,
      std::optional<PortID> injectPort) {
    auto pkt = makeMplsIngressPacket(label, ttl, isV4);
    XLOG(INFO) << "MPLS midpoint injected packet hexdump label "
               << label.value() << " ttl " << static_cast<int>(ttl)
               << " payload " << (isV4 ? "IPv4" : "IPv6") << " send "
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

  void sendMplsLabelStackIngressPacket(
      size_t labelStackDepth,
      uint8_t ttl,
      PortID injectPort) {
    auto pkt = makeMplsLabelStackIngressPacket(labelStackDepth, ttl);
    XLOG(INFO) << "MPLS midpoint injected " << labelStackDepth
               << "-label packet hexdump top label " << kTopLabel.value()
               << " ttl " << static_cast<int>(ttl) << " send front-panel:\n"
               << folly::hexDump(pkt->buf()->data(), pkt->buf()->length());
    EXPECT_TRUE(
        getAgentEnsemble()->ensureSendPacketOutOfPort(
            std::move(pkt), injectPort));
  }

  void setupStaticMplsRoutePush(
      const LabelForwardingAction::LabelStack& pushStack) {
    auto mechanism = trapPacketMechanism();
    auto config = initialConfig(*getAgentEnsemble());

    applyConfigAndEnableTrunks(config);
    // Resolve the PUSH nexthop before programming the MPLS route. TH6/BRCM-SAI
    // currently requires the MPLS nexthop object to exist when the InSeg entry
    // is created. A follow-up link-flap test covers nexthop unresolve and
    // re-resolve convergence after the route has been programmed.
    resolveNextHopForPortWithMac(egressPortDescriptor(), routerMac());

    // TTL-expiry fallback traps the post-PUSH packet on its second pass:
    // - The first pass imposes the pushed label and egresses to a loopback
    // port.
    // - The looped packet uses the router MAC so it gets routed again.
    // - The second pass matches the pushed-label SWAP route and expires TTL.
    // - The MPLS TTL trap sends the packet to CPU for packet snooper
    // inspection.
    configureStaticMplsPushRoute(config, pushStack);
    configureTrapPacketMechanism(config, mechanism, pushedTopLabel(pushStack));
    applyConfigAndEnableTrunks(config);
  }

  void setupStaticMplsPopAndForwardRoute() {
    auto config = initialConfig(*getAgentEnsemble());

    applyConfigAndEnableTrunks(config);
    resolveNextHopForPortWithMac(egressPortDescriptor(), routerMac());

    configureStaticMplsPopAndForwardRoute(config);
    // The pop exposes the inner label, so that is what the second pass
    // matches.
    configureTrapPacketMechanism(
        config, trapPacketMechanism(), Label(kPopAndForwardInnerLabelBase));
    applyConfigAndEnableTrunks(config);
  }

  void verifyMplsPushAndTrapPacket(
      bool isV4,
      std::optional<PortID> injectPort,
      const LabelForwardingAction::LabelStack& expectedPushStack) {
    auto mechanism = trapPacketMechanism();
    BaseT::verifyMplsPushAndTrapPacket(
        "mpls-midpoint-push-verifier",
        isV4,
        injectPort,
        mechanism,
        expectedPushStack,
        [this, isV4, injectPort](uint8_t ttl) {
          sendMplsIngressPacket(kTopLabel, ttl, isV4, injectPort);
        });
  }

  void verifyMplsPopAndForward(size_t labelStackDepth) {
    SCOPED_TRACE(
        folly::to<std::string>(
            "send=front-panel isTrunk=",
            BaseT::kIsTrunk,
            " labelStackDepth=",
            labelStackDepth));

    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mpls-midpoint-pop-and-forward-verifier",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        utility::packetSnooperReceivePacketType::PACKET_TYPE_ALL);
    snooper.ignoreUnclaimedRxPkts();

    auto outPktsBefore =
        utility::getPortOutPkts(this->getLatestPortStats(egressPort()));

    const auto ttl = trapPacketMechanism() == MplsTrapPacketMechanism::TtlExpiry
        ? kDefaultPopAndForwardLabelTtlForTtlExpiryTrap
        : kDefaultPopAndForwardLabelTtlForSrcPortAclTrap;
    sendMplsLabelStackIngressPacket(labelStackDepth, ttl, ingressPort());

    WITH_RETRIES({
      auto outPktsAfter =
          utility::getPortOutPkts(this->getLatestPortStats(egressPort()));
      EXPECT_EVENTUALLY_EQ(1, outPktsAfter - outPktsBefore);
    });

    auto pktBuf = snooper.waitForPacket(10);
    ASSERT_TRUE(pktBuf.has_value());
    ASSERT_TRUE(*pktBuf);
    XLOG(INFO) << "MPLS dataplane pop-and-forward trapped packet hexdump:\n"
               << folly::hexDump((*pktBuf)->data(), (*pktBuf)->length());

    folly::io::Cursor cursor((*pktBuf).get());
    utility::EthFrame frame(cursor);

    auto mplsPayload = frame.mplsPayLoad();
    ASSERT_TRUE(mplsPayload.has_value());

    const auto& mplsHeader = mplsPayload->header();
    const auto& labelStack = mplsHeader.stack();
    XLOG(INFO) << "MPLS dataplane pop-and-forward captured header "
               << mplsHeader;

    const auto expectedLabels = expectedPopAndForwardLabels(labelStackDepth);
    const auto capturedLabels = mpls_test::capturedLabelValues(labelStack);
    EXPECT_EQ(expectedLabels, capturedLabels);
    EXPECT_TRUE(mpls_test::bottomOfStackBitsValid(labelStack));
  }
};

TYPED_TEST_SUITE(AgentMPLSMidpointTest, MplsMidpointPortTypes);

// PushLabel verifies MPLS midpoint PUSH behavior across:
// - IPv4 and IPv6 payloads carried inside the injected MPLS packet.
// - Front-panel and CPU-switched injection paths.
// - Physical-port and single-port LAG nexthops.
// - First-pass egress forwarding to prove the route imposed labels.
// - Trapped packet label-stack inspection:
//   - If src-port ACL is supported, trap packets looped back from the egress
//     port because those packets have already completed the PUSH pass.
//   - Otherwise, use MPLS TTL expiry on the second pass: the looped packet
//     routes again with router MAC, matches the pushed label, expires TTL, and
//     reaches CPU through the MPLS TTL trap.
// Note: the TTL-expiry fallback is needed because programming a simple "trap
// MPLS packet to CPU" inSegEntry does not work and needs an SAI SDK fix.
TYPED_TEST(AgentMPLSMidpointTest, PushLabel) {
  auto setup = [this]() {
    this->setupStaticMplsRoutePush(this->singlePushedLabelStack());
  };

  auto verify = [this]() {
    auto pushStack = this->singlePushedLabelStack();
    const std::array<std::optional<PortID>, 2> injectPorts{
        std::nullopt,
        std::optional<PortID>{this->ingressPort()},
    };
    for (bool isV4 : {false, true}) {
      for (auto injectPort : injectPorts) {
        this->verifyMplsPushAndTrapPacket(isV4, injectPort, pushStack);
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// PushLabelAfterLinkFlap verifies that an already-programmed MPLS PUSH route
// converges after its egress nexthop link flaps:
// - Program and verify the normal MPLS midpoint PUSH route.
// - Bring the egress link down and unresolve the nexthop.
// - Bring the egress link up and re-resolve the nexthop.
// - Verify PUSH dataplane forwarding and trapped pushed-label inspection.
TYPED_TEST(AgentMPLSMidpointTest, PushLabelAfterLinkFlap) {
  auto setup = [this]() {
    this->setupStaticMplsRoutePush(this->singlePushedLabelStack());
    this->flapEgressPortAndReresolveNextHop();
  };

  auto verify = [this]() {
    auto pushStack = this->singlePushedLabelStack();
    const std::array<std::optional<PortID>, 2> injectPorts{
        std::nullopt,
        std::optional<PortID>{this->ingressPort()},
    };
    for (auto injectPort : injectPorts) {
      this->verifyMplsPushAndTrapPacket(
          false /* isV4 */, injectPort, pushStack);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// PopAndForwardPreservesInnerMplsLabels verifies midpoint pop-and-forward
// behavior for packets that remain MPLS after the outer label is popped:
// - Program only the top label route, so forwarding is selected by that route.
// - Inject packets with multiple labels whose inner labels are not separately
//   programmed.
// - Verify first-pass egress forwarding to the route nexthop.
// - Trap the looped post-pop-and-forward packet with src-port ACL and verify
//   only the outer MPLS label is removed.
TYPED_TEST(AgentMPLSMidpointTest, PopAndForwardPreservesInnerMplsLabels) {
  auto setup = [this]() { this->setupStaticMplsPopAndForwardRoute(); };

  auto verify = [this]() {
    for (auto labelStackDepth : kPopAndForwardLabelStackDepths) {
      this->verifyMplsPopAndForward(labelStackDepth);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// PushMaxLabelStack verifies that midpoint PUSH can impose the maximum label
// depth reported by the ASIC and that the captured packet carries the full
// stack in wire/top-first order.
TYPED_TEST(AgentMPLSMidpointTest, PushMaxLabelStack) {
  auto setup = [this]() {
    this->setupStaticMplsRoutePush(this->maxPushedLabelStack());
  };

  auto verify = [this]() {
    auto pushStack = this->maxPushedLabelStack();
    const std::array<std::optional<PortID>, 2> injectPorts{
        std::nullopt,
        std::optional<PortID>{this->ingressPort()},
    };
    for (bool isV4 : {false, true}) {
      for (auto injectPort : injectPorts) {
        this->verifyMplsPushAndTrapPacket(isV4, injectPort, pushStack);
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSMidpointTest, MplsTtlExpiryTrap) {
  auto setup = [this]() {
    auto config = this->initialConfig(*this->getAgentEnsemble());
    this->applyConfigAndEnableTrunks(config);
    this->resolveNextHopForPortWithMac(
        this->egressPortDescriptor(), this->routerMac());
    this->configureStaticMplsSwapRoute(
        config,
        kTtlTrapIngressLabel,
        kTtlTrapSwapLabel,
        this->egressPortDescriptor());
    this->applyConfigAndEnableTrunks(config);
  };

  auto verify = [this]() {
    // TODO: Debug CPU-port packet injection failure and extend this test to
    // cover both CPU and front-panel injection.
    const std::optional<PortID> injectPort{this->ingressPort()};
    for (bool isV4 : {false, true}) {
      SCOPED_TRACE(
          folly::to<std::string>(
              "ipVersion=", isV4 ? "IPv4" : "IPv6", " send=front-panel"));

      // Verify TTL=1 traps to CPU and delivers the trapped packet.
      {
        auto cpuBefore = utility::getQueueOutPacketsWithRetry(
            this->getSw(),
            this->switchIdForPort(this->egressPort()),
            utility::kCoppLowPriQueueId,
            0 /* retryTimes */,
            0 /* expectedNumPkts */);
        auto numTtlExceededPktsBefore =
            fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter)
                .value_or(0);
        utility::SwSwitchPacketSnooper snooper(
            this->getSw(),
            "mpls-ttl-expiry-trap",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            utility::packetSnooperReceivePacketType::PACKET_TYPE_ALL);
        snooper.ignoreUnclaimedRxPkts();

        // A packet with MPLS TTL 1 should expire and trap to CPU.
        this->sendMplsIngressPacket(
            kTtlTrapIngressLabel, 1 /* ttl */, isV4, injectPort);

        WITH_RETRIES({
          auto cpuAfter = utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->egressPort()),
              utility::kCoppLowPriQueueId,
              0 /* retryTimes */,
              cpuBefore + 1);
          EXPECT_EVENTUALLY_EQ(1, cpuAfter - cpuBefore);
        });
        auto pktBuf = snooper.waitForPacket(10);
        ASSERT_TRUE(pktBuf.has_value());
        ASSERT_TRUE(*pktBuf);
        folly::io::Cursor cursor((*pktBuf).get());
        utility::EthFrame frame(cursor);
        ASSERT_TRUE(frame.mplsPayLoad().has_value());
        WITH_RETRIES({
          auto numTtlExceededPktsAfter =
              fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter)
                  .value_or(0);
          EXPECT_EVENTUALLY_EQ(
              1, numTtlExceededPktsAfter - numTtlExceededPktsBefore);
        });
      }

      // Verify TTL=64 forwards without TTL-expiry trapping.
      {
        auto outPktsBefore = utility::getPortOutPkts(
            this->getLatestPortStats(this->egressPort()));
        auto numTtlExceededPktsBefore =
            fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter)
                .value_or(0);

        // A packet with MPLS TTL 64 should not trap. kTtlTrapSwapLabel has no
        // downstream InSeg entry, so first-pass egress forwarding validates the
        // non-expired path without relying on a second-pass MPLS route.
        this->sendMplsIngressPacket(
            kTtlTrapIngressLabel, 64 /* ttl */, isV4, injectPort);

        WITH_RETRIES({
          auto statsAfter = this->getLatestPortStats(this->egressPort());
          auto outPktsAfter = utility::getPortOutPkts(statsAfter);
          EXPECT_EVENTUALLY_EQ(1, outPktsAfter - outPktsBefore);
          auto numTtlExceededPktsAfter =
              fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter)
                  .value_or(0);
          EXPECT_EVENTUALLY_EQ(
              0, numTtlExceededPktsAfter - numTtlExceededPktsBefore);
        });
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
