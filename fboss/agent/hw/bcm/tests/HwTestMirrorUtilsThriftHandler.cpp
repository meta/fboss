// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/hw/bcm/BcmMirror.h"
#include "fboss/agent/hw/bcm/BcmMirrorTable.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"

#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"

namespace facebook::fboss::utility {

using MirrorTraverseArgT = std::pair<bcm_mirror_destination_t*, int>;

static int traverse(
    int /* unit */,
    bcm_mirror_destination_t* mirror_dest,
    void* user_data) {
  MirrorTraverseArgT* args = static_cast<MirrorTraverseArgT*>(user_data);
  bcm_mirror_destination_t* buffer = args->first;
  int handle = args->second;
  if (mirror_dest->mirror_dest_id == handle) {
    std::memcpy(buffer, mirror_dest, sizeof(bcm_mirror_destination_t));
  }
  return 0;
}

bool HwTestThriftHandler::isMirrorProgrammed(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    throw FbossError("isMirrorProgrammed: mirror is null");
  }
  if (!folly::copy(mirror->isResolved().value())) {
    throw FbossError("isMirrorProgrammed: mirror is not resolved");
  }

  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  const auto* bcmMirrorTable = bcmSwitch->getBcmMirrorTable();
  std::string& mirrorName{mirror->name().value()};
  auto* bcmMirror = bcmMirrorTable->getNodeIf(mirrorName);
  if (!bcmMirror || !bcmMirror->isProgrammed()) {
    return false;
  }

  auto handle = bcmMirror->getHandle().value();
  bcm_mirror_destination_t mirror_dest;
  bcm_mirror_destination_t_init(&mirror_dest);
  MirrorTraverseArgT args = std::make_pair(&mirror_dest, handle);

  bcm_mirror_destination_traverse(0, &traverse, &args);
  if (mirror_dest.mirror_dest_id != handle) {
    return false;
  }

  bcm_gport_t gport;
  if (auto cfgPortDesc = mirror->egressPortDesc()) {
    auto egressPort =
        PortDescriptor::fromCfgCfgPortDescriptor(*cfgPortDesc).phyPortID();
    BCM_GPORT_MODPORT_SET(gport, bcmSwitch->getUnit(), egressPort);
    if (mirror_dest.gport != gport) {
      return false;
    }
  } else {
    return false;
  }

  // SPAN tests are excluded because Tos/DSCP is not set and is not relevant for
  // local port mirroring because the packet is not sent to a remote host.
  if (mirrorName != utility::kIngressSpan &&
      mirrorName != utility::kEgressSpan &&
      mirror_dest.tos != mirror->dscp().value()) {
    return false;
  }
  if (bool(mirror_dest.truncate & BCM_MIRROR_PAYLOAD_TRUNCATE) !=
      folly::copy(mirror->truncate().value())) {
    return false;
  }
  auto tunnel = apache::thrift::get_pointer(mirror->tunnel());
  if (!tunnel) {
    if (mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_SFLOW) {
      return false;
    }
    if (mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_IP_GRE) {
      return false;
    }
    return true;
  }

  auto dstPort = apache::thrift::get_pointer(tunnel->udpDstPort());
  auto srcPort = apache::thrift::get_pointer(tunnel->udpSrcPort());
  if (mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_SFLOW) {
    if (!srcPort || !dstPort) {
      return false;
    }
    if (mirror_dest.udp_src_port != *srcPort ||
        mirror_dest.udp_dst_port != *dstPort) {
      return false;
    }
  } else if (!(mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_IP_GRE)) {
    return false;
  }

  auto srcIp = network::toIPAddress(tunnel->srcIp().value());
  auto dstIp = network::toIPAddress(tunnel->dstIp().value());
  if (mirror_dest.version == 4) {
    if (!srcIp.isV4() || !dstIp.isV4()) {
      return false;
    }
    if (srcIp != folly::IPAddress::fromLongHBO(mirror_dest.src_addr) ||
        dstIp != folly::IPAddress::fromLongHBO(mirror_dest.dst_addr)) {
      return false;
    }
  } else if (mirror_dest.version == 6) {
    if (!srcIp.isV6() || !dstIp.isV6()) {
      return false;
    }
    if (srcIp !=
            folly::IPAddress::fromBinary(
                folly::ByteRange(mirror_dest.src6_addr, 16)) ||
        dstIp !=
            folly::IPAddress::fromBinary(
                folly::ByteRange(mirror_dest.dst6_addr, 16))) {
      return false;
    }
  }

