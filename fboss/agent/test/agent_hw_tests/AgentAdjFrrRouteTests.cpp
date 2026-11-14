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
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <algorithm>
#include <limits>

namespace facebook::fboss {

namespace {

constexpr auto kEcmpHashCancelAclName = "test-ecmp-hash-cancel";
constexpr auto kEcmpHashCancelCounterName = "test-ecmp-hash-cancel-stats";

void addFlowletAndEcmpHashCancelAcls(cfg::SwitchConfig& config, bool isSai) {
  if (FLAGS_enable_acl_table_group) {
    utility::addAclTableGroup(
        &config, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
    utility::addDefaultAclTable(config, {utility::kRoceUdfFlowletGroupName});
  }

  cfg::AclEntry flowletAcl;
  flowletAcl.name() = utility::kFlowletAclName;
  flowletAcl.actionType() = cfg::AclActionType::PERMIT;
  flowletAcl.proto() = 17;
  flowletAcl.l4DstPort() = 4791;
  if (isSai) {
    utility::addUdfTableToAcl(
        &flowletAcl,
        utility::kRoceUdfFlowletGroupName,
        {utility::kRoceReserved},
        {utility::kRoceReserved});
  } else {
    flowletAcl.udfGroups() = {utility::kRoceUdfFlowletGroupName};
    flowletAcl.roceBytes() = {utility::kRoceReserved};
    flowletAcl.roceMask() = {utility::kRoceReserved};
  }
  utility::addAcl(&config, flowletAcl, cfg::AclStage::INGRESS);

  cfg::MatchAction flowletAction;
  flowletAction.flowletAction() = cfg::FlowletAction::FORWARD;
  flowletAction.counter() = utility::kFlowletAclCounterName;
  utility::addTrafficCounter(
      &config,
      utility::kFlowletAclCounterName,
      std::vector<cfg::CounterType>{
          cfg::CounterType::PACKETS, cfg::CounterType::BYTES});
  utility::addMatcher(&config, utility::kFlowletAclName, flowletAction);

  cfg::AclEntry ecmpHashCancelAcl;
  ecmpHashCancelAcl.name() = kEcmpHashCancelAclName;
  ecmpHashCancelAcl.actionType() = cfg::AclActionType::PERMIT;
  cfg::Ttl ttl;
  ttl.value() = 0;
  ttl.mask() = 0;
  ecmpHashCancelAcl.ttl() = ttl;
  utility::addAcl(&config, ecmpHashCancelAcl, cfg::AclStage::INGRESS);
  utility::addAclEcmpHashCancelAction(
      &config, kEcmpHashCancelAclName, kEcmpHashCancelCounterName);
}

} // namespace

class AgentAdjFrrRouteTest : public AgentHwTest {
 protected:
  // initialConfig() installs a flowlet ACL (proto 17 + l4DstPort 4791 + a UDF
  // match on the RoCE BTH reserved byte) and, behind it, an ecmp hash cancel
  // ACL whose ttl mask of 0 matches everything. The three traffic types below
  // pick which of the backup group's two selection mechanisms is under test.
  enum class TrafficType {
    // 4791, one 5 tuple. Hits the flowlet ACL, so the backup group's random
    // spray decides per packet. Spreads with no entropy at all -- that spread
    // is the proof spray is live.
    RoceSpray,
    // 1024, one 5 tuple. Hash cancelled and byte identical, so there is nothing
    // to hash on and the group must settle on exactly one member. Makes split
    // horizon a binary outcome rather than a distribution.
    HashCancelSingleFlow,
    // 1024, kFlowCount distinct source IPs. Hash cancelled but with real
    // entropy, so the static hash spreads across members. The only phase that
    // can say anything meaningful about hash balance.
    HashCancelMultiFlow,
  };
  // Any UDP port other than 4791 misses the flowlet ACL.
  static constexpr int kAclMissL4DstPort = 1024;
  static constexpr int kFlowCount = 2048;
  static constexpr int kPacketsPerFlow = 8;
  static constexpr int kMultiFlowPacketCount = kFlowCount * kPacketsPerFlow;
  static constexpr int kSingleFlowPacketCount = 10000;

  // Balance is asserted as each member's share of the fair share rather than
  // as a min/max ratio: the ratio compares the two extremes and so compounds
  // their noise, which tightens the bound as the group narrows. The mean lies
  // between the extremes, so the 25% min/max deviation this replaces implies
  // every member is within [80%, 125%] of the fair share.
  static constexpr utility::MemberShareBounds kEvenShareBounds{80, 125};
  // Pruning concentrates the pruned member's share rather than spreading it.
  // For hashed traffic that concentration is on one survivor, which puts the
  // absorber at 2N/(N+1) of the fair share with one of N+1 members pruned:
  // measured 163.9% at width five and 170.9% at width six. Sprayed traffic
  // does not follow that model and moves in both directions, measured 50.8%
  // to 175.2% across widths four to six and reproducible across boot modes,
  // so the band is set from the measurements rather than from the model.
  static constexpr utility::MemberShareBounds kPrunedShareBounds{25, 200};
  static constexpr size_t kNumRouteNextHops = 5;
  static constexpr size_t kNumDefaultBackupNhops = kNumRouteNextHops - 1;
  static constexpr size_t kNumRequiredPhyLoopbackPorts = kNumRouteNextHops + 1;

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ARS_SOURCE_PORT_PRUNE,
        ProductionFeature::ARS_FLOWLET,
        ProductionFeature::ARS_SPRAY,
        ProductionFeature::ADJACENCY_FRR,
        ProductionFeature::ACL_COUNTER};
  }

