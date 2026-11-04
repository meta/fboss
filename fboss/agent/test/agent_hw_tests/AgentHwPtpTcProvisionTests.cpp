/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/Demangle.h>
#include <folly/IPAddress.h>
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PTPHeader.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/fpga/MultiPimPlatformSystemContainer.h"

namespace {} // namespace

namespace facebook::fboss {

/*
 *  Test case to validate PTP TC on all ports.
 *  1. Select two ports, one ingress and one egress.
 *  2. Add Vlans and put ports in loopback mode (MAC/PHY), config egress port as
 * next hop.
 *  3. Inject PTP packet packet out of ingress port, packet received back on the
 * same port, RX timestamp applied. The packet is now forwarded out of egress
 * port, TX timestamp applied.
 *  4. Packet is received back on egress port, now forward the same to CPU,
 * capture the packet and check for CF field and the UDP checksum in the packet.
 *  5. Run for each port pairs.
 */
class AgentHwPtpTcProvisionTests : public AgentHwTest {
 protected:
  const std::string kdstIpPrefix = "2192::101:";
  const std::string knexthopMacPrefix = "aa:bb:cc:dd:ee:";
  const folly::IPAddressV6 kSrcIp = folly::IPAddressV6("2025::1");
  const folly::MacAddress kSrcMac = folly::MacAddress("aa:bb:cc:00:00:01");

  void SetUp() override {
    try {
      AgentHwTest::SetUp();
    } catch (const PimTypeError& e) {
      // A dead PIM fails platform init before any PTP code runs. Record it so
      // the provision test runner can report it as a hardware fault.
      RecordProperty(
          "exception_type", folly::demangle(typeid(e)).toStdString());
      RecordProperty("exception_message", e.what());
      RecordProperty("exception_pim", e.getPim());
      RecordProperty(
          "exception_reason",
          e.getReason() == PimTypeError::Reason::UNREADABLE ? "UNREADABLE"
                                                            : "UNRECOGNIZED");
      throw;
    }
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::PTP_TC,
        ProductionFeature::PTP_TC_PROVISIONING_TIME_HW_VALIDATION};
  }