  if (folly::MacAddress(tunnel->srcMac().value()) !=
          macFromBcm(mirror_dest.src_mac) ||
      folly::MacAddress(tunnel->dstMac().value()) !=
          macFromBcm(mirror_dest.dst_mac)) {
    return false;
  }
  if (folly::copy(tunnel->ttl().value()) != mirror_dest.ttl) {
    return false;
  }
  return true;
}

bool HwTestThriftHandler::isPortMirrored(
    int32_t port,
    std::unique_ptr<std::string> mirror,
    bool ingress) {
  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  bcm_gport_t port_mirror_dest_id = 0;
  int count = 0;
  bcm_mirror_port_dest_get(
      bcmSwitch->getUnit(),
      port,
      ingress ? BCM_MIRROR_PORT_INGRESS : BCM_MIRROR_PORT_EGRESS,
      1,
      &port_mirror_dest_id,
      &count);

  if (count == 0) {
    return false;
  }

  const auto* bcmMirrorTable = bcmSwitch->getBcmMirrorTable();
  auto* bcmMirror = bcmMirrorTable->getNodeIf(*mirror);

  if (!bcmMirror) {
    return false;
  }
  return (port_mirror_dest_id == bcmMirror->getHandle());
}

bool HwTestThriftHandler::isPortSampled(
    int32_t /*port*/,
    std::unique_ptr<std::string> /*mirror*/,
    bool /*ingress*/) {
  throw FbossError("isPortSampled not implemented");
}

bool HwTestThriftHandler::isAclEntryMirrored(
    std::unique_ptr<std::string> /*aclEntry*/,
    std::unique_ptr<std::string> /*mirror*/,
    bool /*ingress*/) {
  throw FbossError("isAclEntryMirrored not implemented");
}
/*
 * Verify that the mirror is programmed correctly. This function is used to test
 * the mirror programming in the SDK. The mirror is expected to be resolved and
 * programmed in the SDK.
 * @param mirror MirrorFields object
 * @return true if the mirror is programmed correctly, false otherwise
 */
bool HwTestThriftHandler::verifyResolvedMirror(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    XLOG(ERR) << "verifyResolvedMirror: Input MirrorFields is null";
    return false;
  }

  if (!mirror->isResolved().value()) {
    XLOG(DBG2) << "verifyResolvedMirror: " << mirror->name().value()
               << " is not resolved";
    return false;
  }

  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  const auto* bcmMirrorTable = bcmSwitch->getBcmMirrorTable();
  auto* bcmMirror = bcmMirrorTable->getNodeIf(mirror->name().value());
  if (!bcmMirror) {
    XLOG(ERR) << "BcmMirror is null";
    return false;
  }

  if (!bcmMirror->isProgrammed()) {
    XLOG(ERR) << "BcmMirror is not programmed";
    return false;
  }
  auto handle = bcmMirror->getHandle().value();
  bcm_mirror_destination_t mirror_dest;
  bcm_mirror_destination_t_init(&mirror_dest);
  MirrorTraverseArgT args = std::make_pair(&mirror_dest, handle);

  bcm_mirror_destination_traverse(0, &traverse, &args);
  if (mirror_dest.mirror_dest_id != handle) {
    XLOG(ERR) << "mirror_dest.mirror_dest_id: " << mirror_dest.mirror_dest_id
              << " handle: " << handle;
    return false;
  }

  bcm_gport_t gport;
  if (mirror->egressPortDesc().value().portType().value() !=
      cfg::PortDescriptorType::Physical) {
    XLOG(ERR) << "Egress port type is not Physical as expected";
    return false;
  }
  auto egressPort = mirror->egressPortDesc().value().portId().value();
  BCM_GPORT_MODPORT_SET(gport, bcmSwitch->getUnit(), egressPort);

  if (mirror_dest.gport != gport) {
    XLOG(ERR) << "mirror_dest.gport: " << mirror_dest.gport
              << " egressPort: " << egressPort;
    return false;
  }

  if (mirror_dest.tos != mirror->dscp().value()) {
    XLOG(ERR) << "mirror_dest.tos: " << mirror_dest.tos
              << " dscp: " << mirror->dscp().value();
    return false;
  }

  if (auto truncateOpt = mirror->truncate();
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          truncateOpt)) {
    if (bool(mirror_dest.truncate & BCM_MIRROR_PAYLOAD_TRUNCATE) !=
        truncateOpt.value()) {
      XLOG(ERR) << "mirror_dest.truncate: " << mirror_dest.truncate
                << " Expected value: " << truncateOpt.value();
      return false;
    }
  }

  if (!mirror->tunnel().has_value()) {
    XLOG(DBG2) << "Mirror Tunnel is not present";
    return true;
  }
  const auto& tunnel = mirror->tunnel().value();

  // Sflow tunnels carry UDP src/dst ports (both set together by
  // cfg::SflowTunnel); ERSPAN tunnels use GRE and leave both optionals unset.
  // Require both to be present before treating this as the sflow encap so we
  // never dereference an unset optional below.
  if (tunnel.udpSrcPort().has_value() && tunnel.udpDstPort().has_value()) {
    if (!(mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_SFLOW)) {
      XLOG(ERR) << "mirror_dest flags not set to BCM_MIRROR_DEST_TUNNEL_SFLOW";
      return false;
    }
    int16_t udpDstPort = tunnel.udpDstPort().value();
    if (udpDstPort != mirror_dest.udp_dst_port) {
      XLOG(ERR) << "Actual Value of udpDstPort: " << udpDstPort
                << " Expected: " << mirror_dest.udp_dst_port;
      return false;
    }
    int16_t udpSrcPort = tunnel.udpSrcPort().value();
    if (udpSrcPort != mirror_dest.udp_src_port) {
      XLOG(ERR) << "Actual Value of udpSrcPort: " << udpSrcPort
                << " Expected: " << mirror_dest.udp_src_port;
      return false;
    }
  } else {
    if (!(mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_IP_GRE)) {
      XLOG(ERR) << "mirror_dest flags not set to BCM_MIRROR_DEST_TUNNEL_IP_GRE";
      return false;
    }
  }

  return true;
}

