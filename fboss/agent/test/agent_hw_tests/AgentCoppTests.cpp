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
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwRxPacket.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/DHCPv4Packet.h"
#include "fboss/agent/packet/DHCPv6Packet.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/QueueTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

DECLARE_bool(sai_user_defined_trap);

namespace {
constexpr auto kGetQueueOutPktsRetryTimes = 5;
static auto const kIpForLowPriorityQueue = folly::IPAddress("2401::1");

/**
 * Link-local multicast network
 */
const auto kIPv6LinkLocalMcastAbsoluteAddress = folly::IPAddressV6("ff02::0");
const auto kIPv6LinkLocalMcastAddress = folly::IPAddressV6("ff02::5");
// Link-local unicast network
const auto kIPv6LinkLocalUcastAddress = folly::IPAddressV6("fe80::2");

const auto kMcastMacAddress = folly::MacAddress("01:05:0E:01:02:03");

const auto kNetworkControlDscp = 48;
const auto kRandomPort = 54131;

const auto kDhcpV6AllRoutersIp = folly::IPAddressV6("ff02::1:2");
const auto kDhcpV6McastMacAddress = folly::MacAddress("33:33:00:01:00:02");
const auto kDhcpV6ServerGlobalUnicastAddress =
    folly::IPAddressV6("2401:db00:eef0:a67::1");
const auto kRandomIP = folly::IPAddressV6(facebook::fboss::kTestDstIpV6);
const auto kGlobalRateLimit = 1.5 * 1024 * 1024;

using TestTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;
} // unnamed namespace

namespace facebook::fboss {

template <typename TestType>
class AgentCoppTest : public AgentHwTest {
  void setCmdLineFlagOverrides() const override {
    FLAGS_sai_user_defined_trap = true;
    AgentHwTest::setCmdLineFlagOverrides();
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<TestType, PortID>) {
      return {ProductionFeature::COPP};
    } else {
      return {ProductionFeature::COPP, ProductionFeature::LAG};
    }
  }

