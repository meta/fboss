// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <fmt/format.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/io/IOBuf.h>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/SflowShimUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/SflowStructs.h"
#include "fboss/agent/packet/UDPDatagram.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/MultiPortTrafficTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"
#include "fboss/lib/CommonUtils.h"

DEFINE_int32(sflow_test_rate, 90000, "sflow sampling rate for hw test");
constexpr uint16_t kTimeoutSecs = 1;
constexpr uint32_t kUdpSrcPort{6545};
constexpr uint32_t kUdpDstPort{6343};

const std::string kSflowMirror = "sflow_mirror";

namespace facebook::fboss {

namespace {

// Struct to hold all headers and sFlow datagram
struct SflowPacketParsed {
  EthHdr ethHeader;
  std::variant<IPv4Hdr, IPv6Hdr> ipHeader;
  UDPHeader udpHeader;
  sflow::SampleDatagram sflowDatagram;
};

// Function to deserialize an IOBuf containing an sFlow packet
SflowPacketParsed deserializeSflowPacket(const folly::IOBuf* buf) {
  SflowPacketParsed parsed;
  folly::io::Cursor cursor(buf);

  // Parse ethernet header
  parsed.ethHeader = EthHdr(cursor);

  // Parse IP header based on ethertype
  if (parsed.ethHeader.getEtherType() ==
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4)) {
    parsed.ipHeader = IPv4Hdr(cursor);
  } else if (
      parsed.ethHeader.getEtherType() ==
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6)) {
    parsed.ipHeader = IPv6Hdr(cursor);
  } else {
    throw std::runtime_error("Unsupported ethertype");
  }

  // Parse UDP header
  parsed.udpHeader.parse(&cursor);

  // Parse sFlow datagram
  parsed.sflowDatagram = sflow::SampleDatagram::deserialize(cursor);

  return parsed;
}

// Function to create SflowPacketParsed directly from packet parameters
SflowPacketParsed makeSflowV5PacketParsed(
    std::optional<VlanID> vlan,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    uint16_t srcPort,
    uint16_t dstPort,
    uint8_t trafficClass,
    uint8_t hopLimit,
    uint32_t ingressInterface,
    uint32_t egressInterface,
    uint32_t samplingRate,
    std::span<const std::vector<uint8_t>> payloads) {
  SflowPacketParsed parsed;

  // Create ethernet header
  EthHdr::VlanTags_t vlanTags;
  if (vlan.has_value()) {
    vlanTags.emplace_back(
        vlan.value(), static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN));
  }
  parsed.ethHeader = EthHdr(
      dstMac,
      srcMac,
      std::move(vlanTags),
      static_cast<uint16_t>(
          srcIp.isV4() ? ETHERTYPE::ETHERTYPE_IPV4
                       : ETHERTYPE::ETHERTYPE_IPV6));

  // Create IP header based on address type
  if (srcIp.isV4()) {
    IPv4Hdr ipv4Header(
        srcIp.asV4(),
        dstIp.asV4(),
        static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP),
        0);
    ipv4Header.ttl = hopLimit;
    ipv4Header.dscp = trafficClass;
    parsed.ipHeader = std::move(ipv4Header);
  } else {
    IPv6Hdr ipv6Header;
    ipv6Header.srcAddr = srcIp.asV6();
    ipv6Header.dstAddr = dstIp.asV6();
    ipv6Header.nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP);
    ipv6Header.hopLimit = hopLimit;
    ipv6Header.trafficClass = trafficClass;
    parsed.ipHeader = std::move(ipv6Header);
  }

  // Create UDP header
  parsed.udpHeader = UDPHeader(srcPort, dstPort, 0, 0);

  // Create sFlow datagram structure
  sflow::SampleDatagramV5 datagramV5;
  datagramV5.agentAddress = srcIp;
  datagramV5.subAgentID = 0;
  datagramV5.sequenceNumber = 0;
  datagramV5.uptime = 0;

  for (int sampleIndex = 0; sampleIndex < payloads.size(); sampleIndex++) {
    // Create sample header
    sflow::SampledHeader sampleHeader;
    sampleHeader.protocol = sflow::HeaderProtocol::ETHERNET_ISO88023;
    sampleHeader.frameLength = 0;
    sampleHeader.stripped = 0;
    sampleHeader.header = payloads[sampleIndex];

    // Create flow record
    sflow::FlowRecord flowRecord;
    flowRecord.flowFormat = 1;
    flowRecord.flowData = sampleHeader;

    // Create flow sample
    sflow::FlowSample flowSample;
    // [sayeedt-notes] Set the sequence number for the different flow samples
    // for each sample record
    flowSample.sequenceNumber = sampleIndex;
    flowSample.sourceID = 0;
    flowSample.samplingRate = samplingRate;
    flowSample.samplePool = 0;
    flowSample.drops = 0;
    flowSample.input = ingressInterface;
    flowSample.output = egressInterface;
    flowSample.flowRecords = {std::move(flowRecord)};

    // Create sample record
    sflow::SampleRecord sampleRecord;
    sampleRecord.sampleType = 1;
    sampleRecord.sampleData = {std::move(flowSample)};

    datagramV5.samples.push_back(std::move(sampleRecord));
  }

  // Wrap in the outer datagram structure
  sflow::SampleDatagram datagram;
  datagram.datagramV5 = std::move(datagramV5);

  parsed.sflowDatagram = std::move(datagram);

  return parsed;
}

} // namespace