  void SetUp() override {
    AgentHwTest::SetUp();
    if (FLAGS_list_production_feature) {
      return;
    }

    phyLoopbackPortIds_.clear();
    const auto config = getSw()->getConfig();
    for (const auto& port : *config.ports()) {
      if (*port.loopbackMode() == cfg::PortLoopbackMode::PHY) {
        phyLoopbackPortIds_.emplace_back(*port.logicalID());
      }
    }
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /* interfaceHasSubnet */);
    config.udfConfig() =
        utility::addUdfAclConfig(utility::kUdfOffsetBthReserved);
    // BRCM switches require PHY loopback for FRR link
    // state detection.
    for (auto& port : *config.ports()) {
      if (*port.speed() == cfg::PortSpeed::EIGHTHUNDREDG) {
        port.loopbackMode() = cfg::PortLoopbackMode::PHY;
      }
    }
    utility::addFlowletConfigs(
        config,
        ensemble.masterLogicalPortIds(),
        ensemble.isSai(),
        cfg::SwitchingMode::PER_PACKET_QUALITY);
    addFlowletAndEcmpHashCancelAcls(config, ensemble.isSai());
    config.loadBalancers()->push_back(
        utility::getEcmpFullHashConfig(ensemble.getL3Asics()));
    config.switchSettings()->ecmpGroupSettings() = splitHorizonSettings();
    return config;
  }

  // Split horizon is CREATE_ONLY and SaiSwitch rejects an ecmpGroupSettings
  // change after the first config application, so the value can only arrive
  // through initialConfig(). Pruning on is therefore a fixture of its own
  // overriding this, not a setter called mid-test. Empty here: this fixture is
  // the prune-off side.
  virtual EcmpGroupSettingsMap splitHorizonSettings() const {
    return {};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_flowletSwitchingEnable = true;
  }

  void setupRouteWithPrimaryAndBackupNhops(
      bool includePrimaryNextHop = true,
      size_t numBackups = kNumDefaultBackupNhops) {
    CHECK_GE(phyLoopbackPortIds_.size(), numBackups + 2);
    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    boost::container::flat_set<PortDescriptor> nextHopPorts;
    for (size_t i = 0; i <= numBackups; ++i) {
      nextHopPorts.emplace(phyLoopbackPortIds_.at(i));
    }
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, nextHopPorts);
    });

    programRouteWithPrimaryAndBackupNhops(includePrimaryNextHop, numBackups);
  }

  void programRouteWithPrimaryAndBackupNhops(
      bool includePrimaryNextHop,
      size_t numBackups = kNumDefaultBackupNhops) {
    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const auto makeNextHop = [this, &ecmpHelper](
                                 size_t index, NextHopRole role) {
      return UnresolvedNextHop(
          ecmpHelper.ip(PortDescriptor(phyLoopbackPortIds_.at(index))),
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

    RouteNextHopSet nextHops;
    for (size_t i = 1; i <= numBackups; ++i) {
      nextHops.emplace(makeNextHop(i, NextHopRole::BACKUP));
    }
    const auto state = getProgrammedState();
    if (includePrimaryNextHop) {
      nextHops.emplace(makeNextHop(0, NextHopRole::PRIMARY));
      const auto primaryPort = phyLoopbackPortIds_.at(0);
      XLOG(INFO) << "Selected primary next-hop port: "
                 << state->getPorts()->getNode(primaryPort)->getName() << " ("
                 << primaryPort << ")";
    }
    for (size_t i = 1; i <= numBackups; ++i) {
      const auto backupPort = phyLoopbackPortIds_.at(i);
      XLOG(INFO) << "Selected backup next-hop port: "
                 << state->getPorts()->getNode(backupPort)->getName() << " ("
                 << backupPort << ")";
    }
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2001::"),
        16,
        ClientID::BGPD,
        RouteNextHopEntry(nextHops, AdminDistance::EBGP));
    routeUpdater.program();
  }

  void restoreNextHop(PortID port) {
    bringUpPort(port);
    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const boost::container::flat_set<PortDescriptor> nextHopPorts{
        PortDescriptor(port)};
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.unresolveNextHops(state, nextHopPorts);
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, nextHopPorts);
    });
  }

  // Same RoCE shape, but to a UDP port the flowlet ACL cannot match, so the
  // burst is hash cancelled. Deliberately independent of pumpRoceTraffic so
  // this phase does not depend on that helper's signature.
  void pumpAclMissTraffic(
      PortID injectionPort,
      int packetCount,
      const folly::IPAddress& srcIp = folly::IPAddress("1001::1")) {
    utility::pumpRoCETraffic(
        true /* isV6 */,
        utility::getAllocatePktFn(getAgentEnsemble()),
        utility::getSendPktFunc(getAgentEnsemble()),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
        getVlanIDForTx(),
        injectionPort,
        srcIp,
        folly::IPAddress("2001::1"),
        kAclMissL4DstPort,
        255 /* hopLimit */,
        std::nullopt /* srcMacAddr */,
        packetCount,
        utility::kUdfRoceOpcodeAck,
        utility::kRoceReserved,
        std::nullopt /* nextHdr */,
        true /* sameDstQueue */);
  }

  // Dispatches on traffic type. Only the multi flow case varies the source IP;
  // the other two deliberately send one 5 tuple so the group has no entropy.
  int pumpPhaseTraffic(PortID injectionPort, TrafficType traffic) {
    switch (traffic) {
      case TrafficType::RoceSpray:
        pumpRoceTraffic(injectionPort, kSingleFlowPacketCount);
        return kSingleFlowPacketCount;
      case TrafficType::HashCancelSingleFlow:
        pumpAclMissTraffic(injectionPort, kSingleFlowPacketCount);
        return kSingleFlowPacketCount;
      case TrafficType::HashCancelMultiFlow:
        for (int flow = 0; flow < kFlowCount; ++flow) {
          pumpAclMissTraffic(
              injectionPort,
              kPacketsPerFlow,
              folly::IPAddress(folly::to<std::string>("1001::", flow + 1)));
        }
        return kMultiFlowPacketCount;
    }
    throw FbossError("unhandled traffic type");
  }

  // Audits the whole group after one burst. Every packet has to leave the box
  // on some member. When the ingress port is itself a member it is in PHY
  // loopback, so its own counter includes the injected copies and they are
  // subtracted before totalling; it is still counted as a forwarding member if
  // it forwards. Balance is measured only across members that forwarded.
  void sendTrafficAndVerifyGroupDelivery(
      PortID injectionPort,
      const std::vector<PortID>& groupPorts,
      TrafficType traffic,
      std::optional<size_t> expectedForwardingMembers,
      std::optional<utility::MemberShareBounds> shareBounds,
      const char* description,
      std::optional<bool> expectIngressForwards = std::nullopt) {
    const auto state = getProgrammedState();
    const auto beforeStats = getLatestPortStats(groupPorts);
    // Declared outside the retry loop so the settled value survives it.
    int64_t ingressForwarded{0};
    // Which ACL claimed the burst is what decides whether the backup group
    // sprays or hashes, so prove it rather than assuming the L4 port did it.
    const bool sprayEligible = traffic == TrafficType::RoceSpray;
    const auto flowletAclBefore =
        utility::getAclInOutPackets(getSw(), utility::kFlowletAclCounterName);
    const auto cancelAclBefore =
        utility::getAclInOutPackets(getSw(), kEcmpHashCancelCounterName);

    const int packetCount = pumpPhaseTraffic(injectionPort, traffic);

    WITH_RETRIES({
      const auto afterStats = getLatestPortStats(groupPorts);
      int64_t forwarded{0};
      ingressForwarded = 0;
      int64_t lowest{std::numeric_limits<int64_t>::max()};
      int64_t highest{0};
      size_t forwardingMembers{0};
      std::map<std::string, uint64_t> forwardedByMember;
      for (const auto& port : groupPorts) {
        int64_t pkts = *afterStats.at(port).outUnicastPkts__ref() -
            *beforeStats.at(port).outUnicastPkts__ref();
        if (port == injectionPort) {
          pkts -= std::min<int64_t>(pkts, packetCount);
          ingressForwarded = pkts;
        }
        const auto& portName = state->getPorts()->getNode(port)->getName();
        XLOG(INFO) << description << " port " << portName << " (" << port << ")"
                   << (port == injectionPort ? " [INJECT]" : "")
                   << " forwarded " << pkts;
        forwarded += pkts;
        if (pkts > 0) {
          ++forwardingMembers;
          forwardedByMember.emplace(portName, pkts);
          lowest = std::min(lowest, pkts);
          highest = std::max(highest, pkts);
        }
      }
      XLOG(INFO) << description << " forwarded " << forwarded << " of "
                 << packetCount << " across " << forwardingMembers
                 << " members, lowest " << lowest << ", highest " << highest;

      // Nothing may be dropped, whatever the member selection does.
      EXPECT_EVENTUALLY_EQ(forwarded, static_cast<int64_t>(packetCount));

      // Assert both directions on both counters: a burst has to be claimed by
      // exactly one ACL, so checking only that the other did not move would
      // leave "matched nothing at all" indistinguishable from a correct run.
      const auto flowletAclAfter =
          utility::getAclInOutPackets(getSw(), utility::kFlowletAclCounterName);
      const auto cancelAclAfter =
          utility::getAclInOutPackets(getSw(), kEcmpHashCancelCounterName);
      if (sprayEligible) {
        EXPECT_EVENTUALLY_GE(flowletAclAfter, flowletAclBefore + packetCount);
        EXPECT_EVENTUALLY_EQ(cancelAclAfter, cancelAclBefore);
      } else {
        EXPECT_EVENTUALLY_EQ(flowletAclAfter, flowletAclBefore);
        EXPECT_EVENTUALLY_GE(cancelAclAfter, cancelAclBefore + packetCount);
      }
      if (expectedForwardingMembers.has_value()) {
        EXPECT_EVENTUALLY_EQ(forwardingMembers, *expectedForwardingMembers);
      }
      // Whether the ingress member is allowed to carry the traffic it received
      // is the whole point of split horizon, so assert it directly.
      if (expectIngressForwards.has_value()) {
        if (*expectIngressForwards) {
          EXPECT_EVENTUALLY_GT(ingressForwarded, 0);
        } else {
          EXPECT_EVENTUALLY_EQ(ingressForwarded, 0);
        }
      }
      if (shareBounds.has_value() && forwardingMembers > 0) {
        EXPECT_EVENTUALLY_TRUE(
            utility::isWithinMemberShareBounds(
                forwardedByMember, *shareBounds));
      }
    });
  }

  // Whether pruning is on is decided by the fixture, so this takes only the
  // width.
  void setupSecondaryGroup(size_t numBackups) {
    setupRouteWithPrimaryAndBackupNhops(true, numBackups);
  }

  enum class Ingress { NonMember, Member };

  // Runs one phase. The primary is dropped so the secondary group forwards,
  // one burst of the given type is injected, and the outcome the backup
  // group's selection mode implies is asserted. Nothing may ever be dropped.
  //
  // Expectations by traffic type:
  //   RoceSpray            spreads over the group; spray is per packet, so it
  //                        spreads even with no entropy. Even to within 4%
  //                        with nothing pruned, but pruning skews it well
  //                        past what the absorber model predicts, so it is
  //                        not asserted to be even.
  //   HashCancelSingleFlow no entropy -> exactly one member. Pruning must move
  //                        that member off the ingress.
  //   HashCancelMultiFlow  real entropy -> the static hash spreads evenly
  //                        across the group at every width.
  // A non member ingress can never be a pruning candidate, so those phases are
  // the controls: any difference against the member ingress phase of the same
  // traffic type is split horizon and nothing else.
  void verifySecondaryGroupPhase(
      size_t numBackups,
      Ingress ingress,
      TrafficType traffic,
      bool prunes,
      const char* description) {
    const auto primaryPort = phyLoopbackPortIds_.at(0);
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + 1 + numBackups);
    const bool memberIngress = ingress == Ingress::Member;
    const auto injectionPort = memberIngress
        ? backupPorts.front()
        : phyLoopbackPortIds_.at(numBackups + 1);
    // Only a member ingress can be pruned away.
    const bool pruneApplies = prunes && memberIngress;

    std::optional<size_t> expectedMembers;
    std::optional<utility::MemberShareBounds> shareBounds;
    switch (traffic) {
      case TrafficType::HashCancelSingleFlow:
        expectedMembers = 1;
        break;
      case TrafficType::HashCancelMultiFlow:
        expectedMembers = pruneApplies ? numBackups - 1 : numBackups;
        shareBounds = pruneApplies ? kPrunedShareBounds : kEvenShareBounds;
        break;
      case TrafficType::RoceSpray:
        expectedMembers = pruneApplies ? numBackups - 1 : numBackups;
        break;
    }

    bringDownPort(primaryPort);
    sendTrafficAndVerifyGroupDelivery(
        injectionPort,
        backupPorts,
        traffic,
        expectedMembers,
        shareBounds,
        description,
        pruneApplies ? std::optional<bool>(false) : std::nullopt);
    restoreNextHop(primaryPort);
  }

  void sendTrafficAndVerifyOutPackets(
      PortID injectionPort,
      const std::vector<PortID>& egressPorts,
      int packetCount,
      const char* egressPortDescription,
      utility::MemberShareBounds shareBounds = kEvenShareBounds) {
    CHECK(!egressPorts.empty());
    const auto state = getProgrammedState();
    const auto injectionPortState = state->getPorts()->getNode(injectionPort);
    XLOG(INFO) << egressPortDescription
               << " injection port: " << injectionPortState->getName() << " ("
               << injectionPort << ")";
    for (const auto& egressPort : egressPorts) {
      const auto egressPortState = state->getPorts()->getNode(egressPort);
      XLOG(INFO) << egressPortDescription
                 << " expected egress port: " << egressPortState->getName()
                 << " (" << egressPort << ")";
    }
    auto getOutPkts = [&egressPorts](const auto& portStats) {
      uint64_t outPkts{0};
      for (auto port : egressPorts) {
        outPkts += *portStats.at(port).outUnicastPkts__ref();
      }
      return outPkts;
    };
    const auto beforePortStats = getLatestPortStats(egressPorts);
    const auto beforeOutPkts = getOutPkts(beforePortStats);

    pumpRoceTraffic(injectionPort, packetCount);

    WITH_RETRIES({
      const auto afterPortStats = getLatestPortStats(egressPorts);
      const auto afterOutPkts = getOutPkts(afterPortStats);
      auto lowestOutBytesPort = egressPorts.front();
      auto highestOutBytesPort = egressPorts.front();
      auto lowestOutBytesIncrement =
          *afterPortStats.at(lowestOutBytesPort).outBytes_() -
          *beforePortStats.at(lowestOutBytesPort).outBytes_();
      auto highestOutBytesIncrement = lowestOutBytesIncrement;
      std::map<std::string, uint64_t> outBytesByEgressPort;
      for (const auto& egressPort : egressPorts) {
        const auto outBytesIncrement =
            *afterPortStats.at(egressPort).outBytes_() -
            *beforePortStats.at(egressPort).outBytes_();
        outBytesByEgressPort.emplace(
            state->getPorts()->getNode(egressPort)->getName(),
            outBytesIncrement);
        if (outBytesIncrement < lowestOutBytesIncrement) {
          lowestOutBytesIncrement = outBytesIncrement;
          lowestOutBytesPort = egressPort;
        }
        if (outBytesIncrement > highestOutBytesIncrement) {
          highestOutBytesIncrement = outBytesIncrement;
          highestOutBytesPort = egressPort;
        }
      }
      const auto deviationPct = lowestOutBytesIncrement == 0
          ? (highestOutBytesIncrement == 0
                 ? 0.0
                 : std::numeric_limits<double>::infinity())
          : static_cast<double>(
                highestOutBytesIncrement - lowestOutBytesIncrement) /
              lowestOutBytesIncrement * 100.0;
      XLOG(INFO) << egressPortDescription
                 << " out packets before traffic: " << beforeOutPkts
                 << ", after traffic: " << afterOutPkts
                 << ", lowest out bytes port: "
                 << state->getPorts()->getNode(lowestOutBytesPort)->getName()
                 << " (" << lowestOutBytesPort
                 << "), increment: " << lowestOutBytesIncrement
                 << ", highest out bytes port: "
                 << state->getPorts()->getNode(highestOutBytesPort)->getName()
                 << " (" << highestOutBytesPort
                 << "), increment: " << highestOutBytesIncrement
                 << ", deviation: " << deviationPct << "%";
      EXPECT_EVENTUALLY_EQ(afterOutPkts, beforeOutPkts + packetCount);
      EXPECT_EVENTUALLY_TRUE(
          utility::isWithinMemberShareBounds(
              outBytesByEgressPort, shareBounds));
    });
  }

  // reserved carries spray eligibility: utility::kRoceReserved matches the
  // flowlet ACL, anything else falls through to the hash cancel ACL.
  void pumpRoceTraffic(
      PortID injectionPort,
      int packetCount,
      uint8_t reserved = utility::kRoceReserved) {
    utility::pumpRoCETraffic(
        true /* isV6 */,
        utility::getAllocatePktFn(getAgentEnsemble()),
        utility::getSendPktFunc(getAgentEnsemble()),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
        getVlanIDForTx(),
        injectionPort,
        utility::kUdfL4DstPort,
        255 /* hopLimit */,
        std::nullopt /* srcMacAddr */,
        packetCount,
        utility::kUdfRoceOpcodeAck,
        reserved,
        std::nullopt /* nextHdr */,
        true /* sameDstQueue */);
  }

  void sendNonDlbFlowAndVerifySingleEgressPort(
      PortID injectionPort,
      const std::vector<PortID>& egressPorts,
      int packetCount) {
    CHECK(!egressPorts.empty());
    const auto beforePortStats = getLatestPortStats(egressPorts);

    constexpr uint16_t kSrcPort = 10000;
    constexpr uint16_t kDstPort = 20000;
    utility::pumpTraffic(
        utility::getAllocatePktFn(getAgentEnsemble()),
        utility::getSendPktFunc(getAgentEnsemble()),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
        {folly::IPAddressV6("1001::1")},
        {folly::IPAddressV6("2001::1")},
        kSrcPort,
        kDstPort,
        1 /* streams */,
        getVlanIDForTx(),
        injectionPort,
        255 /* hopLimit */,
        std::nullopt /* srcMac */,
        packetCount);

    WITH_RETRIES({
      const auto afterPortStats = getLatestPortStats(egressPorts);
      int64_t totalOutPktsIncrement{0};
      int64_t highestOutPktsIncrement{0};
      size_t egressPortsWithPackets{0};
      for (auto port : egressPorts) {
        const auto beforeOutPkts =
            *beforePortStats.at(port).outUnicastPkts__ref();
        const auto afterOutPkts =
            *afterPortStats.at(port).outUnicastPkts__ref();
        const auto outPktsIncrement = afterOutPkts - beforeOutPkts;
        XLOG(INFO) << "Non-DLB flow backup port " << port
                   << " out packets before traffic: " << beforeOutPkts
                   << ", after traffic: " << afterOutPkts
                   << ", increment: " << outPktsIncrement;
        totalOutPktsIncrement += outPktsIncrement;
        if (outPktsIncrement > 0) {
          ++egressPortsWithPackets;
          highestOutPktsIncrement =
              std::max(highestOutPktsIncrement, outPktsIncrement);
        }
      }
      XLOG(INFO) << "Non-DLB flow backup out packets increment: total="
                 << totalOutPktsIncrement
                 << ", highest=" << highestOutPktsIncrement
                 << ", ports with packets=" << egressPortsWithPackets;
      EXPECT_EVENTUALLY_EQ(totalOutPktsIncrement, packetCount);
      EXPECT_EVENTUALLY_EQ(highestOutPktsIncrement, packetCount);
      EXPECT_EVENTUALLY_EQ(egressPortsWithPackets, 1);
    });
  }

  std::vector<PortID> phyLoopbackPortIds_;
};

