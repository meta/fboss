/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/IPAddressV6.h>
#include <folly/io/Cursor.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortStatsTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

#include <gtest/gtest.h>
#include <memory>

namespace {
const facebook::fboss::Label kTopLabel{1101};
constexpr auto kGetQueueOutPktsRetryTimes = 5;
const std::string kAclName = "acl0";
using TestTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;
} // namespace

namespace facebook::fboss {

template <typename PortType>
class AgentMPLSTest : public AgentHwTest {
  struct AgentPacketVerifier {
    AgentPacketVerifier(
        SwSwitch* sw,
        bool srcPortQualifierSupported,
        MPLSHdr hdr)
        : sw_(sw),
          srcPortQualifierSupported_(srcPortQualifierSupported),
          expectedHdr_(hdr) {
      if (!srcPortQualifierSupported_) {
        return;
      }
      snooper_ = std::make_unique<utility::SwSwitchPacketSnooper>(
          sw_, "mpls-verifier");
    }

    AgentPacketVerifier(AgentPacketVerifier&& other) noexcept
        : sw_(other.sw_),
          srcPortQualifierSupported_(other.srcPortQualifierSupported_),
          snooper_(std::move(other.snooper_)),
          expectedHdr_(other.expectedHdr_) {
      other.srcPortQualifierSupported_ = false;
    }
    AgentPacketVerifier& operator=(AgentPacketVerifier&&) = delete;
    AgentPacketVerifier(const AgentPacketVerifier&) = delete;
    AgentPacketVerifier& operator=(const AgentPacketVerifier&) = delete;

    ~AgentPacketVerifier() {
      if (!srcPortQualifierSupported_) {
        return;
      }
      auto pktBuf = snooper_->waitForPacket(10);
      if (pktBuf && *pktBuf) {
        folly::io::Cursor cursor((*pktBuf).get());
        utility::EthFrame frame(cursor);
        auto mplsPayLoad = frame.mplsPayLoad();
        EXPECT_TRUE(mplsPayLoad.has_value());
        if (mplsPayLoad) {
          EXPECT_EQ(mplsPayLoad->header(), expectedHdr_);
        }
      } else {
        ADD_FAILURE() << "No packet received for MPLS verification";
      }
    }

    SwSwitch* sw_;
    bool srcPortQualifierSupported_;
    std::unique_ptr<utility::SwSwitchPacketSnooper> snooper_;
    MPLSHdr expectedHdr_;
  };

 protected:
  void SetUp() override {
    AgentHwTest::SetUp();
  }

  utility::EcmpSetupTargetedPorts6* getEcmpHelper() {
    if (!ecmpHelper_) {
      ecmpHelper_ = std::make_unique<utility::EcmpSetupTargetedPorts6>(
          getProgrammedState(), getSw()->needL2EntryForNeighbor());
    }
    return ecmpHelper_.get();
  }

