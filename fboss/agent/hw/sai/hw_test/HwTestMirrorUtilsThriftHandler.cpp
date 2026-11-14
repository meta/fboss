// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/StateUtils.h"

namespace facebook::fboss::utility {

namespace {

#define MIRROR_PORT_INGRESS 0x00000001
#define MIRROR_PORT_EGRESS 0x00000002
#define MIRROR_PORT_SFLOW 0x00000004

bool verifyResolvedLocalMirror(
    const SaiSwitch* saiSwitch,
    const state::MirrorFields& mirror,
    SaiMirrorHandle* mirrorHandle) {
  auto egressPort =
      PortDescriptor::fromCfgCfgPortDescriptor(*mirror.egressPortDesc())
          .phyPortID();
  auto portHandle = saiSwitch->managerTable()->portManager().getPortHandle(
      PortID(egressPort));
  if (!portHandle) {
    XLOG(ERR) << "Port " << egressPort << " not found";
    return false;
  }
  auto monitorPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiLocalMirrorTraits::Attributes::MonitorPort());
  if (portHandle->port->adapterKey() != monitorPort) {
    XLOG(ERR) << "Monitor port mismatch for port " << egressPort;
    return false;
  }

  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiLocalMirrorTraits::Attributes::Type());
  if (type != SAI_MIRROR_SESSION_TYPE_LOCAL) {
    XLOG(ERR) << "Mirror type mismatch, expected LOCAL";
    return false;
  }
  return true;
}

bool verifyResolvedMirror(
    const SaiSwitch* saiSwitch,
    const state::MirrorFields& mirror,
    SaiMirrorHandle* mirrorHandle,
    sai_mirror_session_type_t session_type) {
  auto cfgPortDesc = apache::thrift::get_pointer(mirror.egressPortDesc());
  if (!cfgPortDesc) {
    XLOG(ERR) << "verifyResolvedMirror: egressPortDesc is null for mirror "
              << mirror.name().value();
    return false;
  }
  auto egressPort =
      PortDescriptor::fromCfgCfgPortDescriptor(*cfgPortDesc).phyPortID();
  auto portHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(egressPort);
  if (!portHandle) {
    XLOG(ERR) << "verifyResolvedMirror: port handle not found for port "
              << egressPort;
    return false;
  }
  auto monitorPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::MonitorPort());
  if (portHandle->port->adapterKey() != monitorPort) {
    XLOG(ERR) << "verifyResolvedMirror: monitor port mismatch for port "
              << egressPort << " expected SAI OID "
              << portHandle->port->adapterKey() << " got " << monitorPort;
    return false;
  }

  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(), SaiLocalMirrorTraits::Attributes::Type());
  if (type != session_type) {
    XLOG(ERR) << "verifyResolvedMirror: session type mismatch, expected "
              << session_type << " got " << type;
    return false;
  }

  auto tos = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::Tos());
  const int expectedTos = folly::copy(mirror.dscp().value()) << 2;
  if (tos != expectedTos) {
    XLOG(ERR) << "verifyResolvedMirror: TOS mismatch, expected " << expectedTos
              << " got " << (int)tos;
    return false;
  }

  const auto& tunnel = apache::thrift::get_pointer(mirror.tunnel());
  if (!tunnel) {
    XLOG(ERR) << "verifyResolvedMirror: tunnel is null for mirror "
              << mirror.name().value();
    return false;
  }

  auto srcIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::SrcIpAddress());
  if (srcIp != network::toIPAddress(tunnel->srcIp().value())) {
    XLOG(ERR) << "verifyResolvedMirror: src IP mismatch, expected "
              << network::toIPAddress(tunnel->srcIp().value()) << " got "
              << srcIp;
    return false;
  }
  auto dstIp = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::DstIpAddress());
  if (dstIp != network::toIPAddress(tunnel->dstIp().value())) {
    XLOG(ERR) << "verifyResolvedMirror: dst IP mismatch, expected "
              << network::toIPAddress(tunnel->dstIp().value()) << " got "
              << dstIp;
    return false;
  }

  auto srcMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::SrcMacAddress());
  if (srcMac != folly::MacAddress(tunnel->srcMac().value())) {
    XLOG(ERR) << "verifyResolvedMirror: src MAC mismatch, expected "
              << tunnel->srcMac().value() << " got " << srcMac;
    return false;
  }
  auto dstMac = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::DstMacAddress());
  if (dstMac != folly::MacAddress(tunnel->dstMac().value())) {
    XLOG(ERR) << "verifyResolvedMirror: dst MAC mismatch, expected "
              << tunnel->dstMac().value() << " got " << dstMac;
    return false;
  }

  auto ttl = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiEnhancedRemoteMirrorTraits::Attributes::Ttl());
  if (ttl != folly::copy(tunnel->ttl().value())) {
    XLOG(ERR) << "verifyResolvedMirror: TTL mismatch, expected "
              << (int)tunnel->ttl().value() << " got " << (int)ttl;
    return false;
  }
  return true;
}

