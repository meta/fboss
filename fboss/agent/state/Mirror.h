// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <list>
#include <set>

#include <folly/IPAddress.h>
#include <memory>
#include <optional>
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/types.h"
#include "folly/MacAddress.h"

namespace facebook::fboss {

enum class MirrorDirection { INGRESS = 1, EGRESS = 2 };
enum class MirrorAction { START = 1, STOP = 2 };

struct TunnelUdpPorts {
  uint32_t udpSrcPort;
  uint32_t udpDstPort;
  TunnelUdpPorts(uint32_t src, uint32_t dst)
      : udpSrcPort(src), udpDstPort(dst) {}
  bool operator==(const TunnelUdpPorts& that) const {
    return udpSrcPort == that.udpSrcPort && udpDstPort == that.udpDstPort;
  }
  bool operator<(const TunnelUdpPorts& that) const {
    return std::tie(udpSrcPort, udpDstPort) <
        std::tie(that.udpSrcPort, that.udpDstPort);
  }
};

struct MirrorTunnel {
  folly::IPAddress srcIp, dstIp;
  folly::MacAddress srcMac, dstMac;
  std::optional<TunnelUdpPorts> udpPorts;
  uint8_t ttl;
  static constexpr auto kTTL = 127;

  MirrorTunnel(
      const folly::IPAddress& srcIp,
      const folly::IPAddress& dstIp,
      const folly::MacAddress& srcMac,
      const folly::MacAddress& dstMac,
      uint8_t ttl = kTTL)
      : srcIp(srcIp),
        dstIp(dstIp),
        srcMac(srcMac),
        dstMac(dstMac),
        udpPorts(std::nullopt),
        ttl(ttl) {}

  MirrorTunnel(
      const folly::IPAddress& srcIp,
      const folly::IPAddress& dstIp,
      const folly::MacAddress& srcMac,
      const folly::MacAddress& dstMac,
      const TunnelUdpPorts& sflowPorts,
      uint8_t ttl = kTTL)
      : srcIp(srcIp),
        dstIp(dstIp),
        srcMac(srcMac),
        dstMac(dstMac),
        udpPorts(sflowPorts),
        ttl(ttl) {}

  bool operator==(const MirrorTunnel& rhs) const {
    return srcIp == rhs.srcIp && dstIp == rhs.dstIp && srcMac == rhs.srcMac &&
        dstMac == rhs.dstMac && udpPorts == rhs.udpPorts && ttl == rhs.ttl;
  }

  bool operator<(const MirrorTunnel& rhs) const {
    return std::tie(srcIp, dstIp, srcMac, dstMac, udpPorts, ttl) <
        std::tie(
               rhs.srcIp,
               rhs.dstIp,
               rhs.srcMac,
               rhs.dstMac,
               rhs.udpPorts,
               rhs.ttl);
  }

  state::MirrorTunnel toThrift() const {
    state::MirrorTunnel tunnel{};

    tunnel.srcIp() = network::toBinaryAddress(srcIp);
    tunnel.dstIp() = network::toBinaryAddress(dstIp);
    tunnel.srcMac() = srcMac.toString();
    tunnel.dstMac() = dstMac.toString();
    if (udpPorts) {
      tunnel.udpSrcPort() = udpPorts->udpSrcPort;
      tunnel.udpDstPort() = udpPorts->udpDstPort;
    }
    tunnel.ttl() = ttl;
    return tunnel;
  }

  static MirrorTunnel fromThrift(const state::MirrorTunnel& tunnel) {
    auto srcL4Port = tunnel.udpSrcPort();
    auto dstL4Port = tunnel.udpDstPort();
    if (srcL4Port && dstL4Port) {
      return MirrorTunnel(
          network::toIPAddress(*tunnel.srcIp()),
          network::toIPAddress(*tunnel.dstIp()),
          folly::MacAddress(*tunnel.srcMac()),
          folly::MacAddress(*tunnel.dstMac()),
          TunnelUdpPorts(*srcL4Port, *dstL4Port),
          *tunnel.ttl()

      );
    }
    return MirrorTunnel(
        network::toIPAddress(*tunnel.srcIp()),
        network::toIPAddress(*tunnel.dstIp()),
        folly::MacAddress(*tunnel.srcMac()),
        folly::MacAddress(*tunnel.dstMac()),
        *tunnel.ttl());
  }
};

USE_THRIFT_COW(Mirror);

class Mirror : public ThriftStructNode<Mirror, state::MirrorFields> {
 public:
  using BaseT = ThriftStructNode<Mirror, state::MirrorFields>;
  Mirror(
      std::string name,
      std::optional<PortDescriptor> egressPortDesc,
      std::optional<folly::IPAddress> destinationIp,
      std::optional<folly::IPAddress> srcIp = std::nullopt,
      std::optional<TunnelUdpPorts> udpPorts = std::nullopt,
      uint8_t dscp = cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_,
      bool truncate = false,
      std::optional<uint32_t> samplingRate = std::nullopt,
      std::optional<std::string> policerLabel = std::nullopt);
  enum Type { SPAN = 1, ERSPAN = 2, SFLOW = 3 };
  std::string getID() const;
  std::optional<folly::IPAddress> getDestinationIp() const;
  std::optional<PortID> getEgressPort() const;
  std::optional<folly::IPAddress> getSrcIp() const;
  std::optional<TunnelUdpPorts> getTunnelUdpPorts() const;
  std::optional<MirrorTunnel> getMirrorTunnel() const;
  uint8_t getDscp() const;
  bool getTruncate() const;
  void setTruncate(bool truncate);
  void setEgressPort(PortID egressPort);
  void setMirrorTunnel(const MirrorTunnel& tunnel);
  void setSwitchId(SwitchID switchId);
  void setDestinationMac(const folly::MacAddress& dstMac);
  SwitchID getSwitchId() const;
  void setMirrorName(const std::string& name);
  bool configHasEgressPort() const;
  bool isResolved() const;
  void setEgressPortDesc(const PortDescriptor& egressPortDesc);
  std::optional<PortDescriptor> getEgressPortDesc() const;
  std::optional<uint32_t> getSamplingRate() const;
  std::optional<std::string> getPolicerLabel() const;
  void setPolicerLabel(std::optional<std::string> label);

  Type type() const;

  // TODO(pshaikh): make this private
  void markResolved();

 private:
  // Inherit the constructors required for clone()

  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
