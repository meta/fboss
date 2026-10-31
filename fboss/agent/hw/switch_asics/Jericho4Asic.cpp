// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/Jericho4Asic.h"
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/AgentFeatures.h"

namespace {
static constexpr int kDefaultMidPriCpuQueueId = 3;
static constexpr int kDefaultHiPriCpuQueueId = 7;
constexpr auto kCpuPortSpeed = 10000;
constexpr auto kSingleStageCpuPortNumVoqs = 8;
constexpr auto kDualStageCpuPortNumVoqs = 3;
} // namespace

namespace facebook::fboss {

std::vector<HwAsic::InternalSystemPortConfig>
Jericho4Asic::getInternalSystemPortConfig(
    const CpuPortCoreAndPortIndex& cpuPortsCoreAndPortIdx) const {
  CHECK(
      cpuPortsCoreAndPortIdx.size() == 1 ||
      cpuPortsCoreAndPortIdx.size() == 4 ||
      cpuPortsCoreAndPortIdx.size() == 8 || cpuPortsCoreAndPortIdx.size() == 9)
      << "Create one CPU port for the ASIC or one CPU port for each core";
  CHECK(getSwitchId()) << " Switch Id must be set before sys port info";

  const uint32_t switchId = static_cast<uint32_t>(*getSwitchId());
  const uint32_t numVoqs = isDualStage3Q2QMode() ? kDualStageCpuPortNumVoqs
                                                 : kSingleStageCpuPortNumVoqs;
  std::vector<InternalSystemPortConfig> sysPortConfig;
  sysPortConfig.reserve(cpuPortsCoreAndPortIdx.size());
  for (auto [cpuPortID, coreAndPortIdx] : cpuPortsCoreAndPortIdx) {
    auto [core, port] = coreAndPortIdx;
    sysPortConfig.push_back(
        {cpuPortID, switchId, core, port, kCpuPortSpeed, numVoqs});
  }
  return sysPortConfig;
}

bool Jericho4Asic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::OBJECT_KEY_CACHE:
    case HwAsic::Feature::PKTIO:
    case HwAsic::Feature::HOSTTABLE:
    case HwAsic::Feature::OBM_COUNTERS:
    case HwAsic::Feature::MIRROR_PACKET_TRUNCATION:
    case HwAsic::Feature::SFLOW_SAMPLING:
    case HwAsic::Feature::TELEMETRY_AND_MONITORING:
    case HwAsic::Feature::WIDE_ECMP:
    case HwAsic::Feature::ALPM_ROUTE_PROJECTION:
    case HwAsic::Feature::MAC_AGING:
    case HwAsic::Feature::SAI_PORT_SPEED_CHANGE: // CS00011784917
    case HwAsic::Feature::EGRESS_MIRRORING:
    case HwAsic::Feature::EGRESS_SFLOW:
    case HwAsic::Feature::DEFAULT_VLAN:
    case HwAsic::Feature::CPU_PORT:
    case HwAsic::Feature::CPU_QUEUES:
    case HwAsic::Feature::VRF:
    case HwAsic::Feature::SAI_HASH_FIELDS_CLEAR_BEFORE_SET:
    case HwAsic::Feature::SAI_WEIGHTED_NEXTHOPGROUP_MEMBER:
    case HwAsic::Feature::PORT_TX_DISABLE:
    case HwAsic::Feature::SAI_PORT_ERR_STATUS:
    case HwAsic::Feature::ROUTE_PROGRAMMING:
    case HwAsic::Feature::FABRIC_PORTS:
    case HwAsic::Feature::LINK_TRAINING:
    case HwAsic::Feature::SAI_TTL0_PACKET_FORWARD_ENABLE:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::MEDIA_TYPE:
    case HwAsic::Feature::PORT_FABRIC_ISOLATE:
    case HwAsic::Feature::CPU_TX_VIA_RECYCLE_PORT:
    case HwAsic::Feature::SWITCH_DROP_STATS:
    case HwAsic::Feature::PACKET_INTEGRITY_DROP_STATS:
    case HwAsic::Feature::SAI_CONFIGURE_SIX_TAP:
    case HwAsic::Feature::RESOURCE_USAGE_STATS:
    case HwAsic::Feature::BLACKHOLE_ROUTE_DROP_COUNTER:
    case HwAsic::Feature::ECN:
    case HwAsic::Feature::SAI_ECN_WRED:
    case HwAsic::Feature::QUEUE_ECN_COUNTER:
    case HwAsic::Feature::MANAGEMENT_PORT:
    case HwAsic::Feature::ANY_ACL_DROP_COUNTER:
    case HwAsic::Feature::EGRESS_FORWARDING_DROP_COUNTER:
    case HwAsic::Feature::ACL_COUNTER_LABEL:
    case HwAsic::Feature::SWITCH_ATTR_INGRESS_ACL:
    case HwAsic::Feature::ACL_TABLE_GROUP:
    case HwAsic::Feature::ERSPANv4:
    case HwAsic::Feature::ERSPANv6:
    case HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER:
    case HwAsic::Feature::SAI_PRBS:
    case HwAsic::Feature::PORT_SERDES_ZERO_PREEMPHASIS:
    case HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::PQP_ERROR_EGRESS_DROP_COUNTER:
    case HwAsic::Feature::FABRIC_LINK_DOWN_CELL_DROP_COUNTER:
    case HwAsic::Feature::CRC_ERROR_DETECT:
    case HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW:
    case HwAsic::Feature::SFLOWv6:
    case HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT:
    case HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY:
    case HwAsic::Feature::VOQ_LATENCY_WATERMARK_BIN:
    case HwAsic::Feature::ACL_ENTRY_ETHER_TYPE:
    case HwAsic::Feature::ACL_BYTE_COUNTER:
    case HwAsic::Feature::EGRESS_CORE_BUFFER_WATERMARK:
    case HwAsic::Feature::DELETED_CREDITS_STAT:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_DROPPED_PACKETS:
    case HwAsic::Feature::NO_RX_REASON_TRAP:
    case HwAsic::Feature::EGRESS_GVOQ_WATERMARK_BYTES:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_SHARED_WATERMARK:
    case HwAsic::Feature::FAST_LLFC_COUNTER:
    case HwAsic::Feature::INGRESS_SRAM_MIN_BUFFER_WATERMARK:
    case HwAsic::Feature::FDR_FIFO_WATERMARK:
    case HwAsic::Feature::EGRESS_CELL_ERROR_STATS:
    case HwAsic::Feature::ECMP_MEMBER_WIDTH_INTROSPECTION:
    case HwAsic::Feature::CPU_QUEUE_WATERMARK_STATS:
    case HwAsic::Feature::SAMPLE_RATE_CONFIG_PER_MIRROR:
    case HwAsic::Feature::SFLOW_SAMPLES_PACKING:
    case HwAsic::Feature::SDK_REGISTER_DUMP:
    case HwAsic::Feature::BUFFER_POOL_HEADROOM_WATERMARK:
    case HwAsic::Feature::SAI_SET_TC_WITH_USER_DEFINED_TRAP_CPU_ACTION:
    case HwAsic::Feature::DRAM_DATAPATH_PACKET_ERROR_STATS:
    case HwAsic::Feature::EGRESS_POOL_AVAILABLE_SIZE_ATTRIBUTE_SUPPORTED:
    case HwAsic::Feature::ASIC_RESET_NOTIFICATIONS:
    case HwAsic::Feature::RX_SERDES_PARAMETERS:
    case HwAsic::Feature::BULK_CREATE_ECMP_MEMBER:
    case HwAsic::Feature::TECH_SUPPORT:
    case HwAsic::Feature::DRAM_QUARANTINED_BUFFER_STATS:
    case HwAsic::Feature::FABRIC_LINK_MONITORING:
    case HwAsic::Feature::RX_PACKET_TYPE:
    case HwAsic::Feature::TEMPERATURE_MONITORING:
    case HwAsic::Feature::MULTIPLE_ACL_TABLES:
    case HwAsic::Feature::VOQ:
    case HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE:
      return true;
    // Features not expected to work on SIM
    case HwAsic::Feature::SHARED_INGRESS_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::BUFFER_POOL:
    case HwAsic::Feature::PFC:
    case HwAsic::Feature::PFC_XON_TO_XOFF_COUNTER:
    case HwAsic::Feature::SAI_PORT_SERDES_FIELDS_RESET:
    case HwAsic::Feature::FABRIC_TX_QUEUES:
    case HwAsic::Feature::VOQ_DELETE_COUNTER:
    case HwAsic::Feature::L3_QOS:
    case HwAsic::Feature::TC_TO_QUEUE_QOS_MAP_ON_SYSTEM_PORT:
    case HwAsic::Feature::CREDIT_WATCHDOG:
    case HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING:
      return getAsicMode() != AsicMode::ASIC_MODE_SIM;
    // SIM specific features.
    case HwAsic::Feature::SAI_PORT_ETHER_STATS:
    case HwAsic::Feature::SLOW_STAT_UPDATE:
      // supported only on the SIM
      return getAsicMode() == AsicMode::ASIC_MODE_SIM;
    case HwAsic::Feature::SWITCH_ISOLATE:
    case HwAsic::Feature::LINK_INACTIVE_BASED_ISOLATE:
    case HwAsic::Feature::UDF_HASH_FIELD_QUERY:
    case HwAsic::Feature::IN_PAUSE_INCREMENTS_DISCARDS:
    case HwAsic::Feature::SAI_LAG_HASH:
    case HwAsic::Feature::QOS_MAP_GLOBAL:
    case HwAsic::Feature::QCM:
    case HwAsic::Feature::SMAC_EQUALS_DMAC_CHECK_ENABLED:
    case HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_INTERFACE_TYPE:
    case HwAsic::Feature::WEIGHTED_NEXTHOPGROUP_MEMBER:
    case HwAsic::Feature::HSDK:
    case HwAsic::Feature::L3_EGRESS_MODE_AUTO_ENABLED:
    case HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER:
    case HwAsic::Feature::PENDING_L2_ENTRY:
    case HwAsic::Feature::EGRESS_QUEUE_FLEX_COUNTER:
    case HwAsic::Feature::INGRESS_L3_INTERFACE:
    case HwAsic::Feature::NON_UNICAST_HASH:
    case HwAsic::Feature::DETAILED_L2_UPDATE:
    case HwAsic::Feature::COUNTER_REFRESH_INTERVAL:
    case HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD:
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT: // CS00012066057
    case HwAsic::Feature::MACSEC:
    case HwAsic::Feature::SAI_MPLS_QOS:
    case HwAsic::Feature::EMPTY_ACL_MATCHER:
    case HwAsic::Feature::ROUTE_FLEX_COUNTERS:
    case HwAsic::Feature::FEC_DIAG_COUNTERS:
    case HwAsic::Feature::SAI_ACL_TABLE_UPDATE:
    case HwAsic::Feature::SAI_MPLS_TTL_1_TRAP:
    case HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL:
    case HwAsic::Feature::SAI_MPLS_NEXTHOP_OUTSEG_TYPE:
    case HwAsic::Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER:
    case HwAsic::Feature::EXACT_MATCH:
    case HwAsic::Feature::RX_FREQUENCY_PPM:
    case HwAsic::Feature::SAI_FIRMWARE_PATH:
    case HwAsic::Feature::BRIDGE_PORT_8021Q:
    case HwAsic::Feature::EXTENDED_FEC:
    case HwAsic::Feature::SAI_RX_REASON_COUNTER:
    case HwAsic::Feature::SAI_MPLS_INSEGMENT:
    case HwAsic::Feature::XPHY_PORT_STATE_TOGGLE:
    case HwAsic::Feature::SAI_PORT_GET_PMD_LANES:
    case HwAsic::Feature::SAI_PORT_VCO_CHANGE:
    case HwAsic::Feature::ARS:
    case HwAsic::Feature::P4_WARMBOOT:
    case HwAsic::Feature::FEC_AM_LOCK_STATUS:
    case HwAsic::Feature::PCS_RX_LINK_STATUS:
    case HwAsic::Feature::SAI_CONFIGURE_SEVEN_TAP:
    case HwAsic::Feature::SAI_SAMPLEPACKET_TRAP:
    case HwAsic::Feature::SAI_UDF_HASH:
    case HwAsic::Feature::PTP_TC:
    case HwAsic::Feature::PTP_TC_PCS:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_HEADROOM_WATERMARK:
    case HwAsic::Feature::RX_LANE_SQUELCH_ENABLE:
    case HwAsic::Feature::SEPARATE_BYTE_AND_PACKET_ACL_COUNTER:
    case HwAsic::Feature::ARS_PORT_ATTRIBUTES:
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::SAI_EAPOL_TRAP:
    case HwAsic::Feature::SAI_USER_DEFINED_TRAP:
    case HwAsic::Feature::PORT_EYE_VALUES:
    case HwAsic::Feature::ECMP_DLB_OFFSET:
    case HwAsic::Feature::SPAN:
    case HwAsic::Feature::SFLOWv4:
    case HwAsic::Feature::MPLS:
    case HwAsic::Feature::MPLS_ECMP:
    case HwAsic::Feature::RX_SNR:
    case HwAsic::Feature::FEC_CORRECTED_BITS:
    case HwAsic::Feature::ROUTE_COUNTERS:
    case HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION:
    case HwAsic::Feature::ECMP_HASH_V4:
    case HwAsic::Feature::ECMP_HASH_V6:
    case HwAsic::Feature::TRAFFIC_HASHING:
    case HwAsic::Feature::PORT_WRED_COUNTER:
    case HwAsic::Feature::DTL_WATERMARK_COUNTER:
    case HwAsic::Feature::SAI_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::SCHEDULER_PPS:
    case HwAsic::Feature::DATA_CELL_FILTER:
    case HwAsic::Feature::MULTIPLE_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::ENABLE_DELAY_DROP_CONGESTION_THRESHOLD:
    case HwAsic::Feature::L3_MTU_ERROR_TRAP:
    case HwAsic::Feature::L3_INTF_MTU:
    case HwAsic::Feature::DEDICATED_CPU_BUFFER_POOL:
    case HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE:
    case HwAsic::Feature::SAI_HOST_MISS_TRAP:
    case HwAsic::Feature::CPU_TX_PACKET_REQUIRES_VLAN_TAG:
    case HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY:
    case HwAsic::Feature::PFC_WATCHDOG_TIMER_GRANULARITY:
    case HwAsic::Feature::SAI_PORT_IN_CONGESTION_DISCARDS:
    case HwAsic::Feature::ROUTER_INTERFACE_STATISTICS:
    case HwAsic::Feature::AGGREGATE_PORT_ROUTER_INTERFACE:
    case HwAsic::Feature::CPU_PORT_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::ACL_SET_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::SET_NEXT_HOP_GROUP_HASH_ALGORITHM:
    case HwAsic::Feature::MANAGEMENT_PORT_MULTICAST_QUEUE_ALPHA:
    case HwAsic::Feature::SAI_PORT_PG_DROP_STATUS:
    case HwAsic::Feature::FABRIC_INTER_CELL_JITTER_WATERMARK:
    case HwAsic::Feature::MAC_TRANSMIT_DATA_QUEUE_WATERMARK:
    case HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::RESERVED_BYTES_FOR_BUFFER_POOL:
    case HwAsic::Feature::INGRESS_BUFFER_POOL_SIZE_EXCLUDES_HEADROOM:
    case HwAsic::Feature::IN_DISCARDS_EXCLUDES_PFC:
    case HwAsic::Feature::PORT_LEVEL_BUFFER_CONFIGURATION_SUPPORT:
    case HwAsic::Feature::SAI_SERDES_RX_REACH:
    case HwAsic::Feature::SAI_SERDES_PRECODING:
    case HwAsic::Feature::ARS_FUTURE_PORT_LOAD:
    case HwAsic::Feature::ARS_CURRENT_PORT_LOAD:
    case HwAsic::Feature::VIRTUAL_ARS_GROUP:
    case HwAsic::Feature::CUT_THROUGH_FORWARDING:
    case HwAsic::Feature::SRV6_MYSID_DISCARD_COUNTER:
    case HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER:
    case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
    case HwAsic::Feature::PBR_ACL:
    case HwAsic::Feature::DEVICE_WATERMARK_SUPPORT:
    case HwAsic::Feature::ECN_PROBABILISTIC_MARKING:
    case HwAsic::Feature::SWITCH_DROP_DEBUG_COUNTER:
    case HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT:
    case HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT:
    // RCI watermark counters fail on Jericho4 with
    // SAI_STATUS_INVALID_PARAMETER.
    case HwAsic::Feature::RCI_WATERMARK_COUNTER:
    // Disable DRAM counters on Jericho4 until SDK read failures are resolved.
    case HwAsic::Feature::DRAM_ENQUEUE_DEQUEUE_STATS:
    case HwAsic::Feature::DRAM_BLOCK_TIME:
    // Disabling some counters for the time being.
    // This will result in an early return in
    // SaiDebugCounterManager::setupTrapDropCounter(), which is currently
    // failing with SAI 14.x
    // TODO (Q4D/J4/R4): re-enable these features on J4 after SDK supports them
    case HwAsic::Feature::ANY_TRAP_DROP_COUNTER:
    case HwAsic::Feature::SAI_FEC_CODEWORDS_STATS:
    case HwAsic::Feature::SAI_FEC_SYMBOL_ERRORS:
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::SAI_FEC_COUNTERS:
    case HwAsic::Feature::SAI_FEC_CORRECTED_BITS:
    case HwAsic::Feature::FEC_ERROR_DETECT_ENABLE:
    case HwAsic::Feature::CABLE_PROPOGATION_DELAY:
    case HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION:
    case HwAsic::Feature::VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS:
    case HwAsic::Feature::PORT_MTU_ERROR_TRAP:
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    // TODO(J4): re-enable metadata qualifiers once SDK support is validated.
    case HwAsic::Feature::ACL_METADATA_QUALIFER:
    case HwAsic::Feature::ROUTE_METADATA:
    case HwAsic::Feature::CBFC:
    case HwAsic::Feature::LINK_LAYER_RETRANSMISSION:
    case HwAsic::Feature::PORT_DEBOUNCE:
    case HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    case HwAsic::Feature::SLL_HLL_DISCARD_COUNTERS:
    case HwAsic::Feature::HW_PROTECTION_SWITCHOVER_COUNTERS:
    case HwAsic::Feature::PBR_ACL_BYTE_COUNTER:
    case HwAsic::Feature::NEXT_HOP_GROUP_MEMBER_MONITORED_OBJECT:
    case HwAsic::Feature::MY_MAC:
    case HwAsic::Feature::ISOLATION_GROUP:
    case HwAsic::Feature::CSIG:
    case HwAsic::Feature::ACL_ENTRY_OUT_PORT_QUALIFIER:
    case HwAsic::Feature::XPHY_SERDES_TX_FIR_POST3:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
    case HwAsic::Feature::PORT_SERDES_CREATED_WITH_PORT:
      return false;
  }
  return false;
}