  PortDescriptor getPortDescriptor(int index) {
    if constexpr (std::is_same_v<PortType, PortID>) {
      return PortDescriptor(masterLogicalInterfacePortIds()[index]);
    } else {
      return PortDescriptor(AggregatePortID(index + 1));
    }
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto l3Asics = ensemble.getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    auto masterLogicalPorts = ensemble.masterLogicalPortIds();
    cfg::SwitchConfig config;
    if constexpr (std::is_same_v<PortType, AggregatePortID>) {
      config = utility::oneAggregatePortPerInterfaceConfig(
          ensemble.getSw(),
          masterLogicalPorts,
          {utility::AggregatePortInfo(
               AggregatePortID(1), {masterLogicalPorts[0]}),
           utility::AggregatePortInfo(
               AggregatePortID(2), {masterLogicalPorts[1]}),
           utility::AggregatePortInfo(
               AggregatePortID(3), {masterLogicalPorts[2]})},
          true /*interfaceHasSubnet*/);
    } else {
      config = utility::onePortPerInterfaceConfig(
          ensemble.getSw(), masterLogicalPorts, true /*interfaceHasSubnet*/);
    }
    cfg::QosMap qosMap;
    for (auto tc = 0; tc < 8; tc++) {
      // setup ingress qos map for dscp
      cfg::DscpQosMap dscpMap;
      *dscpMap.internalTrafficClass() = tc;
      for (auto dscp = 0; dscp < 8; dscp++) {
        dscpMap.fromDscpToTrafficClass()->push_back(8 * tc + dscp);
      }

      // setup egress qos map for tc
      cfg::ExpQosMap expMap;
      *expMap.internalTrafficClass() = tc;
      expMap.fromExpToTrafficClass()->push_back(tc);
      expMap.fromTrafficClassToExp() = 7 - tc;

      qosMap.dscpMaps()->push_back(dscpMap);
      qosMap.expMaps()->push_back(expMap);
      qosMap.trafficClassToQueueId()->emplace(tc, tc);
    }
    config.qosPolicies()->resize(1);
    *config.qosPolicies()[0].name() = "qp";
    config.qosPolicies()[0].qosMap() = qosMap;

    cfg::TrafficPolicyConfig policy;
    policy.defaultQosPolicy() = "qp";
    config.dataPlaneTrafficPolicy() = policy;

    utility::setDefaultCpuTrafficPolicyConfig(
        config, l3Asics, ensemble.isSai());
    utility::addCpuQueueConfig(config, l3Asics, ensemble.isSai());

    utility::addTrapPacketAcl(asic, &config, masterLogicalPorts[0]);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::MPLS};
  }

  void addRoute(
      LabelID label,
      PortDescriptor port,
      LabelForwardingAction::LabelStack stack = {},
      LabelForwardingAction::LabelForwardingType type =
          LabelForwardingAction::LabelForwardingType::SWAP) {
    applyNewState(
        [this, &port](const std::shared_ptr<SwitchState>& state) {
          return getEcmpHelper()->resolveNextHops(state, {port});
        },
        "resolve nexthops");

    getEcmpHelper()->programMplsRoutes(
        getAgentEnsemble()->getRouteUpdaterWrapper(),
        {port},
        {{port, std::move(stack)}},
        {label},
        type);
  }

  void addRoute(
      folly::IPAddressV6 prefix,
      uint8_t mask,
      PortDescriptor port,
      LabelForwardingAction::LabelStack stack = {}) {
    applyNewState(
        [this, &port](const std::shared_ptr<SwitchState>& state) {
          return getEcmpHelper()->resolveNextHops(state, {port});
        },
        "resolve nexthops");

    if (stack.empty()) {
      getEcmpHelper()->programRoutes(
          getAgentEnsemble()->getRouteUpdaterWrapper(),
          {port},
          {RoutePrefixV6{prefix, mask}});
    } else {
      getEcmpHelper()->programIp2MplsRoutes(
          getAgentEnsemble()->getRouteUpdaterWrapper(),
          {port},
          {{port, std::move(stack)}},
          {RoutePrefixV6{prefix, mask}});
    }
  }

  // Takes nexthops directly; the entry was only read for its inline set.
  void addRoute(LabelID label, const RouteNextHopSet& nhops) {
    auto updater = getAgentEnsemble()->getRouteUpdaterWrapper();
    MplsRoute route;
    route.topLabel() = label;
    route.nextHops() = util::fromRouteNextHopSet(nhops);
    updater->addRoute(ClientID::BGPD, route);
    updater->program();
  }

  void sendL3Packet(
      folly::IPAddressV6 dst,
      PortID from,
      std::optional<DSCP> dscp = std::nullopt) {
    CHECK(getEcmpHelper());
    auto vlan = getVlanIDForTx();
    if (!vlan) {
      throw FbossError("VLAN id unavailable for test");
    }
    auto vlanId = *vlan;

    // construct eth hdr
    const auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);

