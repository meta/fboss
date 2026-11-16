// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/thrift_cow/storage/tests/SwitchStateBuilders.h"

#include <fmt/format.h>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss::fsdb::test {

namespace {

void populatePortQueues(state::PortFields& port) {
  constexpr int kQueueCount = 10;
  constexpr int32_t kAqmThreshold = 120000; // bytes; prn3 prod observation

  std::vector<facebook::fboss::PortQueueFields> queues;
  queues.reserve(kQueueCount);
  for (int q = 0; q < kQueueCount; ++q) {
    facebook::fboss::PortQueueFields queue;
    queue.id() = q;
    queue.weight() = 0;
    queue.scheduling() = apache::thrift::util::enumNameSafe(
        cfg::QueueScheduling::STRICT_PRIORITY);
    queue.streamType() =
        apache::thrift::util::enumNameSafe(cfg::StreamType::UNICAST);
    queue.reserved() = 9984;
    queue.sharedBytes() = 0;
    queue.scalingFactor() =
        apache::thrift::util::enumNameSafe(cfg::MMUScalingFactor::ONE);

    cfg::LinearQueueCongestionDetection linear;
    linear.minimumLength() = kAqmThreshold;
    linear.maximumLength() = kAqmThreshold;
    linear.probability() = 100;
    cfg::QueueCongestionDetection detection;
    detection.linear() = linear;
    cfg::ActiveQueueManagement aqm;
    aqm.detection() = detection;
    aqm.behavior() = cfg::QueueCongestionBehavior::EARLY_DROP;
    queue.aqms() = {std::move(aqm)};

    queues.push_back(std::move(queue));
  }
  port.queues() = std::move(queues);
}

void populatePortPinConfigs(state::PortFields& port, int portId) {
  constexpr int kLaneCount = 4; // 100G port = 4 lanes
  const auto chip = fmt::format("BC{}", portId / 4);
  std::vector<phy::PinConfig> pinConfigs;
  pinConfigs.reserve(kLaneCount);
  for (int lane = 0; lane < kLaneCount; ++lane) {
    phy::PinConfig pc;
    phy::PinID id;
    id.chip() = chip;
    id.lane() = lane;
    pc.id() = id;
    phy::TxSettings tx;
    tx.pre() = -8;
    tx.main() = 132;
    tx.post() = -16;
    pc.tx() = tx;
    pinConfigs.push_back(std::move(pc));
  }
  port.pinConfigs() = std::move(pinConfigs);
}

void populatePortProfileConfig(state::PortFields& port) {
  phy::ProfileSideConfig profile;
  profile.numLanes() = 4;
  profile.modulation() = phy::IpModulation::NRZ;
  profile.fec() = phy::FecMode::RS528;
  profile.medium() = TransmitterTechnology::OPTICAL;
  profile.interfaceType() = phy::InterfaceType::CAUI4;
  port.profileConfig() = std::move(profile);
}

// Helper function to create port fields
state::PortFields createPortFields(int portId, const std::string& portName) {
  state::PortFields port;
  port.portId() = portId;
  port.portName() = portName;
  port.portDescription() = fmt::format("Port {}", portId);
  port.portState() = "ENABLED";
  port.portOperState() = true;
  port.ingressVlan() = 0;
  port.portSpeed() = "HUNDREDG";
  port.portLoopbackMode() = "NONE";
  port.txPause() = false;
  port.rxPause() = false;
  port.portType() = cfg::PortType::INTERFACE_PORT;
  port.maxFrameSize() = cfg::switch_config_constants::DEFAULT_PORT_MTU();
  port.portProfileID() = apache::thrift::util::enumNameSafe(
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL);
  state::VlanInfo membership;
  membership.tagged() = false;
  membership.priorityTagged() = false;
  port.vlanMemberShips() = {{"4001", membership}};
  populatePortQueues(port);
  populatePortPinConfigs(port, portId);
  populatePortProfileConfig(port);
  return port;
}

state::VlanFields createVlanFields(int vlanId) {
  state::VlanFields vlan;
  vlan.vlanId() = vlanId;
  vlan.vlanName() = fmt::format("vlan{}", vlanId);
  vlan.intfID() = vlanId % 65536;
  vlan.dhcpV4Relay() = "0.0.0.0";
  vlan.dhcpV6Relay() = "::";
  return vlan;
}

state::InterfaceFields createInterfaceFields(int ifId, int addressCount) {
  state::InterfaceFields intf;
  intf.interfaceId() = ifId;
  intf.vlanId() = ifId % 65536;
  intf.name() = fmt::format("interface{}", ifId);
  // mac is an int64 in network byte order, use a placeholder value
  intf.mac() = 0x020000000001 + ifId;
  intf.mtu() = 9000;
  intf.isVirtual() = false;
  intf.isStateSyncDisabled() = false;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.dhcpV4Relay() = "0.0.0.0";
  intf.dhcpV6Relay() = "::";

  // Add addresses
  std::map<std::string, int16_t> addresses;
  for (int i = 0; i < addressCount; ++i) {
    addresses[fmt::format("10.{}.{}.1", ifId % 256, i)] = 24;
    addresses[fmt::format("2401:db00:{:x}:{:x}::1", ifId % 65536, i)] = 64;
  }
  intf.addresses() = addresses;

  return intf;
}

// Helper function to create transceiver fields
state::TransceiverSpecFields createTransceiverFields(int transceiverId) {
  state::TransceiverSpecFields transceiver;
  transceiver.id() = transceiverId;
  transceiver.cableLength() = 2.0;
  transceiver.mediaInterface() = MediaInterfaceCode::FR4_200G;
  transceiver.managementInterface() = TransceiverManagementInterface::CMIS;
  return transceiver;
}

// Helper function to create local system port fields
state::SystemPortFields createLocalSystemPortFields(int64_t portId) {
  state::SystemPortFields sysPort;
  sysPort.portId() = portId;
  sysPort.switchId() = 0;
  sysPort.portName() = fmt::format("sysport:{}", portId);
  sysPort.coreIndex() = 0;
  sysPort.corePortIndex() = portId % 8;
  sysPort.speedMbps() = 100000;
  sysPort.numVoqs() = 8;
  sysPort.enabled_DEPRECATED() = true;
  sysPort.qosPolicy() = "defaultQosPolicy";
  sysPort.scope() = cfg::Scope::LOCAL;
  return sysPort;
}

// Helper function to create control plane fields
state::ControlPlaneFields createControlPlaneFields() {
  state::ControlPlaneFields cp;

  std::vector<PortQueueFields> queues;
  for (int i = 0; i < 10; ++i) {
    PortQueueFields q;
    q.id() = i;
    q.weight() = i + 1;
    q.scheduling() = "WEIGHTED_ROUND_ROBIN";
    q.streamType() = "UNICAST";
    q.name() = fmt::format("cpuQueue{}", i);
    queues.push_back(std::move(q));
  }
  cp.queues() = std::move(queues);

  std::vector<cfg::PacketRxReasonToQueue> rxReasons;
  auto addReason = [&](cfg::PacketRxReason reason, int16_t queueId) {
    cfg::PacketRxReasonToQueue entry;
    entry.rxReason() = reason;
    entry.queueId() = queueId;
    rxReasons.push_back(std::move(entry));
  };
  addReason(cfg::PacketRxReason::ARP, 1);
  addReason(cfg::PacketRxReason::ARP_RESPONSE, 1);
  addReason(cfg::PacketRxReason::NDP, 1);
  addReason(cfg::PacketRxReason::BGP, 2);
  addReason(cfg::PacketRxReason::BGPV6, 2);
  addReason(cfg::PacketRxReason::LACP, 3);
  addReason(cfg::PacketRxReason::LLDP, 4);
  addReason(cfg::PacketRxReason::DHCP, 5);
  addReason(cfg::PacketRxReason::DHCPV6, 5);
  cp.rxReasonToQueue() = std::move(rxReasons);

  return cp;
}

// Helper function to create switch settings fields
state::SwitchSettingsFields createSwitchSettingsFields() {
  state::SwitchSettingsFields settings;
  settings.l2LearningMode() = cfg::L2LearningMode::HARDWARE;
  settings.l2AgeTimerSeconds() = 300;
  settings.switchDrainState() = cfg::SwitchDrainState::UNDRAINED;
  settings.hostname() = "switch0";

  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::NPU;
  switchInfo.asicType() = cfg::AsicType::ASIC_TYPE_TOMAHAWK4;
  switchInfo.switchIndex() = 0;
  cfg::Range64 portIdRange;
  portIdRange.minimum() = 0;
  portIdRange.maximum() = 1023;
  switchInfo.portIdRange() = portIdRange;
  settings.switchIdToSwitchInfo()[0] = std::move(switchInfo);

  return settings;
}

// Helper function to create ACL entry fields
state::AclEntryFields createAclEntryFields(const std::string& name) {
  state::AclEntryFields acl;
  acl.priority() = 1;
  acl.name() = name;
  acl.actionType() = cfg::AclActionType::PERMIT;
  acl.enabled() = true;
  return acl;
}

// Helper function to create aggregate port fields
state::AggregatePortFields createAggregatePortFields(int portId) {
  state::AggregatePortFields aggPort;
  aggPort.id() = portId;
  aggPort.name() = fmt::format("po{}", portId);
  aggPort.description() = fmt::format("Port-Channel {}", portId);
  aggPort.minimumLinkCount() = 1;
  aggPort.systemPriority() = 32768;
  aggPort.systemID() = 0x020000000001;

  std::vector<state::Subport> ports;
  for (int i = 0; i < 4; ++i) {
    state::Subport subport;
    subport.id() = portId * 10 + i;
    subport.priority() = 32768;
    subport.lacpPortRate() = cfg::LacpPortRate::FAST;
    subport.lacpPortActivity() = cfg::LacpPortActivity::ACTIVE;
    ports.push_back(std::move(subport));
  }
  aggPort.ports() = std::move(ports);

  std::map<int32_t, bool> portToFwdState;
  for (int i = 0; i < 4; ++i) {
    portToFwdState[portId * 10 + i] = true;
  }
  aggPort.portToFwdState() = std::move(portToFwdState);

  return aggPort;
}

// Helper function to create port flowlet fields
state::PortFlowletFields createPortFlowletFields() {
  state::PortFlowletFields flowlet;
  flowlet.id() = 0;
  flowlet.scalingFactor() = 100;
  flowlet.loadWeight() = 50;
  flowlet.queueWeight() = 50;
  return flowlet;
}

// Helper function to create mirror fields
state::MirrorFields createMirrorFields(const std::string& name) {
  state::MirrorFields mirror;
  mirror.name() = name;
  mirror.configHasEgressPort() = true;
  mirror.isResolved() = true;
  mirror.dscp() = 0;
  mirror.truncate() = false;
  return mirror;
}

// Helper function to create QoS policy fields
state::QosPolicyFields createQosPolicyFields(const std::string& name) {
  state::QosPolicyFields policy;
  policy.name() = name;

  state::TrafficClassToQosAttributeMap dscpMap;
  std::vector<state::TrafficClassToQosAttributeEntry> fromEntries;
  std::vector<state::TrafficClassToQosAttributeEntry> toEntries;
  for (int16_t tc = 0; tc < 8; ++tc) {
    state::TrafficClassToQosAttributeEntry fromEntry;
    fromEntry.trafficClass() = tc;
    fromEntry.attr() = static_cast<int16_t>(tc * 8);
    fromEntries.push_back(std::move(fromEntry));

    state::TrafficClassToQosAttributeEntry toEntry;
    toEntry.trafficClass() = tc;
    toEntry.attr() = static_cast<int16_t>(tc * 8);
    toEntries.push_back(std::move(toEntry));
  }
  dscpMap.from() = std::move(fromEntries);
  dscpMap.to() = std::move(toEntries);
  policy.dscpMap() = std::move(dscpMap);

  std::map<int16_t, int16_t> tcToQueue;
  for (int16_t tc = 0; tc < 8; ++tc) {
    tcToQueue[tc] = tc;
  }
  policy.trafficClassToQueueId() = std::move(tcToQueue);

  return policy;
}

// Helper function to create load balancer fields
state::LoadBalancerFields createLoadBalancerFields(cfg::LoadBalancerID id) {
  state::LoadBalancerFields lb;
  lb.id() = id;
  lb.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  lb.seed() = 0;
  lb.v4Fields() = {
      cfg::IPv4Field::SOURCE_ADDRESS, cfg::IPv4Field::DESTINATION_ADDRESS};
  lb.v6Fields() = {
      cfg::IPv6Field::SOURCE_ADDRESS, cfg::IPv6Field::DESTINATION_ADDRESS};
  lb.transportFields() = {
      cfg::TransportField::SOURCE_PORT, cfg::TransportField::DESTINATION_PORT};
  return lb;
}

// Helper function to create IP tunnel fields
state::IpTunnelFields createIpTunnelFields(const std::string& name) {
  state::IpTunnelFields tunnel;
  tunnel.ipTunnelId() = name;
  tunnel.underlayIntfId() = 1;
  tunnel.dstIp() = "10.0.0.1";
  tunnel.type() = 0;
  tunnel.tunnelTermType() = 0;
  tunnel.ttlMode() = 0;
  tunnel.dscpMode() = 0;
  tunnel.ecnMode() = 0;
  tunnel.srcIp() = "10.0.0.2";
  return tunnel;
}

// Helper function to create ACL table group fields
state::AclTableGroupFields createAclTableGroupFields() {
  state::AclTableGroupFields group;
  group.name() = "ingress-ACL-Table-Group";
  group.stage() = cfg::AclStage::INGRESS;
  return group;
}

// Helper function to create mirror on drop report fields
state::MirrorOnDropReportFields createMirrorOnDropReportFields(
    const std::string& name) {
  state::MirrorOnDropReportFields report;
  report.name() = name;
  report.mirrorPortId() = 1;
  report.localSrcPort() = 10000;
  report.collectorPort() = 20000;
  report.mtu() = 1500;
  report.truncateSize() = 128;
  report.dscp() = 0;
  return report;
}

} // namespace