bool verifyResolvedErspanMirror(
    const SaiSwitch* saiSwitch,
    const state::MirrorFields& mirror,
    SaiMirrorHandle* mirrorHandle) {
  return verifyResolvedMirror(
      saiSwitch, mirror, mirrorHandle, SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE);
}

bool verifyResolvedSflowMirror(
    const SaiSwitch* saiSwitch,
    const state::MirrorFields& mirror,
    SaiMirrorHandle* mirrorHandle) {
  if (!verifyResolvedMirror(
          saiSwitch, mirror, mirrorHandle, SAI_MIRROR_SESSION_TYPE_SFLOW)) {
    XLOG(ERR) << "verifyResolvedMirror failed";
    return false;
  }

  // src and dst UDP port
  auto udpSrcPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::UdpSrcPort());

  auto srcPort = apache::thrift::get_pointer(
      apache::thrift::get_pointer(mirror.tunnel())->udpSrcPort());
  auto dstPort = apache::thrift::get_pointer(
      apache::thrift::get_pointer(mirror.tunnel())->udpDstPort());

  if (udpSrcPort != *srcPort) {
    XLOG(ERR) << "Src UDP port mismatch";
    return false;
  }
  auto udpDstPort = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      mirrorHandle->adapterKey(),
      SaiSflowMirrorTraits::Attributes::UdpDstPort());
  if (udpDstPort != *dstPort) {
    XLOG(ERR) << "Dst UDP port mismatch";
    return false;
  }
  return true;
}
} // namespace

bool HwTestThriftHandler::isMirrorProgrammed(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    throw FbossError("isMirrorProgrammed: mirror is null");
  }
  if (!folly::copy(mirror->isResolved().value())) {
    XLOG(INFO) << "isMirrorProgrammed: " << mirror->name().value()
               << " is not resolved";
    return false;
  }
  XLOG(INFO) << "isMirrorProgrammed: " << mirror->name().value()
             << " is resolved";

  std::string jsonStr;
  apache::thrift::SimpleJSONSerializer::serialize(*mirror, &jsonStr);
  XLOG(INFO) << jsonStr;

  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(
          mirror->name().value());
  if (!mirrorHandle) {
    return false;
  }
  auto tunnel = apache::thrift::get_pointer(mirror->tunnel());
  if (!tunnel) {
    // regular local mirror
    return verifyResolvedLocalMirror(saiSwitch, *mirror, mirrorHandle);
  }
  if (apache::thrift::get_pointer(tunnel->udpSrcPort())) {
    // sflow mirror
    return verifyResolvedSflowMirror(saiSwitch, *mirror, mirrorHandle);
  }
  // erspan mirror
  return verifyResolvedErspanMirror(saiSwitch, *mirror, mirrorHandle);
}

