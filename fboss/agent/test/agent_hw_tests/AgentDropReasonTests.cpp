// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentDropTestBase.h"
#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

class AgentDropReasonTest : public AgentDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // Discovery runs without an ensemble. This common gate covers either
    // reporting mechanism; the adapter is chosen once hardware is available.
    return {ProductionFeature::DROP_REASON_REPORTING};
  }

 protected:
  DropReasonReportingImpl& dropReasonReporting() {
    if (!dropReasonReportingImpl_) {
      DropReasonReportingContext context;
      context.readLogMessages = [this]() {
        std::vector<std::string> messages;
        for (const auto& switchId :
             getSw()->getSwitchInfoTable().getSwitchIDs()) {
          std::vector<std::string> matches;
          getAgentEnsemble()
              ->getHwAgentTestClient(switchId)
              ->sync_getMatchingLogMessages(matches, "DROP reasons ");
          messages.insert(messages.end(), matches.begin(), matches.end());
        }
        return messages;
      };
      context.readSwitchStats = [this]() { return getAllHwSwitchStats(); };
      for (const auto& switchIndex :
           getSw()->getSwitchInfoTable().getSwitchIndices()) {
        context.switchIndices.push_back(switchIndex);
      }
      const auto* asic = checkSameAndGetAsicForTesting(getL3Asics());
      dropReasonReportingImpl_ = createDropReasonReportingImpl(
          asic->getAsicType(), std::move(context));
    }
    return *dropReasonReportingImpl_;
  }

  // Log the drop reasons after every collection, not just the final state.
  // If an expectation is wrong, the run shows what the ASIC reported, which is
  // what it takes to correct the expected name without another round trip.
  void logObserved(const char* desc, const DropReasonReportingImpl& reporting) {
    XLOG(INFO) << "Drop reason test [" << desc
               << "] observed: " << reporting.dropReasonToString();
  }

  void setupDropScenario(DropScenario scenario) {
    switch (scenario) {
      case DropScenario::EgressMtu:
        setupEgressMtuDropScenario();
        return;
      case DropScenario::TtlError:
        setupRouteToEgressPort();
        return;
      case DropScenario::SrcPortKnockout:
        setupSrcPortKnockoutDropScenario();
        return;
      case DropScenario::AclDeny: {
        setupRouteToEgressPort();
        auto config = getAgentEnsemble()->getCurrentConfig();
        cfg::AclEntry acl;
        acl.name() = "drop-reason-deny-v6";
        acl.actionType() = cfg::AclActionType::DENY;
        acl.dstIp() = kRoutedDstIp().str() + "/128";
        utility::addAcl(&config, acl, cfg::AclStage::INGRESS);
        applyNewConfig(config);
        return;
      }
      case DropScenario::L3DstDiscard:
      case DropScenario::MacSaMulticast:
      case DropScenario::Ipv4HeaderError:
      case DropScenario::Ipv6HeaderError:
      case DropScenario::InvalidEtherType:
        return;
    }
  }

  void triggerDropScenario(DropScenario scenario) {
    switch (scenario) {
      case DropScenario::L3DstDiscard:
        sendPacketToUnroutedDst();
        return;
      case DropScenario::TtlError:
        sendTtlExpiredPacket();
        return;
      case DropScenario::MacSaMulticast:
        sendMulticastSmacPacket();
        return;
      case DropScenario::AclDeny:
        sendPacketToRoutedDst();
        return;
      case DropScenario::SrcPortKnockout:
        sendSrcPortKnockoutPacket();
        return;
      case DropScenario::EgressMtu:
        sendOversizedPacketToRoutedDst();
        return;
      case DropScenario::Ipv4HeaderError:
        sendInvalidIpVersionPacket<folly::IPAddressV4>();
        return;
      case DropScenario::Ipv6HeaderError:
        sendInvalidIpVersionPacket<folly::IPAddressV6>();
        return;
      case DropScenario::InvalidEtherType:
        sendOutOfRangeEtherTypePacket();
        return;
    }
  }

  void runDropScenarios(
      std::initializer_list<DropScenario> scenarios,
      const char* name) {
    auto& reporting = dropReasonReporting();
    auto setup = [&]() {
      for (const auto scenario : scenarios) {
        setupDropScenario(scenario);
      }
    };
    auto verify = [&]() {
      for (const auto scenario : scenarios) {
        // Reset between phases as well as across warm boots, so the previous
        // packet's drop cannot satisfy the next scenario's assertion.
        ASSERT_NO_FATAL_FAILURE(reporting.prepareDropObservation());
        // Start capture after draining so old drops cannot satisfy this packet.
        installLogCapture();
        WITH_RETRIES({
          triggerDropScenario(scenario);
          reporting.collectDropObservation();
          logObserved(name, reporting);
          EXPECT_EVENTUALLY_TRUE(
              reporting.expectedDropReasonObserved(scenario));
        });
        logPortDropCounters(name);
        XLOG(INFO) << "Drop reason test [" << name
                   << "] final: " << reporting.dropReasonToString();
        reporting.verifyOnlyExpectedDropReasonReported(scenario);
        verifyDropReasonLogged(reporting.expectedDropLog(scenario), name);
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }

 private:
  std::unique_ptr<DropReasonReportingImpl> dropReasonReportingImpl_;
};

// A packet to an address with no matching route is discarded at L3
// destination resolution. The ASIC reports L3_DST_DISCARD, not
// L3_DST_LOOKUP_MISS, accompanied by the generic RDROP.
TEST_F(AgentDropReasonTest, ingressL3DstDiscardDrop) {
  runDropScenarios({DropScenario::L3DstDiscard}, "L3 DST discard");
}

// A routed packet arriving with hop limit 1 decrements to 0 and is dropped.
TEST_F(AgentDropReasonTest, ingressTtlErrorDrop) {
  runDropScenarios({DropScenario::TtlError}, "L3 TTL error");
}

// A packet with a multicast source MAC. The ASIC reports SRC_ROUTE_DROP,
// accompanied only by the generic RDROP.
TEST_F(AgentDropReasonTest, ingressMacSaMulticastDrop) {
  runDropScenarios({DropScenario::MacSaMulticast}, "MACSA multicast");
}

// A packet with EtherType 0x05E0, in the reserved range 0x05DD..0x05FF.
TEST_F(AgentDropReasonTest, ingressInvalidEtherTypeDrop) {
  runDropScenarios({DropScenario::InvalidEtherType}, "invalid EtherType");
}

// A packet denied by an ingress ACL.
//
// The ACL matches only the single routed destination this test sends to, and
// the packet is routable, so the deny is the only thing that can drop it.
// Both details matter. Denying ::/0 also denies the switch's own background
// IPv6 traffic, which reports RFILDR in every collection window whether or
// not the test sends anything, leaving the assertion below to pass without
// the injected packet contributing to it; and sending to an unrouted
// destination adds an L3_DST_DISCARD that competes with the deny for
// attribution.
TEST_F(AgentDropReasonTest, ingressAclDenyDrop) {
  runDropScenarios({DropScenario::AclDeny}, "ACL deny");
}

// A routed packet larger than the egress port MTU reports EGR_MTU on the
// egress list and nothing on ingress.
TEST_F(AgentDropReasonTest, egressMtuDrop) {
  runDropScenarios({DropScenario::EgressMtu}, "egress MTU");
}

class AgentDropReasonSrcPortKnockoutTest : public AgentDropReasonTest {
 protected:
  std::optional<cfg::InterfaceType> interfaceType() const override {
    return cfg::InterfaceType::VLAN;
  }
};

// A bridged packet whose destination resolves to its ingress port is dropped
// by source-port filtering before transmission.
TEST_F(AgentDropReasonSrcPortKnockoutTest, ingressSrcPortKnockoutDrop) {
  runDropScenarios({DropScenario::SrcPortKnockout}, "src port knockout");
}

// Invalid IP versions are rejected before route lookup, independently of TTL.
TEST_F(AgentDropReasonTest, ingressIpHeaderErrorDrop) {
  runDropScenarios(
      {DropScenario::Ipv4HeaderError, DropScenario::Ipv6HeaderError},
      "IP header error");
}

} // namespace facebook::fboss