    EthHdr::VlanTags_t vlans{
        VlanTag(vlanId, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN))};

    EthHdr eth{intfMac, intfMac, std::move(vlans), 0x86DD};
    // construct l3 hdr
    IPv6Hdr ip6{folly::IPAddressV6("1::10"), dst};
    ip6.nextHeader = 17; /* udp payload */
    if (dscp) {
      ip6.trafficClass = (dscp.value() << 2); // last two bits are ECN
    }
    UDPHeader udp(4049, 4050, 1);
    utility::UDPDatagram datagram(udp, {0xff});
    auto pkt = utility::EthFrame(
                   eth, utility::IPPacket<folly::IPAddressV6>(ip6, datagram))
                   .getTxPacket([sw = getSw()](uint32_t size) {
                     return sw->allocatePacket(size);
                   });
    XLOG(DBG2) << "sending packet: ";
    XLOG(DBG2) << PktUtil::hexDump(pkt->buf());
    // send pkt on src port, let it loop back in switch and be l3 switched
    getAgentEnsemble()->ensureSendPacketOutOfPort(std::move(pkt), from);
  }

  void sendMplsPacket(
      uint32_t topLabel,
      PortID from,
      std::optional<EXP> exp = std::nullopt,
      uint8_t ttl = 128,
      folly::IPAddressV6 dstIp = folly::IPAddressV6("2001::")) {
    const auto srcMac = utility::kLocalCpuMac();
    const auto dstMac = utility::kLocalCpuMac(); /* for l3 switching */

    auto vlan = getVlanIDForTx();
    if (!vlan) {
      throw FbossError("VLAN id unavailable for test");
    }
    auto vlanId = *vlan;

    uint8_t tc = exp.has_value() ? static_cast<uint8_t>(exp.value()) : 0;
    MPLSHdr::Label mplsLabel{topLabel, tc, true, ttl};

    auto srcIp = folly::IPAddressV6(("1001::"));

    auto frame = utility::getEthFrame(
        srcMac,
        dstMac,
        {mplsLabel},
        srcIp,
        dstIp,
        10000,
        20000,
        VlanID(vlanId));

    auto pkt = frame.getTxPacket(
        [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
    XLOG(DBG2) << "sending packet: ";
    XLOG(DBG2) << PktUtil::hexDump(pkt->buf());
    getAgentEnsemble()->ensureSendPacketOutOfPort(std::move(pkt), from);
  }

  void getNexthops(
      const std::vector<std::pair<PortDescriptor, InterfaceID>>& portIntfs,
      const std::vector<LabelForwardingAction::LabelStack>& stacks,
      RouteNextHopSet& nhops) {
    int idx = 0;
    for (auto portIntf : portIntfs) {
      nhops.emplace(ResolvedNextHop(
          getEcmpHelper()->ip(portIntf.first),
          portIntf.second,
          UCMP_DEFAULT_WEIGHT,
          LabelForwardingAction(
              LabelForwardingAction::LabelForwardingType::PUSH,
              stacks[idx++])));
    }
  }

  void addRedirectToNexthopAcl(
      const std::string& aclName,
      uint32_t ingressVlanId,
      const std::string& dstPrefix,
      const std::vector<std::string>& redirectNexthopIps,
      const std::vector<std::pair<PortDescriptor, InterfaceID>>& portIntfs,
      const std::vector<LabelForwardingAction::LabelStack>& stacks) {
    RouteNextHopSet recursiveNexthops;
    getNexthops(portIntfs, stacks, recursiveNexthops);
    boost::container::flat_set<PortDescriptor> ports;
    for (auto portIntf : portIntfs) {
      ports.emplace(portIntf.first);
    }
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& state) {
          std::shared_ptr<SwitchState> newState;
          if (ports.size() > 0) {
            newState = getEcmpHelper()->resolveNextHops(state, ports);
          } else {
            newState = state->clone();
          }
          auto newAcl = std::make_shared<AclEntry>(
              AclTable::kDataplaneAclMaxPriority, aclName);
          newAcl->setDstIp(
              folly::IPAddress::tryCreateNetwork(dstPrefix).value());
          newAcl->setVlanID(ingressVlanId);
          auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
          for (auto nhIp : redirectNexthopIps) {
            cfg::RedirectNextHop nhop;
            nhop.ip() = nhIp;
            cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
          }
          auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
          redirectToNextHop.first = cfgRedirectToNextHop;
          redirectToNextHop.second = recursiveNexthops;
          MatchAction action = MatchAction();
          action.setRedirectToNextHop(redirectToNextHop);
          newAcl->setAclAction(action);
          auto acls = newState->getAcls()->modify(&newState);
          acls->addNode(newAcl, scopeResolver().scope(newAcl));
          return newState;
        },
        "add redirect to nexthop ACL");
  }

  void setup() {
    if constexpr (std::is_same_v<PortType, AggregatePortID>) {
      applyNewState(
          [](const std::shared_ptr<SwitchState>& state) {
            return utility::enableTrunkPorts(state);
          },
          "enable trunk ports");
    }
  }

  void sendMplsPktAndVerifyTrappedCpuQueue(
      int queueId,
      int label = 1101,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1,
      uint8_t ttl = 128) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),
        switchIdForPort(masterLogicalInterfacePortIds()[0]),
        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    for (int i = 0; i < numPktsToSend; i++) {
      sendMplsPacket(label, masterLogicalInterfacePortIds()[1], EXP(5), ttl);
    }
    WITH_RETRIES({
      auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
          getSw(),
          switchIdForPort(masterLogicalInterfacePortIds()[0]),
          queueId,
          kGetQueueOutPktsRetryTimes,
          beforeOutPkts + numPktsToSend);
      XLOG(DBG0) << ". Queue=" << queueId << ", before pkts:" << beforeOutPkts
                 << ", after pkts:" << afterOutPkts;
      EXPECT_EVENTUALLY_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
    });
  }

  AgentPacketVerifier getPacketVerifier(MPLSHdr hdr) {
    return AgentPacketVerifier(
        getSw(),
        isSupportedOnAllAsics(
            HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER),
        hdr);
  }

  std::unique_ptr<utility::EcmpSetupTargetedPorts6> ecmpHelper_;
};