TEST_F(AgentAdjFrrRouteTest, routeWithPrimaryAndBackupNhops) {
  auto setup = [this]() { setupRouteWithPrimaryAndBackupNhops(); };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    auto state = getProgrammedState();
    auto primaryPort = phyLoopbackPortIds_.at(0);
    auto injectionPort = phyLoopbackPortIds_.at(kNumRouteNextHops);
    const std::vector<PortID> primaryPorts{primaryPort};
    std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);
    auto primaryPortState = state->getPorts()->getNode(primaryPort);
    auto injectionPortState = state->getPorts()->getNode(injectionPort);
    XLOG(INFO) << "Injecting traffic through port "
               << injectionPortState->getName() << " (" << injectionPort
               << "); checking primary next-hop port "
               << primaryPortState->getName() << " (" << primaryPort << ")";
    sendTrafficAndVerifyOutPackets(
        injectionPort, primaryPorts, kPacketCount, "Primary");

    bringDownPort(primaryPort);

    // While the primary is down, every failover packet must count a
    // switchover event on the ingress port. Only asserted where supported.
    const bool checkSwitchoverEvents = isSupportedOnAllAsics(
        HwAsic::Feature::HW_PROTECTION_SWITCHOVER_COUNTERS);
    const auto eventsBefore = checkSwitchoverEvents
        ? std::optional(getLatestPortStats(injectionPort))
        : std::nullopt;
    sendTrafficAndVerifyOutPackets(
        injectionPort, backupPorts, kPacketCount, "Backup");
    if (checkSwitchoverEvents) {
      WITH_RETRIES({
        const auto eventsAfter = getLatestPortStats(injectionPort);
        EXPECT_EVENTUALLY_TRUE(
            eventsAfter.hwProtectionSwitchoverEvents_().has_value());
        EXPECT_EVENTUALLY_GT(
            eventsAfter.hwProtectionSwitchoverEvents_().value_or(0),
            eventsBefore->hwProtectionSwitchoverEvents_().value_or(0));
      });
    }

    restoreNextHop(primaryPort);

    sendTrafficAndVerifyOutPackets(
        injectionPort,
        primaryPorts,
        kPacketCount,
        "Primary after next-hop re-resolution");
  };

  verifyAcrossWarmBoots(setup, verify);
}