template <typename AddrT>
class AgentSflowMirrorTest : public AgentHwTest {
 public:
  void applyPlatformConfigOverrides(
      const cfg::SwitchConfig& sw,
      cfg::PlatformConfig& config) const override {
    // On TH5, force SDK to initialize multicast queues without an alpha
    // setting, in order to test our multicast queue buffer config logic.
    if (checkSameAndGetAsicType(sw) == cfg::AsicType::ASIC_TYPE_TOMAHAWK5) {
      utility::modifyPlatformConfig(
          config,
          [](std::string& yamlCfg) {
            std::string toReplace("LOSSY");
            std::size_t pos = yamlCfg.find(toReplace);
            if (pos != std::string::npos) {
              yamlCfg.replace(
                  pos,
                  toReplace.length(),
                  "LOSSY_AND_LOSSLESS\n      SKIP_BUFFER_RESERVATION: 1");
            }
            yamlCfg += R"(
---
device:
  0:
    TM_THD_MC_Q:
      ?
        PORT_ID: 76
        TM_MC_Q_ID: [[0,3]]
      :
        DYNAMIC_SHARED_LIMITS: 0
...
)";
          },
          [](std::map<std::string, std::string>&) {});
    }
  }

  // Index in the sample ports where data traffic is expected!
  const int kDataTrafficPortIndex{0};
  inline static const folly::MacAddress kMirrorDstMac{"06:00:00:00:00:01"};

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // Multi-switch Gibraltar needs system/recycle ports in config
    return std::nullopt;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return {ProductionFeature::SFLOWv4_SAMPLING};
    } else {
      return {ProductionFeature::SFLOWv6_SAMPLING};
    }
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);

    std::vector<PortID> allPorts;
    if (FLAGS_hyper_port) {
      allPorts = ensemble.masterLogicalHyperPortIds();
    } else {
      allPorts = ensemble.masterLogicalInterfacePortIds();
    }
    auto port0 = allPorts[0];
    auto port0Switch =
        ensemble.getSw()->getScopeResolver()->scope(port0).switchId();
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(port0Switch);
    auto ports = getPortsForSampling(allPorts, asic);
    if (asic->isSupported(HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW)) {
      utility::addEventorVoqConfig(&cfg, cfg::StreamType::UNICAST);
      for (auto& port : *cfg.ports()) {
        if (port.portType() == cfg::PortType::EVENTOR_PORT) {
          port.maxFrameSize() = 1600; // Guard against large packets
        }
      }
    }
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), cfg);
    return cfg;
  }

  virtual bool enableTruncation() const = 0;

  void configureMirror(
      cfg::SwitchConfig& cfg,
      std::optional<bool> isV4Addr = std::nullopt,
      uint32_t udpSrcPort = kUdpSrcPort,
      uint32_t udpDstPort = kUdpDstPort,
      std::optional<int> sampleRate = std::nullopt) const {
    bool v4 = isV4Addr.has_value() ? *isV4Addr
                                   : std::is_same_v<AddrT, folly::IPAddressV4>;
    utility::configureSflowMirror(
        cfg,
        kSflowMirror,
        enableTruncation(),
        utility::getSflowMirrorDestination(v4).str(),
        udpSrcPort,
        udpDstPort,
        v4,
        sampleRate);
  }

  void configSampling(cfg::SwitchConfig& config, int sampleRate) const {
    auto ports = getPortsForSampling();
    utility::configureSflowSampling(config, kSflowMirror, ports, sampleRate);
  }

  void configureMirrorWithSampling(
      cfg::SwitchConfig& cfg,
      int sampleRate,
      std::optional<bool> v4 = std::nullopt,
      uint32_t udpSrcPort = kUdpSrcPort,
      uint32_t udpDstPort = kUdpDstPort) const {
    std::optional<int> sampleRateCfg;
    auto asic = checkSameAndGetAsic();
    if (asic->isSupported(HwAsic::Feature::SAMPLE_RATE_CONFIG_PER_MIRROR)) {
      // Handle sampleRate config needed on mirror
      sampleRateCfg = sampleRate;
    }
    configureMirror(cfg, v4, udpSrcPort, udpDstPort, sampleRateCfg);
    configSampling(cfg, sampleRate);
  }

  void configureTrapAcl(
      cfg::SwitchConfig& cfg,
      bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>) const {
    auto asic = checkSameAndGetAsic();
    return asic->isSupported(HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER)
        ? utility::configureTrapAcl(
              asic, cfg, getNonSflowSampledInterfacePort())
        : utility::configureTrapAcl(asic, cfg, isV4);
  }

  PortID getNonSflowSampledInterfacePort() const {
    return checkSameAndGetAsic()->isSupported(HwAsic::Feature::MANAGEMENT_PORT)
        ? masterLogicalPortIds({cfg::PortType::MANAGEMENT_PORT})[0]
        : masterLogicalInterfacePortIds()[0];
  }

  cfg::PortType getNonSflowSampledInterfacePortType() const {
    return checkSameAndGetAsic()->isSupported(HwAsic::Feature::MANAGEMENT_PORT)
        ? cfg::PortType::MANAGEMENT_PORT
        : cfg::PortType::INTERFACE_PORT;
  }

  std::vector<PortID> getAllPorts() const {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds();
    }
    return masterLogicalInterfacePortIds();
  }

  std::vector<PortID> getPortsForSampling() const {
    std::vector<PortID> allPorts = getAllPorts();
    auto nonSampledPort = getNonSflowSampledInterfacePort();
    // Ports with sampling enabled are all interface ports except any
    // on which we are sending the sampled packets out!
    std::vector<PortID> sampledPorts;
    std::copy_if(
        allPorts.begin(),
        allPorts.end(),
        std::back_inserter(sampledPorts),
        [nonSampledPort](const auto& portId) {
          return portId != nonSampledPort;
        });
    auto switchID = switchIdForPort(allPorts[0]);
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchID);
    return getPortsForSampling(sampledPorts, asic);
  }

  std::vector<PortID> getPortsForSampling(
      const std::vector<PortID>& ports,
      const HwAsic* asic) const {
    /*
     * Tajo does not share sample packet session yet and creates
     * one per port and the maximum is 16. Configure 16 ports
     * for now on Tajo to enable sflow with mirroring.
     */
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_P200) {
      return std::vector<PortID>(ports.begin(), ports.begin() + 16);
    }
    return ports;
  }

  const HwAsic* checkSameAndGetAsic() const {
    return checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
  }

  std::optional<uint32_t> getHwLogicalPortId(PortID port) const {
    auto asic = checkSameAndGetAsic();
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_P200 ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_YUBA ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_G202X) {
      return std::nullopt;
    }
    return getSw()->getHwLogicalPortId(port);
  }

  void verifySflowExporterIp(const std::vector<uint8_t>& sflowPayload) {
    auto asic = checkSameAndGetAsic();
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      uint8_t exporterIpBytes[16];
      for (int i = 0; i < 16; i++) {
        exporterIpBytes[i] = sflowPayload[i + 8];
      }
      auto exporterIp =
          folly::IPAddressV6::fromBinary(folly::ByteRange(exporterIpBytes, 16));
      EXPECT_EQ(exporterIp, utility::getSflowMirrorSource(false /* isV4 */));
    }
  }
  void validateSflowPacketHeader(
      const std::vector<uint8_t>& sflowPayload,
      PortID srcPortId) {
    PortID expectedSrcPortId;
    /*
     * sflow shim format for Tajo:
     *
     * Source system port GID - 16 bits
     * Destination system port GID - 16 bits
     * Source logical port GID - 20 bits
     * Destination logical port GID - 20 bits
     *
     * sflow shim format for BCM:
     *
     * version field (if applicable): 32, sport : 8, smod : 8, dport : 8,
     * dmod : 8, source_sample : 1, dest_sample : 1, flex_sample : 1
     * multicast : 1, discarded : 1, truncated : 1,
     * dest_port_encoding : 3, reserved : 23
     */
    auto asic = checkSameAndGetAsic();
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_P200 ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_YUBA ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_G202X) {
      auto systemPortId = sflowPayload[0] << 8 | sflowPayload[1];
      expectedSrcPortId =
          static_cast<PortID>(systemPortId - asic->getSflowPortIDOffset());
    } else if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      /*
       * Bytes 68 through 71 carry the ingress ifindex in sflow v5 header
       * which is a 32 bit field
       */
      auto constexpr offsetBytesStart = 68;
      auto systemPortId = 0;
      for (int i = 0, j = 24; i < 4; i++, j -= 8) {
        systemPortId |=
            static_cast<uint32_t>((sflowPayload[offsetBytesStart + i]) << j);
      }
      expectedSrcPortId =
          getPortID(SystemPortID(systemPortId), getProgrammedState());
      verifySflowExporterIp(sflowPayload);
    } else {
      auto sourcePortOffset = 0;
      if (asic->isSupported(HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
        sourcePortOffset += 4;
      }
      auto sport = sflowPayload[sourcePortOffset];
      auto smod = sflowPayload[sourcePortOffset + 1];
      expectedSrcPortId =
          static_cast<PortID>((static_cast<uint32_t>(smod) << 8) | sport);
    }
    EXPECT_EQ(expectedSrcPortId, srcPortId);
  }

  uint16_t getMirrorTruncateSize() const {
    return checkSameAndGetAsic()->getMirrorTruncateSize();
  }

  template <typename T = AddrT>
  void resolveRouteForMirrorDestinationImpl() {
    const auto mirrorDestinationPort = getNonSflowSampledInterfacePort();
    boost::container::flat_set<PortDescriptor> nhopPorts{
        PortDescriptor(mirrorDestinationPort)};

    this->getAgentEnsemble()->applyNewState(
        [&](const std::shared_ptr<SwitchState>& state) {
          utility::EcmpSetupTargetedPorts<T> ecmpHelper(
              state,
              getSw()->needL2EntryForNeighbor(),
              RouterID(0),
              {getNonSflowSampledInterfacePortType()});
          auto newState = ecmpHelper.resolveNextHops(state, nhopPorts);
          return newState;
        },
        "resolve mirror nexthop");

    auto mirror = getSw()->getState()->getMirrors()->getNodeIf(kSflowMirror);
    auto dip = mirror->getDestinationIp();

    RoutePrefix<T> prefix(T(dip->str()), dip->bitCount());
    utility::EcmpSetupTargetedPorts<T> ecmpHelper(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        kMirrorDstMac,
        RouterID(0),
        false /*forProdConfig*/,
        {getNonSflowSampledInterfacePortType()});

    ecmpHelper.programRoutes(
        getAgentEnsemble()->getRouteUpdaterWrapper(), nhopPorts, {prefix});
  }

  void resolveRouteForMirrorDestination(
      bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>) {
    if (isV4) {
      resolveRouteForMirrorDestinationImpl<folly::IPAddressV4>();
    } else {
      resolveRouteForMirrorDestinationImpl<folly::IPAddressV6>();
    }
  }

  std::string getTrafficDestinationIp(int portIndex) {
    return folly::to<std::string>(kIpStr, portIndex, "::1");
  }

  void setupEcmpTraffic() {
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())};

    const PortDescriptor port(getDataTrafficPort());
    RoutePrefixV6 route{
        folly::IPAddressV6(getTrafficDestinationIp(kDataTrafficPortIndex)),
        128};

    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });

    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater, {port}, {route}, {}, std::nullopt, true);
    utility::disablePortTTLDecrementIfSupported(
        getAgentEnsemble(), ecmpHelper.getRouterId(), ecmpHelper.nhop(port));
  }

  std::unique_ptr<facebook::fboss::TxPacket> genPacket(
      int portIndex,
      size_t payloadSize) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    folly::IPAddressV6 sip{"2401:db00:dead:beef::2401"};
    folly::IPAddressV6 dip{getTrafficDestinationIp(portIndex)};
    uint16_t sport = 9701;
    uint16_t dport = 9801;
    std::vector<uint8_t> payload(payloadSize, 0xff);
    auto pkt = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        intfMac,
        intfMac,
        sip,
        dip,
        sport,
        dport,
        0,
        255,
        payload);
    return pkt;
  }

  int getSflowPacketNonSflowHeaderLen(bool isV4) {
    auto ipHeaderLen = isV4 ? 20 : 40;
    auto asic = checkSameAndGetAsic();
    auto vlanHeaderLen = asic->getSwitchType() == cfg::SwitchType::VOQ ? 0 : 4;
    return 14 /* ethernet header */ + vlanHeaderLen + ipHeaderLen +
        UDPHeader::size();
  }

  int getSflowPacketHeaderLength(bool isV4) {
    auto asic = checkSameAndGetAsic();
    int sflowShimHeaderLength = asic->getSflowShimHeaderSize();
    if (asic->isSupported(HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
      sflowShimHeaderLength += 4;
    }
    return sflowShimHeaderLength + getSflowPacketNonSflowHeaderLen(isV4);
  }

  void verifySflowCapturedPacket(
      folly::IOBuf* capturedPktBuf,
      PortID txPort,
      std::span<const std::vector<uint8_t>> sFlowPayloads,
      bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>) {
    // Parse captured packet into SflowPacketParsed struct
    SflowPacketParsed capturedParsed = deserializeSflowPacket(capturedPktBuf);

    // Create expected SflowPacketParsed from inputs similar to sFlowPacket
    folly::MacAddress intfMac{
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())};
    SflowPacketParsed expectedParsed = makeSflowV5PacketParsed(
        getVlanIDForTx() /*vlan*/,
        intfMac /*srcMac*/,
        kMirrorDstMac /*dstMac*/,
        utility::getSflowMirrorSource(isV4) /*srcIp*/,
        utility::getSflowMirrorDestination(isV4) /*dstIp*/,
        kUdpSrcPort /*srcPort*/,
        kUdpDstPort /*dstPort*/,
        0 /*trafficClass*/,
        126 /*hopLimit*/,
        static_cast<uint32_t>(getSystemPortID(
            txPort,
            getProgrammedState(),
            scopeResolver().scope(txPort).switchId())) /*ingressInterface*/,
        0 /*egressInterface*/,
        1 /*samplingRate*/,
        sFlowPayloads /*payloads*/);

    // Compare the contents of the structs, only fields not masked by
    // maskSflowFields Ethernet headers should match
    EXPECT_EQ(
        capturedParsed.ethHeader.getDstMac(),
        expectedParsed.ethHeader.getDstMac());
    EXPECT_EQ(
        capturedParsed.ethHeader.getSrcMac(),
        expectedParsed.ethHeader.getSrcMac());
    EXPECT_EQ(
        capturedParsed.ethHeader.getEtherType(),
        expectedParsed.ethHeader.getEtherType());

    // IP headers should match (preserve IP header fields)
    if (std::holds_alternative<IPv4Hdr>(capturedParsed.ipHeader)) {
      ASSERT_TRUE(std::holds_alternative<IPv4Hdr>(expectedParsed.ipHeader));
      auto& capturedIpv4 = std::get<IPv4Hdr>(capturedParsed.ipHeader);
      auto& expectedIpv4 = std::get<IPv4Hdr>(expectedParsed.ipHeader);
      EXPECT_EQ(capturedIpv4.srcAddr, expectedIpv4.srcAddr);
      EXPECT_EQ(capturedIpv4.dstAddr, expectedIpv4.dstAddr);
      // Skip ttl comparison - could be modified by maskSflowFields logic
      EXPECT_EQ(capturedIpv4.dscp, expectedIpv4.dscp);
      EXPECT_EQ(capturedIpv4.protocol, expectedIpv4.protocol);
    } else {
      ASSERT_TRUE(std::holds_alternative<IPv6Hdr>(expectedParsed.ipHeader));
      auto& capturedIpv6 = std::get<IPv6Hdr>(capturedParsed.ipHeader);
      auto& expectedIpv6 = std::get<IPv6Hdr>(expectedParsed.ipHeader);
      EXPECT_EQ(capturedIpv6.srcAddr, expectedIpv6.srcAddr);
      EXPECT_EQ(capturedIpv6.dstAddr, expectedIpv6.dstAddr);
      EXPECT_EQ(capturedIpv6.trafficClass, expectedIpv6.trafficClass);
      EXPECT_EQ(capturedIpv6.nextHeader, expectedIpv6.nextHeader);
      EXPECT_EQ(capturedIpv6.hopLimit, expectedIpv6.hopLimit);
    }

    // UDP headers should match (preserve source port, destination port)
    EXPECT_EQ(
        capturedParsed.udpHeader.srcPort, expectedParsed.udpHeader.srcPort);
    EXPECT_EQ(
        capturedParsed.udpHeader.dstPort, expectedParsed.udpHeader.dstPort);
    // UDP length and checksum is zeroed in maskSflowFields, so don't compare it

    // sFlow datagram comparison - skip fields that are masked
    auto& capturedDatagram = capturedParsed.sflowDatagram.datagramV5;
    auto& expectedDatagram = expectedParsed.sflowDatagram.datagramV5;

    // Preserve agent address type and IPv6 address
    EXPECT_EQ(capturedDatagram.agentAddress, expectedDatagram.agentAddress);

    // Skip sub-agent ID, sequence number, uptime - these are zeroed by
    // maskSflowFields

    // Preserve number of samples
    EXPECT_EQ(capturedDatagram.samples.size(), expectedDatagram.samples.size());

    for (int sampleIndex = 0; sampleIndex < capturedDatagram.samples.size();
         ++sampleIndex) {
      auto& capturedSample = capturedDatagram.samples[sampleIndex];
      auto& expectedSample = expectedDatagram.samples[sampleIndex];

      // Preserve sFlow data format
      EXPECT_EQ(capturedSample.sampleType, expectedSample.sampleType);

      // Compare sample data - skip sequence number and source ID as they're
      // zeroed
      if (!capturedSample.sampleData.empty() &&
          !expectedSample.sampleData.empty()) {
        // sampleData is a vector, get the first element
        if (std::holds_alternative<sflow::FlowSample>(
                capturedSample.sampleData[0]) &&
            std::holds_alternative<sflow::FlowSample>(
                expectedSample.sampleData[0])) {
          auto& capturedFlowSample =
              std::get<sflow::FlowSample>(capturedSample.sampleData[0]);
          auto& expectedFlowSample =
              std::get<sflow::FlowSample>(expectedSample.sampleData[0]);

          // Skip sequence number and source ID (zeroed by maskSflowFields)
          // Preserve sample rate, sample pool, drops, input/output interfaces
          EXPECT_EQ(
              capturedFlowSample.samplingRate, expectedFlowSample.samplingRate);
          EXPECT_EQ(
              capturedFlowSample.samplePool, expectedFlowSample.samplePool);
          EXPECT_EQ(capturedFlowSample.drops, expectedFlowSample.drops);
          EXPECT_EQ(capturedFlowSample.input, expectedFlowSample.input);
          EXPECT_EQ(capturedFlowSample.output, expectedFlowSample.output);

          // Preserve flow records count and format
          EXPECT_EQ(
              capturedFlowSample.flowRecords.size(),
              expectedFlowSample.flowRecords.size());

          if (!capturedFlowSample.flowRecords.empty() &&
              !expectedFlowSample.flowRecords.empty()) {
            auto& capturedFlowRecord = capturedFlowSample.flowRecords[0];
            auto& expectedFlowRecord = expectedFlowSample.flowRecords[0];

            EXPECT_EQ(
                capturedFlowRecord.flowFormat, expectedFlowRecord.flowFormat);
            // Skip sample header frame length (zeroed by maskSflowFields)
            // Compare flow data payload
            ASSERT_TRUE(
                std::holds_alternative<sflow::SampledHeader>(
                    capturedFlowRecord.flowData));
            auto& capturedSampleHeader =
                std::get<sflow::SampledHeader>(capturedFlowRecord.flowData);
            auto& expectedSampleHeader =
                std::get<sflow::SampledHeader>(expectedFlowRecord.flowData);
            EXPECT_EQ(
                capturedSampleHeader.protocol, expectedSampleHeader.protocol);
            EXPECT_EQ(
                capturedSampleHeader.stripped, expectedSampleHeader.stripped);
            EXPECT_EQ(capturedSampleHeader.header, expectedSampleHeader.header);
          }
        }
      }
    }
  }

  PortID getPortWithMaxHwLogicalPortId() const {
    auto ports = getPortsForSampling();
    PortID bestPort = ports[1];
    uint32_t maxHwId = 0;
    for (size_t i = 1; i < ports.size(); ++i) {
      auto hwId = getHwLogicalPortId(ports[i]);
      if (hwId && *hwId > maxHwId) {
        maxHwId = *hwId;
        bestPort = ports[i];
      }
    }

    for (const auto& port : ports) {
      auto hwId = getHwLogicalPortId(port);
      if (!hwId) {
        continue;
      }
      auto portObj = getProgrammedState()->getPorts()->getNodeIf(port);
      auto name = portObj ? portObj->getName() : "unknown";
      uint8_t smod = static_cast<uint8_t>(*hwId >> 8);
      uint8_t sport = static_cast<uint8_t>(*hwId & 0xFF);
      uint32_t reconstructed = (static_cast<uint32_t>(smod) << 8) | sport;
      XLOG(INFO) << "Port " << name << " (swPortId=" << port
                 << "): hwLogicalPortId=" << *hwId << " -> smod=" << (int)smod
                 << ", sport=" << (int)sport
                 << ", reconstructed=" << reconstructed
                 << (reconstructed == *hwId ? " [OK]" : " [MISMATCH]");
    }

    if (maxHwId > 255) {
      XLOG(INFO) << "Selected port " << bestPort
                 << " with hwLogicalPortId=" << maxHwId
                 << " (exceeds 8-bit range, exercises modId path)";
    } else {
      XLOG(INFO) << "No port with hwLogicalPortId > 255 on this platform; "
                 << "using port " << bestPort
                 << " with hwLogicalPortId=" << maxHwId;
    }
    return bestPort;
  }

  void bringDownOtherPorts(
      const std::vector<PortID>& ports,
      const PortID& samplePort) {
    std::vector<PortID> portsToDown;
    for (const auto& port : ports) {
      if (port != ports[0] && port != samplePort) {
        portsToDown.push_back(port);
      }
    }
    getAgentEnsemble()->bringDownPorts(portsToDown);
  }

  void verifySampledPacket(
      bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>,
      const std::optional<PortID>& samplePortOverride = std::nullopt) {
    auto ports = getPortsForSampling();
    auto samplePort = samplePortOverride.value_or(ports[1]);
    bringDownOtherPorts(ports, samplePort);

    auto pkt = genPacket(1, 256);
    auto length = pkt->buf()->length();

    utility::SwSwitchPacketSnooper snooper(getSw(), "snooper");
    XLOG(DBG2) << "Sending packet through port " << samplePort;
    getAgentEnsemble()->sendPacketAsync(
        std::move(pkt), PortDescriptor(samplePort), std::nullopt);

    std::optional<std::unique_ptr<folly::IOBuf>> capturedPktBuf;
    WITH_RETRIES({
      capturedPktBuf = snooper.waitForPacket(kTimeoutSecs);
      ASSERT_EVENTUALLY_TRUE(capturedPktBuf.has_value());
    });
    folly::io::Cursor capturedPktCursor{capturedPktBuf->get()};
    auto capturedPkt = utility::EthFrame(capturedPktCursor);

    // captured packet has encap header on top
    ASSERT_GE(capturedPkt.length(), length);
    EXPECT_GE(capturedPkt.length(), getSflowPacketHeaderLength(isV4));

    auto payloadLength = length;
    if (checkSameAndGetAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO3) {
      /*
       * J3 pads upto 512 bytes if the packet size is < 512 bytes.
       * J3 trimes upto 512 bytes if the packet size is > 512 bytes.
       */
      payloadLength = 512;
    }
    auto delta = capturedPkt.length() - payloadLength;
    EXPECT_LE(delta, getSflowPacketHeaderLength(isV4));
    auto udpPayload = isV4 ? capturedPkt.v4PayLoad()->udpPayload()
                           : capturedPkt.v6PayLoad()->udpPayload();
    auto payload = udpPayload->payload();
    EXPECT_EQ(udpPayload->header().csum, 0);
    auto hwLogicalPortId = getHwLogicalPortId(samplePort);
    if (!hwLogicalPortId) {
      validateSflowPacketHeader(payload, samplePort);
    } else {
      validateSflowPacketHeader(payload, PortID(*hwLogicalPortId));
    }

    if (checkSameAndGetAsic()->getAsicType() != cfg::AsicType::ASIC_TYPE_EBRO &&
        checkSameAndGetAsic()->getAsicType() != cfg::AsicType::ASIC_TYPE_P200 &&
        checkSameAndGetAsic()->getAsicType() != cfg::AsicType::ASIC_TYPE_YUBA &&
        checkSameAndGetAsic()->getAsicType() !=
            cfg::AsicType::ASIC_TYPE_G202X &&
        checkSameAndGetAsic()->getAsicType() !=
            cfg::AsicType::ASIC_TYPE_JERICHO3) {
      // Call parseSflowShim here. And verify
      // 1. Src port is correct
      // 2. Parser correctly identified whether the pkt is from TH3 or TH4.
      // TODO(vsp): As we incorporate Cisco sFlow packet in parser, enhance this
      // test case.
      auto buf = folly::IOBuf::wrapBuffer(payload.data(), payload.size());
      folly::io::Cursor cursor{buf.get()};
      auto shim = utility::parseSflowShim(cursor);
      XLOG(DBG2) << fmt::format(
          "srcPort = {}, dstPort = {}", shim.srcPort, shim.dstPort);
      if (!hwLogicalPortId) {
        EXPECT_EQ(shim.srcPort, static_cast<uint32_t>(samplePort));
      } else {
        EXPECT_EQ(shim.srcPort, static_cast<uint32_t>(*hwLogicalPortId));
      }
      if (checkSameAndGetAsic()->isSupported(
              HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
        EXPECT_EQ(shim.asic, utility::SflowShimAsic::SFLOW_SHIM_ASIC_TH4);
      } else {
        EXPECT_EQ(shim.asic, utility::SflowShimAsic::SFLOW_SHIM_ASIC_TH3);
      }
    }
  }

  void verifySflowPacketUdpChecksum(
      const folly::IOBuf* capturedPktBuf,
      const utility::EthFrame& capturedPkt,
      const std::optional<utility::UDPDatagram>& udpPayload,
      bool isV4) {
    uint16_t csum = 0;
    if (udpPayload->header().csum) {
      folly::io::Cursor pktCursor{capturedPktBuf};
      pktCursor += getSflowPacketNonSflowHeaderLen(isV4);
      auto udpHdr = UDPHeader(
          udpPayload->header().srcPort,
          udpPayload->header().dstPort,
          udpPayload->header().length,
          udpPayload->header().csum);
      if (isV4) {
        csum = udpHdr.computeChecksum(
            capturedPkt.v4PayLoad()->header(), pktCursor);
      } else {
        csum = udpHdr.computeChecksum(
            capturedPkt.v6PayLoad()->header(), pktCursor);
      }
    }
    XLOG(DBG2) << "Sflow packet expected UDP csum: " << csum
               << ", csum in packet: " << udpPayload->header().csum;
    // If sflow packet has non zero UDP csum, then make sure that
    // its as expected!
    EXPECT_EQ(udpPayload->header().csum, csum);
  }

  void verifySampledPacketWithTruncate(
      bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>,
      const std::optional<PortID>& samplePortOverride = std::nullopt) {
    auto ports = getPortsForSampling();
    auto samplePort = samplePortOverride.value_or(ports[1]);
    bringDownOtherPorts(ports, samplePort);

    std::unique_ptr<TxPacket> pkt = genPacket(1, 8000);
    std::size_t length = pkt->buf()->length();

    const uint8_t* packetData = pkt->buf()->data();
    std::vector<uint8_t> sFlowPayload(
        packetData, packetData + getMirrorTruncateSize());

    utility::SwSwitchPacketSnooper snooper(getSw(), "snooper");
    PortID txPort{samplePort};
    XLOG(DBG2) << "Sending packet through port " << txPort;
    getAgentEnsemble()->sendPacketAsync(
        std::move(pkt), PortDescriptor(txPort), std::nullopt);

    std::optional<std::unique_ptr<folly::IOBuf>> capturedPktBuf;
    WITH_RETRIES({
      capturedPktBuf = snooper.waitForPacket(kTimeoutSecs);
      EXPECT_EVENTUALLY_TRUE(capturedPktBuf.has_value());
    });
    folly::io::Cursor capturedPktCursor{capturedPktBuf->get()};
    auto capturedPkt = utility::EthFrame(capturedPktCursor);

    // captured packet has encap header on top
    EXPECT_LE(capturedPkt.length(), length);
    auto capturedHdrSize = getSflowPacketHeaderLength(isV4);
    EXPECT_GE(capturedPkt.length(), capturedHdrSize);

    EXPECT_LE(
        capturedPkt.length() - capturedHdrSize,
        getMirrorTruncateSize()); /* TODO: confirm length in CS00010399535 and
                                     CS00012130950 */
    auto udpPayload = isV4 ? capturedPkt.v4PayLoad()->udpPayload()
                           : capturedPkt.v6PayLoad()->udpPayload();
    verifySflowExporterIp(udpPayload->payload());
    verifySflowPacketUdpChecksum(
        capturedPktBuf->get(), capturedPkt, udpPayload, isV4);

    if (checkSameAndGetAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO3) {
      verifySflowCapturedPacket(
          capturedPktBuf->get(), txPort, std::array{sFlowPayload});
    }
  }

  uint64_t getSampleCount(
      const std::map<PortID, HwPortStats>& newStats,
      const std::map<PortID, HwPortStats>& oldStats) {
    const auto& newPortStats = newStats.at(getNonSflowSampledInterfacePort());
    const auto& oldPortStats = oldStats.at(getNonSflowSampledInterfacePort());
    return *newPortStats.outUnicastPkts_() - *oldPortStats.outUnicastPkts_();
  }

  const PortID getDataTrafficPort() {
    return getPortsForSampling()[kDataTrafficPortIndex];
  }

  uint64_t getExpectedSampleCount(
      const std::map<PortID, HwPortStats>& newStats,
      const std::map<PortID, HwPortStats>& oldStats) {
    auto port = getDataTrafficPort();
    const auto& newPortStats = newStats.at(port);
    const auto& oldPortStats = oldStats.at(port);
    // In theory it's more correct to calcuate this based on inUnicastPkts, but
    // Rx counters are not supported on TH5 in EDB loopback, so use Tx.
    uint64_t expectedSampleCount =
        (*newPortStats.outUnicastPkts_() - *oldPortStats.outUnicastPkts_()) /
        FLAGS_sflow_test_rate;
    XLOG(DBG2) << "total packets tx delta: "
               << (*newPortStats.outUnicastPkts_() -
                   *oldPortStats.outUnicastPkts_());
    return expectedSampleCount;
  }

  void verifySampledPacketRate() {
    auto trafficPort = getDataTrafficPort();
    for (int i = 0; i < getAgentEnsemble()->getMinPktsForLineRate(trafficPort);
         i++) {
      auto pkt = genPacket(kDataTrafficPortIndex, 256 /*payloadSize*/);
      getAgentEnsemble()->sendPacketAsync(
          std::move(pkt), PortDescriptor(trafficPort), std::nullopt);
    }
    const uint64_t portSpeedBps =
        static_cast<uint64_t>(getProgrammedState()
                                  ->getPorts()
                                  ->getNodeIf(trafficPort)
                                  ->getSpeed()) *
        1000 * 1000 * 0.99; // 99% is good enough
    getAgentEnsemble()->waitForSpecificRateOnPort(trafficPort, portSpeedBps);

    auto samplingPorts = getPortsForSampling();
    auto mirrorPort = getNonSflowSampledInterfacePort();
    // Bring down all sampling ports except the data traffic port.
    std::vector<PortID> portsToDown;
    for (const auto& port : samplingPorts) {
      if (port != trafficPort) {
        portsToDown.push_back(port);
      }
    }
    getAgentEnsemble()->bringDownPorts(portsToDown);
    // Collect stats from the data traffic port and mirror egress port.
    std::vector<PortID> statsPorts = {trafficPort, mirrorPort};
    int percentError = 100;
    std::map<PortID, HwPortStats> lastStats;
    WITH_RETRIES({
      auto stats = getLatestPortStats(statsPorts);
      if (lastStats.empty()) {
        lastStats = stats;
        continue;
      }
      auto actualSampleCount = getSampleCount(stats, lastStats);
      auto expectedSampleCount = getExpectedSampleCount(stats, lastStats);
      lastStats = stats;
      XLOG(DBG2) << "Number of sflow samples expected: " << expectedSampleCount
                 << ", samples seen: " << actualSampleCount;
      if (expectedSampleCount == 0 || actualSampleCount == 0) {
        // Not enough traffic in this interval, retry.
        continue;
      }
      auto difference = (expectedSampleCount > actualSampleCount)
          ? (expectedSampleCount - actualSampleCount)
          : (actualSampleCount - expectedSampleCount);
      percentError = (difference * 100) / actualSampleCount;
      EXPECT_EVENTUALLY_LE(percentError, kDefaultPercentErrorThreshold);
    });

    getAgentEnsemble()->bringUpPorts(portsToDown);
    getAgentEnsemble()->clearPortStats();
  }

  virtual void testSampledPacket() {
    auto setup = [=, this]() {
      auto ports = getPortsForSampling();
      auto config = initialConfig(*getAgentEnsemble());
      configureMirrorWithSampling(config, 1 /*sampleRate*/);
      configureTrapAcl(config);
      applyNewConfig(config);
      resolveRouteForMirrorDestination();
    };
    auto verify = [=, this]() {
      if (enableTruncation()) {
        verifySampledPacketWithTruncate();
      } else {
        verifySampledPacket();
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void testSampledPacketHighPortId() {
    auto setup = [=, this]() {
      auto config = initialConfig(*getAgentEnsemble());
      configureMirrorWithSampling(config, 1 /*sampleRate*/);
      configureTrapAcl(config);
      applyNewConfig(config);
      resolveRouteForMirrorDestination();
    };
    auto verify = [=, this]() {
      auto port = getPortWithMaxHwLogicalPortId();
      if (enableTruncation()) {
        verifySampledPacketWithTruncate(
            std::is_same_v<AddrT, folly::IPAddressV4>, port);
      } else {
        verifySampledPacket(std::is_same_v<AddrT, folly::IPAddressV4>, port);
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void testSampledPacketRate() {
    auto setup = [=, this]() {
      auto config = initialConfig(*getAgentEnsemble());
      configureMirrorWithSampling(config, FLAGS_sflow_test_rate /*sampleRate*/);
      applyNewConfig(config);
      resolveRouteForMirrorDestination();
      setupEcmpTraffic();
    };
    auto verify = [=, this]() { verifySampledPacketRate(); };
    verifyAcrossWarmBoots(setup, verify);
  }

  void stressRecreateMirror() {
    auto setup = [=, this]() {
      auto ports = getPortsForSampling();

      // Test for resource leaks by repeatedly creating and removing mirror.
      for (int i = 0; i < 50; i++) {
        XLOG(INFO) << "Add mirror iteration " << i;
        auto config = initialConfig(*getAgentEnsemble());
        configureMirrorWithSampling(config, 1 /*sampleRate*/);
        applyNewConfig(config);
        resolveRouteForMirrorDestination();

        XLOG(INFO) << "Remove mirror iteration " << i;
        config = initialConfig(*getAgentEnsemble()); // remove mirror
        applyNewConfig(config);
      }

      XLOG(INFO) << "Final add mirror";
      auto config = initialConfig(*getAgentEnsemble());
      configureMirrorWithSampling(config, 1 /*sampleRate*/);
      configureTrapAcl(config);
      applyNewConfig(config);
      resolveRouteForMirrorDestination();
    };
    auto verify = [=, this]() {
      if (enableTruncation()) {
        verifySampledPacketWithTruncate();
      } else {
        verifySampledPacket();
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void verifySrcPortRandomizationOnSflowPacket(uint16_t numPackets = 10) {
    auto ports = getPortsForSampling();
    getAgentEnsemble()->bringDownPorts(
        std::vector<PortID>(ports.begin() + 2, ports.end()));
    std::set<uint16_t> l4SrcPorts;
    for (auto i = 0; i < numPackets; i++) {
      auto pkt = genPacket(1, 256);
      utility::SwSwitchPacketSnooper snooper(getSw(), "snooper");
      XLOG(DBG2) << "Sending packet through port " << ports[1];
      getAgentEnsemble()->sendPacketAsync(
          std::move(pkt), PortDescriptor(ports[1]), std::nullopt);

      std::optional<std::unique_ptr<folly::IOBuf>> capturedPktBuf;
      WITH_RETRIES({
        capturedPktBuf = snooper.waitForPacket(kTimeoutSecs);
        EXPECT_EVENTUALLY_TRUE(capturedPktBuf.has_value());
      });
      folly::io::Cursor capturedPktCursor{capturedPktBuf->get()};
      auto capturedPkt = utility::EthFrame(capturedPktCursor);
      auto udpPayload = capturedPkt.v6PayLoad()->udpPayload();
      // Validate UDP checksum added to the packet is accurate
      verifySflowPacketUdpChecksum(
          capturedPktBuf->get(), capturedPkt, udpPayload, false /*isV4*/);
      l4SrcPorts.insert(udpPayload->header().srcPort);
      // J3 randomize the source based on the time stamp in the asic.
      // Add a delay to ensure the source ports are different for
      // every packet that is trapped to CPU with SFLOW header.
      sleep(2);
    }
    EXPECT_GT(l4SrcPorts.size(), 1);
  }

  utility::MacAddressGenerator macGenerator = utility::MacAddressGenerator();
  constexpr static size_t kDefaultPayloadSize = 1400;
  constexpr static size_t kDefaultPercentErrorThreshold = 7;
  constexpr static auto kIpStr = "2401:db00:dead:beef:";
};

template <typename AddrT>
class AgentSflowMirrorUntruncateTest : public AgentSflowMirrorTest<AddrT> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return {
          ProductionFeature::SFLOWv4_SAMPLING,
          ProductionFeature::UNTRUNCATED_SFLOW};
    } else {
      return {
          ProductionFeature::SFLOWv6_SAMPLING,
          ProductionFeature::UNTRUNCATED_SFLOW};
    }
  }

  virtual bool enableTruncation() const override {
    return false;
  }
};

template <typename AddrT>
class AgentSflowMirrorTruncateTest : public AgentSflowMirrorTest<AddrT> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return {
          ProductionFeature::SFLOWv4_SAMPLING,
          ProductionFeature::MIRROR_PACKET_TRUNCATION};
    } else {
      return {
          ProductionFeature::SFLOWv6_SAMPLING,
          ProductionFeature::MIRROR_PACKET_TRUNCATION};
    }
  }

  virtual bool enableTruncation() const override {
    return true;
  }
};

// These tests try to program sflow mirror with IPv4, then change the config
// to IPv6, or vice versa. So both V4 and V6 must be supported.
class AgentSflowMirrorAddressFamilySwitchingTest
    : public AgentSflowMirrorTruncateTest<folly::IPAddressV4> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SFLOWv4_SAMPLING,
        ProductionFeature::SFLOWv6_SAMPLING,
        ProductionFeature::MIRROR_PACKET_TRUNCATION};
  }
};