std::set<cfg::StreamType> Jericho4Asic::getQueueStreamTypes(
    cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::CPU_PORT:
      return {cfg::StreamType::UNICAST};
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return {cfg::StreamType::UNICAST};
    case cfg::PortType::FABRIC_PORT:
      return {cfg::StreamType::FABRIC_TX};
  }
  throw FbossError(
      "Jericho4 ASIC does not support:",
      apache::thrift::util::enumNameSafe(portType));
}
int Jericho4Asic::getDefaultNumPortQueues(
    cfg::StreamType streamType,
    cfg::PortType portType) const {
  if (getAsicMode() == AsicMode::ASIC_MODE_SIM) {
    return 0;
  }
  switch (streamType) {
    case cfg::StreamType::UNICAST:
      switch (portType) {
        case cfg::PortType::CPU_PORT:
        case cfg::PortType::RECYCLE_PORT:
        case cfg::PortType::INTERFACE_PORT:
        case cfg::PortType::MANAGEMENT_PORT:
        case cfg::PortType::EVENTOR_PORT:
        case cfg::PortType::HYPER_PORT:
        case cfg::PortType::HYPER_PORT_MEMBER:
          return 8;
        case cfg::PortType::FABRIC_PORT:
          break;
      }
      break;
    case cfg::StreamType::MULTICAST:
      break;
    case cfg::StreamType::FABRIC_TX:
      if (portType != cfg::PortType::FABRIC_PORT) {
        break;
      }
      return 1;
    case cfg::StreamType::ALL:
      break;
  }
  throw FbossError(
      "Unexpected, stream: ",
      apache::thrift::util::enumNameSafe(streamType),
      " portType: ",
      apache::thrift::util::enumNameSafe(portType),
      " combination");
}

