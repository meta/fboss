// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/io/IOBuf.h>

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTestBase.h"
#include "fboss/agent/test/utils/PacketSnooper.h"

namespace facebook::fboss {

// Common fields extracted from a captured MirrorOnDrop packet.
struct MirrorOnDropPacketFields {
  // Outer headers
  folly::MacAddress outerSrcMac;
  folly::MacAddress outerDstMac;
  folly::IPAddressV6 outerSrcIp;
  folly::IPAddressV6 outerDstIp;
  uint16_t outerSrcPort{0};
  uint16_t outerDstPort{0};

  // Drop metadata
  uint16_t ingressPort{0};
  uint16_t dropReasonIngress{0};
  uint16_t dropReasonEgress{0};

  // Inner (sampled) packet
  std::optional<folly::MacAddress> innerSrcMac;
  std::optional<folly::MacAddress> innerDstMac;
  folly::IPAddressV6 innerSrcIp;
  folly::IPAddressV6 innerDstIp;
  uint16_t innerSrcPort{0};
  uint16_t innerDstPort{0};

  // Raw ASIC-specific metadata (for platform-specific assertions)
  std::map<std::string, uint64_t> asicMetadata;
};

// Captured drop-reason values placed into the wire format's two slots
// (ingress-pipeline drops vs egress/MMU drops). Constructed by the framework
// from the single ASIC-specific code returned by MirrorOnDropImpl.
struct MirrorOnDropDropReasonCodes {
  uint16_t ingressDropReason{0};
  uint16_t egressDropReason{0};
};

// Strategy interface: each stateless MirrorOnDrop platform provides an impl.
//
// To add support for a new ASIC:
// 1. Create AgentMirrorOnDrop<Vendor>Impl.{h,cpp} with a class implementing
//    MirrorOnDropImpl.
// 2. Add a new case to createMirrorOnDropImpl() in
//    AgentMirrorOnDropStatelessTest.cpp.
class MirrorOnDropImpl {
 public:
  virtual ~MirrorOnDropImpl() = default;

  // Build the ASIC-specific MirrorOnDropReport for this platform.
  virtual cfg::MirrorOnDropReport makeReport(
      const std::string& name,
      const folly::IPAddressV6& collectorIp,
      int16_t collectorPort,
      int16_t srcPort,
      const folly::IPAddressV6& switchIp,
      std::optional<int32_t> samplingRate) const = 0;

  // Deserialize a captured MirrorOnDrop packet into common fields.
  virtual MirrorOnDropPacketFields parsePacket(
      const folly::IOBuf* buf) const = 0;

  // Check ASIC-specific wire-format invariants
  // (e.g., IPFIX version for XGS).
  virtual void verifyInvariants(const folly::IOBuf* buf) const = 0;

  // ASIC-specific raw drop-reason code for each category. The framework
  // packs these into the ingress-vs-egress slot expected by the wire format.
  virtual uint16_t getDefaultRouteDropReason() const = 0;
  virtual uint16_t getAclDropReason() const = 0;
  virtual uint16_t getMmuDropReason() const = 0;
  // TODO: replace placeholder values with actual ASIC drop-reason codes
  // once available from the vendor SDK.
  virtual uint16_t getSrv6MidpointIsLastSidDropReason() const = 0;
  virtual uint16_t getSrv6DecapNonLastSegmentDropReason() const = 0;
  virtual uint16_t getSrv6BindingSidNonLastSidDropReason() const = 0;
  virtual uint16_t getSrv6MidpointUnresolvedDropReason() const = 0;
  // Egress drop when SRv6/tunnel header imposition pushes the packet past the
  // egress L3 MTU (L3_TX_MTU_FAILURE on Tajo).
  virtual uint16_t getSrv6EncapMtuExceededDropReason() const = 0;

  // Configure an ERSPAN (GRE tunnel) mirror used by the sampling test to
  // generate a high drop-rate packet loop.
  virtual void configureErspanMirror(
      cfg::SwitchConfig& config,
      const std::string& mirrorName,
      const folly::IPAddressV6& tunnelDstIp,
      const folly::IPAddressV6& tunnelSrcIp,
      const PortID& srcPortId) const = 0;

  // Production feature gating tests on hardware that supports this impl.
  virtual ProductionFeature getProductionFeature() const = 0;