void populatePorts(state::SwitchState& state, const SwitchStateScale& scale) {
  if (scale.portCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int16_t, state::PortFields> portMap;

  for (int i = 0; i < scale.portCount; ++i) {
    std::string portName = fmt::format(
        "eth{}/{}/{}", (i / 100) + 1, ((i / 10) % 10) + 1, (i % 10) + 1);
    portMap[i] = createPortFields(i, portName);
  }

  state.portMaps()[switchIdList] = std::move(portMap);
}

void populateVlans(state::SwitchState& state, const SwitchStateScale& scale) {
  if (scale.vlanCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int16_t, state::VlanFields> vlanMap;

  for (int i = 0; i < scale.vlanCount; ++i) {
    int vlanId = 1000 + i;
    vlanMap[vlanId] = createVlanFields(vlanId);
  }

  state.vlanMaps()[switchIdList] = std::move(vlanMap);
}

void populateInterfaces(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.interfaceCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int32_t, state::InterfaceFields> interfaceMap;

  for (int i = 0; i < scale.interfaceCount; ++i) {
    interfaceMap[i] = createInterfaceFields(i, 2);
  }

  state.interfaceMaps()[switchIdList] = std::move(interfaceMap);
}

void populateTransceivers(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.transceiverCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int16_t, state::TransceiverSpecFields> transceiverMap;

  for (int i = 0; i < scale.transceiverCount; ++i) {
    transceiverMap[i] = createTransceiverFields(i);
  }

  state.transceiverMaps()[switchIdList] = std::move(transceiverMap);
}

void populateSystemPorts(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.systemPortCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int64_t, state::SystemPortFields> sysPortMap;

  for (int i = 0; i < scale.systemPortCount; ++i) {
    sysPortMap[i] = createLocalSystemPortFields(i);
  }

  state.systemPortMaps()[switchIdList] = std::move(sysPortMap);
}

void populateDsfNodes(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.dsfNodeCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int64_t, cfg::DsfNode> dsfNodeMap;

  for (int i = 0; i < scale.dsfNodeCount; ++i) {
    cfg::DsfNode node;
    node.name() = fmt::format("dsfNode{}", i);
    node.switchId() = i;
    node.type() = cfg::DsfNodeType::INTERFACE_NODE;
    node.asicType() = cfg::AsicType::ASIC_TYPE_TOMAHAWK4;
    node.platformType() = PlatformType::PLATFORM_FAKE_SAI;
    node.loopbackIps() = {
        fmt::format("10.0.{}.{}", i / 256, i % 256),
        fmt::format("2401:db00::{:x}", i)};
    dsfNodeMap[i] = std::move(node);
  }

  state.dsfNodesMap()[switchIdList] = std::move(dsfNodeMap);
}

void populateControlPlane(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (!scale.hasControlPlane) {
    return;
  }

  std::string switchIdList = "Id:0";
  state.controlPlaneMap()[switchIdList] = createControlPlaneFields();
}

void populateSwitchSettings(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (!scale.hasSwitchSettings) {
    return;
  }

  std::string switchIdList = "Id:0";
  state.switchSettingsMap()[switchIdList] = createSwitchSettingsFields();
}

void populateBufferPoolCfg(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.bufferPoolCfgCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, BufferPoolFields> bufferPoolMap;

  for (int i = 0; i < scale.bufferPoolCfgCount; ++i) {
    BufferPoolFields pool;
    std::string poolId = fmt::format("bufferPool{}", i);
    pool.id() = poolId;
    pool.headroomBytes() = 1024 * (i + 1);
    pool.sharedBytes() = 4096 * (i + 1);
    bufferPoolMap[poolId] = std::move(pool);
  }

  state.bufferPoolCfgMaps()[switchIdList] = std::move(bufferPoolMap);
}

void populateMirrors(state::SwitchState& state, const SwitchStateScale& scale) {
  if (scale.mirrorCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::MirrorFields> mirrorMap;

  for (int i = 0; i < scale.mirrorCount; ++i) {
    std::string mirrorName = fmt::format("mirror{}", i);
    mirrorMap[mirrorName] = createMirrorFields(mirrorName);
  }

  state.mirrorMaps()[switchIdList] = std::move(mirrorMap);
}

void populateQosPolicies(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.qosPolicyCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::QosPolicyFields> qosPolicyMap;

  for (int i = 0; i < scale.qosPolicyCount; ++i) {
    std::string policyName = fmt::format("qosPolicy{}", i);
    qosPolicyMap[policyName] = createQosPolicyFields(policyName);
  }

  state.qosPolicyMaps()[switchIdList] = std::move(qosPolicyMap);
}

void populateLoadBalancers(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.loadBalancerCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<cfg::LoadBalancerID, state::LoadBalancerFields> loadBalancerMap;

  loadBalancerMap[cfg::LoadBalancerID::ECMP] =
      createLoadBalancerFields(cfg::LoadBalancerID::ECMP);
  if (scale.loadBalancerCount > 1) {
    loadBalancerMap[cfg::LoadBalancerID::AGGREGATE_PORT] =
        createLoadBalancerFields(cfg::LoadBalancerID::AGGREGATE_PORT);
  }

  state.loadBalancerMaps()[switchIdList] = std::move(loadBalancerMap);
}

void populateAclTableGroups(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (!scale.hasAclTableGroup) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<cfg::AclStage, state::AclTableGroupFields> aclTableGroupMap;

  state::AclTableGroupFields group = createAclTableGroupFields();

  state::AclTableFields table;
  table.id() = "ingress-ACL-Table";
  table.priority() = 0;
  group.aclTableMap() = {{"ingress-ACL-Table", std::move(table)}};

  aclTableGroupMap[cfg::AclStage::INGRESS] = std::move(group);

  state.aclTableGroupMaps()[switchIdList] = std::move(aclTableGroupMap);
}

void populateAcls(state::SwitchState& state, const SwitchStateScale& scale) {
  if (scale.aclCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::AclEntryFields> aclMap;

  for (int i = 0; i < scale.aclCount; ++i) {
    std::string aclName = fmt::format("acl{}", i);
    aclMap[aclName] = createAclEntryFields(aclName);
  }

  state.aclMaps()[switchIdList] = std::move(aclMap);
}

void populateIpTunnels(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.ipTunnelCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::IpTunnelFields> ipTunnelMap;

  for (int i = 0; i < scale.ipTunnelCount; ++i) {
    std::string tunnelName = fmt::format("tunnel{}", i);
    ipTunnelMap[tunnelName] = createIpTunnelFields(tunnelName);
  }

  state.ipTunnelMaps()[switchIdList] = std::move(ipTunnelMap);
}

void populateAggregatePorts(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.aggregatePortCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<int16_t, state::AggregatePortFields> aggregatePortMap;

  for (int i = 0; i < scale.aggregatePortCount; ++i) {
    aggregatePortMap[i + 1] = createAggregatePortFields(i + 1);
  }

  state.aggregatePortMaps()[switchIdList] = std::move(aggregatePortMap);
}

void populatePortFlowletCfg(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.portFlowletCfgCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::PortFlowletFields> flowletMap;

  for (int i = 0; i < scale.portFlowletCfgCount; ++i) {
    std::string flowletName = fmt::format("flowlet{}", i);
    flowletMap[flowletName] = createPortFlowletFields();
  }

  state.portFlowletCfgMaps()[switchIdList] = std::move(flowletMap);
}

void populateMirrorOnDropReports(
    state::SwitchState& state,
    const SwitchStateScale& scale) {
  if (scale.mirrorOnDropReportCount == 0) {
    return;
  }

  std::string switchIdList = "Id:0";
  std::map<std::string, state::MirrorOnDropReportFields> modReportMap;

  for (int i = 0; i < scale.mirrorOnDropReportCount; ++i) {
    std::string reportName = fmt::format("modReport{}", i);
    modReportMap[reportName] = createMirrorOnDropReportFields(reportName);
  }

  state.mirrorOnDropReportMaps()[switchIdList] = std::move(modReportMap);
}

bool filterSwitchStateScaleForPath(
    SwitchStateScale& scale,
    const std::string& targetField) {
  if (targetField.empty()) {
    return true;
  }

  // Save the values for the target field, zero everything, then restore.
  // Note: SwitchStateScale has default-true bools, so we must zero them.
  SwitchStateScale filtered{};
  filtered.hasControlPlane = false;
  filtered.hasSwitchSettings = false;
  filtered.hasAclTableGroup = false;

  if (targetField == "fibsInfoMap") {
    filtered.fibV4Size = scale.fibV4Size;
    filtered.fibV6Size = scale.fibV6Size;
    filtered.v4Nexthops = scale.v4Nexthops;
    filtered.v6Nexthops = scale.v6Nexthops;
  } else if (targetField == "remoteSystemPortMaps") {
    filtered.remoteSystemPortMapSize = scale.remoteSystemPortMapSize;
  } else if (targetField == "remoteInterfaceMaps") {
    filtered.remoteInterfaceMapSize = scale.remoteInterfaceMapSize;
  } else if (targetField == "portMaps") {
    filtered.portCount = scale.portCount;
  } else if (targetField == "vlanMaps") {
    filtered.vlanCount = scale.vlanCount;
  } else if (targetField == "interfaceMaps") {
    filtered.interfaceCount = scale.interfaceCount;
  } else if (targetField == "transceiverMaps") {
    filtered.transceiverCount = scale.transceiverCount;
  } else if (targetField == "systemPortMaps") {
    filtered.systemPortCount = scale.systemPortCount;
  } else if (targetField == "dsfNodesMap") {
    filtered.dsfNodeCount = scale.dsfNodeCount;
  } else if (targetField == "controlPlaneMap") {
    filtered.hasControlPlane = scale.hasControlPlane;
  } else if (targetField == "switchSettingsMap") {
    filtered.hasSwitchSettings = scale.hasSwitchSettings;
  } else if (targetField == "bufferPoolCfgMaps") {
    filtered.bufferPoolCfgCount = scale.bufferPoolCfgCount;
  } else if (targetField == "mirrorMaps") {
    filtered.mirrorCount = scale.mirrorCount;
  } else if (targetField == "qosPolicyMaps") {
    filtered.qosPolicyCount = scale.qosPolicyCount;
  } else if (targetField == "loadBalancerMaps") {
    filtered.loadBalancerCount = scale.loadBalancerCount;
  } else if (targetField == "aclTableGroupMaps") {
    filtered.hasAclTableGroup = scale.hasAclTableGroup;
  } else if (targetField == "aclMaps") {
    filtered.aclCount = scale.aclCount;
  } else if (targetField == "ipTunnelMaps") {
    filtered.ipTunnelCount = scale.ipTunnelCount;
  } else if (targetField == "aggregatePortMaps") {
    filtered.aggregatePortCount = scale.aggregatePortCount;
  } else if (targetField == "portFlowletCfgMaps") {
    filtered.portFlowletCfgCount = scale.portFlowletCfgCount;
  } else if (targetField == "mirrorOnDropReportMaps") {
    filtered.mirrorOnDropReportCount = scale.mirrorOnDropReportCount;
  } else {
    return false;
  }

  scale = filtered;
  return true;
}

} // namespace facebook::fboss::fsdb::test
