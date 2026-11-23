// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/PlatformPort.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PTPHeader.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"

using namespace facebook::fboss;

constexpr int kStartTtl = 3;
const folly::IPAddressV6 kIPv6Dst = folly::IPAddressV6("2::1"); // arbit
const auto kSrcMac = folly::MacAddress{"00:00:00:00:01:03"}; // arbit

class AgentEnsemblePtpTests : public AgentEnsembleLinkTest {
 private:
  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L2_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::PTP};
  }

 public:
  std::unique_ptr<facebook::fboss::TxPacket> createPtpPkt(
      PTPMessageType ptpType) {
    // note: we are not creating flood here, but want routing
    // of packets so that TTL goes down from 255 -> 0
    auto vlan = getAgentEnsemble()->getVlanIDForTx();
    // TODO: Remove the dependency on VLAN below
    if (!vlan) {
      throw FbossError("VLAN id unavailable for test");
    }
    auto vlanId = *vlan;
    auto scope = getSw()->getScopeResolver()->scope(
        getSw()->getState()->getVlans()->getNode(vlanId));
    const auto dstMac = getSw()->getLocalMac(scope.switchId());
    auto intf =
        getSw()->getState()->getInterfaces()->getInterfaceInVlan(vlanId);

    auto srcIp = folly::IPAddressV6("1::1"); // arbit
    return utility::makePTPTxPacket(
        getSw(),
        vlanId,
        kSrcMac,
        dstMac,
        srcIp,
        kIPv6Dst,
        0 /* dscp */,
        kStartTtl,
        ptpType);
  }

  bool sendAndVerifyPtpPkts(
      PTPMessageType ptpType,
      const PortDescriptor& portDescriptor) {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "snooper-1",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        facebook::fboss::utility::packetSnooperReceivePacketType::
            PACKET_TYPE_PTP);
    snooper.ignoreUnclaimedRxPkts();
    XLOG(DBG2) << "Validating PTP packet fields on Port "
               << portDescriptor.phyPortID();
    auto matcher =
        getSw()->getScopeResolver()->scope(getSw()->getState(), portDescriptor);
    auto localMac = getSw()->getLocalMac(matcher.switchId());
    // Send out PTP packet
    auto ptpPkt = createPtpPkt(ptpType);
    getAgentEnsemble()->ensureSendPacketOutOfPort(
        std::move(ptpPkt), portDescriptor.phyPortID());

    // Hop total should be equal to (kStartTtl + 1) * kStartTtl / 2
    // In edge cases packets can come out-of-order. Wait for hopTotal to reach
    // the expected total number of hops before moving to next port.
    int hopTotal = 0;
    while (hopTotal != (kStartTtl + 1) * kStartTtl / 2) {
      auto pktBufOpt = snooper.waitForPacket(10 /* seconds */);
      if (!pktBufOpt.has_value()) {
        return true;
      }

      auto pktBuf = *pktBufOpt.value().get();
      folly::io::Cursor pktCursor(&pktBuf);
      if (!utility::isPtpEventPacket(pktCursor)) {
        // if we continue to encounter non ptp packets
        // and hit threshold of kMaxPktLimit, abort the test
        // as something is wrong
        continue;
      }

      XLOG(DBG2) << "PTP event packet found";
      PTPHeader ptpHdr(&pktCursor);
      auto correctionField = ptpHdr.getCorrectionField();
      // Verify PTP fields unchanged.
      EXPECT_EQ(ptpType, ptpHdr.getPtpType());
      EXPECT_EQ(PTPVersion::PTP_V2, ptpHdr.getPtpVersion());
      EXPECT_EQ(PTP_DELAY_REQUEST_MSG_SIZE, ptpHdr.getPtpMessageLength());

      pktCursor.reset(&pktBuf);
      auto hopLimit = utility::getIpHopLimit(pktCursor);

      // On leaba devices, the trap we installed precedes drops for TTL=0.
      // Therefore, we would still receive PTP packets here.
      if (hopLimit == 0) {
        XLOG(DBG2) << "Skipping checks on loopped back packets where TTL=0";
        continue;
      }
      hopTotal += hopLimit;

      // nano secs is first 48-bits, last 16 bits is subnano secs (remove it)
      uint64_t cfInNsecs = (correctionField >> 16) & 0x0000ffffffffffff;
      XLOG(DBG2) << "PTP packet found on port " << portDescriptor.phyPortID()
                 << " with hop limit : " << hopLimit
                 << " and CorrectionField (CF) set to " << std::hex
                 << cfInNsecs;

      pktCursor.reset(&pktBuf);
      EthHdr ethHdr(pktCursor);
      auto srcMac = ethHdr.getSrcMac();
      auto dstMac = ethHdr.getDstMac();

      auto asic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      if (hopLimit == kStartTtl) {
        if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
          // On chenab, the first packet is also timestamped,
          // CPU tx pipeline (with pipeline bypass) has correction field update
          EXPECT_GT(correctionField, 0);
          EXPECT_LT(cfInNsecs, 2000 * (kStartTtl - hopLimit + 1));
        } else {
          // this is the original pkt, and has no timestamp on it
          EXPECT_EQ(correctionField, 0);
        }
        // Original packet should have the same src and dst mac as we sent out
        EXPECT_EQ(srcMac, kSrcMac);
        EXPECT_EQ(dstMac, localMac);
      } else {
        EXPECT_GT(correctionField, 0);
        // CF for first pkt is ~800nsecs for BCM and ~1.7 msecs for Tajo
        // Also account for loopback multiple times
        EXPECT_LT(cfInNsecs, 2000 * (kStartTtl - hopLimit + 1));

        // Both src and mac address should be local mac
        EXPECT_EQ(srcMac, localMac);
        EXPECT_EQ(dstMac, localMac);
      }
    }
    return false;
  }

  void verifyPtpTcOnPorts(
      boost::container::flat_set<PortDescriptor>& ecmpPorts,
      PTPMessageType ptpType) {
    if (!ecmpPorts.empty()) {
      const auto& portDescriptor = *ecmpPorts.begin();
      // There are cases where a burst of other packets could occupy the CPU
      // queue (e.g. NDP etc.) and force PTP packets to be dropped. In this
      // case, retry 3 times on the same port.
      int retries = 3;
      bool retryable = true;
      while (retries > 0 && retryable) {
        retryable = sendAndVerifyPtpPkts(ptpType, portDescriptor);
        retries--;
      }
      EXPECT_FALSE(retryable)
          << "Failed to capture PTP packets on port "
          << portDescriptor.phyPortID() << " after multiple retries";
    }
  }

  void setPtpTcEnable(bool enable) {
    std::string updateMsg = enable ? "PTP enable" : "PTP disable";
    getSw()->updateStateBlocking(updateMsg, [=](auto state) {
      auto newState = state->clone();
      auto switchSettings =
          utility::getFirstNodeIf(newState->getSwitchSettings())
              ->modify(&newState);
      switchSettings->setPtpTcEnable(enable);
      return newState;
    });
  }

  void trapPackets(const folly::CIDRNetwork& prefix) {
    cfg::SwitchConfig cfg = getSw()->getConfig();
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    utility::addTrapPacketAcl(asic, &cfg, prefix);
    getSw()->applyConfig("trapPackets", cfg);
  }

 protected:
  void setCmdLineFlagOverrides() const override {
    // disable other processes which generate pkts
    FLAGS_enable_lldp = false;
    // tunnel interface enable especially on fboss2000 results
    FLAGS_tun_intf = false;
    // disable neighbor updates
    FLAGS_disable_neighbor_updates = true;
    AgentEnsembleLinkTest::setCmdLineFlagOverrides();
  }
};