template <typename AddrT>
class AgentSflowMirrorOnTrunkTest : public AgentSflowMirrorTruncateTest<AddrT> {
 public:
  using AgentSflowMirrorTest<AddrT>::getPortsForSampling;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return {
          ProductionFeature::SFLOWv4_SAMPLING,
          ProductionFeature::LAG,
          ProductionFeature::MIRROR_PACKET_TRUNCATION,
          ProductionFeature::LAG_MIRRORING};
    } else {
      return {
          ProductionFeature::SFLOWv6_SAMPLING,
          ProductionFeature::LAG,
          ProductionFeature::MIRROR_PACKET_TRUNCATION,
          ProductionFeature::LAG_MIRRORING};
    }
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentSflowMirrorTruncateTest<AddrT>::initialConfig(ensemble);
    configTrunk(config, ensemble);
    return config;
  }

  void configTrunk(cfg::SwitchConfig& config, const AgentEnsemble& ensemble)
      const {
    auto port0 = ensemble.masterLogicalPortIds()[0];
    auto port0Switch =
        ensemble.getSw()->getScopeResolver()->scope(port0).switchId();
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(port0Switch);
    utility::addAggPort(
        1,
        {getPortsForSampling(ensemble.masterLogicalPortIds(), asic)[0]},
        &config);
  }
};