 protected:
  static constexpr auto isTrunk = std::is_same_v<TestType, AggregatePortID>;

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    if (isTrunk) {
      return getTrunkInitialConfig(ensemble);
    }
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto asic = checkSameAndGetAsic(
        ensemble.getL3Asics(), static_cast<int32_t>(switchId));
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getPlatformMapping(),
        asic,
        ensemble.masterLogicalPortIds(switchId),
        ensemble.supportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        true /*interfaceHasSubnet*/,
        true /*setInterfaceMac*/,
        utility::kBaseVlanId,
        false /*enableFabricPorts*/,
        ensemble.getSw()->getSwitchInfoTable().getSwitchIdToSwitchInfo(),
        ensemble.getSw()->getHwAsicTable()->getHwAsics(),
        ensemble.getSw()->getPlatformType());

    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(cfg, ensemble.getL3Asics(), ensemble.isSai());
    return cfg;
  }

  cfg::SwitchConfig getTrunkInitialConfig(const AgentEnsemble& ensemble) const {
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto interfacePorts = ensemble.masterLogicalInterfacePortIds(switchId);
    std::vector<PortID> members;
    for (auto port : {interfacePorts[0], interfacePorts[1]}) {
      members.emplace_back(port);
    }
    auto cfg = utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getSw(), members, {{AggregatePortID(1), members}});
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(cfg, ensemble.getL3Asics(), ensemble.isSai());
    return cfg;
  }

  void setup() {
    // COPP is part of initial config already
    if constexpr (isTrunk) {
      applyNewState([this](std::shared_ptr<SwitchState> /*in*/) {
        return utility::enableTrunkPorts(getProgrammedState());
      });
    }
  }

  std::vector<PortID> portIdsForTest() {
    auto switchId = this->getCurrentSwitchIdForTesting();
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds(switchId);
    }
    return masterLogicalInterfacePortIds(switchId);
  }

  folly::IPAddress getInSubnetNonSwitchIP() const {
    // For non-VOQ switches, use the simple config-based approach
    if (!(this->isSupportedOnAllAsics(HwAsic::Feature::VOQ))) {
      auto config = initialConfig(*getAgentEnsemble());
      auto ipAddress = config.interfaces()[0].ipAddresses()[0];
      return folly::IPAddress::createNetwork(ipAddress, -1, true).first;
    }

    // For VOQ switches, iterate interfaces for the NPU under test,
    // find a GLOBAL-scoped interface with an IPv6 address, and offset
    // it by 0x100 to get a different IP in the same subnet
    auto switchId = getSwitchIdUnderTest(*getAgentEnsemble());
    auto state = getProgrammedState();

    for (const auto& [matcher, intfMap] :
         std::as_const(*state->getInterfaces())) {
      if (!HwSwitchMatcher(matcher).has(switchId)) {
        continue;
      }
      for (const auto& [intfID, intf] : std::as_const(*intfMap)) {
        if (intf->isVirtual() || intf->getScope() != cfg::Scope::GLOBAL) {
          continue;
        }

        for (const auto& [addr, mask] : std::as_const(*intf->getAddresses())) {
          auto ip = folly::IPAddress(addr);
          if (!ip.isV6()) {
            continue;
          }
          // Add 0x100 to get a non-switch IP in the same subnet
          auto bytes = ip.asV6().toByteArray();
          bytes[bytes.size() - 2] += 1; // add 0x100
          auto randomIP = folly::IPAddress(folly::IPAddressV6(bytes));
          return randomIP;
        }
      }
    }
    throw FbossError(
        "No GLOBAL-scoped interface with IPv6 address found for switchId: ",
        switchId);
  }

  void sendTcpPkts(
      int numPktsToSend,
      const folly::IPAddress& dstIpAddress,
      int l4SrcPort,
      int l4DstPort,
      const std::optional<folly::MacAddress>& dstMac = std::nullopt,
      uint8_t trafficClass = 0,
      std::optional<std::vector<uint8_t>> payload = std::nullopt) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    utility::sendTcpPkts(
        getSw(),
        numPktsToSend,
        vlanId,
        dstMac ? *dstMac : intfMac,
        dstIpAddress,
        l4SrcPort,
        l4DstPort,
        portIdsForTest()[0],
        trafficClass,
        payload);
  }

  // With onePortPerInterfaceConfig, we have a large (~200) number of
  // interfaces. Thus, if we send packet for every interface, the test takes
  // really long (5+ mins) to complete), and does not really offer additional
  // coverage. Thus, pick one IPv4 and IPv6 address and test.
  std::vector<std::string> getIpAddrsToSendPktsTo() const {
    std::set<std::string> ips;
    auto addV4AndV6 = [&](const auto& ipAddrs) {
      auto ipv4Addr =
          std::find_if(ipAddrs.begin(), ipAddrs.end(), [](const auto& ipAddr) {
            auto ip = folly::IPAddress::createNetwork(ipAddr, -1, false).first;
            return ip.isV4();
          });
      auto ipv6Addr =
          std::find_if(ipAddrs.begin(), ipAddrs.end(), [](const auto& ipAddr) {
            auto ip = folly::IPAddress::createNetwork(ipAddr, -1, false).first;
            return ip.isV6();
          });
      ips.insert(*ipv4Addr);
      ips.insert(*ipv6Addr);
    };
    auto switchId = getSwitchIdUnderTest(*getAgentEnsemble());
    auto intfId = firstInterfaceIDWithPortsForTesting(getProgrammedState());
    auto config = this->initialConfig(*getAgentEnsemble());
    for (const auto& configIntf : *config.interfaces()) {
      if (InterfaceID(*configIntf.intfID()) == intfId) {
        addV4AndV6(*configIntf.ipAddresses());
        break;
      }
    }

    auto dsfNode = getProgrammedState()->getDsfNodes()->getNodeIf(switchId);
    if (dsfNode) {
      auto loopbackIps = dsfNode->getLoopbackIps();
      std::vector<std::string> subnets;
      std::for_each(
          loopbackIps->begin(), loopbackIps->end(), [&](const auto& ip) {
            subnets.push_back(**ip);
          });
      addV4AndV6(subnets);
    }

    return std::vector<std::string>{ips.begin(), ips.end()};
  }

  void sendPkt(
      std::unique_ptr<TxPacket> pkt,
      bool outOfPort,
      bool expectRxPacket = false,
      bool skipTtlDecrement = true,
      const utility::PacketComparatorFn& packetComparator = std::nullopt) {
    XLOG(DBG2) << "Packet Dump::"
               << folly::hexDump(pkt->buf()->data(), pkt->buf()->length());

    auto ethFrame = utility::makeEthFrame(*pkt, skipTtlDecrement);
    utility::SwSwitchPacketSnooper snooper(
        getSw(), "snoop", std::nullopt, ethFrame, packetComparator);
    if (outOfPort) {
      getSw()->sendPacketOutOfPortAsync(std::move(pkt), portIdsForTest()[0]);
    } else {
      sendPacketSwitchedAsync(std::move(pkt));
    }
    if (expectRxPacket) {
      WITH_RETRIES({
        auto frameRx = snooper.waitForPacket(1);
        EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
      });
    } else {
      auto frameRx = snooper.waitForPacket(10);
      EXPECT_FALSE(frameRx.has_value());
    }
  }

  void sendTcpPktAndVerifyCpuQueue(
      int queueId,
      const folly::IPAddress& dstIpAddress,
      const int l4SrcPort,
      const int l4DstPort,
      const std::optional<folly::MacAddress>& dstMac = std::nullopt,
      uint8_t trafficClass = 0,
      std::optional<std::vector<uint8_t>> payload = std::nullopt,
      bool expectQueueHit = true,
      bool outOfPort = true,
      bool skipTtlDecrement = true,
      bool verifyPktCntInOtherQueues = true,
      const utility::PacketComparatorFn& packetComparator = std::nullopt) {
    const auto kNumPktsToSend = 1;
    auto vlanId = getVlanIDForTx();
    auto destinationMac = dstMac.value_or(
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()));
    if (outOfPort) {
      // Same SA makeTCPTxPacket derives.
      learnL2EntryIfPending(
          destinationMac.isUnicast()
              ? folly::MacAddress::fromHBO(destinationMac.u64HBO() + 1)
              : folly::MacAddress("00:00:01:02:03:04"),
          portIdsForTest()[0]);
    }
    auto sendAndInspect = [=, this]() {
      auto pkt = utility::makeTCPTxPacket(
          getSw(),
          vlanId,
          destinationMac,
          dstIpAddress,
          l4SrcPort,
          l4DstPort,
          trafficClass,
          payload);
      sendPkt(
          std::move(pkt),
          outOfPort,
          expectQueueHit /*expectRxPacket*/,
          skipTtlDecrement,
          packetComparator);
    };
    utility::sendPktAndVerifyCpuQueue(
        getSw(),
        switchIdForPort(portIdsForTest()[0]),
        queueId,
        sendAndInspect,
        expectQueueHit ? kNumPktsToSend : 0,
        verifyPktCntInOtherQueues);
  }

  void sendUdpPkt(
      const folly::IPAddress& dstIpAddress,
      int l4SrcPort,
      int l4DstPort,
      uint8_t ttl,
      bool outOfPort,
      bool expectPktTrap) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    // arbit
    const auto srcIp =
        folly::IPAddress(dstIpAddress.isV4() ? "1.0.0.11" : "1::11");
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        srcIp,
        dstIpAddress,
        l4SrcPort,
        l4DstPort,
        0 /* dscp */,
        ttl);

    XLOG(DBG2) << "UDP packet Dump::"
               << folly::hexDump(
                      txPacket->buf()->data(), txPacket->buf()->length());
    sendPkt(std::move(txPacket), outOfPort, expectPktTrap /*expectRxPacket*/);
  }

  void sendUdpPktAndVerify(
      int queueId,
      const folly::IPAddress& dstIpAddress,
      const int l4SrcPort,
      const int l4DstPort,
      bool expectPktTrap = true,
      const int ttl = 255,
      bool outOfPort = false) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    auto expectedPktDelta = expectPktTrap ? 1 : 0;
    sendUdpPkt(
        dstIpAddress, l4SrcPort, l4DstPort, ttl, outOfPort, expectPktTrap);
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + 1);
    XLOG(DBG0) << "Queue=" << queueId << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }

  void sendEthPkts(
      int numPktsToSend,
      facebook::fboss::ETHERTYPE etherType,
      const std::optional<folly::MacAddress>& dstMac = std::nullopt,
      std::optional<std::vector<uint8_t>> payload = std::nullopt) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());

    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket = utility::makeEthTxPacket(
          getSw(),
          vlanId,
          intfMac,
          dstMac ? *dstMac : intfMac,
          etherType,
          payload);
      sendPkt(std::move(txPacket), true /*outOfPort*/, true /*expectRxPacket*/);
    }
  }

  void sendPktAndVerifyEthPacketsCpuQueue(
      int queueId,
      facebook::fboss::ETHERTYPE etherType,
      const std::optional<folly::MacAddress>& dstMac = std::nullopt) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    static auto payload = std::vector<uint8_t>(256, 0xff);
    payload[0] = 0x1; // sub-version of lacp packet
    sendEthPkts(1, etherType, dstMac, payload);
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + 1);
    XLOG(DBG0) << "Packet of dstMac="
               << (dstMac ? (*dstMac).toString()
                          : getLocalMacAddress().toString())
               << ". Ethertype=" << std::hex << int(etherType)
               << ". Queue=" << queueId << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(1, afterOutPkts - beforeOutPkts);
  }

  void setupEcmp(bool useInterfaceMac = false) {
    if constexpr (!isTrunk) {
      utility::EcmpSetupAnyNPorts6 ecmpHelper(
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          useInterfaceMac
              ? getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())
              : getLocalMacAddress());
      resolveNeighborAndProgramRoutes(ecmpHelper, 1);
    } else {
      utility::EcmpSetupTargetedPorts6 ecmpHelper(
          getProgrammedState(),
          getSw()->needL2EntryForNeighbor(),
          useInterfaceMac
              ? getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())
              : getLocalMacAddress());
      boost::container::flat_set<PortDescriptor> ports;
      ports.insert(PortDescriptor(AggregatePortID(1)));
      applyNewState(
          [this, &ports, &ecmpHelper](std::shared_ptr<SwitchState> /*in*/) {
            return ecmpHelper.resolveNextHops(getProgrammedState(), ports);
          });
      auto wrapper = getSw()->getRouteUpdater();
      ecmpHelper.programRoutes(&wrapper, ports);
    }
  }

  bool needL3EcmpLoop() {
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    // no l2 bridging, need to create L3 loop or chenab in which case l3 rifs
    // are used
    return !this->isSupportedOnAllAsics(HwAsic::Feature::BRIDGE_PORT_8021Q) ||
        asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB;
  }

  void setupL3EcmpLoopIf() {
    if (!needL3EcmpLoop()) {
      return;
    }
    setupEcmp(true);
  }

  void sendArpPkts(
      int numPktsToSend,
      const folly::IPAddress& dstIpAddress,
      ARP_OPER arpType,
      bool outOfPort) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket = utility::makeARPTxPacket(
          getSw(),
          vlanId,
          srcMac,
          arpType == ARP_OPER::ARP_OPER_REQUEST
              ? folly::MacAddress("ff:ff:ff:ff:ff:ff")
              : intfMac,
          folly::IPAddress("1.1.1.2"),
          dstIpAddress,
          arpType);
      sendPkt(std::move(txPacket), outOfPort, true /*expectRxPacket*/);
    }
  }

  void sendPktAndVerifyArpPacketsCpuQueue(
      int queueId,
      const folly::IPAddress& dstIpAddress,
      ARP_OPER arpType,
      bool outOfPort = true,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),
        switchIdForPort(portIdsForTest()[0]),
        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    sendArpPkts(numPktsToSend, dstIpAddress, arpType, outOfPort);
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),
        switchIdForPort(portIdsForTest()[0]),
        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + 1);
    XLOG(DBG0) << "Packet of DstIp=" << dstIpAddress.str()
               << ", dstMac=" << ". Queue=" << queueId
               << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }

  void sendNdpPkts(
      int numPktsToSend,
      const folly::IPAddressV6& neighborIp,
      ICMPv6Type type,
      bool outOfPort,
      bool selfSolicit,
      bool expectRxPacket = true) {
    InterfaceID intfId =
        firstInterfaceIDWithPortsForTesting(getProgrammedState());
    auto intf = getProgrammedState()->getInterfaces()->getNode(intfId);
    std::optional<VlanID> vlanId{};
    if (intf->getType() == cfg::InterfaceType::VLAN) {
      vlanId = intf->getVlanID();
    }
    auto myAddr = utility::getIntfAddrsV6(getProgrammedState(), intfId)[0];
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto neighborMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);

    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket =
          (type == ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION)
          ? utility::makeNeighborSolicitation(
                getSw(),
                vlanId,
                selfSolicit ? neighborMac : intfMac, // solicitar mac
                neighborIp, // solicitar ip
                selfSolicit ? myAddr
                            : folly::IPAddressV6("1::2")) // solicited address
          : utility::makeNeighborAdvertisement(
                getSw(),
                vlanId,
                neighborMac, // sender mac
                intfMac, // my mac
                neighborIp, // sender ip
                myAddr); // sent to me
      sendPkt(std::move(txPacket), outOfPort, expectRxPacket);
    }
  }

  void sendPktAndVerifyNdpPacketsCpuQueue(
      int queueId,
      const folly::IPAddressV6& neighborIp,
      ICMPv6Type ndpType,
      bool selfSolicit = true,
      bool outOfPort = true,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1,
      bool expectRxPacket = true) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    sendNdpPkts(
        numPktsToSend,
        neighborIp,
        ndpType,
        outOfPort,
        selfSolicit,
        expectRxPacket);
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + expectedPktDelta);
    XLOG(DBG0) << "Packet of neighbor=" << neighborIp.str()
               << ". Queue=" << queueId << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }

  void sendPktAndVerifyLldpPacketsCpuQueue(
      int queueId,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto neighborMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket = utility::makeLLDPPacket(
          getSw(),
          neighborMac,
          vlanId,
          "rsw1dx.21.frc3",
          "eth1/1/1",
          "fsw001.p023.f01.frc3:eth4/9/1",
          LldpManager::TTL_TLV_VALUE,
          LldpManager::SYSTEM_CAPABILITY_ROUTER);
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket),
          PortID(
              masterLogicalPortIds(this->getCurrentSwitchIdForTesting())[0]));
    }
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + 1);
    XLOG(DBG0) << "Packet of dstMac=" << LldpManager::LLDP_DEST_MAC.toString()
               << ". Ethertype=" << std::hex << int(LldpManager::ETHERTYPE_LLDP)
               << ". Queue=" << queueId << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }

  void sendPktAndVerifyStpPacketsCpuQueue(
      int queueId,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto neighborMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),
        switchIdForPort(portIdsForTest()[0]),
        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket = LldpManager::createLldpPktCustomBridge(
          utility::makeAllocator(getSw()),
          neighborMac,
          vlanId,
          "rsw1dx.21.frc3",
          "eth1/1/1",
          "fsw001.p023.f01.frc3:eth4/9/1",
          LldpManager::TTL_TLV_VALUE,
          LldpManager::SYSTEM_CAPABILITY_ROUTER);
      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket),
          PortID(
              masterLogicalPortIds(this->getCurrentSwitchIdForTesting())[0]));
    }
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),
        switchIdForPort(portIdsForTest()[0]),
        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + 1);
    XLOG(DBG0) << "STP packet dstMac="
               << LldpManager::LLDP_CUSTOMER_BRIDGE_MAC.toString()
               << ". Queue=" << queueId << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }

  void
  sendDHCPv6Pkts(int numPktsToSend, DHCPv6Type type, int ttl, bool outOfPort) {
    auto intfId = firstInterfaceIDWithPortsForTesting(getProgrammedState());
    auto myIpv6 = utility::getIntfAddrsV6(getProgrammedState(), intfId)[0];
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto neighborMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);

    for (int i = 0; i < numPktsToSend; i++) {
      auto txPacket = (type == DHCPv6Type::DHCPv6_SOLICIT)
          ? utility::makeUDPTxPacket(
                getSw(),
                vlanId,
                neighborMac, // SrcMAC: Client's MAC address
                kDhcpV6McastMacAddress, // DstMac: 33:33:00:01:00:02
                kIPv6LinkLocalUcastAddress, // SrcIP: Client's Link Local addr
                kDhcpV6AllRoutersIp, // DstIP: ff02::1:2
                DHCPv6Packet::DHCP6_CLIENT_UDPPORT, // SrcPort: 546
                DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT, // DstPort: 547
                0 /* dscp */,
                ttl)
          : utility::makeUDPTxPacket( // DHCPv6Type::DHCPv6_ADVERTISE
                getSw(),
                vlanId,
                neighborMac, // srcMac: Server's MAC
                intfMac, // dstMac: Switch/our MAC
                kDhcpV6ServerGlobalUnicastAddress, // srcIp: Server's global
                                                   // unicast address
                myIpv6, // dstIp: Switch/our IP
                kRandomPort, // SrcPort:DHCPv6 server's random port
                DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT, // DstPort: 547
                0 /* dscp */,
                ttl); // sent to me
      sendPkt(std::move(txPacket), outOfPort, true /* expectRxPacket*/);
    }
  }

  void sendPktAndVerifyDHCPv6PacketsCpuQueue(
      int queueId,
      DHCPv6Type dhcpType,
      const int ttl = 1,
      bool outOfPort = true,
      const int numPktsToSend = 1,
      const int expectedPktDelta = 1) {
    auto beforeOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        0 /* retryTimes */,
        0 /* expectedNumPkts */);
    sendDHCPv6Pkts(numPktsToSend, dhcpType, ttl, outOfPort);
    auto afterOutPkts = utility::getQueueOutPacketsWithRetry(
        getSw(),

        switchIdForPort(portIdsForTest()[0]),

        queueId,
        kGetQueueOutPktsRetryTimes,
        beforeOutPkts + expectedPktDelta);
    auto msgType =
        dhcpType == DHCPv6Type::DHCPv6_SOLICIT ? "SOLICIT" : "ADVERTISEMENT";
    XLOG(DBG0) << "DHCPv6 " << msgType << " packet" << ". Queue=" << queueId
               << ", before pkts:" << beforeOutPkts
               << ", after pkts:" << afterOutPkts;
    EXPECT_EQ(expectedPktDelta, afterOutPkts - beforeOutPkts);
  }
};