// Purpose of this test is to validate Transparent clock (TC) for PTP
// TC embeds timestamps in CF field for PTP event messages such as
// Delay request, sync etc.
// This test enables PTP, send PTP delay request pkt from CPU and externally
// loopback to another port on the switch. (port B -> port A -> port B -> port A
// ..) But PTP doesn't generate timestamps on first hop, its because we
// originated pkt from CPU and  egress port can't detect meta data which it
// needs to timestamp this pkt.  On ingress, timestamp is updated in meta
// header. So subsequent loop of same pkt through the fwd pipeline generates
// timestamp on egress. All these pkts are punted to CPU when they ingress again
// on port A. So we send 1 pkt with TTL decrementing from 255 all the way to 0
// We capture these pkts coming to CPU.
// if (pkt == ptp {
//    if (pkt_ttl == 255) EXPECT_EQ(CF_field, 0)
//    else EXPECT_GT(CF_field, 0)
// }
TEST_F(AgentEnsemblePtpTests, verifyPtpTcDelayRequest) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::PTP});
  auto ecmpPorts = getSingleVlanOrRoutedCabledPorts();
  std::vector<PortID> portVec;
  for (const auto& portDescriptor : ecmpPorts) {
    portVec.push_back(portDescriptor.phyPortID());
  }
  addTestedPorts(portVec);
  // create ACL to trap any packets to CPU coming with given dst IP
  // Ideally we should have used the l4port (PTP_UDP_EVENT_PORT), but
  // SAI doesn't support this qualifier yet
  folly::CIDRNetwork dstPrefix = folly::CIDRNetwork{kIPv6Dst, 128};
  this->trapPackets(dstPrefix);
  programDefaultRoute(ecmpPorts, getSw()->getLocalMac(scope(ecmpPorts)));

  setPtpTcEnable(true);

  verifyPtpTcOnPorts(ecmpPorts, PTPMessageType::PTP_DELAY_REQUEST);
}