std::optional<uint64_t> Jericho4Asic::getDefaultReservedBytes(
    cfg::StreamType streamType,
    cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::CPU_PORT:
      return 1778;
    case cfg::PortType::RECYCLE_PORT:
      return 4096;
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
    case cfg::PortType::FABRIC_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return 0;
  }
  throw FbossError(
      "Unexpected, stream: ",
      apache::thrift::util::enumNameSafe(streamType),
      " portType: ",
      apache::thrift::util::enumNameSafe(portType),
      " combination");
}
cfg::Range64 Jericho4Asic::getReservedEncapIndexRange() const {
  return makeRange(0x200000, 0x300000);
}

HwAsic::RecyclePortInfo Jericho4Asic::getRecyclePortInfo(
    InterfaceNodeRole intfRole) const {
  if (intfRole == InterfaceNodeRole::DUAL_STAGE_EDGE_NODE) {
    CHECK(isDualStage3Q2QMode());
    return {
        .coreId = 2,
        .corePortIndex = 6,
        .speedMbps = 100000, // 100G
        .inbandPortId = 10,
    };
  } else if (isDualStage3Q2QMode()) {
    return {
        .coreId = 0,
        .corePortIndex = 13,
        .speedMbps = 100000, // 100G
        .inbandPortId = 16391,
    };
  }
  return {
      .coreId = 2,
      .corePortIndex = 2,
      .speedMbps = 100000, // 100G
      .inbandPortId = 1,
  };
}