TYPED_TEST_SUITE(AgentCoppTest, TestTypes);

TYPED_TEST(AgentCoppTest, LocalDstIpBgpPortToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    // Make sure all dstip(=interfaces local ips) + BGP port packets send to
    // cpu high priority queue
    enum SRC_DST { SRC, DST };
    for (const auto& ipAddress : this->getIpAddrsToSendPktsTo()) {
      for (int dir = 0; dir <= DST; dir++) {
        XLOG(DBG2) << "Send Pkt to: " << ipAddress
                   << " dir: " << (dir == DST ? " DST" : " SRC");
        this->sendTcpPktAndVerifyCpuQueue(
            utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
                this->getAgentEnsemble()->getL3Asics())),
            folly::IPAddress::createNetwork(ipAddress, -1, false).first,
            dir == SRC ? utility::kBgpPort : utility::kNonSpecialPort1,
            dir == DST ? utility::kBgpPort : utility::kNonSpecialPort1);
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, LocalDstIpNonBgpPortToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    for (const auto& ipAddress : this->getIpAddrsToSendPktsTo()) {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
          folly::IPAddress::createNetwork(ipAddress, -1, false).first,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2);

      // Also high-pri queue should always be 0
      EXPECT_EQ(
          0,
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
                  this->getAgentEnsemble()->getL3Asics())),
              kGetQueueOutPktsRetryTimes,
              0));
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ipv6LinkLocalMcastToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    const auto addresses = folly::make_array(
        kIPv6LinkLocalMcastAbsoluteAddress, kIPv6LinkLocalMcastAddress);
    for (const auto& address : addresses) {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
          address,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          kDhcpV6McastMacAddress);

      // Also high-pri queue should always be 0
      EXPECT_EQ(
          0,
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
                  this->getAgentEnsemble()->getL3Asics())),
              kGetQueueOutPktsRetryTimes,
              0));
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ipv6LinkLocalMcastTxFromCpu) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    // Intent of this test is to verify that
    // link local ipv6 address is not looped back when sent from CPU
    this->sendUdpPktAndVerify(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        folly::IPAddressV6("ff02::1"),
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        false /* expectPktTrap */);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ipv6LinkLocalUcastToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    // Device link local unicast address should use mid-pri queue
    {
      const folly::IPAddressV6 linkLocalAddr = folly::IPAddressV6(
          folly::IPAddressV6::LINK_LOCAL, getLocalMacAddress());
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
          linkLocalAddr,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2);

      // Also high-pri queue should always be 0
      EXPECT_EQ(
          0,
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
                  this->getAgentEnsemble()->getL3Asics())),
              kGetQueueOutPktsRetryTimes,
              0));
    }
    // Non device link local unicast address should also use mid-pri queue
    {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
          kIPv6LinkLocalUcastAddress,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2);

      // Also high-pri queue should always be 0
      EXPECT_EQ(
          0,
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
                  this->getAgentEnsemble()->getL3Asics())),
              kGetQueueOutPktsRetryTimes,
              0));
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, SlowProtocolsMacToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    this->sendPktAndVerifyEthPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        facebook::fboss::ETHERTYPE::ETHERTYPE_SLOW_PROTOCOLS,
        LACPDU::kSlowProtocolsDstMac());
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, DstIpNetworkControlDscpToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    for (const auto& ipAddress : this->getIpAddrsToSendPktsTo()) {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
              this->getAgentEnsemble()->getL3Asics())),
          folly::IPAddress::createNetwork(ipAddress, -1, false).first,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          std::nullopt,
          kNetworkControlDscp);
    }
    // TODO(zecheng): Packet with NC DSCP is still being trapped to CPU.
    // Will need to debug further, but enable the test to verify the positive
    // test case.

    // Non local dst ip with kNetworkControlDscp should not hit high pri queue
    // (since it won't even trap to cpu)
    // this->sendTcpPktAndVerifyCpuQueue(
    //     utility::getCoppHighPriQueueId(checkSameAndGetAsic(
    //         this->getAgentEnsemble()->getL3Asics())),
    //     folly::IPAddress("2::2"),
    //     utility::kNonSpecialPort1,
    //     utility::kNonSpecialPort2,
    //     std::nullopt,
    //     kNetworkControlDscp,
    //     std::nullopt,
    //     false /*expectQueueHit*/);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ipv6LinkLocalUcastIpNetworkControlDscpToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    // Device link local unicast address + kNetworkControlDscp should use
    // high-pri queue
    {
      XLOG(DBG2) << "send device link local packet";
      const folly::IPAddressV6 linkLocalAddr = folly::IPAddressV6(
          folly::IPAddressV6::LINK_LOCAL, utility::kLocalCpuMac());

      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
              this->getAgentEnsemble()->getL3Asics())),
          linkLocalAddr,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          std::nullopt,
          kNetworkControlDscp,
          std::nullopt /* payload */,
          true /* expectQueueHit */,
          true /* outOfPort */,
          true /* skipTtlDecrement */,
          false /* verifyPktCntInOtherQueues */);
    }
    // Non device link local unicast address + kNetworkControlDscp dscp should
    // also use high-pri queue
    {
      XLOG(DBG2) << "send non-device link local packet";
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
              this->getAgentEnsemble()->getL3Asics())),
          kIPv6LinkLocalUcastAddress,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          std::nullopt,
          kNetworkControlDscp,
          std::nullopt /* payload */,
          true /* expectQueueHit */,
          true /* outOfPort */,
          true /* skipTtlDecrement */,
          false /* verifyPktCntInOtherQueues */);
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