bool HwTestThriftHandler::isPortMirrored(
    int32_t port,
    std::unique_ptr<std::string> mirror,
    bool ingress) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  auto portHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(PortID(port));
  std::optional<SaiPortTraits::Attributes::IngressMirrorSession> ingressMirror;
  std::optional<SaiPortTraits::Attributes::EgressMirrorSession> egressMirror;

  std::optional<sai_object_id_t> mirrorSaiOid{};
  if (ingress) {
    ingressMirror = SaiApiTable::getInstance()->portApi().getAttribute(
        portHandle->port->adapterKey(), ingressMirror);
  } else {
    egressMirror = SaiApiTable::getInstance()->portApi().getAttribute(
        portHandle->port->adapterKey(), egressMirror);
  }
  if (!ingressMirror && !egressMirror) {
    XLOG(DBG2) << "Port " << port << " is not mirrored";
    return false;
  } else if (ingressMirror) {
    mirrorSaiOid = ingressMirror.value().value()[0];
  } else if (egressMirror) {
    mirrorSaiOid = egressMirror.value().value()[0];
  }
  if (!mirrorSaiOid) {
    return false;
  }

  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(*mirror);
  if (!mirrorHandle) {
    XLOG(DBG2) << "Mirror " << *mirror << " not found";
    return false;
  }

  return mirrorHandle->adapterKey() == mirrorSaiOid.value();
}

bool HwTestThriftHandler::isPortSampled(
    int32_t /*port*/,
    std::unique_ptr<std::string> /*mirror*/,
    bool /*ingress*/) {
  throw FbossError("isPortSampled not implemented");
}

bool HwTestThriftHandler::isAclEntryMirrored(
    std::unique_ptr<std::string> aclEntry,
    std::unique_ptr<std::string> mirror,
    bool ingress) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);

  // Get the ACL entry from the state
  auto state = hwSwitch_->getProgrammedState();
  auto aclEntry_ptr = utility::getAclEntryByName(state, *aclEntry);
  if (!aclEntry_ptr) {
    return false;
  }

  // Get the ACL table name that contains this ACL entry
  auto aclTableNameOpt =
      utility::getAclTableNameForEntry(state, aclEntry_ptr->getID());
  if (!aclTableNameOpt) {
    return false;
  }
  std::string aclTableName = *aclTableNameOpt;

  // Get the ACL table handle and entry handle from SAI manager
  const auto& aclTableManager = saiSwitch->managerTable()->aclTableManager();
  auto aclTableHandle = aclTableManager.getAclTableHandle(aclTableName);
  if (!aclTableHandle) {
    return false;
  }

  auto aclEntryHandle = aclTableManager.getAclEntryHandle(
      aclTableHandle, aclEntry_ptr->getPriority(), aclEntry_ptr->getID());
  if (!aclEntryHandle) {
    return false;
  }

  // Check if the ACL entry has the expected mirror
  const auto& expectedMirror = *mirror;
  if (ingress) {
    auto actualMirror = aclEntryHandle->ingressMirror;
    return actualMirror && *actualMirror == expectedMirror;
  } else {
    auto actualMirror = aclEntryHandle->egressMirror;
    return actualMirror && *actualMirror == expectedMirror;
  }
}

bool HwTestThriftHandler::verifyResolvedMirror(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    XLOG(ERR) << "verifyResolvedMirror: Input MirrorFields is null";
    return false;
  }
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  auto mirrorName = mirror->name().value();
  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(mirrorName);
  if (!mirrorHandle) {
    XLOG(ERR) << "verifyResolvedMirror: mirror handle not found for "
              << mirrorName;
    return false;
  }
  if (!mirror->isResolved().value()) {
    XLOG(ERR) << "verifyResolvedMirror: mirror " << mirrorName
              << " is not resolved in SW state";
    return false;
  }
  auto egressPortDesc = apache::thrift::get_pointer(mirror->egressPortDesc());
  if (!egressPortDesc) {
    XLOG(ERR) << "verifyResolvedMirror: mirror " << mirrorName
              << " has no egressPortDesc";
    return false;
  }
  XLOG(DBG2) << "verifyResolvedMirror: mirror=" << mirrorName
             << " isResolved=1 hasEgressPort=1 hasTunnel="
             << (apache::thrift::get_pointer(mirror->tunnel()) != nullptr);
  auto tunnel = apache::thrift::get_pointer(mirror->tunnel());
  if (!tunnel) {
    return verifyResolvedLocalMirror(saiSwitch, *mirror, mirrorHandle);
  }
  if (apache::thrift::get_pointer(tunnel->udpSrcPort())) {
    return verifyResolvedSflowMirror(saiSwitch, *mirror, mirrorHandle);
  }
  return verifyResolvedErspanMirror(saiSwitch, *mirror, mirrorHandle);
}