class AgentSflowMirrorWithLineRateTrafficTest
    : public AgentSflowMirrorTruncateTest<folly::IPAddressV6> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto productionFeatures = AgentSflowMirrorTruncateTest<
        folly::IPAddressV6>::getProductionFeaturesVerified();
    productionFeatures.push_back(ProductionFeature::LINERATE_SFLOW);
    return productionFeatures;
  }

  void setCmdLineFlagOverrides() const override {
    AgentSflowMirrorTruncateTest::setCmdLineFlagOverrides();
    // VerifySflowEgressCongestionShort is also used in n-warmboot tests, where
    // we want to test basic fabric port init.
    FLAGS_hide_fabric_ports = false;
    // Test that neighbor advertisements or stray packets won't cause eventor
    // port to get stuck (CS00012404377).
    FLAGS_disable_neighbor_updates = false;
    FLAGS_allow_eventor_send_packet = true;
  }

  static const int kLosslessPriority{2};
  void testSflowEgressCongestion(int iterations) {
    int kNumDataTrafficPorts;
    if (FLAGS_hyper_port) {
      kNumDataTrafficPorts = 4;
    } else {
      kNumDataTrafficPorts = 6;
    }
    auto setup = [=, this]() {
      auto allPorts = getAllPorts();
      std::vector<PortID> portIds(
          allPorts.begin(), allPorts.begin() + kNumDataTrafficPorts);
      std::vector<int> losslessPgIds = {kLosslessPriority};
      std::vector<int> lossyPgIds = {0};
      auto config = initialConfig(*getAgentEnsemble());
      // Configure 1:1 sampling to ensure high rate on mirror egress port
      configureMirrorWithSampling(config, 1 /*sampleRate*/);
      // PFC buffer configurations to ensure we have lossless traffic
      const std::map<int, int> tcToPgOverride{};
      // We dont want PFC here, so set global shared threshold to be high
      auto asicType =
          checkSameAndGetAsicType(this->initialConfig(*getAgentEnsemble()));
      auto bufferParams =
          utility::PfcBufferParams::getPfcBufferParams(asicType);
      bufferParams.globalShared = 20 * 1024 * 1024;
      bufferParams.scalingFactor = cfg::MMUScalingFactor::ONE;
      utility::setupPfcBuffers(
          getAgentEnsemble(),
          config,
          portIds,
          losslessPgIds,
          lossyPgIds,
          bufferParams,
          utility::PfcQosMapParams{.tcToPg = tcToPgOverride});
      // Make sure that traffic is going to loop for ever!
      utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), config);
      applyNewConfig(config);
      resolveRouteForMirrorDestination();
      utility::setupEcmpDataplaneLoopOnAllPorts(getAgentEnsemble());
      // 1. Low rate of the order of 1% of 800G is good enough to saturate the
      // eventor port, which currently has a max possible rate of ~13Gbps.
      // 2. For prod, we sample 1 out of 60K packets, hence the rate expected
      // on eventor port is low, 1/60K of 14.4Tbps ~240Mbps.
      utility::createTrafficOnMultiplePorts(
          getAgentEnsemble(),
          kNumDataTrafficPorts,
          sendPacket,
          1 /*desiredPctLineRate*/);
    };
    auto verify = [=, this]() {
      // Attempt to send packets directly into the eventor port to brick it.
      if (checkSameAndGetAsic()->isSupported(
              HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW)) {
        auto eventorPortId =
            masterLogicalPortIds({cfg::PortType::EVENTOR_PORT})[0];
        XLOG(INFO) << "Eventor port: " << eventorPortId;
        for (int i = 0; i < 10; ++i) {
          auto pkt = utility::makeUDPTxPacket(
              getSw(),
              std::nullopt, // vlanID
              folly::MacAddress("02:00:00:00:0F:0B"),
              folly::MacAddress("FF:FF:FF:FF:FF:FF"),
              folly::IPAddressV6("::1"),
              folly::IPAddressV6("::2"),
              1234, // srcPort
              5678, // dstPort
              0, // dscp
              255, // hopLimit
              std::vector<uint8_t>(100, 0xff)); // 100B payload
          getAgentEnsemble()->sendPacketAsync(
              std::move(pkt), PortDescriptor(eventorPortId), std::nullopt);
        }
      }

      verifySflowEgressPortNotStuck(iterations);
      if (checkSameAndGetAsic()->isSupported(
              HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW)) {
        validateEventorPortQueueLimitRespected();
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }

 protected:
  static void sendPacket(
      facebook::fboss::AgentEnsemble* ensemble,
      const folly::IPAddressV6& dstIp) {
    folly::IPAddressV6 kSrcIp("2402::1");
    const auto dstMac = getMacForFirstInterfaceWithPortsForTesting(
        ensemble->getProgrammedState());
    const auto srcMac = utility::MacAddressGenerator().get(dstMac.u64HBO() + 1);

    auto txPacket = utility::makeUDPTxPacket(
        ensemble->getSw(),
        std::nullopt, // vlanID
        srcMac,
        dstMac,
        kSrcIp,
        dstIp,
        8000, // l4 src port
        8001, // l4 dst port
        kLosslessPriority * 8 << 2, // dscp for lossless traffic
        255, // hopLimit
        std::vector<uint8_t>(4500));
    // Forward the packet in the pipeline
    ensemble->getSw()->sendPacketSwitchedAsync(
        std::move(txPacket), {getSwitchIdUnderTest(*ensemble)});
  }

  void verifySflowEgressPortNotStuck(int iterations) {
    if (checkSameAndGetAsic()->isSupported(
            HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW)) {
      // Ensure eventor port stats keep incrementing
      const PortID kEventorPortId =
          masterLogicalPortIds({cfg::PortType::EVENTOR_PORT})[0];
      auto eventorStatsBefore = getLatestPortStats(kEventorPortId);
      WITH_RETRIES({
        auto eventorStatsAfter = getLatestPortStats(kEventorPortId);
        XLOG(DBG0) << "Eventor port outPackets: "
                   << *eventorStatsAfter.outUnicastPkts_();
        EXPECT_EVENTUALLY_GT(
            *eventorStatsAfter.outUnicastPkts_(),
            *eventorStatsBefore.outUnicastPkts_());
      });
    }
    auto portId = getNonSflowSampledInterfacePort();
    // Expect atleast 1Gbps of mirror traffic!
    const uint64_t kDesiredMirroredTrafficRate{1000000000};
    EXPECT_NO_THROW(
        getAgentEnsemble()->waitForSpecificRateOnPort(
            portId, kDesiredMirroredTrafficRate));
    // Make sure that we can sustain the rate for longer duration
    constexpr int kWaitPeriod{5};
    auto prevPortStats = getLatestPortStats(portId);
    for (int iter = 0; iter < iterations; iter++) {
      sleep(kWaitPeriod);
      auto curPortStats = getLatestPortStats(portId);
      auto rate = getAgentEnsemble()->getTrafficRate(
          prevPortStats, curPortStats, kWaitPeriod);
      // Ensure that we always see greater than the desired rate
      EXPECT_GT(rate, kDesiredMirroredTrafficRate);
      prevPortStats = curPortStats;
    }
  }

  void validateEventorPortQueueLimitRespected() {
    auto config{initialConfig(*getAgentEnsemble())};
    uint32_t maxExpectedQueueLimitBytes{0};
    const PortID eventorPortId =
        masterLogicalPortIds({cfg::PortType::EVENTOR_PORT})[0];
    for (auto& port : *config.ports()) {
      if (PortID(*port.logicalID()) == eventorPortId) {
        auto voqConfigName = *port.portVoqConfigName();
        for (auto& voqConfig : config.portQueueConfigs()[voqConfigName]) {
          // Set the expected queue limit bytes to be 15% higher than
          // what we are configuring, as queue limit is not always
          // respected accurately.
          if (voqConfig.id() == 0) {
            maxExpectedQueueLimitBytes =
                *voqConfig.maxDynamicSharedBytes() * 1.15;
            break;
          }
        }
        break;
      }
    }
    EXPECT_GT(maxExpectedQueueLimitBytes, 0);
    auto eventorSysPortId = getSystemPortID(
        eventorPortId, getProgrammedState(), switchIdForPort(eventorPortId));
    WITH_RETRIES({
      auto latestSysPortStats = getLatestSysPortStats(eventorSysPortId);
      auto watermarkBytes = latestSysPortStats.queueWatermarkBytes_()->at(0);
      EXPECT_EVENTUALLY_GT(watermarkBytes, 0);
      EXPECT_LT(watermarkBytes, maxExpectedQueueLimitBytes);
    });
  }
};

