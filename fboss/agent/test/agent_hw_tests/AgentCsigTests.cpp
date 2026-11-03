// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/Synchronized.h>
#include <folly/io/Cursor.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <vector>

#include "fboss/agent/PacketObserver.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

namespace {

constexpr uint16_t kCsigTpid = 0x9900;
constexpr uint8_t kAbwcSignalType = 1;
// Probe tag: ABWC bucket 7 with the valid bit (0x10) set, link locator 0x5.
constexpr uint8_t kProbeSignal = 0x17;
constexpr uint8_t kProbeLinkLocator = 0x5;
// Link locator programmed on the egress port, stamped by UPDATE.
constexpr uint8_t kEgressLinkLocator = 0x42;
// UPDATE on an idle egress link rewrites the signal to ABWC bucket 0.
constexpr uint8_t kIdleUpdateSignal = 0x10;
constexpr uint8_t kProbeHopLimit = 64;
constexpr size_t kProbePayloadSize = 50;
constexpr size_t kCsigTagSize = 4;
// A probe loops on the MAC-loopback egress port until its hop limit expires,
// so expect at most one copy per hop.
constexpr size_t kMaxFramesToCollect = kProbeHopLimit;

const folly::IPAddressV6 kProbeSrcIp{"1::10"};

struct CsigTag {
  uint8_t signalType{0};
  uint8_t signal{0};
  uint8_t linkLocator{0};
};

// A copy of the probe looped back on the egress port.
struct ProbeCopy {
  std::optional<CsigTag> csigTag;
  folly::IPAddressV6 dstIp;
  uint8_t hopLimit{0};
  std::string hexDump;
};

// Tag layout: signalType(3) | reserved(1) | signal(5) | linkLocator(7),
// followed by the ethertype of the encapsulated packet.
uint16_t encodeCsigTag(uint8_t signalType, uint8_t signal, uint8_t locator) {
  return static_cast<uint16_t>(
      ((signalType & 0x7) << 13) | ((signal & 0x1f) << 7) | (locator & 0x7f));
}

CsigTag decodeCsigTag(uint16_t tag) {
  return CsigTag{
      .signalType = static_cast<uint8_t>((tag >> 13) & 0x7),
      .signal = static_cast<uint8_t>((tag >> 7) & 0x1f),
      .linkLocator = static_cast<uint8_t>(tag & 0x7f),
  };
}

// Parses an IPv6 frame with an optional CSIG tag. Frames punted to the CPU
// carry an 802.1Q tag ahead of the CSIG tag.
std::optional<ProbeCopy> parseProbeCopy(const folly::IOBuf* buf) {
  folly::io::Cursor cursor(buf);
  // EthHdr::SIZE includes room for one 802.1Q tag.
  if (cursor.totalLength() < EthHdr::SIZE + kCsigTagSize + IPv6Hdr::SIZE) {
    return std::nullopt;
  }
  cursor.skip(2 * folly::MacAddress::SIZE);
  auto etherType = cursor.readBE<uint16_t>();
  if (etherType == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN)) {
    cursor.skip(2);
    etherType = cursor.readBE<uint16_t>();
  }
  ProbeCopy copy;
  if (etherType == kCsigTpid) {
    copy.csigTag = decodeCsigTag(cursor.readBE<uint16_t>());
    etherType = cursor.readBE<uint16_t>();
  }
  if (etherType != static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6)) {
    return std::nullopt;
  }
  const IPv6Hdr ipHdr(cursor);
  copy.dstIp = ipHdr.dstAddr;
  copy.hopLimit = ipHdr.hopLimit;
  copy.hexDump = PktUtil::hexDump(buf);
  return copy;
}

// Collects IPv6 frames punted from one port. PacketSnooper can't be used: it
// parses every frame with EthFrame, which throws on the CSIG ethertype.
class ProbeCopyCollector : public PacketObserverIf {
 public:
  ProbeCopyCollector(SwSwitch* sw, PortID port) : sw_(sw), port_(port) {
    sw_->getPacketObservers()->registerPacketObserver(this, kName);
  }

  ~ProbeCopyCollector() override {
    sw_->getPacketObservers()->unregisterPacketObserver(this, kName);
  }

  void packetReceived(const RxPacket* pkt) noexcept override {
    if (pkt->getSrcPort() != port_) {
      return;
    }
    std::optional<ProbeCopy> copy;
    try {
      copy = parseProbeCopy(pkt->buf());
    } catch (const std::exception& ex) {
      XLOG(DBG2) << "Ignoring unparsable frame: " << ex.what();
    }
    if (!copy.has_value()) {
      return;
    }
    auto copies = copies_.wlock();
    if (copies->size() < kMaxFramesToCollect) {
      copies->push_back(std::move(*copy));
    }
  }

  // Copies the ASIC routed to dstIp (hop limit decremented). The egress tag
  // action applies only to routed packets, so bridged copies are ignored.
  std::vector<ProbeCopy> routedCopiesTo(const folly::IPAddressV6& dstIp) {
    std::vector<ProbeCopy> matching;
    for (const auto& copy : *copies_.rlock()) {
      if (copy.dstIp == dstIp && copy.hopLimit < kProbeHopLimit) {
        matching.push_back(copy);
      }
    }
    return matching;
  }

 private:
  static constexpr auto kName = "ProbeCopyCollector";
  SwSwitch* sw_;
  PortID port_;
  folly::Synchronized<std::vector<ProbeCopy>> copies_;
};

} // namespace