const std::map<cfg::PortType, cfg::PortLoopbackMode>&
Jericho4Asic::desiredLoopbackModes() const {
  static const std::map<cfg::PortType, cfg::PortLoopbackMode> kLoopbackMode = {
      {cfg::PortType::INTERFACE_PORT, cfg::PortLoopbackMode::PHY},
      {cfg::PortType::HYPER_PORT_MEMBER, cfg::PortLoopbackMode::PHY},
      {cfg::PortType::MANAGEMENT_PORT, cfg::PortLoopbackMode::PHY},
      {cfg::PortType::FABRIC_PORT, cfg::PortLoopbackMode::MAC},
      {cfg::PortType::RECYCLE_PORT, cfg::PortLoopbackMode::NONE},
      {cfg::PortType::EVENTOR_PORT, cfg::PortLoopbackMode::NONE},
      {cfg::PortType::HYPER_PORT, cfg::PortLoopbackMode::PHY}};
  return kLoopbackMode;
}

HwAsic::AsicMode Jericho4Asic::getAsicMode() const {
  static const char* kSimPath = std::getenv("BCM_SIM_PATH");
  if (kSimPath) {
    return AsicMode::ASIC_MODE_SIM;
  }
  return AsicMode::ASIC_MODE_HW;
}

std::optional<uint32_t> Jericho4Asic::computePortGroupSkew(
    const std::map<PortID, uint32_t>& portId2cableLen) const {
  std::map<int, uint32_t> portGroup2MaxCableLen;
  auto updatePortGroupMax = [&portGroup2MaxCableLen](
                                int groupId, uint32_t cableLen) {
    auto pgItr = portGroup2MaxCableLen.find(groupId);
    auto currentMax = pgItr != portGroup2MaxCableLen.end() ? pgItr->second : 0;
    portGroup2MaxCableLen[groupId] = std::max(currentMax, cableLen);
  };
  auto const kPortGroups = getPortGroups();
  for (const auto& [portId, cableLen] : portId2cableLen) {
    auto portIdInt = static_cast<int>(portId);
    for (auto g = 0; g < kPortGroups.size(); ++g) {
      auto [portGroupStart, portGroupEnd] = kPortGroups.at(g);
      if (portIdInt >= portGroupStart && portIdInt <= portGroupEnd) {
        updatePortGroupMax(g, cableLen);
        continue;
      }
    }
  }
  std::set<uint32_t> portGroupMaxLensSorted;
  std::for_each(
      portGroup2MaxCableLen.begin(),
      portGroup2MaxCableLen.end(),
      [&portGroupMaxLensSorted](auto groupAndLen) {
        portGroupMaxLensSorted.insert(groupAndLen.second);
      });
  if (portGroupMaxLensSorted.empty()) {
    return std::nullopt;
  }
  return *portGroupMaxLensSorted.rbegin() - *portGroupMaxLensSorted.begin();
}

std::vector<std::pair<int, int>> Jericho4Asic::getPortGroups() const {
  constexpr auto kPortGroupStart = 1024;
  constexpr auto kPortGroupSize = 40;
  constexpr auto kNumPortGroups = 4;
  std::vector<std::pair<int, int>> portGroups;
  for (auto g = 0; g < kNumPortGroups; ++g) {
    auto portGroupStart = kPortGroupStart + g * kPortGroupSize;
    auto portGroupEnd = portGroupStart + kPortGroupSize - 1;
    portGroups.emplace_back(portGroupStart, portGroupEnd);
  }
  return portGroups;
}

int Jericho4Asic::getMidPriCpuQueueId() const {
  return kDefaultMidPriCpuQueueId;
}

int Jericho4Asic::getHiPriCpuQueueId() const {
  return kDefaultHiPriCpuQueueId;
}

std::optional<uint32_t> Jericho4Asic::getMaxEcmpGroups() const {
  return (isDualStage3Q2QMode() && FLAGS_ecmp_width >= 2048) ? 16 : 64;
}
} // namespace facebook::fboss