/*
 * Testcase to test that link local ucast packets from cpu port does not get
 * copied back to cpu. The test does the following
 * 1. copp acl to match link local ucast address and cpu srcPort is created as
 * a part of setup
 * 2. Sends a link local unicast packet through CPU PIPELINE_LOOKUP.
 * 3. Packet hits the newly created acl(and so does not get forwarded to cpu).
 * It goes out through the front port and loops back in.
 * 4. On getting received, the packet bypasses the newly created acl(since src
 * port is no longer cpu port) and hits the acl for regular link local ucast
 * packets. This gets forwarded to cpu queue 9
 */
TYPED_TEST(AgentCoppTest, CpuPortIpv6LinkLocalUcastIp) {
  auto setup = [=, this]() {
    this->setup();
    this->setupL3EcmpLoopIf();
  };

  auto verify = [=, this]() {
    std::optional<folly::MacAddress> dstMac;
    bool skipTtlDecrement;
    utility::PacketComparatorFn packetComparatorFn = std::nullopt;
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    if (!this->needL3EcmpLoop() &&
        asic->getAsicType() != cfg::AsicType::ASIC_TYPE_P200) {
      // use random mac, packets would be flooded and loopback
      dstMac = folly::MacAddress("00:00:00:00:00:01");
      skipTtlDecrement = true;
    } else {
      // use interface mac, otherwise would be dropped on dnx
      dstMac = getMacForFirstInterfaceWithPortsForTesting(
          this->getProgrammedState());
      skipTtlDecrement = false;
      utility::PacketMatchFields fields{dstMac};
      packetComparatorFn = utility::makePacketComparator(fields);
    }
    bool outOfPort = false; /* route link local packet */
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_P200) {
      outOfPort = true; /* routing link local packet is not supported */
      skipTtlDecrement =
          true; /* ttl will not decrement because packet is not routed */
    }
    auto nbrLinkLocalAddr = folly::IPAddressV6("fe80:face:b11c::1");
    this->sendTcpPktAndVerifyCpuQueue(
        utility::getCoppHighPriQueueId(asic),
        nbrLinkLocalAddr,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        dstMac,
        kNetworkControlDscp,
        std::nullopt,
        true,
        outOfPort,
        skipTtlDecrement,
        true,
        packetComparatorFn);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ipv6LinkLocalMcastNetworkControlDscpToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    const auto addresses = folly::make_array(
        kIPv6LinkLocalMcastAbsoluteAddress, kIPv6LinkLocalMcastAddress);
    for (const auto& address : addresses) {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
              this->getAgentEnsemble()->getL3Asics())),
          address,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          kDhcpV6McastMacAddress,
          kNetworkControlDscp);
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, L3MTUErrorToLowPriQ) {
  auto setup = [=, this]() {
    this->setup();
    this->setupEcmp();
  };
  auto verify = [=, this]() {
    // Make sure all packets packet with large payload (> MTU)
    // are sent to cpu low priority queue.
    // Port Max Frame size is set to 9412 and L3 MTU is set as 9000
    // Thus sending a packet sized between 9000 and 9412 to cause the trap.
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        kRandomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt,
        0, /* traffic class*/
        std::vector<uint8_t>(9200, 0xff));
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

template <typename TestType>
class AgentCoppPortMtuTest : public AgentCoppTest<TestType> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<TestType, PortID>) {
      return {ProductionFeature::COPP, ProductionFeature::PORT_MTU_ERROR_TRAP};
    } else {
      return {
          ProductionFeature::COPP,
          ProductionFeature::LAG,
          ProductionFeature::PORT_MTU_ERROR_TRAP};
    }
  }
};