class AgentSflowMirrorTruncateWithSamplesPackingTestV6
    : public AgentSflowMirrorTruncateTest<folly::IPAddressV6> {
 public:
  const int kNumSflowSamplesPacked{4};
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto productionFeatures = AgentSflowMirrorTruncateTest<
        folly::IPAddressV6>::getProductionFeaturesVerified();
    productionFeatures.push_back(ProductionFeature::SFLOW_SAMPLES_PACKING);
    return productionFeatures;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config =
        AgentSflowMirrorTruncateTest<folly::IPAddressV6>::initialConfig(
            ensemble);
    config.switchSettings()->numberOfSflowSamplesPerPacket() =
        kNumSflowSamplesPacked;
    return config;
  }

  void verifySflowSamplePacking(uint8_t numSflowSamplesPacked) {
    auto ports = getPortsForSampling();
    getAgentEnsemble()->bringDownPorts(
        std::vector<PortID>(ports.begin() + 2, ports.end()));
    utility::SwSwitchPacketSnooper snooper(getSw(), "snooper");
    // Send as many packets as the samples packed in a single
    // packet and expect to receive a single sampled packet.
    int numPacketsToSend{numSflowSamplesPacked};
    PortID txPort{ports[1]};
    std::vector<std::vector<uint8_t>> sFlowPayloads{};
    for (auto i = 0; i < numPacketsToSend; i++) {
      auto pkt = genPacket(1, 256);
      const uint8_t* packetData = pkt->buf()->data();
      sFlowPayloads.emplace_back(
          packetData, packetData + getMirrorTruncateSize());
      XLOG(DBG2) << "Sending packet through port " << ports[1];
      getAgentEnsemble()->sendPacketAsync(
          std::move(pkt), PortDescriptor(txPort), std::nullopt);
    }
    WITH_RETRIES({
      // Make sure that packets TXed from port stats!
      auto portStats = getLatestPortStats(txPort);
      EXPECT_EVENTUALLY_GE(*portStats.outUnicastPkts_(), numPacketsToSend);
    });
    int sflowPacketCount{0};
    std::optional<std::unique_ptr<folly::IOBuf>> capturedPacketBuf;
    // Should not expect to see more than number of packets sent
    for (int i = 0; i < numPacketsToSend; i++) {
      capturedPacketBuf = snooper.waitForPacket(1);
      if (capturedPacketBuf.has_value()) {
        sflowPacketCount++;
        folly::MacAddress intfMac{
            getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())};
        bool isV4 = false;
        SflowPacketParsed expectedParsed = makeSflowV5PacketParsed(
            getVlanIDForTx() /*vlan*/,
            intfMac /*srcMac*/,
            kMirrorDstMac /*dstMac*/,
            utility::getSflowMirrorSource(isV4) /*srcIp*/,
            utility::getSflowMirrorDestination(isV4) /*dstIp*/,
            kUdpSrcPort /*srcPort*/,
            kUdpDstPort /*dstPort*/,
            0 /*trafficClass*/,
            126 /*hopLimit*/,
            static_cast<uint32_t>(getSystemPortID(
                txPort,
                getProgrammedState(),
                scopeResolver().scope(txPort).switchId())) /*ingressInterface*/,
            0 /*egressInterface*/,
            1 /*samplingRate*/,
            sFlowPayloads /*payloads*/);
        SflowPacketParsed capturedParsed =
            deserializeSflowPacket(capturedPacketBuf->get());
        verifySflowCapturedPacket(
            capturedPacketBuf->get(), txPort, sFlowPayloads);
      }
    }
    // Make sure that we received exactly one sflow packet
    EXPECT_EQ(sflowPacketCount, 1);
    // Verify packing!
    // TODO: Inspect packet for the samples!
  }
};