TYPED_TEST_SUITE(AgentMPLSTest, TestTypes);

TYPED_TEST(AgentMPLSTest, Push) {
  auto setup = [=, this]() {
    this->setup();
    // setup ip2mpls route to 2401::201:ab00/120 through
    // port 0 w/ stack {101, 102}
    this->addRoute(
        folly::IPAddressV6("2401::201:ab00"),
        120,
        this->getPortDescriptor(0),
        {101, 102});
  };
  auto verify = [=, this]() {
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{102, 5, 0, 254}, // exp = 5 for tc = 2
        MPLSHdr::Label{101, 5, 1, 254}, // exp = 5 for tc = 2
    });
    [[maybe_unused]] auto verifier = this->getPacketVerifier(expectedMplsHdr);

    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));

    // generate the packet entering port 1
    this->sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        this->masterLogicalInterfacePortIds()[1],
        DSCP(16)); // tc = 2 for dscp = 16

    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, Swap) {
  auto setup = [=, this]() {
    this->setup();
    this->addRoute(LabelID(1101), this->getPortDescriptor(0), {11});
  };
  auto verify = [=, this]() {
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{11, 2, true, 127}, // exp is remarked to 2
    });
    [[maybe_unused]] auto verifier = this->getPacketVerifier(expectedMplsHdr);

    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));

    // generate the packet entering port 1
    this->sendMplsPacket(
        1101,
        this->masterLogicalInterfacePortIds()[1],
        EXP(5)); // send packet with exp 5

    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, Pop) {
  auto setup = [=, this]() {
    this->setup();
    // pop and lookup 1101
    this->addRoute(
        LabelID(1101),
        this->getPortDescriptor(0),
        {},
        LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP);
    // setup route for 2001::, dest ip under label 1101
    this->addRoute(
        folly::IPAddressV6("2001::"), 128, this->getPortDescriptor(0));
  };
  auto verify = [=, this]() {
    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
    // send mpls packet with label and let it pop
    this->sendMplsPacket(1101, this->masterLogicalInterfacePortIds()[1]);
    // ip packet should be forwarded as per route for 2001::/128
    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, Php) {
  auto setup = [=, this]() {
    this->setup();
    // php to exit out of port 0
    this->addRoute(
        LabelID(1101),
        this->getPortDescriptor(0),
        {},
        LabelForwardingAction::LabelForwardingType::PHP);
  };
  auto verify = [=, this]() {
    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
    // send mpls packet with label and let it forward with php
    this->sendMplsPacket(1101, this->masterLogicalInterfacePortIds()[1]);
    // ip packet should be forwarded through port 0
    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, Pop2Cpu) {
  auto setup = [=, this]() {
    this->setup();
    // pop and lookup 1101
    this->addRoute(
        LabelID(1101),
        this->getPortDescriptor(0),
        {},
        LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP);
  };
  auto verify = [=, this]() {
    utility::SwSwitchPacketSnooper snooper(this->getSw(), "pop2cpu-verifier");

    // send mpls packet with label and let it pop
    this->sendMplsPacket(
        1101,
        this->masterLogicalInterfacePortIds()[1],
        EXP(0),
        128,
        folly::IPAddressV6("1::0"));
    // ip packet should be punted to cpu after Pop
    auto pktBuf = snooper.waitForPacket(10);
    ASSERT_TRUE(pktBuf.has_value());
    ASSERT_TRUE(*pktBuf);

    folly::io::Cursor cursor((*pktBuf).get());
    utility::EthFrame frame(cursor);

    auto mplsPayLoad = frame.mplsPayLoad();
    ASSERT_FALSE(mplsPayLoad.has_value());

    auto v6PayLoad = frame.v6PayLoad();
    ASSERT_TRUE(v6PayLoad.has_value());

    auto udpPayload = v6PayLoad->udpPayload();
    ASSERT_TRUE(udpPayload.has_value());

    auto hdr = v6PayLoad->header();
    EXPECT_EQ(hdr.srcAddr, folly::IPAddress("1001::"));
    EXPECT_EQ(hdr.dstAddr, folly::IPAddress("1::0"));
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, punt2Cpu) {
  auto setup = [=, this]() {
    this->setup();
    // An empty set is the TO_CPU case, all the old entry argument conveyed.
    this->addRoute(LabelID(1101), RouteNextHopSet{});
  };
  auto verify = [=, this]() {
    utility::SwSwitchPacketSnooper snooper(this->getSw(), "punt2cpu-verifier");

    // send mpls packet with label
    this->sendMplsPacket(
        1101,
        this->masterLogicalInterfacePortIds()[1],
        EXP(0),
        128,
        folly::IPAddressV6("1::0"));
    // mpls packet should be punted to cpu
    auto pktBuf = snooper.waitForPacket(10);
    ASSERT_TRUE(pktBuf.has_value());
    ASSERT_TRUE(*pktBuf);

    folly::io::Cursor cursor((*pktBuf).get());
    utility::EthFrame frame(cursor);

    auto mplsPayLoad = frame.mplsPayLoad();
    ASSERT_TRUE(mplsPayLoad.has_value());
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, ExpiringTTL) {
  auto setup = [=, this]() {
    this->setup();
    this->addRoute(LabelID(1101), this->getPortDescriptor(0), {11});
  };
  auto verify = [=, this]() {
    this->sendMplsPktAndVerifyTrappedCpuQueue(
        utility::kCoppLowPriQueueId, 1101, 1, 1, 1);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, MplsNoMatchPktsToLowPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    auto portStatsBefore =
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[1]);
    auto statBefore = *portStatsBefore.inLabelMissDiscards_();

    this->sendMplsPktAndVerifyTrappedCpuQueue(utility::kCoppLowPriQueueId);

    WITH_RETRIES({
      auto portStatsAfter =
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[1]);
      auto statAfter = *portStatsAfter.inLabelMissDiscards_();

      EXPECT_EVENTUALLY_EQ(statBefore + 1, statAfter);
      EXPECT_EQ(
          portStatsAfter.inDiscardsRaw_(), portStatsBefore.inDiscardsRaw_());
      EXPECT_EQ(
          portStatsAfter.inDstNullDiscards_(),
          portStatsBefore.inDstNullDiscards_());
      EXPECT_EQ(portStatsAfter.inDiscards_(), portStatsBefore.inDiscards_());
    });
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, MplsMatchPktsNottrapped) {
  auto setup = [=, this]() {
    this->setup();
    this->addRoute(LabelID(1101), this->getPortDescriptor(0), {11});
  };

  auto verify = [=, this]() {
    const auto& mplsNoMatchCounter = utility::getMplsDestNoMatchCounterName();
    auto statBefore =
        utility::getAclInOutPackets(this->getSw(), mplsNoMatchCounter);

    this->sendMplsPktAndVerifyTrappedCpuQueue(
        utility::kCoppLowPriQueueId, 1101, 1 /* To send*/, 0 /* expected*/);

    WITH_RETRIES({
      auto statAfter =
          utility::getAclInOutPackets(this->getSw(), mplsNoMatchCounter);
      EXPECT_EVENTUALLY_EQ(statBefore, statAfter);
    });
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, AclRedirectToNexthop) {
  auto setup = [=, this]() {
    this->setup();
    std::string dstIp{"2401::201:ab00"};
    uint8_t mask = 120;
    this->addRoute(
        folly::IPAddressV6(dstIp),
        mask,
        this->getPortDescriptor(0),
        {101, 102});
    std::string dstPrefix{fmt::format("{}/{}", dstIp, mask)};
    // We are using port 1 as ingress port. Which has ingress vlan_id
    // kBaseVlanId + 1
    uint32_t ingressVlan = utility::kBaseVlanId + 1;
    auto config = this->initialConfig(*this->getAgentEnsemble());
    std::vector<std::pair<PortDescriptor, InterfaceID>> portIntfs{
        std::make_pair(
            this->getPortDescriptor(0),
            InterfaceID(*config.interfaces()[0].intfID()))};
    this->addRedirectToNexthopAcl(
        kAclName, ingressVlan, dstPrefix, {"1000::1"}, portIntfs, {{201, 202}});
  };
  auto verify = [=, this]() {
    // Use different labels from that of the rib route to verify that the
    // redirect ACL is in effect
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{202, 5, 0, 254},
        MPLSHdr::Label{201, 5, 1, 254},
    });
    [[maybe_unused]] auto verifier = this->getPacketVerifier(expectedMplsHdr);
    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
    this->sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        this->masterLogicalInterfacePortIds()[1],
        DSCP(16));
    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentMPLSTest, AclRedirectToNexthopDrop) {
  // Packets must be dropped if there are no resolved nexthops
  // in the redirect ACL entry
  auto setup = [=, this]() {
    this->setup();
    std::string dstIp{"2401::201:ab00"};
    uint8_t mask = 120;
    this->addRoute(
        folly::IPAddressV6(dstIp),
        mask,
        this->getPortDescriptor(0),
        {101, 102});
    std::string dstPrefix{fmt::format("{}/{}", dstIp, mask)};
    // We are using port 1 as ingress port. Which has ingress vlan_id
    // kBaseVlanId + 1
    uint32_t ingressVlan = utility::kBaseVlanId + 1;
    std::vector<std::pair<PortDescriptor, InterfaceID>> portIntfs;
    this->addRedirectToNexthopAcl(
        kAclName, ingressVlan, dstPrefix, {"1000::1"}, portIntfs, {});
  };
  auto verify = [=, this]() {
    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
    this->sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        this->masterLogicalInterfacePortIds()[1],
        DSCP(16));
    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      // Packet drop expected
      EXPECT_EVENTUALLY_EQ(outPktsAfter, outPktsBefore);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

// Verify that redirect does not happen when ACL qualifiers do not match
TYPED_TEST(AgentMPLSTest, AclRedirectToNexthopMismatch) {
  auto setup = [=, this]() {
    this->setup();
    std::string dstIp{"2401::201:ab00"};
    uint8_t mask = 120;
    this->addRoute(
        folly::IPAddressV6(dstIp),
        mask,
        this->getPortDescriptor(0),
        {101, 102});
    std::string dstPrefix{fmt::format("{}/{}", dstIp, mask)};
    auto config = this->initialConfig(*this->getAgentEnsemble());
    std::vector<std::pair<PortDescriptor, InterfaceID>> portIntfs{
        std::make_pair(
            this->getPortDescriptor(0),
            InterfaceID(*config.interfaces()[0].intfID()))};
    // Use VLAN ID qualifier value that does not match the actual
    // ingress vlan.
    uint32_t ingressVlan = utility::kBaseVlanId + 100;
    this->addRedirectToNexthopAcl(
        kAclName, ingressVlan, dstPrefix, {"1000::1"}, portIntfs, {{201, 202}});
  };
  auto verify = [=, this]() {
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{102, 5, 0, 254},
        MPLSHdr::Label{101, 5, 1, 254},
    });
    // Since ACL qualifiers do not match packet fields, packet must
    // exit via RIB route nexthops
    [[maybe_unused]] auto verifier = this->getPacketVerifier(expectedMplsHdr);
    auto outPktsBefore = utility::getPortOutPkts(
        this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
    this->sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        this->masterLogicalInterfacePortIds()[1],
        DSCP(16));
    WITH_RETRIES({
      auto outPktsAfter = utility::getPortOutPkts(
          this->getLatestPortStats(this->masterLogicalInterfacePortIds()[0]));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

// Test ACL redirect to an ECMP nexthop
TYPED_TEST(AgentMPLSTest, AclRedirectToNexthopMultipleNexthops) {
  auto setup = [=, this]() {
    this->setup();
    std::string dstIp{"2401::201:ab00"};
    uint8_t mask = 120;
    std::string dstPrefix{fmt::format("{}/{}", dstIp, mask)};
    // We are using port 2 as ingress port. Which has ingress vlan_id
    // kBaseVlanId + 2
    uint32_t ingressVlan = utility::kBaseVlanId + 2;
    auto config = this->initialConfig(*this->getAgentEnsemble());
    std::vector<std::pair<PortDescriptor, InterfaceID>> portIntfs{
        std::make_pair(
            this->getPortDescriptor(0),
            InterfaceID(*config.interfaces()[0].intfID())),
        std::make_pair(
            this->getPortDescriptor(1),
            InterfaceID(*config.interfaces()[1].intfID())),
    };
    this->addRedirectToNexthopAcl(
        kAclName,
        ingressVlan,
        dstPrefix,
        {"1000::1"},
        portIntfs,
        {{201, 202}, {301, 302}});
  };
  auto verify = [=, this]() {
    std::vector<PortID> ports{
        this->masterLogicalInterfacePortIds()[0],
        this->masterLogicalInterfacePortIds()[1]};
    auto outPktsBefore =
        utility::getPortOutPkts(this->getLatestPortStats(ports));
    this->sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        this->masterLogicalInterfacePortIds()[2],
        DSCP(16));
    WITH_RETRIES({
      auto outPktsAfter =
          utility::getPortOutPkts(this->getLatestPortStats(ports));
      EXPECT_EVENTUALLY_EQ((outPktsAfter - outPktsBefore), 1);
    });
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