TYPED_TEST_SUITE(AgentCoppPortMtuTest, TestTypes);
TYPED_TEST(AgentCoppPortMtuTest, PortMTUErrorToLowPriQ) {
  auto setup = [=, this]() {
    this->setup();
    this->setupEcmp();
  };
  auto verify = [=, this]() {
    // Make sure all packets packet with large payload (> MTU)
    // are sent to cpu low priority queue.
    // Port Max Frame size is set to 9412
    // Ethernet header size is 14 bytes Ipv6 header size is 40, TCP header
    // size is 20. Thus paload 9412 - 20 - 40 - 14 = 9338

    // send packet with payload 9338, should not be trapped
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        kRandomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt,
        0, /* traffic class*/
        std::vector<uint8_t>(9338, 0xff),
        false /* expectQueueHit */);

    // send packet with payload 9339, should be trapped
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        kRandomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt,
        0, /* traffic class*/
        std::vector<uint8_t>(9339, 0xff),
        true /* expectQueueHit */);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, ArpRequestAndReplyToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };
  auto verify = [=, this]() {
    this->sendPktAndVerifyArpPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        folly::IPAddressV4("1.1.1.5"),
        ARP_OPER::ARP_OPER_REQUEST);
    this->sendPktAndVerifyArpPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        folly::IPAddressV4("1.1.1.5"),
        ARP_OPER::ARP_OPER_REPLY);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, NdpSolicitationToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };
  auto verify = [=, this]() {
    XLOG(DBG2) << "verifying solicitation";
    this->sendPktAndVerifyNdpPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        folly::IPAddressV6("1::2"), // sender of solicitation
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, NdpSolicitNeighbor) {
  // This test case makes sure that the CPU-port multicast ACL is
  // present in the ACL table and at a higher priority than the
  // ACL entry to trap NDP solicit to high priority queue.
  // If both ACL entries are present in the ACL table and at the right order
  // we would recive 1 packet to CPU.
  // Reason: NS packet enters ASIC via CPU port, hits the CPU-port multicast
  // ACL because, prefix macthes ff02::/16 and source port is set to CPU port.
  // Hence it will be forwarded. Since it's a multicast packet, it will be
  // sent out of port 1, and the packet loops back into ASIC via port 1.
  //  Now, it hits the NDP solicit ACL entry and copied to CPU.
  // Note that it does not hit the CPU-port multicast ACL because source port
  // is set to 1 after looping back.

  // If not we would receive 2 packets to CPU.
  // Reason: When packet enters the ASIC via CPU port, it hits the
  // CPU-port multicast ACL and it's copied to CPU and another copy is sent out
  // of port 1. Now, since we have port 1 in loopback mode, packet enters back
  // into ASIC via port 1, the CPU-port multicast ACL hits again and copies it
  // to CPU again.

  // More explanation in the test plan section of - D34782575
  auto setup = [=, this]() {
    this->setup();
    this->setupL3EcmpLoopIf();
  };
  auto verify = [=, this]() {
    XLOG(DBG2) << "verifying solicitation";
    // do not snoop when L2 is not supported, e.g. J3, where NDP packets goes
    // through L3 pipeline and might change ttl and dst mac
    bool expectRxPacket = !this->needL3EcmpLoop();
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    // P200 uses port (routed) interfaces: there is no vlan to flood the
    // solicitation injected from cpu, so send it out of port instead
    bool outOfPort = asic->getAsicType() == cfg::AsicType::ASIC_TYPE_P200;
    this->sendPktAndVerifyNdpPacketsCpuQueue(
        utility::getCoppHighPriQueueId(asic),
        folly::IPAddressV6("1::1"), // sender of solicitation
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION,
        false,
        outOfPort,
        1,
        1,
        expectRxPacket);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, NdpAdvertisementToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };
  auto verify = [=, this]() {
    XLOG(DBG2) << "verifying advertisement";
    this->sendPktAndVerifyNdpPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        folly::IPAddressV6("1::2"), // sender of advertisement
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, UnresolvedRoutesToLowPriQueue) {
  auto setup = [=, this]() {
    this->setup();
    utility::EcmpSetupAnyNPorts6 ecmp6(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmp6.programRoutes(&wrapper, 1);
  };
  auto randomIP = this->getInSubnetNonSwitchIP();
  auto verify = [=, this]() {
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        randomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt);
    // unresolved route packet with network control dscp 48 should
    // stil go to low priority cpu queue
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        randomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt,
        kNetworkControlDscp);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, UnresolvedRouteNextHopToLowPriQueue) {
  static const std::vector<RoutePrefixV6> routePrefixes = {
      RoutePrefix<folly::IPAddressV6>{
          folly::IPAddressV6{"2803:6080:d038:3063::"}, 64},
      RoutePrefix<folly::IPAddressV6>{
          folly::IPAddressV6{"2803:6080:d038:3065::1"}, 128}};
  auto setup = [=, this]() {
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    FLAGS_classid_for_unresolved_routes =
        (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_CHENAB);
    this->setup();
    utility::EcmpSetupAnyNPorts6 ecmp6(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmp6.programRoutes(&wrapper, 1, routePrefixes);
  };
  // Different from UnresolvedRoutesToLowPriQueue as traffic is
  // destined to a remote route for which next hop is unresolved.
  const auto randomNonsubnetUnicastIpAddresses = {
      folly::IPAddressV6("2803:6080:d038:3063::1"),
      folly::IPAddressV6("2803:6080:d038:3065::1")};
  auto verify = [=, this]() {
    for (auto& randomNonsubnetUnicastIpAddress :
         randomNonsubnetUnicastIpAddresses) {
      this->sendTcpPktAndVerifyCpuQueue(
          utility::kCoppLowPriQueueId,
          randomNonsubnetUnicastIpAddress,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          std::nullopt,
          0 /* trafficClass */,
          std::nullopt,
          true /* expectQueueHit */);
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, JumboFramesToQueues) {
  auto setup = [=, this]() {
    this->setup();
    // Program ECMP V6 routes so that the offset IP from
    // getInSubnetNonSwitchIP() has a route to be punted to CPU
    utility::EcmpSetupAnyNPorts6 ecmp6(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        std::nullopt,
        RouterID(0),
        false /* forProdConfig */,
        {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT});
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmp6.programRoutes(&wrapper, 1);
  };

  auto verify = [=, this]() {
    std::vector<uint8_t> jumboPayload(7000, 0xff);
    for (const auto& ipAddress : this->getIpAddrsToSendPktsTo()) {
      // High pri queue
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
              this->getAgentEnsemble()->getL3Asics())),
          folly::IPAddress::createNetwork(ipAddress, -1, false).first,
          utility::kBgpPort,
          utility::kNonSpecialPort2,
          std::nullopt, /*mac*/
          0, /* traffic class*/
          jumboPayload);
      // Mid pri queue
      this->sendTcpPktAndVerifyCpuQueue(
          utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
          folly::IPAddress::createNetwork(ipAddress, -1, false).first,
          utility::kNonSpecialPort1,
          utility::kNonSpecialPort2,
          std::nullopt, /*mac*/
          0, /* traffic class*/
          jumboPayload);
    }
    this->sendTcpPktAndVerifyCpuQueue(
        utility::kCoppLowPriQueueId,
        this->getInSubnetNonSwitchIP(),
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        std::nullopt, /*mac*/
        0, /* traffic class*/
        jumboPayload);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, LldpProtocolToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    this->sendPktAndVerifyLldpPacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()));
    this->sendPktAndVerifyStpPacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, Ttl1PacketToLowPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    auto randomIP = folly::IPAddressV6("2::2");
    this->sendUdpPktAndVerify(
        utility::kCoppLowPriQueueId,
        randomIP,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2,
        true /* expectPktTrap */,
        1 /* TTL */,
        true /* send out of port */);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, DhcpPacketToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    auto intfID =
        firstInterfaceIDWithPortsForTesting(this->getProgrammedState());
    auto v4IntfAddr =
        utility::getIntfAddrsV4(this->getProgrammedState(), intfID)[0];
    auto v6IntfAddr =
        utility::getIntfAddrsV6(this->getProgrammedState(), intfID)[0];
    std::array<folly::IPAddress, 2> dstIP{v4IntfAddr, v6IntfAddr};
    std::array<std::pair<int, int>, 2> dhcpPortPairs{
        std::make_pair(67, 68), std::make_pair(546, 547)};
    for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) {
        auto l4SrcPort =
            j == 0 ? dhcpPortPairs[i].first : dhcpPortPairs[i].second;
        auto l4DstPort =
            j == 0 ? dhcpPortPairs[i].second : dhcpPortPairs[i].first;
        this->sendUdpPktAndVerify(
            utility::getCoppMidPriQueueId(
                this->getAgentEnsemble()->getL3Asics()),
            dstIP[i],
            l4SrcPort,
            l4DstPort,
            true /* expectPktTrap */,
            255 /* TTL */,
            true /* send out of port */);
        XLOG(DBG0) << "Sending packet with src port " << l4SrcPort
                   << " dst port " << l4DstPort << ", dst IP: " << dstIP[i];
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, DHCPv6SolicitToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };
  auto verify = [=, this]() {
    XLOG(DBG2) << "verifying DHCPv6 solicitation with TTL 1";
    this->sendPktAndVerifyDHCPv6PacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        DHCPv6Type::DHCPv6_SOLICIT);
    XLOG(DBG2) << "verifying DHCPv6 solicitation with TTL 128";
    this->sendPktAndVerifyDHCPv6PacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        DHCPv6Type::DHCPv6_SOLICIT,
        128);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentCoppTest, DHCPv6AdvertiseToMidPriQ) {
  auto setup = [=, this]() { this->setup(); };
  auto verify = [=, this]() {
    XLOG(DBG2) << "verifying DHCPv6 Advertise with TTL 1";
    this->sendPktAndVerifyDHCPv6PacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        DHCPv6Type::DHCPv6_ADVERTISE);
    XLOG(DBG2) << "verifying DHCPv6 Advertise with TTL 128";
    this->sendPktAndVerifyDHCPv6PacketsCpuQueue(
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        DHCPv6Type::DHCPv6_ADVERTISE,
        128);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

class AgentCoppQosTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::COPP,
        ProductionFeature::L3_QOS,
        ProductionFeature::COPP_SCHEDULER};
  }
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto switchId = this->getCurrentSwitchIdForTesting();
    auto asic = checkSameAndGetAsic(
        ensemble.getL3Asics(), static_cast<int32_t>(switchId));
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getPlatformMapping(),
        asic,
        ensemble.masterLogicalPortIds(switchId),
        ensemble.supportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        true /*interfaceHasSubnet*/,
        true /*setInterfaceMac*/,
        utility::kBaseVlanId,
        false /*enableFabricPorts*/,
        ensemble.getSw()->getSwitchInfoTable().getSwitchIdToSwitchInfo(),
        ensemble.getSw()->getHwAsicTable()->getHwAsics(),
        ensemble.getSw()->getPlatformType());
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai());
    addCustomCpuQueueConfig(cfg, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    if (isDualStage3Q2QQos()) {
      auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
      auto streamType =
          *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
      utility::addNetworkAIQueueConfig(
          &cfg, streamType, cfg::QueueScheduling::STRICT_PRIORITY, hwAsic);
    } else {
      utility::addOlympicQueueConfig(&cfg, ensemble.getL3Asics());
    }
    auto prefix = folly::CIDRNetwork{kIpForLowPriorityQueue, 128};
    utility::addTrapPacketAcl(asic, &cfg, prefix);
    return cfg;
  }

  std::vector<PortID> portIdsForTest() {
    auto switchId = this->getCurrentSwitchIdForTesting();
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds(switchId);
    }
    return masterLogicalInterfacePortIds(switchId);
  }

  void setupEcmpDataplaneLoop() {
    auto dstMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());

    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), dstMac);
    resolveNeighborAndProgramRoutes(ecmpHelper, 1, true);
    utility::disablePortTTLDecrementIfSupported(
        getAgentEnsemble(),
        ecmpHelper.getRouterId(),
        ecmpHelper.getNextHops()[0]);
  }

  std::optional<folly::IPAddress> getDestinationIpIfValid(RxPacket* pkt) {
    std::optional<folly::IPAddress> destinationAddress;
    auto pktData = pkt->buf()->clone();
    folly::io::Cursor cursor{pktData.get()};
    auto receivedPkt = utility::EthFrame{cursor};
    auto v6Packet = receivedPkt.v6PayLoad();
    if (v6Packet.has_value()) {
      auto v6Header = v6Packet->header();
      destinationAddress = v6Header.dstAddr;
    } else {
      auto v4Packet = receivedPkt.v4PayLoad();
      if (v4Packet.has_value()) {
        auto v4Header = v4Packet->header();
        destinationAddress = v4Header.dstAddr;
      }
    }

    return destinationAddress;
  }

  void sendTcpPktsOnPort(
      PortID port,
      std::optional<VlanID> vlanId,
      int numPktsToSend,
      const folly::IPAddress& dstIpAddress,
      int l4SrcPort,
      int l4DstPort,
      const std::optional<folly::MacAddress>& dstMac = std::nullopt,
      uint8_t trafficClass = 0,
      std::optional<std::vector<uint8_t>> payload = std::nullopt) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    utility::sendTcpPkts(
        getSw(),
        numPktsToSend,
        vlanId,
        dstMac ? *dstMac : intfMac,
        dstIpAddress,
        l4SrcPort,
        l4DstPort,
        port,
        trafficClass,
        payload);
  }

  void createLineRateTrafficOnPort(
      PortID port,
      std::optional<VlanID> vlanId,
      const folly::IPAddress& dstIpAddress,
      bool waitForLineRate = true) {
    // Some ASICs require extra pkts to be sent for line rate
    // when its done in conjunction with copying the pkt to cpu
    auto minPktsForLineRate =
        getAgentEnsemble()->getMinPktsForLineRate(port) * 2;
    auto dstMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());

    // Create a loop with specified destination packets.
    // We want to send atleast 2 traffic streams to ensure we dont run
    // into throughput limits with single flow and flow cache for TAJO.
    for (auto i = 0; i < minPktsForLineRate; ++i) {
      for (auto j = 0; j < 2; ++j) {
        sendTcpPktsOnPort(
            port,
            vlanId,
            1,
            dstIpAddress,
            utility::kNonSpecialPort1 + j,
            utility::kNonSpecialPort2 + j,
            dstMac);
      }
    }
    std::string vlanStr = (vlanId ? folly::to<std::string>(*vlanId) : "None");
    XLOG(DBG0) << "Sent " << minPktsForLineRate << " TCP packets on port "
               << static_cast<int>(port) << " / VLAN " << vlanStr;

    if (waitForLineRate) {
      // Wait for packet loop buildup
      getAgentEnsemble()->waitForLineRateOnPort(port);
      XLOG(DBG0) << "Created dataplane loop with packets for "
                 << dstIpAddress.str();
    }
  }

  /*
   * Send packets as fast as possible from CPU in groups of
   * packetsPerBurst every second. Once these many packets
   * are sent, sleep for rest of the time (if any) in that
   * second. This helps ensure that for a burst of packets
   * that can be sent in a second, we dont exceed the number
   * of packets sent in each second.
   */
  void sendPacketBursts(
      const PortID& port,
      std::optional<VlanID> vlanId,
      const int packetCount,
      const int packetsPerBurst,
      const folly::IPAddress& dstIpAddress,
      const int l4SrcPort,
      const int l4DstPort) {
    double waitTimeMsec = 1000;
    int packetsSent = 0;
    uint64_t hiPriWatermarkBytes{};
    uint64_t hiPriorityCoppQueueDiscardStats{};

    while (packetsSent < packetCount) {
      auto loopStartTime(std::chrono::steady_clock::now());
      auto pktsInThisBurst = (packetCount - packetsSent > packetsPerBurst)
          ? packetsPerBurst
          : packetCount - packetsSent;
      sendTcpPktsOnPort(
          port, vlanId, pktsInThisBurst, dstIpAddress, l4SrcPort, l4DstPort);
      packetsSent += pktsInThisBurst;

      std::chrono::duration<double, std::milli> msecUsed =
          std::chrono::steady_clock::now() - loopStartTime;
      if (waitTimeMsec > msecUsed.count()) {
        auto remainingTimeMsec = waitTimeMsec - msecUsed.count();
        // @lint-ignore CLANGTIDY
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<long>(remainingTimeMsec)));
      } else {
        XLOG(WARN) << "Not sleeping between bursts as time taken to send "
                   << msecUsed.count() << "msec is higher than " << waitTimeMsec
                   << "msec";
      }

      /*
       * Debug code to be removed later.
       * Keep checking the low and high priority watermarks for
       * CPU queues, printing for now to help analyze the drops
       * happening in HW!
       */
      auto switchId = switchIdForPort(port);
      auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
      auto cpuPortStats = utility::getCpuPortStats(getSw(), switchId);
      auto watermarkStats = *(cpuPortStats.portStats_());
      auto loPriWatermarkBytes_1 = utility::getCpuQueueWatermarkBytes(
          watermarkStats, utility::kCoppLowPriQueueId);
      auto hiPriWatermarkBytes_1 = utility::getCpuQueueWatermarkBytes(
          watermarkStats, utility::getCoppHighPriQueueId(asic));
      WITH_RETRIES({
        auto queueId = utility::getCoppHighPriQueueId(asic);
        EXPECT_EVENTUALLY_NE(
            cpuPortStats.queueDiscardPackets_()->find(queueId),
            cpuPortStats.queueDiscardPackets_()->end());
        if (cpuPortStats.queueDiscardPackets_()->find(queueId) !=
            cpuPortStats.queueDiscardPackets_()->end()) {
          auto hiPriorityCoppQueueDiscardStats_1 =
              cpuPortStats.queueDiscardPackets_()->find(queueId)->second;
          if (hiPriWatermarkBytes_1 != hiPriWatermarkBytes ||
              hiPriorityCoppQueueDiscardStats_1 !=
                  hiPriorityCoppQueueDiscardStats) {
            XLOG(DBG0) << "HiPri watermark: " << hiPriWatermarkBytes_1
                       << ", LoPri watermark: " << loPriWatermarkBytes_1
                       << ", HiPri Drops: "
                       << hiPriorityCoppQueueDiscardStats_1;
            hiPriWatermarkBytes = hiPriWatermarkBytes_1;
            hiPriorityCoppQueueDiscardStats = hiPriorityCoppQueueDiscardStats_1;
          }
        }
      });
    }
    std::string vlanStr = (vlanId ? folly::to<std::string>(*vlanId) : "None");
    XLOG(DBG0) << "Sent " << packetCount << " TCP packets on port "
               << static_cast<int>(port) << " / VLAN " << vlanStr
               << " in bursts of " << packetsPerBurst << " packets";
  }

  /*
   * addCustomCpuQueueConfig() is a modified version of
   * utility::addCpuQueueConfig(), differences:
   *   1) CPU low pri queue is given the same weight as mid pri queue
   *   2) No rate limiting on queues
   *   3) Optional param to specify ECN config on queue
   * The objective of the config is to make sure low priority queue
   * behaves just like the mid priority queue. Test here tries to
   * validate prioritization of CPU high priority queue traffic vs
   * other priority traffic. To ensure there is maximum traffic in
   * the lower priority queue, dataplane loop is created on one of the
   * port and the same traffic copied to CPU. As there is not enough
   * support to copy traffic from data plane to mid priority queue
   * for all platforms, copying this traffic to low priority queue
   * and removing the rate limiting on low priority CPU queue.
   */
  void addCustomCpuQueueConfig(
      cfg::SwitchConfig& config,
      std::vector<const HwAsic*> hwAsics,
      bool addEcnConfig = false,
      bool addQueueRate = false,
      bool addGlobalRateLimit = false) const {
    std::vector<cfg::PortQueue> cpuQueues;
    auto hwAsic = checkSameAndGetAsicForTesting(hwAsics);
    cfg::PortQueue queue0;
    queue0.id() = utility::kCoppLowPriQueueId;
    queue0.name() = "cpuQueue-low";
    queue0.streamType() = utility::getCpuDefaultStreamType(hwAsic);
    queue0.scheduling() = cfg::QueueScheduling::STRICT_PRIORITY;
    if (addEcnConfig) {
      queue0.aqms() = {};
      queue0.aqms()->push_back(utility::GetEcnConfig(*hwAsic));
    }
    if (addQueueRate) {
      queue0.portQueueRate() =
          utility::getPortQueueRate(hwAsic, utility::kCoppLowPriQueueId);
    }
    utility::setPortQueueMaxDynamicSharedBytes(queue0, hwAsic);
    cpuQueues.push_back(queue0);

    cfg::PortQueue queue2;
    queue2.id() = utility::getCoppMidPriQueueId({hwAsic});
    queue2.name() = "cpuQueue-mid";
    queue2.streamType() = utility::getCpuDefaultStreamType(hwAsic);
    queue2.scheduling() = cfg::QueueScheduling::STRICT_PRIORITY;
    if (addEcnConfig) {
      queue2.aqms() = {};
      queue2.aqms()->push_back(utility::GetEcnConfig(*hwAsic));
    }
    utility::setPortQueueMaxDynamicSharedBytes(queue2, hwAsic);
    cpuQueues.push_back(queue2);

    cfg::PortQueue queue9;
    queue9.id() = utility::getCoppHighPriQueueId(hwAsic);
    queue9.name() = "cpuQueue-high";
    queue9.streamType() = utility::getCpuDefaultStreamType(hwAsic);
    queue9.scheduling() = cfg::QueueScheduling::STRICT_PRIORITY;
    if (addEcnConfig) {
      queue9.aqms() = {};
      queue9.aqms()->push_back(utility::GetEcnConfig(*hwAsic));
    }
    cpuQueues.push_back(queue9);

    config.cpuQueues() = cpuQueues;
    if (hwAsic->isSupported(HwAsic::Feature::VOQ)) {
      std::vector<cfg::PortQueue> cpuVoqs;
      cfg::PortQueue voq0;
      voq0.id() = utility::kCoppLowPriQueueId;
      voq0.name() = "cpuVoq-low";
      voq0.streamType() = utility::getCpuDefaultStreamType(hwAsic);
      voq0.scheduling() = cfg::QueueScheduling::INTERNAL;
      voq0.maxDynamicSharedBytes() = 20 * 1024 * 1024;
      cpuVoqs.push_back(voq0);

      cfg::PortQueue voq1;
      voq1.id() =
          isDualStage3Q2QQos() ? 1 : utility::getCoppMidPriQueueId({hwAsic});
      voq1.name() = "cpuVoq-mid";
      voq1.streamType() = utility::getCpuDefaultStreamType(hwAsic);
      voq1.scheduling() = cfg::QueueScheduling::INTERNAL;
      voq1.maxDynamicSharedBytes() = 20 * 1024 * 1024;
      cpuVoqs.push_back(voq1);

      cfg::PortQueue voq2;
      voq2.id() = isDualStage3Q2QQos()
          ? 2
          : getLocalPortNumVoqs(cfg::PortType::CPU_PORT, cfg::Scope::LOCAL) - 1;
      voq2.name() = "cpuVoq-high";
      voq2.streamType() = utility::getCpuDefaultStreamType(hwAsic);
      voq2.scheduling() = cfg::QueueScheduling::INTERNAL;
      cpuVoqs.push_back(voq2);

      config.cpuVoqs() = cpuVoqs;
    }

    if (addGlobalRateLimit) {
      std::map<int, int> tc2RateLimit{{0, kGlobalRateLimit}};
      config.switchSettings()->tcToRateLimitKbps() = std::move(tc2RateLimit);
    }
  }
};