// Test class for sflow mirror with remote system port as destination
// This tests that sflow packets are correctly sent to a remote system port
// via the VOQ infrastructure.
class AgentSflowMirrorRemoteSystemPortTest
    : public AgentSflowMirrorTruncateTest<folly::IPAddressV6> {
 public:
  const folly::IPAddressV6 kRemoteIntfIpV6{"100::1"};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SFLOWv6_SAMPLING,
        ProductionFeature::MIRROR_PACKET_TRUNCATION,
        ProductionFeature::VOQ};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentSflowMirrorTruncateTest<folly::IPAddressV6>::initialConfig(
        ensemble);
    // Add remote interface node to DSF config
    cfg.dsfNodes() = *utility::addRemoteIntfNodeCfg(*cfg.dsfNodes(), 1);
    return cfg;
  }

  // Helper to get port name from port ID
  std::string getPortName(PortID portId) {
    auto port = getProgrammedState()->getPorts()->getNodeIf(portId);
    return port ? port->getName()
                : fmt::format("unknown({})", static_cast<int>(portId));
  }

  // Helper to log port statistics for debugging
  void logPortStats(const std::string& context, const PortID& portId) {
    auto portStats = getLatestPortStats(portId);
    auto portName = getPortName(portId);
    XLOG(DBG2) << context << " - Port " << portName
               << " stats: outUnicastPkts=" << *portStats.outUnicastPkts_()
               << ", inUnicastPkts=" << *portStats.inUnicastPkts_();
  }

  // Sflow mirrored packets are sent to queue 0
  static constexpr int kSflowMirrorQueue = 0;

  // Helper to log system port VOQ queue 0 statistics for debugging
  void logSysPortStats(const std::string& context, SystemPortID sysPortId) {
    auto sysPortStats = getLatestSysPortStats(sysPortId);
    auto queue0Bytes = sysPortStats.queueOutBytes_()->at(kSflowMirrorQueue);
    XLOG(DBG2) << context << " - System port " << sysPortId << " VOQ queue "
               << kSflowMirrorQueue << ": outBytes=" << queue0Bytes;
  }

  // Set up route for traffic to be forwarded when sent to a specific destIp
  void setupTrafficRoute(const PortID& egressPort) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    // Use a different MAC to avoid looping - add 10 to the last byte
    auto nbrMac = folly::MacAddress::fromHBO(intfMac.u64HBO() + 10);

    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), nbrMac};

    const PortDescriptor port(egressPort);
    RoutePrefixV6 route{folly::IPAddressV6(getTrafficDestinationIp(1)), 128};

    XLOG(DBG2) << "Setting up route for dest IP " << route.network() << "/"
               << static_cast<int>(route.mask()) << " to egress "
               << getPortName(egressPort)
               << " with neighbor MAC: " << nbrMac.toString();

    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });

    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&routeUpdater, {port}, {route});
  }

  // Send packets and verify they are queued on the VOQ for the remote system
  // port. Sflow mirrored packets are sent to queue 0.
  void sendPacketsAndVerifyVoqCounters(
      const PortID& ingressPort,
      SystemPortID remoteSysPortId,
      int numPackets = 100) {
    auto beforeSysPortStats = getLatestSysPortStats(remoteSysPortId);
    auto beforeQueueBytes =
        beforeSysPortStats.queueOutBytes_()->at(kSflowMirrorQueue);

    XLOG(DBG2) << "Sending " << numPackets
               << " packets to trigger sflow sampling...";
    for (int i = 0; i < numPackets; ++i) {
      auto pkt = genPacket(1, 256);
      getAgentEnsemble()->sendPacketAsync(
          std::move(pkt), PortDescriptor(ingressPort), std::nullopt);
    }

    WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(1000), {
      auto afterSysPortStats = getLatestSysPortStats(remoteSysPortId);
      auto afterQueueBytes =
          afterSysPortStats.queueOutBytes_()->at(kSflowMirrorQueue);

      XLOG(DBG2) << "Remote system port " << remoteSysPortId << " VOQ queue "
                 << kSflowMirrorQueue
                 << ": Bytes - before: " << beforeQueueBytes
                 << ", after: " << afterQueueBytes;

      EXPECT_EVENTUALLY_GT(afterQueueBytes, beforeQueueBytes);
    });
  }
  // Override testSampledPacket to set up remote system port for mirror

  void testSampledPacket() override {
    const auto remoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto remoteIntfId = utility::getRemoteIntfId(remoteSysPortId);
    auto setup = [=, this]() {
      auto config = initialConfig(*getAgentEnsemble());
      configureMirrorWithSampling(config, 1 /*sampleRate*/);
      applyNewConfig(config);

      // Add remote system port and interface
      auto remoteSwitchID = utility::getRemoteVoqSwitchId(getSw());
      XLOG(DBG2) << "Adding remote system port " << remoteSysPortId
                 << " and interface " << remoteIntfId << " on remote switch "
                 << remoteSwitchID << " with IP: " << kRemoteIntfIpV6.str();
      utility::addRemoteSysPortAndInterface(
          getSw(),
          remoteSwitchID,
          remoteSysPortId,
          remoteIntfId,
          {{folly::IPAddress(kRemoteIntfIpV6), 64}});

      // Resolve route to remote system port for mirror destination
      auto mirrorDestIp = utility::getSflowMirrorDestination(false /* isV4 */);
      XLOG(DBG2) << "Resolving route for sflow collector " << mirrorDestIp.str()
                 << " to remote system port " << remoteSysPortId;
      utility::resolveRouteToRemoteSysPort(
          getProgrammedState(),
          getSw(),
          getAgentEnsemble(),
          remoteSysPortId,
          folly::IPAddressV6(mirrorDestIp.str()));

      // Set up route for traffic to egress
      auto ports = getPortsForSampling();
      setupTrafficRoute(ports[0]);
    };
    auto verify = [=, this]() {
      // Get the mirror destination IP (sflow collector)
      auto mirrorDestIp = utility::getSflowMirrorDestination(false /* isV4 */);

      // Get ports for sampling
      auto ports = getPortsForSampling();
      getAgentEnsemble()->bringDownPorts(
          std::vector<PortID>(ports.begin() + 2, ports.end()));

      // Log test configuration
      XLOG(DBG2) << "Traffic destination port: " << getPortName(ports[0])
                 << ", ingress port: " << getPortName(ports[1])
                 << ", sflow collector IP: " << mirrorDestIp.str()
                 << ", remote system port: " << remoteSysPortId
                 << " on remote switch "
                 << utility::getRemoteVoqSwitchId(getSw());

      // Log initial statistics, send packets, and verify VOQ counters
      logPortStats("Before traffic", ports[0]);
      logPortStats("Before traffic", ports[1]);
      logSysPortStats("Before traffic", remoteSysPortId);

      sendPacketsAndVerifyVoqCounters(ports[1], remoteSysPortId);

      // Log final port statistics for comparison
      logPortStats("After traffic", ports[0]);
      logPortStats("After traffic", ports[1]);
      logSysPortStats("After traffic", remoteSysPortId);
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

using AgentSflowMirrorUntruncateV4Test =
    AgentSflowMirrorUntruncateTest<folly::IPAddressV4>;
using AgentSflowMirrorUntruncateV6Test =
    AgentSflowMirrorUntruncateTest<folly::IPAddressV6>;
using AgentSflowMirrorTruncateV4Test =
    AgentSflowMirrorTruncateTest<folly::IPAddressV4>;
using AgentSflowMirrorTruncateV6Test =
    AgentSflowMirrorTruncateTest<folly::IPAddressV6>;
using AgentSflowMirrorOnTrunkV4Test =
    AgentSflowMirrorOnTrunkTest<folly::IPAddressV4>;
using AgentSflowMirrorOnTrunkV6Test =
    AgentSflowMirrorOnTrunkTest<folly::IPAddressV6>;

#define SFLOW_SAMPLING_TEST(fixture, name, code) \
  TEST_F(fixture, name) {                        \
    {                                            \
      code                                       \
    }                                            \
  }

#define SFLOW_SAMPLING_UNTRUNCATE_TEST_V4_V6(name, code)            \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorUntruncateV4Test, name, code) \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorUntruncateV6Test, name, code)

#define SFLOW_SAMPLING_TRUNCATE_TEST_V4_V6(name, code)            \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorTruncateV4Test, name, code) \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorTruncateV6Test, name, code)

#define SFLOW_SAMPLING_TRUNK_TEST_V4_V6(name, code)              \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorOnTrunkV4Test, name, code) \
  SFLOW_SAMPLING_TEST(AgentSflowMirrorOnTrunkV6Test, name, code)

SFLOW_SAMPLING_UNTRUNCATE_TEST_V4_V6(VerifySampledPacket, {
  this->testSampledPacket();
})
SFLOW_SAMPLING_UNTRUNCATE_TEST_V4_V6(VerifySampledPacketRate, {
  this->testSampledPacketRate();
})
SFLOW_SAMPLING_UNTRUNCATE_TEST_V4_V6(StressRecreateMirror, {
  this->stressRecreateMirror();
})

SFLOW_SAMPLING_TRUNCATE_TEST_V4_V6(VerifyTruncate, {
  this->testSampledPacketHighPortId();
})
SFLOW_SAMPLING_TRUNCATE_TEST_V4_V6(VerifySampledPacketRate, {
  this->testSampledPacketRate();
})
SFLOW_SAMPLING_TRUNCATE_TEST_V4_V6(StressRecreateMirror, {
  this->stressRecreateMirror();
})

SFLOW_SAMPLING_TRUNK_TEST_V4_V6(VerifySampledPacket, {
  this->testSampledPacket();
})
SFLOW_SAMPLING_TRUNK_TEST_V4_V6(VerifySampledPacketRate, {
  this->testSampledPacketRate();
})

TEST_F(AgentSflowMirrorRemoteSystemPortTest, VerifySflowWithRemoteSystemPort) {
  this->testSampledPacket();
}

TEST_F(AgentSflowMirrorWithLineRateTrafficTest, VerifySflowEgressCongestion) {
  this->testSflowEgressCongestion(6);
}

TEST_F(
    AgentSflowMirrorWithLineRateTrafficTest,
    VerifySflowEgressCongestionShort) {
  // Test fewer iterations as we will be using this in an n-warmboot test.
  this->testSflowEgressCongestion(1);
}

class AgentSflowMirrorEventorTest
    : public AgentSflowMirrorWithLineRateTrafficTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto productionFeatures = AgentSflowMirrorWithLineRateTrafficTest::
        getProductionFeaturesVerified();
    productionFeatures.push_back(
        ProductionFeature::SFLOW_SAMPLES_PACKING); // implies eventor port
    return productionFeatures;
  }

  void setCmdLineFlagOverrides() const override {
    AgentSflowMirrorWithLineRateTrafficTest::setCmdLineFlagOverrides();
    // Prevent neighbor advertisement traffic from generating stray packets.
    FLAGS_disable_neighbor_updates = true;
  }

 protected:
  void setSflowTruncation(bool truncate) {
    // Set truncation using shell command, since this is not supported by SDK.
    std::string out;
    // Start with a blank command first and then set the truncation
    // This is because, sometimes first diag command doesn't work in some SDK
    // versions and in FBOSS OSS
    for (const auto& [switchId, _] : getAsics()) {
      getAgentEnsemble()->runDiagCommand("\n", out, switchId);
    }
    for (int core = 0; core < 4; ++core) {
      for (const auto& [switchId, _] : getAsics()) {
        getAgentEnsemble()->runDiagCommand(
            fmt::format(
                "dbal entry commit table=SNIF_COMMAND_TABLE core_id={} "
                "snif_command_id=1 SNIF_TYPE=STATISTICAL_SAMPLE CROP_ENABLE={}\n",
                core,
                truncate ? "1" : "0"),
            out,
            switchId);
      }
    }
  }

  // Verify that packet count on a port remains constant for several iterations.
  void verifyNoTrafficOnPort(PortID portId, int iterations = 5) {
    auto statsBefore = getLatestPortStats(portId);
    int count = 0;
    WITH_RETRIES({
      auto statsAfter = getLatestPortStats(portId);
      XLOG(DBG0) << "Port " << portId
                 << " outPackets: " << *statsAfter.outUnicastPkts_();
      if (*statsBefore.outUnicastPkts_() == *statsAfter.outUnicastPkts_()) {
        ++count;
      } else {
        // Reset the count.
        statsBefore = statsAfter;
        count = 0;
      }
      EXPECT_EVENTUALLY_GE(count, iterations);
    });
  }

  // Stop traffic on ports by removing neighbor and adding it back.
  void stopTrafficOnAllPorts() {
    auto intfMac = getMacForFirstInterfaceWithPortsForTesting(
        getAgentEnsemble()->getProgrammedState());
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getAgentEnsemble()->getProgrammedState(),
        getAgentEnsemble()->getSw()->needL2EntryForNeighbor(),
        intfMac);
    for (auto portId : getAllPorts()) {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return ecmpHelper.unresolveNextHops(in, {PortDescriptor(portId)});
      });
    }
    verifyNoTrafficOnPort(getAllPorts()[0], 3 /*iterations*/);
    for (auto portId : getAllPorts()) {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return ecmpHelper.resolveNextHops(in, {PortDescriptor(portId)});
      });
    }
  }
};