  // Snooper RX filter for this platform's MoD export. Tajo shares the CPU
  // punt path with unrelated control traffic, so it filters to MoD packets by
  // their punt-header signature; other platforms accept all packets.
  virtual utility::packetSnooperReceivePacketType snooperReceivePacketType()
      const {
    return utility::packetSnooperReceivePacketType::PACKET_TYPE_ALL;
  }
};

// Factory: returns the right impl for the given AsicType. This is the only
// switch in the framework — adding a new ASIC means adding one case here.
// Throws FbossError if the AsicType is not a supported stateless MoD platform.
std::unique_ptr<MirrorOnDropImpl> createMirrorOnDropImpl(cfg::AsicType type);

// Concrete test class for stateless MirrorOnDrop platforms. Holds a
// polymorphic MirrorOnDropImpl that's lazily instantiated based on the
// hardware ASIC type. Each dispatch method delegates to the strategy.
//
// DNX tests are NOT part of this hierarchy — they use a fundamentally
// different MirrorOnDrop architecture (event map, aging groups,
// recirculation ports).
class AgentMirrorOnDropStatelessTest : public AgentMirrorOnDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified() const override;

 protected:
  // Lazily instantiate the strategy implementation based on the hardware
  // ASIC type detected at runtime.
  MirrorOnDropImpl* impl();

  // ===== STRATEGY DISPATCH METHODS =====
  // Thin wrappers that delegate to impl().

  cfg::MirrorOnDropReport makeMirrorOnDropReport(
      const std::string& name,
      std::optional<int32_t> samplingRate = std::nullopt);

  MirrorOnDropPacketFields parseMirrorOnDropPacket(const folly::IOBuf* buf);

  void verifyAsicSpecificInvariants(const folly::IOBuf* buf);

  // Build the full {ingress, egress} drop-reason struct expected on the wire
  // for each scenario. Default-route and ACL drops are ingress-pipeline; MMU
  // drops land in the egress slot.
  MirrorOnDropDropReasonCodes getDefaultRouteDropReasons();
  MirrorOnDropDropReasonCodes getAclDropReasons();
  MirrorOnDropDropReasonCodes getMmuDropReasons();
  MirrorOnDropDropReasonCodes getSrv6MidpointIsLastSidDropReason();
  MirrorOnDropDropReasonCodes getSrv6DecapNonLastSegmentDropReasons();
  MirrorOnDropDropReasonCodes getSrv6BindingSidNonLastSidDropReasons();
  MirrorOnDropDropReasonCodes getSrv6MidpointUnresolvedDropReasons();
  MirrorOnDropDropReasonCodes getSrv6EncapMtuExceededDropReasons();

  void configureErspanMirror(
      cfg::SwitchConfig& config,
      const std::string& mirrorName,
      const folly::IPAddressV6& tunnelDstIp,
      const folly::IPAddressV6& tunnelSrcIp,
      const PortID& srcPortId);

  // ===== TEMPLATE TEST METHODS =====

  // Send a packet with no matching route and verify MirrorOnDrop captures it.
  void testDefaultRouteDrop();

  // Add a deny ACL, send a matching packet, and verify capture.
  void testAclDrop();

  // Exhaust MMU buffers by disabling TX, and verify capture.
  void testMmuDrop();

  // Coldboot without MoD; add MoD with sampling post-warmboot and verify.
  void testWarmbootEnableSampling(int samplingRate = 90000);

  // Coldboot with MoD sampling; remove post-warmboot and verify absence.
  void testWarmbootDisableSampling(int samplingRate = 90000);

  // ===== COMMON UTILITIES =====

  void validateMirrorOnDropPacket(
      const folly::IOBuf* captured,
      const PortID& injectionPortId,
      const MirrorOnDropDropReasonCodes& expectedReasons,
      std::optional<folly::IPAddressV6> expectedInnerDstIp = std::nullopt,
      std::optional<folly::IPAddressV6> expectedInnerSrcIp = std::nullopt);

  // Wait until outUnicastPkts on every port stabilizes across 3 iterations.
  void waitForStatsToStabilize(const std::vector<PortID>& ports);

 private:
  uint32_t getExpectedModPacketIngressPortId(PortID port) const;

  // Shared body of the two warmboot toggle tests above. enablePostWarmboot
  // selects the direction: true = coldboot empty → warmboot adds the report;
  // false = coldboot has the report → warmboot removes it.
  void testWarmbootToggleSampling(bool enablePostWarmboot, int samplingRate);

  std::unique_ptr<MirrorOnDropImpl> impl_;
};

} // namespace facebook::fboss