// A route whose whole forwarding set is one backup next hop -- the shape a
// protected route collapses to once resolution drops its primary. Such a route
// is programmed as a protection group rather than a plain next hop, so the
// group is created with a single member in its backup half. Widths above one
// are covered by the balance tests below; this pins the narrowest case, where
// the group has the least room to hide a programming error.
TEST_F(AgentAdjFrrRouteTest, routeWithSingleBackupNhopOnly) {
  constexpr size_t kNumBackups = 1;
  auto setup = [this]() {
    setupRouteWithPrimaryAndBackupNhops(
        false /* includePrimaryNextHop */, kNumBackups);
  };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const std::vector<PortID> backupPorts{phyLoopbackPortIds_.at(1)};
    const auto injectionPort = phyLoopbackPortIds_.at(kNumRouteNextHops);
    sendTrafficAndVerifyOutPackets(
        injectionPort, backupPorts, kPacketCount, "Single backup next hop");
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on. Both FRR halves are named together: the parent arms the
// same-src-dst port check and the backup provides the tertiary path, and
// ApplyThriftConfig rejects a config that enables only one.
class AgentAdjFrrRoutePruneEnabledTest : public AgentAdjFrrRouteTest {
 protected:
  EcmpGroupSettingsMap splitHorizonSettings() const override {
    cfg::EcmpGroupSettings settings;
    settings.enableSplitHorizon() = true;
    return {
        {cfg::EcmpGroupType::FRR_PRIMARY, settings},
        {cfg::EcmpGroupType::FRR_BACKUP, settings}};
  }
};

TEST_F(AgentAdjFrrRoutePruneEnabledTest, sourcePortGetsPruned) {
  auto setup = [this]() { setupRouteWithPrimaryAndBackupNhops(); };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const auto primaryPort = phyLoopbackPortIds_.at(0);
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);

    sendTrafficAndVerifyOutPackets(
        primaryPort,
        backupPorts,
        kPacketCount,
        "Backup with primary next-hop ingress");

    bringDownPort(primaryPort);
    const auto backupInjectionPort = backupPorts.front();
    const std::vector<PortID> remainingBackupPorts(
        backupPorts.begin() + 1, backupPorts.end());
    sendTrafficAndVerifyOutPackets(
        backupInjectionPort,
        remainingBackupPorts,
        kPacketCount,
        "Backup with backup next-hop ingress",
        kPrunedShareBounds);

    restoreNextHop(primaryPort);
    sendTrafficAndVerifyOutPackets(
        primaryPort,
        backupPorts,
        kPacketCount,
        "Backup after primary next-hop restoration");
  };

  verifyAcrossWarmBoots(setup, verify);
}