TEST_F(AgentSflowMirrorEventorTest, VerifyEventorMtu) {
  constexpr int kNumPorts = 1;
  const PortID trafficLoopPortId = getAllPorts()[0];
  const PortID eventorPortId =
      masterLogicalPortIds({cfg::PortType::EVENTOR_PORT})[0];

  auto setup = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    configureMirrorWithSampling(config, 1 /*sampleRate*/);
    utility::setTTLZeroCpuConfig(getAgentEnsemble()->getL3Asics(), config);
    applyNewConfig(config);
    resolveRouteForMirrorDestination();
    utility::setupEcmpDataplaneLoopOnAllPorts(getAgentEnsemble());
  };
  auto verify = [=, this]() {
    // Turn off truncation, then start traffic loops on a port. We don't expect
    // sFlow egress traffic because packets should be blocked by MTU.
    setSflowTruncation(false);
    utility::createTrafficOnMultiplePorts(
        getAgentEnsemble(), kNumPorts, sendPacket, 1 /*desiredPctLineRate*/);
    verifyNoTrafficOnPort(eventorPortId);

    // Stop traffic before changing truncation setting.
    stopTrafficOnAllPorts();
    verifyNoTrafficOnPort(trafficLoopPortId);

    // Turn truncation back on and start traffic again. We expect sFlow to work
    // now, i.e. eventor port should not be broken by previous large packets.
    setSflowTruncation(true);
    utility::createTrafficOnMultiplePorts(
        getAgentEnsemble(), kNumPorts, sendPacket, 1 /*desiredPctLineRate*/);
    verifySflowEgressPortNotStuck(3 /* iterations */);

    // Turn off traffic so that truncation can be changed after warmboot.
    stopTrafficOnAllPorts();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentSflowMirrorAddressFamilySwitchingTest, MoveToV6) {
  const bool kIsV4 = true;
  // Test to migrate v4 mirror to v6
  auto setup = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    configureMirrorWithSampling(config, 1 /* sampleRate */, kIsV4);
    configureTrapAcl(config, kIsV4);
    applyNewConfig(config);
    resolveRouteForMirrorDestination(kIsV4);
  };
  auto verify = [=, this]() { verifySampledPacketWithTruncate(kIsV4); };
  auto setupPostWb = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    config.mirrors()->clear();
    /* move to v6 */
    configureMirrorWithSampling(config, 1 /* sampleRate */, !kIsV4);
    configureTrapAcl(config, !kIsV4);
    applyNewConfig(config);
    resolveRouteForMirrorDestination(!kIsV4);
  };
  auto verifyPostWb = [=, this]() { verifySampledPacketWithTruncate(!kIsV4); };
  verifyAcrossWarmBoots(setup, verify, setupPostWb, verifyPostWb);
}

