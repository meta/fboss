// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTest.h"

namespace facebook::fboss {

// XGS (Tomahawk5, Tomahawk6) MirrorOnDrop Strategy implementation.
//
// XGS MirrorOnDrop format:
//   - Tunnel-based MirrorDestination with srcIp.
//   - IPFIX/PSAMP wire format for the captured packet.
//   - Drop reason codes defined in BCM SDK (e.g. 0x1A = L3 destination
//     discard, 0x10 = ingress FP/ACL drop). MMU codes vary by ASIC.
class XgsMirrorOnDropImpl : public MirrorOnDropImpl {
 public:
  explicit XgsMirrorOnDropImpl(cfg::AsicType asicType) : asicType_(asicType) {}

  cfg::MirrorOnDropReport makeReport(
      const std::string& name,
      const folly::IPAddressV6& collectorIp,
      int16_t collectorPort,
      int16_t srcPort,
      const folly::IPAddressV6& switchIp,
      std::optional<int32_t> samplingRate) const override;

  MirrorOnDropPacketFields parsePacket(const folly::IOBuf* buf) const override;

  void verifyInvariants(const folly::IOBuf* buf) const override;

  uint16_t getDefaultRouteDropReason() const override;
  uint16_t getAclDropReason() const override;
  uint16_t getMmuDropReason() const override;
  uint16_t getSrv6MidpointIsLastSidDropReason() const override;
  uint16_t getSrv6DecapNonLastSegmentDropReason() const override;
  uint16_t getSrv6BindingSidNonLastSidDropReason() const override;
  uint16_t getSrv6MidpointUnresolvedDropReason() const override;
  uint16_t getSrv6EncapMtuExceededDropReason() const override;

  void configureErspanMirror(
      cfg::SwitchConfig& config,
      const std::string& mirrorName,
      const folly::IPAddressV6& tunnelDstIp,
      const folly::IPAddressV6& tunnelSrcIp,
      const PortID& srcPortId) const override;

  ProductionFeature getProductionFeature() const override;

 private:
  cfg::AsicType asicType_;
};

} // namespace facebook::fboss