class AgentCoppQueueStuckTest : public AgentCoppQosTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::COPP, ProductionFeature::COPP_SHAPER};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentCoppQosTest::initialConfig(ensemble);
    addCustomCpuQueueConfig(
        cfg,
        ensemble.getSw()->getHwAsicTable()->getL3Asics(),
        true /*addEcnConfig*/,
        true /*addQueueRate*/);
    return cfg;
  }
};
TEST_F(AgentCoppQueueStuckTest, CpuQueueHighRateTraffic) {
  auto setup = [=, this]() { setupEcmpDataplaneLoop(); };

  auto verify = [&]() {
    // Create dataplane loop with lowerPriority traffic on port0
    auto baseVlan = getVlanIDForTx();
    createLineRateTrafficOnPort(
        portIdsForTest()[0], baseVlan, kIpForLowPriorityQueue);

    const double kVariance = 0.30; // i.e. + or -30%
    uint64_t kDurationInSecs = 12;
    uint64_t pktSize = EthHdr::SIZE + IPv6Hdr::size() + 256;
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    auto portQueueRate =
        utility::getPortQueueRate(asic, utility::kCoppLowPriQueueId);
    uint64_t expectedRate;
    if (portQueueRate.getType() == cfg::PortQueueRate::Type::kbitsPerSec) {
      expectedRate =
          portQueueRate.kbitsPerSec().as_const()->maximum().value() * 1024;
    } else {
      // pktsPerSec
      expectedRate = portQueueRate.pktsPerSec().as_const()->maximum().value() *
          pktSize * 8;
    }
    auto expectedRateLow = expectedRate * (1 - kVariance);
    auto expectedRateHigh = expectedRate * (1 + kVariance);
    WITH_RETRIES({
      // Read low priority copp queue counters
      uint64_t lowPriorityPacketCountBefore =
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::kCoppLowPriQueueId,
              0 /* retryTimes */,
              0 /* expectedNumPkts */);
      /* sleep override */
      sleep(kDurationInSecs);
      uint64_t lowPriorityPacketCountAfter =
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::kCoppLowPriQueueId,
              0 /* retryTimes */,
              0 /* expectedNumPkts */);

      uint64_t actualRate =
          (lowPriorityPacketCountAfter - lowPriorityPacketCountBefore) *
          pktSize * 8 / kDurationInSecs;
      XLOG(DBG0) << "Before packet count: " << lowPriorityPacketCountBefore
                 << ", After packet count: " << lowPriorityPacketCountAfter
                 << ", Actual rate in bps: " << actualRate
                 << ", Expected rate low in bps: " << expectedRateLow
                 << ", Expected rate high in bps: " << expectedRateHigh;
      EXPECT_EVENTUALLY_TRUE(
          expectedRateLow <= actualRate && actualRate <= expectedRateHigh);
    });
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