TEST_F(AgentEnsemblePtpTests, verifyPtpTcAfterLinkFlap) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::PTP});
  folly::CIDRNetwork dstPrefix = folly::CIDRNetwork{kIPv6Dst, 128};
  this->trapPackets(dstPrefix);
  auto ecmpPorts = getSingleVlanOrRoutedCabledPorts();
  std::vector<PortID> portVec;
  boost::container::flat_set<PortDescriptor> portDescriptorSet;
  // For the sake of time, use the first 5 ports.
  const auto kNumPort = 5;
  for (const auto& portDescriptor : ecmpPorts) {
    portVec.push_back(portDescriptor.phyPortID());
    portDescriptorSet.insert(portDescriptor);
    if (portVec.size() >= kNumPort) {
      break;
    }
  }
  addTestedPorts(portVec);
  programDefaultRoute(ecmpPorts, getSw()->getLocalMac(scope(ecmpPorts)));

  // 1. Disable PTP
  XLOG(DBG2) << "Disabling PTP";
  setPtpTcEnable(false);

  // 2. Flap links
  XLOG(DBG2) << "Flapping ports the first time";
  setLinkState(false, portVec);
  setLinkState(true, portVec);

  // 3. Enable PTP and ensure PTP TC is working properly. For now verify PTP on
  // the first kNumPort ports.
  XLOG(DBG2) << "Enabling PTP and verify PTP TC";
  setPtpTcEnable(true);
  verifyPtpTcOnPorts(portDescriptorSet, PTPMessageType::PTP_DELAY_REQUEST);

  // 4. Flap links
  XLOG(INFO) << "Flapping ports the second time";
  setLinkState(false, portVec);
  setLinkState(true, portVec);

  // 5. Ensure PTP TC still works as expected.
  XLOG(INFO) << "Ensure PTP TC still works as expected";
  verifyPtpTcOnPorts(portDescriptorSet, PTPMessageType::PTP_DELAY_REQUEST);
}

// Validate PTP TC when PTP is enabled while port is down.
TEST_F(AgentEnsemblePtpTests, enablePtpPortDown) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::PTP});
  folly::CIDRNetwork dstPrefix = folly::CIDRNetwork{kIPv6Dst, 128};
  this->trapPackets(dstPrefix);
  auto ecmpPorts = getSingleVlanOrRoutedCabledPorts();
  std::vector<PortID> portVec;
  boost::container::flat_set<PortDescriptor> portDescriptorSet;
  // For the sake of time, use the first 5 ports.
  const auto kNumPort = 5;
  for (const auto& portDescriptor : ecmpPorts) {
    portVec.push_back(portDescriptor.phyPortID());
    portDescriptorSet.insert(portDescriptor);
    if (portVec.size() >= kNumPort) {
      break;
    }
  }
  addTestedPorts(portVec);
  programDefaultRoute(ecmpPorts, getSw()->getLocalMac(scope(ecmpPorts)));

  // 1. Disable PTP
  XLOG(DBG2) << "Disabling PTP";
  setPtpTcEnable(false);

  // 2. Bring ports down
  XLOG(DBG2) << "Bringing port down";
  setLinkState(false, portVec);

  // 3. Enable PTP
  XLOG(DBG2) << "Enabling PTP";
  setPtpTcEnable(true);

  // 4. Bring ports up
  XLOG(DBG2) << "Bringing port up";
  setLinkState(true, portVec);

  // 5. Ensure PTP TC works.
  XLOG(INFO) << "Ensure PTP TC still works after port up";
  verifyPtpTcOnPorts(portDescriptorSet, PTPMessageType::PTP_DELAY_REQUEST);
}