/*
 * A CSIG-tagged IPv6 probe is injected into the pipeline from the CPU and
 * routed to a neighbor on a MAC-loopback egress port. The neighbor MAC is the
 * router MAC, so the frame egresses (where the port's CSIG tag action
 * applies), loops back in, and an ingress ACL on the egress port copies it to
 * the CPU for inspection.
 */
class AgentCsigTest : public AgentHwTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }

  bool isCsigSupported() const {
    return isSupportedOnAllAsics(HwAsic::Feature::CSIG);
  }

  PortID egressPort() const {
    return masterLogicalInterfacePortIds()[0];
  }

  utility::EcmpSetupTargetedPorts6 makeEcmpHelper() {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getMacForFirstInterfaceWithPorts(getProgrammedState()));
  }

  folly::IPAddressV6 probeDstIp() {
    return makeEcmpHelper().ip(PortDescriptor(egressPort()));
  }

  void setupCsig(
      cfg::CsigTagAction tagAction,
      std::optional<uint8_t> linkLocator = std::nullopt) {
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, egressPort());
    portCfg->csigTagAction() = tagAction;
    if (linkLocator.has_value()) {
      portCfg->csigLinkLocator() = *linkLocator;
    }
    utility::addTrapPacketAcl(
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics()),
        &config,
        egressPort());
    applyNewConfig(config);

    auto ecmpHelper = makeEcmpHelper();
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper.resolveNextHops(in, {PortDescriptor(egressPort())});
        },
        "resolve CSIG probe neighbor");
  }

  std::unique_ptr<TxPacket> makeCsigProbe(const folly::IPAddressV6& dstIp) {
    std::vector<uint8_t> payload(kProbePayloadSize, 0);
    IPv6Hdr ipHdr(kProbeSrcIp, dstIp);
    ipHdr.nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP);
    ipHdr.payloadLength = UDPHeader::size() + payload.size();
    ipHdr.hopLimit = kProbeHopLimit;
    UDPHeader udpHdr(8000, 8001, UDPHeader::size() + payload.size());

    auto pkt = getSw()->allocatePacket(
        EthHdr::UNTAGGED_PKT_SIZE + kCsigTagSize + IPv6Hdr::SIZE +
        UDPHeader::size() + payload.size());
    folly::io::RWPrivateCursor cursor(pkt->buf());
    const auto intfMac = getMacForFirstInterfaceWithPorts(getProgrammedState());
    TxPacket::writeEthHeader(&cursor, intfMac, intfMac, kCsigTpid);
    cursor.writeBE<uint16_t>(
        encodeCsigTag(kAbwcSignalType, kProbeSignal, kProbeLinkLocator));
    cursor.writeBE<uint16_t>(static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
    ipHdr.serialize(&cursor);
    cursor.writeBE<uint16_t>(udpHdr.srcPort);
    cursor.writeBE<uint16_t>(udpHdr.dstPort);
    cursor.writeBE<uint16_t>(udpHdr.length);
    folly::io::RWPrivateCursor csumCursor(cursor);
    cursor.skip(2);
    folly::io::Cursor payloadStart(cursor);
    cursor.push(payload.data(), payload.size());
    csumCursor.writeBE<uint16_t>(udpHdr.computeChecksum(ipHdr, payloadStart));
    return pkt;
  }

  // Sends a probe and returns the routed copies that loop back on the egress
  // port.
  std::vector<ProbeCopy> sendProbeAndCollectRoutedCopies() {
    const auto dstIp = probeDstIp();
    ProbeCopyCollector collector(getSw(), egressPort());
    std::vector<ProbeCopy> copies;
    WITH_RETRIES({
      sendPacketSwitchedAsync(makeCsigProbe(dstIp));
      copies = collector.routedCopiesTo(dstIp);
      EXPECT_EVENTUALLY_FALSE(copies.empty());
    });
    EXPECT_FALSE(copies.empty()) << "No routed copy of the probe to " << dstIp
                                 << " looped back on port " << egressPort();
    return copies;
  }

  void verifyEgressCsigTag(uint8_t expectedSignal, uint8_t expectedLocator) {
    for (const auto& copy : sendProbeAndCollectRoutedCopies()) {
      ASSERT_TRUE(copy.csigTag.has_value()) << copy.hexDump;
      EXPECT_EQ(copy.csigTag->signalType, kAbwcSignalType) << copy.hexDump;
      EXPECT_EQ(copy.csigTag->signal, expectedSignal) << copy.hexDump;
      EXPECT_EQ(copy.csigTag->linkLocator, expectedLocator) << copy.hexDump;
    }
  }

  void verifyEgressCsigTagStripped() {
    for (const auto& copy : sendProbeAndCollectRoutedCopies()) {
      EXPECT_FALSE(copy.csigTag.has_value()) << copy.hexDump;
    }
  }
};

// STRIP egress removes the tag. The SDK default is PASSTHROUGH.
TEST_F(AgentCsigTest, strip) {
  if (!isCsigSupported()) {
    GTEST_SKIP() << "CSIG not supported";
  }
  auto setup = [this]() { setupCsig(cfg::CsigTagAction::STRIP); };
  auto verify = [this]() { verifyEgressCsigTagStripped(); };
  verifyAcrossWarmBoots(setup, verify);
}

// UPDATE egress rewrites the signal with the egress link's available bandwidth
// and stamps the egress port's link locator.
TEST_F(AgentCsigTest, update) {
  if (!isCsigSupported()) {
    GTEST_SKIP() << "CSIG not supported";
  }
  auto setup = [this]() {
    setupCsig(cfg::CsigTagAction::UPDATE, kEgressLinkLocator);
  };
  auto verify = [this]() {
    verifyEgressCsigTag(kIdleUpdateSignal, kEgressLinkLocator);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