// The primary group is empty, so the secondary carries everything. Pruning
// still has to drop the ingress port from the forwarding set.
TEST_F(AgentAdjFrrRoutePruneEnabledTest, sourcePortPruneWithEmptyPrimary) {
  auto setup = [this]() {
    setupRouteWithPrimaryAndBackupNhops(false /* includePrimaryNextHop */);
  };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);
    const auto backupInjectionPort = backupPorts.front();
    const std::vector<PortID> remainingBackupPorts(
        backupPorts.begin() + 1, backupPorts.end());

    sendTrafficAndVerifyOutPackets(
        backupInjectionPort,
        remainingBackupPorts,
        kPacketCount,
        "Empty primary, backup next-hop ingress",
        kPrunedShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning off: every backup, including the one traffic ingressed on, has to
// carry an even share and nothing may be dropped.
TEST_F(AgentAdjFrrRouteTest, frrNoPruneFourBackupsBalancesEvenly) {
  constexpr size_t kNumBackups = 4;
  constexpr bool kPrune = false;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, four backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, four backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, four backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, four backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, four backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, four backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAdjFrrRouteTest, frrNoPruneFiveBackupsBalancesEvenly) {
  constexpr size_t kNumBackups = 5;
  constexpr bool kPrune = false;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, five backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, five backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, five backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, five backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, five backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, five backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAdjFrrRouteTest, frrNoPruneSixBackupsBalancesEvenly) {
  constexpr size_t kNumBackups = 6;
  constexpr bool kPrune = false;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, six backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, six backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, six backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "no prune, six backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "no prune, six backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "no prune, six backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on, four backups: the pruned member's share goes to a single
// survivor, which then carries twice what the other two do. Three members
// forward and nothing is dropped.
TEST_F(
    AgentAdjFrrRoutePruneEnabledTest,
    frrPruneFourBackupsRedistributesToOneMember) {
  constexpr size_t kNumBackups = 4;
  constexpr bool kPrune = true;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "prune, four backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, four backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, four backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "prune, four backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, four backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, four backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on, five and six backups: padding keeps every backup reachable, so
// the group loses exactly the pruned member and no more. Each phase asserts the
// expected forwarding member count and no loss; the balance bound is widened to
// admit the 2:1 the pruned member's redistribution creates.
TEST_F(AgentAdjFrrRoutePruneEnabledTest, frrPruneFiveBackupsNoLoss) {
  constexpr size_t kNumBackups = 5;
  constexpr bool kPrune = true;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "prune, five backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, five backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, five backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "prune, five backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, five backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, five backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAdjFrrRoutePruneEnabledTest, frrPruneSixBackupsNoLoss) {
  constexpr size_t kNumBackups = 6;
  constexpr bool kPrune = true;
  auto setup = [&]() { setupSecondaryGroup(kNumBackups); };
  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: never a pruning candidate, so
    // these are the controls for the member ingress phases below.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::RoceSpray,
        kPrune,
        "prune, six backups, non member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, six backups, non member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::NonMember,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, six backups, non member, hash cancel multi flow");
    // Phases 4-6, ingress on a member: the same three bursts, now with the
    // ingress inside the group so split horizon can act on it.
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::RoceSpray,
        kPrune,
        "prune, six backups, member, spray");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelSingleFlow,
        kPrune,
        "prune, six backups, member, hash cancel single flow");
    verifySecondaryGroupPhase(
        kNumBackups,
        Ingress::Member,
        TrafficType::HashCancelMultiFlow,
        kPrune,
        "prune, six backups, member, hash cancel multi flow");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAdjFrrRouteTest, nonDlbFlowUsesSingleBackupNextHop) {
  auto setup = [this]() { setupRouteWithPrimaryAndBackupNhops(); };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const auto primaryPort = phyLoopbackPortIds_.at(0);
    const auto injectionPort = phyLoopbackPortIds_.at(kNumRouteNextHops);
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);

    bringDownPort(primaryPort);
    sendNonDlbFlowAndVerifySingleEgressPort(
        injectionPort, backupPorts, kPacketCount);
    restoreNextHop(primaryPort);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAdjFrrRouteTest, priAndBackupNextHopFlap) {
  auto setup = [this]() {
    setupRouteWithPrimaryAndBackupNhops(false /* includePrimaryNextHop */);
  };

  auto verify = [this]() {
    constexpr int kPacketCount = 10000;
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const auto primaryPort = phyLoopbackPortIds_.at(0);
    const auto injectionPort = phyLoopbackPortIds_.at(kNumRouteNextHops);
    const std::vector<PortID> primaryPorts{primaryPort};
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);
    const auto firstBackupPort = backupPorts.front();
    const std::vector<PortID> remainingBackupPorts(
        backupPorts.begin() + 1, backupPorts.end());

    programRouteWithPrimaryAndBackupNhops(true);
    bringDownPort(primaryPort);
    bringDownPort(firstBackupPort);
    sendTrafficAndVerifyOutPackets(
        injectionPort, remainingBackupPorts, kPacketCount, "Remaining backups");

    restoreNextHop(primaryPort);
    sendTrafficAndVerifyOutPackets(
        injectionPort, primaryPorts, kPacketCount, "Restored primary");

    restoreNextHop(firstBackupPort);
    bringDownPort(primaryPort);
    sendTrafficAndVerifyOutPackets(
        injectionPort, backupPorts, kPacketCount, "All backups");

    restoreNextHop(primaryPort);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Verifies the backup next hop group as an ECMP random spray group. Split out
// from AgentAdjFrrRouteTest so the extra production feature gating does not
// apply to the tests above. The flowlet and hash cancel ACLs it reads are
// already installed by AgentAdjFrrRouteTest::initialConfig() and carry no
// srcPort qualifier, so no additional ACL config is needed here.
class AgentAdjFrrRouteSprayTest : public AgentAdjFrrRouteTest {
 protected:
  static constexpr int kSprayPacketCount = 200000;

  // One row per traffic burst, printed as a summary table at the end so every
  // phase can be compared side by side.
  struct PhaseResult {
    std::string phase;
    std::string traffic;
    uint64_t packetsSent{0};
    uint64_t sprayAclDelta{0};
    uint64_t cancelAclDelta{0};
    uint64_t outPktsDelta{0};
    size_t activePorts{0};
    size_t totalPorts{0};
    uint64_t lowestOutBytes{0};
    uint64_t highestOutBytes{0};
  };

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentAdjFrrRouteTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::ECMP_RANDOM_SPRAY);
    features.push_back(ProductionFeature::ACL_COUNTER);
    return features;
  }

  uint64_t sprayAclPackets() const {
    return utility::getAclInOutPackets(
        getSw(), utility::kFlowletAclCounterName);
  }

  uint64_t cancelAclPackets() const {
    return utility::getAclInOutPackets(getSw(), kEcmpHashCancelCounterName);
  }

  // Ports that forwarded at least one packet, and the sole active port when
  // there is exactly one.
  static std::pair<size_t, std::optional<PortID>> activeEgressPorts(
      const std::map<PortID, HwPortStats>& before,
      const std::map<PortID, HwPortStats>& after) {
    size_t active{0};
    std::optional<PortID> onlyActivePort;
    for (const auto& [portId, afterStats] : after) {
      if (*afterStats.outUnicastPkts__ref() >
          *before.at(portId).outUnicastPkts__ref()) {
        ++active;
        onlyActivePort = portId;
      }
    }
    if (active != 1) {
      onlyActivePort.reset();
    }
    return {active, onlyActivePort};
  }

  // Every packet carries the same 5 tuple, so a static hash pins the whole
  // burst to one member while a spray group scatters it. Eligibility rides
  // solely in the RoCE BTH reserved field, so the ACL counters say which class
  // the stream landed in and the port spread says whether the action took
  // effect.
  void sendTrafficAndVerifySpray(
      PortID injectionPort,
      const std::vector<PortID>& nextHopPorts,
      bool sprayEligible,
      bool sprayExpected,
      std::optional<PortID> expectedSingleEgressPort,
      const char* phaseDescription) {
    CHECK(!nextHopPorts.empty());
    const auto sprayAclBefore = sprayAclPackets();
    const auto cancelAclBefore = cancelAclPackets();
    const auto beforePortStats = getLatestPortStats(nextHopPorts);

    pumpRoceTraffic(
        injectionPort,
        kSprayPacketCount,
        sprayEligible ? utility::kRoceReserved : 0);

    PhaseResult row;
    row.phase = phaseDescription;
    row.traffic =
        sprayEligible ? "RoCE spray eligible" : "RoCE spray ineligible";
    row.packetsSent = kSprayPacketCount;
    row.totalPorts = nextHopPorts.size();

    WITH_RETRIES({
      const auto state = getProgrammedState();
      const auto sprayAclAfter = sprayAclPackets();
      const auto cancelAclAfter = cancelAclPackets();
      const auto afterPortStats = getLatestPortStats(nextHopPorts);
      const auto [highestOutBytes, lowestOutBytes] =
          utility::getHighestAndLowestBytesIncrement(
              beforePortStats, afterPortStats);
      const auto [activePorts, onlyActivePort] =
          activeEgressPorts(beforePortStats, afterPortStats);
      uint64_t outPktsDelta{0};
      std::map<std::string, uint64_t> outBytesByMember;
      for (const auto& [portId, afterStats] : afterPortStats) {
        outPktsDelta += *afterStats.outUnicastPkts__ref() -
            *beforePortStats.at(portId).outUnicastPkts__ref();
        const auto outBytesIncrement =
            *afterStats.outBytes_() - *beforePortStats.at(portId).outBytes_();
        // A member carrying nothing is left out: it would fail the floor and
        // also pull the fair share down for the members that do forward. The
        // active port count is what catches a member dropping out.
        if (outBytesIncrement > 0) {
          outBytesByMember.emplace(
              state->getPorts()->getNode(portId)->getName(), outBytesIncrement);
        }
      }
      row.sprayAclDelta = sprayAclAfter - sprayAclBefore;
      row.cancelAclDelta = cancelAclAfter - cancelAclBefore;
      row.outPktsDelta = outPktsDelta;
      row.activePorts = activePorts;
      row.lowestOutBytes = lowestOutBytes;
      row.highestOutBytes = highestOutBytes;

      XLOG(INFO) << phaseDescription << " ("
                 << (sprayEligible ? "spray eligible" : "spray ineligible")
                 << "): flowlet acl " << sprayAclBefore << " -> "
                 << sprayAclAfter << ", cancel acl " << cancelAclBefore
                 << " -> " << cancelAclAfter << ", out pkts " << outPktsDelta
                 << ", active ports " << activePorts << "/"
                 << nextHopPorts.size() << ", lowest out bytes "
                 << lowestOutBytes << ", highest out bytes " << highestOutBytes;

      // Classification: exactly one of the two ACLs must claim the stream.
      if (sprayEligible) {
        EXPECT_EVENTUALLY_GE(sprayAclAfter, sprayAclBefore + kSprayPacketCount);
        EXPECT_EVENTUALLY_EQ(cancelAclAfter, cancelAclBefore);
      } else {
        EXPECT_EVENTUALLY_GE(
            cancelAclAfter, cancelAclBefore + kSprayPacketCount);
        EXPECT_EVENTUALLY_EQ(sprayAclAfter, sprayAclBefore);
      }

      EXPECT_EVENTUALLY_EQ(outPktsDelta, kSprayPacketCount);

      // Forwarding: a sprayed stream reaches every member, a hashed one pins
      // to a single member.
      if (sprayExpected) {
        EXPECT_EVENTUALLY_EQ(activePorts, nextHopPorts.size());
        EXPECT_EVENTUALLY_TRUE(
            utility::isWithinMemberShareBounds(
                outBytesByMember, kEvenShareBounds));
      } else {
        EXPECT_EVENTUALLY_EQ(activePorts, 1);
        if (expectedSingleEgressPort.has_value() &&
            onlyActivePort.has_value()) {
          EXPECT_EVENTUALLY_EQ(*onlyActivePort, *expectedSingleEgressPort);
        }
      }
    });
    phaseResults_.push_back(row);
  }

  void printPhaseResults() const {
    std::string table = "\nAdjacency FRR spray verification summary\n";
    table += fmt::format(
        "| {:<38} | {:<21} | {:>9} | {:>9} | {:>10} | {:>12} |\n",
        "Phase",
        "Traffic",
        "Pkts sent",
        "UDF ACL",
        "Cancel ACL",
        "Active/Total");
    for (const auto& row : phaseResults_) {
      table += fmt::format(
          "| {:<38} | {:<21} | {:>9} | {:>9} | {:>10} | {:>12} |\n",
          row.phase,
          row.traffic,
          row.packetsSent,
          row.sprayAclDelta,
          row.cancelAclDelta,
          fmt::format("{}/{}", row.activePorts, row.totalPorts));
    }
    XLOG(INFO) << table;
  }

  std::vector<PhaseResult> phaseResults_;
};