class AgentCoppGlobalRateLimitTest : public AgentCoppQosTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::COPP, ProductionFeature::GLOBAL_TC_RATE_LIMIT};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentCoppQosTest::initialConfig(ensemble);
    addCustomCpuQueueConfig(
        cfg,
        ensemble.getSw()->getHwAsicTable()->getL3Asics(),
        false /*addEcnConfig*/,
        false /*addQueueRate*/,
        true /*addGlobalRateLimit*/);
    return cfg;
  }
};

TEST_F(AgentCoppGlobalRateLimitTest, verifyLowPriorityTrafficRateLimit) {
  auto setup = [=, this]() { setupEcmpDataplaneLoop(); };

  auto verify = [&]() {
    // Create dataplane loop with lowerPriority traffic on port0
    auto baseVlan = getVlanIDForTx();
    auto switchDropStatsBefore = getAggregatedSwitchDropStats();
    auto rateLimitDropBefore = *switchDropStatsBefore.tc0RateLimitDrops();
    createLineRateTrafficOnPort(
        portIdsForTest()[0], baseVlan, kIpForLowPriorityQueue, false);

    const double kVariance = 0.50; // i.e. + or -50%
    uint64_t kDurationInSecs = 12;
    uint64_t pktSize = EthHdr::SIZE + IPv6Hdr::size() + 256;
    uint64_t expectedRate = kGlobalRateLimit * 1000; // bps
    auto expectedRateHigh = expectedRate * (1 + kVariance);
    // most packets should be dropped due to rate limit, since even one packet
    // in L3 dataplane loop could cause ~1Gbps traffic on J3
    auto expectedRateLimitDropLow =
        getAgentEnsemble()->getMinPktsForLineRate(portIdsForTest()[0]) * 0.99;
    WITH_RETRIES({
      // Read low priority copp queue counters
      uint64_t lowPriorityPacketCountBefore =
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::kCoppLowPriQueueId,
              0 /* retryTimes */,
              0 /* expectedNumPkts */);
      auto portStatsBefore = getLatestPortStats(portIdsForTest()[0]);
      auto portQueuePacketsBefore = portStatsBefore.queueOutPackets_()
                                        ->find(utility::kCoppLowPriQueueId)
                                        ->second;

      /* sleep override */
      sleep(kDurationInSecs);
      uint64_t lowPriorityPacketCountAfter =
          utility::getQueueOutPacketsWithRetry(
              this->getSw(),
              this->switchIdForPort(this->portIdsForTest()[0]),
              utility::kCoppLowPriQueueId,
              0 /* retryTimes */,
              0 /* expectedNumPkts */);
      auto portStatsAfter = getLatestPortStats(portIdsForTest()[0]);
      auto portQueuePacketsAfter = portStatsAfter.queueOutPackets_()
                                       ->find(utility::kCoppLowPriQueueId)
                                       ->second;
      auto switchDropStatsAfter = getAggregatedSwitchDropStats();
      auto rateLimitDropAfter = *switchDropStatsAfter.tc0RateLimitDrops();

      uint64_t actualCpuPortRate =
          (lowPriorityPacketCountAfter - lowPriorityPacketCountBefore) *
          pktSize * 8 / kDurationInSecs;
      uint64_t actualNifPortRate =
          (portQueuePacketsAfter - portQueuePacketsBefore) * pktSize * 8 /
          kDurationInSecs;
      XLOG(DBG0) << "Before packet count: " << lowPriorityPacketCountBefore
                 << ", After packet count: " << lowPriorityPacketCountAfter
                 << ", Actual rate in bps: " << actualCpuPortRate
                 << ", Expected rate high in bps: " << expectedRateHigh;
      XLOG(DBG0) << "Before nif port packet count: " << portQueuePacketsBefore
                 << ", After nif port packet count: " << portQueuePacketsAfter
                 << ", Actual nif port rate in bps: " << actualNifPortRate;
      XLOG(DBG0) << "Before tc0 rate limit drop: " << rateLimitDropBefore
                 << ", After tc0 rate limit drop: " << rateLimitDropAfter
                 << ", Expected rate limit low drop: "
                 << expectedRateLimitDropLow;
      // CPU port traffic could be even lower, so only verify lower than
      // expectedRateHigh
      EXPECT_EVENTUALLY_TRUE(actualCpuPortRate <= expectedRateHigh);
      EXPECT_EVENTUALLY_TRUE(actualNifPortRate <= expectedRateHigh);
      EXPECT_EVENTUALLY_TRUE(
          expectedRateLimitDropLow <= rateLimitDropAfter - rateLimitDropBefore);
    });
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentCoppQosTest, HighVsLowerPriorityCpuQueueTrafficPrioritization) {
  constexpr int kReceiveRetries = 2;
  constexpr int kHigherPriorityPacketCount = 30000;
  constexpr int packetsPerBurst = 1000;

  auto setup = [=, this]() { setupEcmpDataplaneLoop(); };

  auto verify = [&]() {
    auto configIntf = folly::copy(
        *(this->initialConfig(*getAgentEnsemble())).interfaces())[1];
    const auto ipForHigherPriorityQueue =
        folly::IPAddress::createNetwork(configIntf.ipAddresses()[1], -1, false)
            .first;
    auto baseVlan = getVlanIDForTx();

    // Create dataplane loop with lowerPriority traffic on port0
    createLineRateTrafficOnPort(
        portIdsForTest()[0], baseVlan, kIpForLowPriorityQueue);
    std::optional<VlanID> nextVlan;
    if (baseVlan) {
      nextVlan = *baseVlan + 1;
    }

    auto portId = portIdsForTest()[1];
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);

    // first verify high priority traffic vs low priority traffic
    auto highPriorityCoppQueueStatsBefore =
        utility::getQueueOutPacketsWithRetry(
            getSw(),
            this->switchIdForPort(this->portIdsForTest()[0]),
            utility::getCoppHighPriQueueId(asic),
            kReceiveRetries,
            0);

    // Send a fixed number of high priority packets on port1
    sendPacketBursts(
        portId,
        nextVlan,
        kHigherPriorityPacketCount,
        packetsPerBurst,
        ipForHigherPriorityQueue,
        utility::kNonSpecialPort1,
        utility::kBgpPort);

    // Check high priority queue stats to see if all packets are received
    auto highPriorityCoppQueueStatsAfter = utility::getQueueOutPacketsWithRetry(
        getSw(),
        this->switchIdForPort(this->portIdsForTest()[0]),
        utility::getCoppHighPriQueueId(asic),
        kReceiveRetries,
        highPriorityCoppQueueStatsBefore + kHigherPriorityPacketCount);

    EXPECT_EQ(
        kHigherPriorityPacketCount,
        highPriorityCoppQueueStatsAfter - highPriorityCoppQueueStatsBefore);

    // then verify mid priority traffic vs low priority traffic
    auto midPriorityCoppQueueStatsBefore = utility::getQueueOutPacketsWithRetry(
        getSw(),
        this->switchIdForPort(this->portIdsForTest()[0]),
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        kReceiveRetries,
        0);

    // Send a fixed number of mid priority packets on port1
    sendPacketBursts(
        portId,
        nextVlan,
        kHigherPriorityPacketCount,
        packetsPerBurst,
        ipForHigherPriorityQueue,
        utility::kNonSpecialPort1,
        utility::kNonSpecialPort2);

    // Check mid priority queue stats to see if all packets are received
    auto midPriorityCoppQueueStatsAfter = utility::getQueueOutPacketsWithRetry(
        getSw(),
        this->switchIdForPort(this->portIdsForTest()[0]),
        utility::getCoppMidPriQueueId(this->getAgentEnsemble()->getL3Asics()),
        kReceiveRetries,
        highPriorityCoppQueueStatsBefore + kHigherPriorityPacketCount);

    EXPECT_EQ(
        kHigherPriorityPacketCount,
        midPriorityCoppQueueStatsAfter - midPriorityCoppQueueStatsBefore);

    if (asic->isSupported(HwAsic::Feature::VOQ)) {
      // check watermark of low priority voq should reach max shared buffer
      // size
      const double kVariance = 0.01;
      auto watermarkBytesLow = utility::getDnxCoppMaxDynamicSharedBytes(
                                   utility::kCoppLowPriQueueId) *
          (1 - kVariance);
      auto watermarkBytesHigh = utility::getDnxCoppMaxDynamicSharedBytes(
                                    utility::kCoppLowPriQueueId) *
          (1 + kVariance);
      WITH_RETRIES({
        auto voqWaterMarkBytes = folly::copy(
            getLatestCpuSysPortStats().value().queueWatermarkBytes_().value());
        auto lowPriorityWaterMarkBytes =
            voqWaterMarkBytes.at(utility::kCoppLowPriQueueId);
        XLOG(DBG2) << "low priority cpu voq watermark counter "
                   << lowPriorityWaterMarkBytes;
        for (auto iter = voqWaterMarkBytes.begin();
             iter != voqWaterMarkBytes.end();
             iter++) {
          XLOG(DBG2) << "cpu voq " << iter->first << " has watermarker counter "
                     << iter->second;
        }
        EXPECT_EVENTUALLY_TRUE(
            lowPriorityWaterMarkBytes < watermarkBytesHigh &&
            lowPriorityWaterMarkBytes > watermarkBytesLow);
      });
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

template <typename TestType>
class AgentCoppEapolTest : public AgentCoppTest<TestType> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<TestType, PortID>) {
      return {ProductionFeature::COPP, ProductionFeature::EAPOL_TRAP};
    } else {
      return {
          ProductionFeature::COPP,
          ProductionFeature::LAG,
          ProductionFeature::EAPOL_TRAP};
    }
  }
};

TYPED_TEST_SUITE(AgentCoppEapolTest, TestTypes);

TYPED_TEST(AgentCoppEapolTest, EapolToHighPriQ) {
  auto setup = [=, this]() { this->setup(); };

  auto verify = [=, this]() {
    this->sendPktAndVerifyEthPacketsCpuQueue(
        utility::getCoppHighPriQueueId(checkSameAndGetAsicForTesting(
            this->getAgentEnsemble()->getL3Asics())),
        facebook::fboss::ETHERTYPE::ETHERTYPE_EAPOL,
        folly::MacAddress("ff:ff:ff:ff:ff:ff"));
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