bool HwTestThriftHandler::verifyUnResolvedMirror(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    XLOG(ERR) << "verifyUnResolvedMirror: Input MirrorFields is null";
    return false;
  }
  if (mirror->isResolved().value()) {
    XLOG(ERR) << "verifyUnResolvedMirror: " << mirror->name().value()
              << " is resolved";
    return false;
  }

  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  const auto* bcmMirrorTable = bcmSwitch->getBcmMirrorTable();
  auto* bcmMirror = bcmMirrorTable->getNodeIf(mirror->name().value());
  if (!bcmMirror) {
    XLOG(ERR) << "BcmMirror is null";
    return false;
  }
  if (bcmMirror->isProgrammed()) {
    XLOG(ERR) << "BcmMirror is programmed";
    return false;
  }
  return true;
}

bool HwTestThriftHandler::verifyPortMirrorDestination(
    int32_t port,
    int32_t flags,
    int64_t mirrorDestID) {
  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  bcm_gport_t port_mirror_dest_id = 0;
  int count = 0;
  bcm_mirror_port_dest_get(
      bcmSwitch->getUnit(), port, flags, 1, &port_mirror_dest_id, &count);

  if (!count) {
    XLOG(ERR) << "Mirror port destination count is 0";
    return false;
  }

  if (mirrorDestID != port_mirror_dest_id) {
    XLOG(ERR) << "mirrorDestID is expected " << mirrorDestID
              << "but Actual value : " << port_mirror_dest_id;
    return false;
  }
  return true;
}

bool HwTestThriftHandler::verifyPortNoMirrorDestination(
    int32_t port,
    int32_t flags) {
  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  bcm_gport_t port_mirror_dest_id = 0;
  int count = 0;
  bcm_mirror_port_dest_get(
      bcmSwitch->getUnit(), port, flags, 1, &port_mirror_dest_id, &count);
  if (count) {
    XLOG(ERR) << "Mirror port destination count is " << count << ", expected 0";
    return false;
  }

  if (port_mirror_dest_id != 0) {
    XLOG(ERR) << "port_mirror_dest_id expected value 0  but Actual value "
              << port_mirror_dest_id;

    return false;
  }
  return true;
}

void HwTestThriftHandler::getAllMirrorDestinations(
    ::std::vector<int64_t>& destinations) {
  auto traverse = [&destinations](bcm_mirror_destination_t* destination) {
    destinations.push_back(destination->mirror_dest_id);
  };
  auto callback = [](int /*unit*/,
                     bcm_mirror_destination_t* destination,
                     void* closure) -> int {
    (*static_cast<decltype(traverse)*>(closure))(destination);
    return 0;
  };
  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  bcm_mirror_destination_traverse(bcmSwitch->getUnit(), callback, &traverse);
}

bool HwTestThriftHandler::isMirrorSflowTunnelEnabled(int64_t destination) {
  bcm_mirror_destination_t mirror_dest;
  bcm_mirror_destination_t_init(&mirror_dest);
  BcmSwitch* bcmSwitch = static_cast<BcmSwitch*>(hwSwitch_);
  bcm_mirror_destination_get(
      bcmSwitch->getUnit(),
      static_cast<bcm_gport_t>(destination),
      &mirror_dest);
  return mirror_dest.flags & BCM_MIRROR_DEST_TUNNEL_SFLOW;
}

} // namespace facebook::fboss::utility