/* Route 2001::/16 = 1 primary nhop + 4 backup nhops, the backups forming an
 * ECMP random spray group. Spray eligibility rides in the RoCE BTH reserved
 * field: eligible packets match the flowlet ACL, everything else falls through
 * to the catch-all which cancels the hash and forces a static assignment.
 *
 * The existing coverage sends many distinct 5 tuples, so traffic spreads over
 * the backups whether the group sprays or merely hashes. A fixed 5 tuple burst
 * separates the two: it pins to one member under a hash and scatters under
 * spray.
 *
 * 1. Primary up,   RoCE reserved set > flowlet acl, single nhop (primary)
 * 2. Primary up,   RoCE reserved 0   > cancel acl,  single nhop (primary)
 * 3. Primary down, RoCE reserved set > flowlet acl, sprayed over 4/4 backups
 * 4. Primary down, RoCE reserved 0   > cancel acl,  pinned to 1/4 backups
 */
TEST_F(AgentAdjFrrRouteSprayTest, backupGroupSprayEligibility) {
  auto setup = [this]() { setupRouteWithPrimaryAndBackupNhops(); };

  auto verify = [this]() {
    CHECK_GE(phyLoopbackPortIds_.size(), kNumRequiredPhyLoopbackPorts);
    const auto primaryPort = phyLoopbackPortIds_.at(0);
    // Outside the group, so nothing here is a pruning or failover candidate.
    const auto injectionPort = phyLoopbackPortIds_.at(kNumRouteNextHops);
    const std::vector<PortID> backupPorts(
        phyLoopbackPortIds_.begin() + 1,
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);
    const std::vector<PortID> allNextHopPorts(
        phyLoopbackPortIds_.begin(),
        phyLoopbackPortIds_.begin() + kNumRouteNextHops);

    // While the primary is up neither class may spray: every packet leaves on
    // the primary next hop regardless of eligibility.
    sendTrafficAndVerifySpray(
        injectionPort,
        allNextHopPorts,
        true /* sprayEligible */,
        false /* sprayExpected */,
        primaryPort,
        "Primary up");
    sendTrafficAndVerifySpray(
        injectionPort,
        allNextHopPorts,
        false /* sprayEligible */,
        false /* sprayExpected */,
        primaryPort,
        "Primary up");

    bringDownPort(primaryPort);
    sendTrafficAndVerifySpray(
        injectionPort,
        backupPorts,
        true /* sprayEligible */,
        true /* sprayExpected */,
        std::nullopt,
        "Primary down");
    sendTrafficAndVerifySpray(
        injectionPort,
        backupPorts,
        false /* sprayEligible */,
        false /* sprayExpected */,
        std::nullopt,
        "Primary down");
    restoreNextHop(primaryPort);

    printPhaseResults();
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
