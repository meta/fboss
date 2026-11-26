// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/state/Mirror.h"
#include <fboss/agent/state/Thrifty.h>
#include <memory>
#include "common/network/if/gen-cpp2/Address_types.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/Port.h"
#include "folly/IPAddress.h"
#include "folly/MacAddress.h"
#include "folly/json/dynamic.h"

namespace facebook::fboss {

Mirror::Mirror(
    std::string name,
    std::optional<PortDescriptor> egressPortDesc,
    std::optional<folly::IPAddress> destinationIp,
    std::optional<folly::IPAddress> srcIp,
    std::optional<TunnelUdpPorts> udpPorts,
    uint8_t dscp,
    bool truncate,
    std::optional<uint32_t> samplingRate,
    std::optional<std::string> policerLabel)
    : ThriftStructNode<Mirror, state::MirrorFields>() {
  // span mirror is resolved as soon as it is created
  // erspan and sflow are resolved when tunnel is set
  set<switch_state_tags::name>(name);
  set<switch_state_tags::dscp>(dscp);
  set<switch_state_tags::truncate>(truncate);
  set<switch_state_tags::configHasEgressPort>(false);
  set<switch_state_tags::isResolved>(false);
  if (samplingRate.has_value()) {
    set<switch_state_tags::samplingRate>(samplingRate.value());
  }

  if (egressPortDesc.has_value()) {
    set<switch_state_tags::egressPortDesc>(egressPortDesc.value().toThrift());
    set<switch_state_tags::egressPort>(egressPortDesc.value().phyPortID());
    set<switch_state_tags::configHasEgressPort>(true);
  }
  if (destinationIp) {
    set<switch_state_tags::destinationIp>(
        network::toBinaryAddress(*destinationIp));
  }
  if (srcIp) {
    set<switch_state_tags::srcIp>(network::toBinaryAddress(*srcIp));
  }
  if (udpPorts) {
    set<switch_state_tags::udpSrcPort>(udpPorts->udpSrcPort);
    set<switch_state_tags::udpDstPort>(udpPorts->udpDstPort);
  }
  setPolicerLabel(std::move(policerLabel));
  markResolved();
}

std::string Mirror::getID() const {
  return get<switch_state_tags::name>()->cref();
}

std::optional<PortID> Mirror::getEgressPort() const {
  if (auto port = get<switch_state_tags::egressPort>()) {
    return PortID(port->cref());
  }
  return std::nullopt;
}

std::optional<PortDescriptor> Mirror::getEgressPortDesc() const {
  if (auto portDesc = get<switch_state_tags::egressPortDesc>()) {
    return PortDescriptor::fromThrift(portDesc->toThrift());
  }
  return std::nullopt;
}

std::optional<TunnelUdpPorts> Mirror::getTunnelUdpPorts() const {
  auto srcPort = get<switch_state_tags::udpSrcPort>();
  auto dstPort = get<switch_state_tags::udpDstPort>();
  if (srcPort && dstPort) {
    return TunnelUdpPorts(srcPort->cref(), dstPort->cref());
  }
  return std::nullopt;
}

std::optional<MirrorTunnel> Mirror::getMirrorTunnel() const {
  auto tunnel = get<switch_state_tags::tunnel>();
  if (tunnel) {
    return MirrorTunnel::fromThrift(tunnel->toThrift());
  }
  return std::nullopt;
}

uint8_t Mirror::getDscp() const {
  return get<switch_state_tags::dscp>()->cref();
}

bool Mirror::getTruncate() const {
  return get<switch_state_tags::truncate>()->cref();
}

void Mirror::setTruncate(bool truncate) {
  set<switch_state_tags::truncate>(truncate);
}

void Mirror::setEgressPort(PortID egressPort) {
  set<switch_state_tags::egressPort>(egressPort);
}

void Mirror::setEgressPortDesc(const PortDescriptor& egressPortDesc) {
  set<switch_state_tags::egressPortDesc>(egressPortDesc.toThrift());
}

void Mirror::setDestinationMac(const folly::MacAddress& dstMac) {
  auto tunnel = get<switch_state_tags::tunnel>();
  if (tunnel) {
    auto tunnelThrift = tunnel->toThrift();
    tunnelThrift.dstMac() = dstMac.toString();
    set<switch_state_tags::tunnel>(tunnelThrift);
  }
}

void Mirror::setMirrorTunnel(const MirrorTunnel& tunnel) {
  set<switch_state_tags::tunnel>(tunnel.toThrift());
  // sflow or erspan mirror is resolved.
  markResolved();
}

void Mirror::setSwitchId(SwitchID switchId) {
  set<switch_state_tags::switchId>(switchId);
}

SwitchID Mirror::getSwitchId() const {
  return static_cast<SwitchID>(cref<ctrl_if_tags::switchId>()->toThrift());
}

void Mirror::setMirrorName(const std::string& name) {
  set<switch_state_tags::name>(name);
}

bool Mirror::isResolved() const {
  // either mirror has no destination ip (which means it is span)
  // or its destination ip is resolved.
  return get<switch_state_tags::isResolved>()->cref();
}

void Mirror::markResolved() {
  set<switch_state_tags::isResolved>(false);
  if (type() == Mirror::Type::SPAN || get<switch_state_tags::tunnel>()) {
    // either mirror has no destination ip (which means it is span)
    // or its destination ip is resolved.
    set<switch_state_tags::isResolved>(true);
  }
}

bool Mirror::configHasEgressPort() const {
  return get<switch_state_tags::configHasEgressPort>()->cref();
}

std::optional<folly::IPAddress> Mirror::getDestinationIp() const {
  auto ip = get<switch_state_tags::destinationIp>();
  if (!ip) {
    return std::nullopt;
  }
  return network::toIPAddress(ip->toThrift());
}

std::optional<folly::IPAddress> Mirror::getSrcIp() const {
  auto ip = get<switch_state_tags::srcIp>();
  if (!ip) {
    return std::nullopt;
  }
  return network::toIPAddress(ip->toThrift());
}

Mirror::Type Mirror::type() const {
  if (!getDestinationIp()) {
    return Mirror::Type::SPAN;
  }
  if (!getTunnelUdpPorts()) {
    return Mirror::Type::ERSPAN;
  }
  return Mirror::Type::SFLOW;
}

std::optional<uint32_t> Mirror::getSamplingRate() const {
  if (auto samplingRate = get<switch_state_tags::samplingRate>()) {
    return samplingRate->cref();
  }
  return std::nullopt;
}

std::optional<std::string> Mirror::getPolicerLabel() const {
  if (auto label = get<switch_state_tags::policerLabel>()) {
    return label->cref();
  }
  return std::nullopt;
}

void Mirror::setPolicerLabel(std::optional<std::string> label) {
  if (label) {
    set<switch_state_tags::policerLabel>(std::move(*label));
  } else {
    remove<switch_state_tags::policerLabel>();
  }
}

template struct ThriftStructNode<Mirror, state::MirrorFields>;

} // namespace facebook::fboss