  // The test exercises every enabled interface port
  // (configurePortGroupsForMaxSpeed enables the full port set), so each must
  // have an L3 interface/nhop. Opt out of the default interface-port cap to
  // configure the full port set.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto l3Asics = ensemble.getSw()->getHwAsicTable()->getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    config.switchSettings()->ptpTcEnable() = true;
    utility::configurePortGroupsForMaxSpeed(
        ensemble.getPlatformMapping(),
        ensemble.getSw()->getPlatformSupportsAddRemovePort(),
        config);
    auto ports = ensemble.masterLogicalInterfacePortIds();
    // Use PHY loopback instead of MAC loopback for Broadcom platforms
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_BCM) {
      for (auto& port : *config.ports()) {
        if (port.portType() == cfg::PortType::INTERFACE_PORT) {
          port.loopbackMode() = cfg::PortLoopbackMode::PHY;
        }
      }
    }
    // Todo: We should use dst mac to avoid same packet trapped twice
    // on inject and dst ports. But Leaba 1.42.8 SDK does not support it yet.
    std::set<folly::CIDRNetwork> prefixs;
    for (int idx = 0; idx < ports.size(); idx++) {
      prefixs.emplace(getDstIp(idx), 128);
    }
    utility::addTrapPacketAcl(asic, &config, prefixs);
    return config;
  }

  int getPortIndexFromDstIp(folly::IPAddressV6 ip) {
    auto bytes = ip.toByteArray();
    return ((bytes[14] << 8) | bytes[15]) - 10;
  }

  folly::IPAddressV6 getDstIp(int portIndex) const {
    std::stringstream ss;
    auto offset = 10;
    ss << std::hex << portIndex + offset;
    return folly::IPAddressV6(kdstIpPrefix + ss.str());
  }

  int getPortIndexFromNexthopMac(folly::MacAddress mac) {
    auto lastByte = mac.bytes()[5];
    return lastByte - 10;
  }

  folly::MacAddress getNexthopMac(int portIndex) {
    std::stringstream ss;
    auto offset = 10;
    ss << std::hex << portIndex + offset;
    return folly::MacAddress(knexthopMacPrefix + ss.str());
  }

  std::string getPortName(const PortID& portId) const {
    for (const auto& portMap :
         std::as_const(*getSw()->getState()->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if (port.second->getID() == portId) {
          return port.second->getName();
        }
      }
    }
    throw FbossError("No port with ID: ", portId);
  }

  void setupEcmpTraffic(
      const PortID& portID,
      const folly::MacAddress& nexthopMac,
      const folly::IPAddressV6& dstIp) {
    utility::EcmpSetupTargetedPorts6 ecmpHelper6{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), nexthopMac};
    auto dstPort = PortDescriptor(portID);
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto newState = ecmpHelper6.resolveNextHops(in, {dstPort});
      return newState;
    });
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper6.programRoutes(
        &wrapper, {dstPort}, {RoutePrefix<folly::IPAddressV6>{dstIp, 128}});
  }

  void sendPtpPkts(
      PTPMessageType ptpType,
      const PortID& injectPortID,
      const folly::IPAddressV6& dstIp,
      const PortID& dstPort) {
    XLOGF(
        DBG2,
        "Sending PTP packet from port {} to port {}",
        getPortName(injectPortID),
        getPortName(dstPort));
    auto portDescriptor = PortDescriptor(injectPortID);
    auto intfID = getSw()->getState()->getInterfaceIDForPort(portDescriptor);
    auto dstMac = utility::getInterfaceMac(getSw()->getState(), intfID);
    // Send out PTP packet
    auto ptpPkt = makePtpPkt(getVlanIDForTx().value(), dstIp, dstMac, ptpType);
    getAgentEnsemble()->sendPacketAsync(
        std::move(ptpPkt), portDescriptor, std::nullopt);
  }

  // A transparent clock rewrites correctionField inside the PTP payload, so it
  // must also fix up the UDP checksum. Over IPv6 that checksum is mandatory, so
  // a stale or zeroed one makes the receiving host drop the packet silently
  // while every switch side counter stays clean (S701902).
  void verifyUdpChecksum(
      const UDPHeader& udpHdr,
      const IPv6Hdr& ipHdr,
      const folly::io::Cursor& ptpPayloadStart,
      const PortID& injectPortID,
      const PortID& dstPortID) {
    ASSERT_GE(ptpPayloadStart.totalLength(), udpHdr.length - UDPHeader::size())
        << "Truncated PTP packet, cannot verify UDP checksum";
    // computeChecksum() treats the checksum field as zero, so its result is
    // directly comparable with the value carried on the wire.
    EXPECT_EQ(udpHdr.csum, udpHdr.computeChecksum(ipHdr, ptpPayloadStart))
        << "Bad UDP checksum on PTP packet sent from "
        << getPortName(injectPortID) << " and received on port "
        << getPortName(dstPortID);
  }

  uint64_t verifyPtpPkts(
      PTPMessageType ptpType,
      utility::SwSwitchPacketSnooper& snooper,
      const PortID& injectPortID,
      const PortID& dstPortID,
      const std::vector<PortID>& ports) {
    uint64_t cfInNsecs = 0;
    // expect two PTP packet, one trapped from inject port and one from dst port
    for (int i = 0; i < 2; i++) {
      auto pktBufOpt = snooper.waitForPacket(5);
      EXPECT_TRUE(pktBufOpt.has_value());
      auto pktBuf = *pktBufOpt.value().get();
      folly::io::Cursor pktCursor(&pktBuf);
      EXPECT_TRUE(utility::isPtpEventPacket(pktCursor));
      PTPHeader ptpHdr(&pktCursor);

      pktCursor.reset(&pktBuf);
      EthHdr ethHdr(pktCursor);
      auto srcMac = ethHdr.getSrcMac();
      auto dstMac = ethHdr.getDstMac();
      if (srcMac == kSrcMac) {
        // Continue if the packet trapped from inject port
        continue;
      }
      IPv6Hdr ipHdr(pktCursor);
      auto dstIp = ipHdr.dstAddr;
      // Cursor now sits at the UDP header, parsing it advances to the start of
      // the PTP payload, which is where computeChecksum() expects to begin.
      UDPHeader udpHdr;
      udpHdr.parse(&pktCursor);
      // use dstMac and dstIP to find the dst port and inject port
      auto dstPortIdx = getPortIndexFromNexthopMac(dstMac);
      auto injectPortIdx = getPortIndexFromDstIp(dstIp);
      // Verify packet is received on the correct port
      EXPECT_EQ(dstPortIdx, (injectPortIdx + 1) % ports.size());
      EXPECT_EQ(ports[injectPortIdx], injectPortID);
      EXPECT_EQ(ports[dstPortIdx], dstPortID);
      // Verify PTP fields
      auto correctionField = ptpHdr.getCorrectionField();
      cfInNsecs = (correctionField >> 16) & 0x0000ffffffffffff;
      XLOG(DBG2) << "PTP packet with CorrectionField (CF) set to " << cfInNsecs
                 << " for packet sent from "
                 << getPortName(ports[injectPortIdx])
                 << " and received on port " << getPortName(ports[dstPortIdx]);
      EXPECT_EQ(ptpType, ptpHdr.getPtpType());
      EXPECT_EQ(PTPVersion::PTP_V2, ptpHdr.getPtpVersion());
      EXPECT_EQ(PTP_DELAY_REQUEST_MSG_SIZE, ptpHdr.getPtpMessageLength());
      EXPECT_GT(cfInNsecs, 0);
      verifyUdpChecksum(
          udpHdr, ipHdr, pktCursor, ports[injectPortIdx], ports[dstPortIdx]);
    }
    return cfInNsecs;
  }

  folly::MacAddress getIntfMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }

 private:
  std::unique_ptr<facebook::fboss::TxPacket> makePtpPkt(
      const VlanID& vlanID,
      const folly::IPAddressV6& dstIp,
      const folly::MacAddress& dstMac,
      PTPMessageType ptpType) {
    return utility::makePTPTxPacket(
        getSw(),
        vlanID,
        kSrcMac,
        dstMac,
        kSrcIp,
        dstIp,
        0 /* dscp */,
        5, /* Avoid ttl 1/0 punt*/
        ptpType);
  }
};

