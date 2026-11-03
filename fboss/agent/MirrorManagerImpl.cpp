// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/MirrorManagerImpl.h"

#include "folly/IPAddress.h"

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {

folly::MacAddress getEventorPortInterfaceMac(
    const std::shared_ptr<SwitchState>& state,
    PortID portId) {
  auto intfID = state->getInterfaceIDForPort(PortDescriptor(portId));
  auto intf = state->getInterfaces()->getNodeIf(intfID);
  return intf->getMac();
}

bool isEgressPortOnSwitch(
    const SwitchIdScopeResolver* resolver,
    const PortDescriptor& egressPort,
    SwitchID switchId) {
  if (!egressPort.isPhysicalPort()) {
    return true;
  }
  return resolver->scope(egressPort.phyPortID()).has(switchId);
}

} // namespace
template <typename AddrT>
MirrorManagerImpl<AddrT>::MirrorManagerImpl(SwSwitch* sw)
    : sw_(sw), destinationAddrResolver_(sw) {}

template <typename AddrT>
PortID MirrorManagerImpl<AddrT>::getEventorPortForSflowMirror(
    SwitchID switchId) {
  const auto& portIds =
      sw_->getPlatformMapping()->getPlatformPorts(cfg::PortType::EVENTOR_PORT);
  HwSwitchMatcher matcher(std::unordered_set<SwitchID>({switchId}));
  for (const auto& portId : portIds) {
    if (sw_->getScopeResolver()->scope(portId) == matcher) {
      return portId;
    }
  }
  throw FbossError("No eventor port found for sflow mirror");
}

template <typename AddrT>
std::shared_ptr<Mirror> MirrorManagerImpl<AddrT>::updateMirror(
    const std::shared_ptr<Mirror>& mirror) {
  bool isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
  const AddrT destinationIp =
      getIPAddress<AddrT>(mirror->getDestinationIp().value());
  const auto state = sw_->getState();

  // Use NextHopResolver for route lookup
  const auto nexthops =
      destinationAddrResolver_.resolveNextHops(state, destinationIp);

  std::optional<PortDescriptor> egressPortDesc = std::nullopt;
  if (mirror->configHasEgressPort()) {
    egressPortDesc = PortDescriptor(mirror->getEgressPort().value());
  }
  auto newMirror = std::make_shared<Mirror>(
      mirror->getID(),
      egressPortDesc,
      mirror->getDestinationIp(),
      mirror->getSrcIp(),
      mirror->getTunnelUdpPorts(),
      mirror->getDscp(),
      mirror->getTruncate(),
      mirror->getSamplingRate());
  newMirror->setSwitchId(mirror->getSwitchId());

  auto asic = sw_->getHwAsicTable()->getHwAsic(mirror->getSwitchId());
  const bool useEventorPort = newMirror->type() == Mirror::Type::SFLOW &&
      asic->isSupported(HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW);
  // A HwSwitch can only program a mirror whose physical egress port it owns.
  // Mirrors are replicated to every switch, so skip next hops on another
  // switch's ports unless the egress port is overridden below.
  const bool checkEgressPortScope =
      !useEventorPort && FLAGS_sflow_egress_port_id <= 0;

  for (const auto& nexthop : nexthops) {
    // Dont consider nextHops that do not match incoming Address family
    if (isV4 != nexthop.addr().isV4()) {
      continue;
    }

    // Use NextHopResolver for neighbor lookup
    const auto entry = destinationAddrResolver_.resolveNextHopNeighbor(
        state, destinationIp, nexthop);

    if (!entry || entry->zeroPort()) {
      // unresolved next hop
      continue;
    }
    auto neighborPort = entry->getPort();
    if (mirror->configHasEgressPort()) {
      egressPortDesc = mirror->getEgressPortDesc().value();
      if (!neighborPort.isPhysicalPort() ||
          neighborPort.phyPortID() != egressPortDesc->phyPortID()) {
        // TODO: support configuring LAG egress for mirror
        continue;
      }
    }

    // Use NextHopResolver for egress port resolution
    auto resolvedEgressPort =
        destinationAddrResolver_.resolveEgressPort(state, entry);

    if (!resolvedEgressPort) {
      continue;
    }

    if (checkEgressPortScope &&
        !isEgressPortOnSwitch(
            sw_->getScopeResolver(),
            resolvedEgressPort.value(),
            mirror->getSwitchId())) {
      continue;
    }

    std::optional<AddrT> srcIp = newMirror->getSrcIp()
        ? getIPAddress<AddrT>(mirror->getSrcIp().value())
        : std::optional<AddrT>(std::nullopt);
    newMirror->setMirrorTunnel(resolveMirrorTunnel(
        state,
        destinationIp,
        srcIp,
        nexthop,
        entry,
        newMirror->getTunnelUdpPorts()));
    newMirror->setEgressPortDesc(resolvedEgressPort.value());
    break;
  }

  if (useEventorPort) {
    auto eventorPort = getEventorPortForSflowMirror(mirror->getSwitchId());
    auto eventorMac = getEventorPortInterfaceMac(state, eventorPort);
    XLOG(DBG2) << "updateMirror: switchId=" << mirror->getSwitchId()
               << " eventorPort=" << eventorPort << " prevEgressPort="
               << (newMirror->getEgressPortDesc().has_value()
                       ? newMirror->getEgressPortDesc()->str()
                       : "none");
    newMirror->setEgressPortDesc(PortDescriptor(eventorPort));
    newMirror->setDestinationMac(eventorMac);
  }
  // For overriding the egress port in tests.
  if (FLAGS_sflow_egress_port_id > 0) {
    PortID egressPort(FLAGS_sflow_egress_port_id);
    newMirror->setEgressPortDesc(PortDescriptor(egressPort));
    newMirror->setDestinationMac(getEventorPortInterfaceMac(state, egressPort));
  }

  if (*mirror == *newMirror) {
    return std::shared_ptr<Mirror>(nullptr);
  }

  XLOG(DBG2) << "updateMirror: CHANGED mirror=" << mirror->getID()
             << " switchId=" << mirror->getSwitchId() << " oldEgressPort="
             << (mirror->getEgressPortDesc().has_value()
                     ? mirror->getEgressPortDesc()->str()
                     : "none")
             << " newEgressPort="
             << (newMirror->getEgressPortDesc().has_value()
                     ? newMirror->getEgressPortDesc()->str()
                     : "none");
  return newMirror;
}

template <typename AddrT>
MirrorTunnel MirrorManagerImpl<AddrT>::resolveMirrorTunnel(
    const std::shared_ptr<SwitchState>& state,
    const AddrT& destinationIp,
    const std::optional<AddrT>& srcIp,
    const NextHop& nextHop,
    const std::shared_ptr<NeighborEntryT>& neighbor,
    const std::optional<TunnelUdpPorts>& udpPorts) {
  InterfaceID mirrorEgressInterface = nextHop.intf();
  // Use NextHopResolver for interface lookup
  auto interface =
      destinationAddrResolver_.getInterface(state, mirrorEgressInterface);
  const auto iter = interface->getAddressToReach(neighbor->getIP());

  if (udpPorts.has_value()) {
    return MirrorTunnel(
        srcIp ? srcIp.value() : iter->first,
        destinationIp,
        interface->getMac(),
        neighbor->getMac(),
        udpPorts.value());
  }
  return MirrorTunnel(
      srcIp ? srcIp.value() : iter->first,
      destinationIp,
      interface->getMac(),
      neighbor->getMac());
}

template class MirrorManagerImpl<folly::IPAddressV4>;
template class MirrorManagerImpl<folly::IPAddressV6>;

} // namespace facebook::fboss