TEST_F(AgentSflowMirrorAddressFamilySwitchingTest, MoveToV4) {
  const bool kIsV4 = true;
  // Test to migrate v6 mirror to v4
  auto setup = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    configureMirrorWithSampling(config, 1 /* sampleRate */, !kIsV4);
    configureTrapAcl(config, !kIsV4);
    applyNewConfig(config);
    resolveRouteForMirrorDestination(!kIsV4);
  };
  auto verify = [=, this]() { verifySampledPacketWithTruncate(!kIsV4); };
  auto setupPostWb = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    config.mirrors()->clear();
    /* move to v4 */
    configureMirrorWithSampling(config, 1 /* sampleRate */, kIsV4);
    configureTrapAcl(config, kIsV4);
    applyNewConfig(config);
    resolveRouteForMirrorDestination(kIsV4);
  };
  auto verifyPostWb = [=, this]() { verifySampledPacketWithTruncate(kIsV4); };
  verifyAcrossWarmBoots(setup, verify, setupPostWb, verifyPostWb);
}

TEST_F(AgentSflowMirrorTruncateV6Test, verifyL4SrcPortRandomization) {
  auto setup = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    configureMirrorWithSampling(
        config, 1 /* sampleRate */, false /* v4 */, 0 /* udpSrcPort */);
    configureTrapAcl(config);
    applyNewConfig(config);
    resolveRouteForMirrorDestination();
  };
  auto verify = [=, this]() {
    this->verifySrcPortRandomizationOnSflowPacket();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentSflowMirrorTruncateWithSamplesPackingTestV6,
    verifySflowSamplesPacking) {
  auto setup = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    configureMirrorWithSampling(config, 1 /*sampleRate*/);
    configureTrapAcl(config);
    applyNewConfig(config);
    resolveRouteForMirrorDestination();
  };
  auto verify = [=, this]() {
    this->verifySflowSamplePacking(kNumSflowSamplesPacked);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