// Each port will be selected as Ingress port once and Egress port once.
TEST_F(AgentHwPtpTcProvisionTests, VerifyPtpTcDelayRequest) {
  std::vector<PortID> ports;
  std::vector<folly::MacAddress> nexthopMacs; // binding to ingress port
  std::vector<folly::IPAddressV6> dstIps; // binding to egress port

  auto cfg = getAgentEnsemble()->getCurrentConfig();
  // Test all enabled ports
  for (auto& port : *cfg.ports()) {
    if (port.state() == cfg::PortState::ENABLED &&
        port.portType() == cfg::PortType::INTERFACE_PORT) {
      ports.emplace_back(*port.logicalID());
    }
  }
  if (ports.size() < 2) {
    XLOG(WARNING) << "Only " << ports.size()
                  << " ports enabled, need at least 2 for PTP TC test,"
                  << " skipping";
    return;
  }
  for (int idx = 0; idx < ports.size(); idx++) {
    nexthopMacs.emplace_back(getNexthopMac(idx));
    dstIps.emplace_back(getDstIp(idx));
  }

  XLOG(DBG0) << "Test " << ports.size() << " Ports for PTP TC test:";
  for (auto& port : *cfg.ports()) {
    if (port.state() == cfg::PortState::ENABLED &&
        port.portType() == cfg::PortType::INTERFACE_PORT) {
      XLOG(DBG0) << "  " << *port.name() << " (port " << *port.logicalID()
                 << ") at " << static_cast<int>(*port.speed()) << " kbps";
    }
  }

  auto setup = [=, this]() {
    for (int idx = 0; idx < ports.size(); idx++) {
      auto dstIdx = (idx + 1) % ports.size();
      // For inject port (idx) IP, routed to dest port (dstIdx)
      setupEcmpTraffic(ports[dstIdx], nexthopMacs[dstIdx], dstIps[idx]);
    }
  };

  // Send packet using dstIps[ingressPortIdx]
  // The Eth header will be modified later to use nexthopMacs[egressPortIdx]
  // as dstMac once trapped from egress port
  auto verify = [=, this]() {
    utility::SwSwitchPacketSnooper snooper(getSw(), "snooper-ptp");

    constexpr int kMaxRetries = 3;
    constexpr uint64_t kCfThresholdNsecs = 8000;
    constexpr uint64_t kCfMaxValidNsecs =
        10'000'000; // 10ms, to avoid outlier due to pipeline delay
    for (int idx = 0; idx < ports.size(); idx++) {
      auto dstIdx = (idx + 1) % ports.size();
      uint64_t cfInNsecs = 0;
      for (int attempt = 0; attempt < kMaxRetries; attempt++) {
        sendPtpPkts(
            PTPMessageType::PTP_DELAY_REQUEST,
            ports[idx],
            dstIps[idx],
            ports[dstIdx]);

        cfInNsecs = verifyPtpPkts(
            PTPMessageType::PTP_DELAY_REQUEST,
            snooper,
            ports[idx],
            ports[dstIdx],
            ports);
        if (cfInNsecs < kCfThresholdNsecs) {
          break;
        }
        // CF is IEEE 1588 signed field stored as uint64_t. A negative CF
        // (HW timestamping defect, see S634677) wraps to >= 2^47, well above
        // 10ms, so this check catches both outliers and HW defects.
        if (cfInNsecs >= kCfMaxValidNsecs) {
          XLOG(ERR) << "CF value " << cfInNsecs
                    << " exceeds maximum valid range on attempt " << attempt + 1
                    << " for " << getPortName(ports[idx]) << " -> "
                    << getPortName(ports[dstIdx]);
          break;
        }
        XLOG(WARNING) << "CF value " << cfInNsecs
                      << " exceeded threshold on attempt " << attempt + 1
                      << " for " << getPortName(ports[idx]) << " -> "
                      << getPortName(ports[dstIdx]);
      }
      EXPECT_LT(cfInNsecs, kCfThresholdNsecs);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