bool HwTestThriftHandler::verifyUnResolvedMirror(
    std::unique_ptr<state::MirrorFields> mirror) {
  if (!mirror) {
    XLOG(ERR) << "verifyUnResolvedMirror: Input MirrorFields is null";
    return false;
  }
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  auto mirrorHandle =
      saiSwitch->managerTable()->mirrorManager().getMirrorHandle(
          mirror->name().value());
  if (!mirrorHandle) {
    return true;
  }
  return false;
}

template <typename AttrT>
static std::vector<sai_object_id_t> getCachedPortMirrorAttr(
    SaiPortHandle* portHandle) {
  auto attrOpt = std::get<std::optional<AttrT>>(portHandle->port->attributes());
  if (attrOpt.has_value()) {
    return attrOpt.value().value();
  }
  return {};
}

static bool verifyPortMirrorDestinationImpl(
    SaiSwitch* saiSwitch,
    const PortID& port,
    int32_t flags,
    std::optional<uint64_t> mirrorDestID) {
  auto portHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(port);
  std::vector<sai_object_id_t> mirrorSaiOidList;
  if (flags & MIRROR_PORT_SFLOW) {
    if (flags & MIRROR_PORT_INGRESS) {
      mirrorSaiOidList = getCachedPortMirrorAttr<
          SaiPortTraits::Attributes::IngressSampleMirrorSession>(portHandle);
    } else {
      mirrorSaiOidList = getCachedPortMirrorAttr<
          SaiPortTraits::Attributes::EgressSampleMirrorSession>(portHandle);
    }
  } else {
    if (flags & MIRROR_PORT_INGRESS) {
      mirrorSaiOidList = getCachedPortMirrorAttr<
          SaiPortTraits::Attributes::IngressMirrorSession>(portHandle);
    } else {
      mirrorSaiOidList = getCachedPortMirrorAttr<
          SaiPortTraits::Attributes::EgressMirrorSession>(portHandle);
    }
  }
  if (mirrorDestID.has_value()) {
    if (mirrorSaiOidList.size() == 0) {
      XLOG(ERR) << "mirrorSaiOidList.size() == 0"
                << " expected to find mirror dest ID: " << mirrorDestID.value();
      return false;
    }
    if (mirrorSaiOidList[0] != mirrorDestID.value()) {
      XLOG(ERR) << "mirror dest ID: Actual value:"
                << (uint64_t)mirrorSaiOidList[0]
                << " Expected value: " << mirrorDestID.value();
      return false;
    }
  } else {
    if (mirrorSaiOidList.size() != 0) {
      XLOG(ERR) << "mirrorSaiOidList.size() != 0";
      return false;
    }
  }
  return true;
}

bool HwTestThriftHandler::verifyPortMirrorDestination(
    int32_t ports,
    int32_t flags,
    int64_t mirrorDestID) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  return verifyPortMirrorDestinationImpl(
      saiSwitch, PortID(ports), flags, mirrorDestID);
}

bool HwTestThriftHandler::verifyPortNoMirrorDestination(
    int32_t ports,
    int32_t flags) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  return verifyPortMirrorDestinationImpl(
      saiSwitch, PortID(ports), flags, std::nullopt);
}

void HwTestThriftHandler::getAllMirrorDestinations(
    ::std::vector<int64_t>& destinations) {
  auto saiSwitch = static_cast<SaiSwitch*>(hwSwitch_);
  auto mirrorSaiOids =
      saiSwitch->managerTable()->mirrorManager().getAllMirrorSessionOids();
  std::transform(
      mirrorSaiOids.begin(),
      mirrorSaiOids.end(),
      std::back_inserter(destinations),
      [](MirrorSaiId mirrorOid) -> uint64_t { return mirrorOid; });
}

bool HwTestThriftHandler::isMirrorSflowTunnelEnabled(int64_t destination) {
  auto type = SaiApiTable::getInstance()->mirrorApi().getAttribute(
      MirrorSaiId(destination), SaiSflowMirrorTraits::Attributes::Type());
  return type == SAI_MIRROR_SESSION_TYPE_SFLOW;
}
} // namespace facebook::fboss::utility
