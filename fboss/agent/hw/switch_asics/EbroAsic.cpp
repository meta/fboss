// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/EbroAsic.h"
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::fboss {

std::vector<HwAsic::InternalSystemPortConfig>
EbroAsic::getInternalSystemPortConfig(
    const CpuPortCoreAndPortIndex& /*cpuPortsCoreAndPortIdx*/) const {
  CHECK(getSwitchId()) << " Switch Id must be set before sys port info";
  auto switchIdVal = static_cast<uint32_t>(*getSwitchId());
  // Below are a mixture system port configs for
  // internal {loopback, CPU} ports. From the speeds (in Mbps)
  // one can infer that ports 6-10 are 1G CPU ports and 10-14 are
  // 100G loopback ports (TODO - confirm this with vendor)
  //
  return {
      {11, switchIdVal, 0, 25, 100000, 8},
      {12, switchIdVal, 2, 25, 100000, 8},
      {13, switchIdVal, 4, 25, 100000, 8},
      {14, switchIdVal, 6, 25, 100000, 8},
      {15, switchIdVal, 8, 25, 100000, 8},
      {16, switchIdVal, 10, 25, 100000, 8},
      {6, switchIdVal, 0, 24, 1000, 8},
      {7, switchIdVal, 4, 24, 1000, 8},
      {8, switchIdVal, 6, 24, 1000, 8},
      {9, switchIdVal, 8, 24, 1000, 8},
      {10, switchIdVal, 1, 24, 1000, 8}};
}

bool EbroAsic::isSupportedNonFabric(Feature feature) const {
  switch (feature) {
    /*
     * None of the features are verified on the asic.
     * Marking them as true for now but need to revisit
     * this as we verify the features.
     */
    case HwAsic::Feature::SPAN:
    case HwAsic::Feature::ERSPANv4:
    case HwAsic::Feature::SFLOWv4:
    case HwAsic::Feature::MPLS:
    case HwAsic::Feature::MPLS_ECMP:
    case HwAsic::Feature::ERSPANv6:
    case HwAsic::Feature::SFLOWv6:
    case HwAsic::Feature::ECN:
    case HwAsic::Feature::L3_QOS:
    case HwAsic::Feature::QOS_MAP_GLOBAL:
    case HwAsic::Feature::SMAC_EQUALS_DMAC_CHECK_ENABLED:
    case HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::SAI_WEIGHTED_NEXTHOPGROUP_MEMBER:
    case HwAsic::Feature::SWITCH_ATTR_INGRESS_ACL:
    case HwAsic::Feature::BUFFER_POOL:
    case HwAsic::Feature::MIRROR_PACKET_TRUNCATION:
    case HwAsic::Feature::SFLOW_SAMPLING:
    case HwAsic::Feature::SAI_ECN_WRED:
    case HwAsic::Feature::RESOURCE_USAGE_STATS:
    case HwAsic::Feature::BLACKHOLE_ROUTE_DROP_COUNTER:
    case HwAsic::Feature::PENDING_L2_ENTRY:
    case HwAsic::Feature::COUNTER_REFRESH_INTERVAL:
    case HwAsic::Feature::MAC_AGING:
    case HwAsic::Feature::TELEMETRY_AND_MONITORING:
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::DEFAULT_VLAN:
    case HwAsic::Feature::TRAFFIC_HASHING:
    case HwAsic::Feature::ACL_TABLE_GROUP:
    case HwAsic::Feature::CPU_PORT:
    case HwAsic::Feature::CPU_QUEUES:
    case HwAsic::Feature::VRF:
    case HwAsic::Feature::SAI_PORT_SERDES_FIELDS_RESET:
    case HwAsic::Feature::PTP_TC:
    case HwAsic::Feature::PTP_TC_PCS:
    case HwAsic::Feature::SAI_FEC_COUNTERS:
    case HwAsic::Feature::ROUTE_PROGRAMMING:
    case HwAsic::Feature::ECMP_HASH_V4:
    case HwAsic::Feature::ECMP_HASH_V6:
    case HwAsic::Feature::MEDIA_TYPE:
    case HwAsic::Feature::FEC:
    case HwAsic::Feature::ECMP_MEMBER_WIDTH_INTROSPECTION:
    case HwAsic::Feature::SAI_MPLS_INSEGMENT:
    case HwAsic::Feature::SAI_PORT_SPEED_CHANGE:
    case HwAsic::Feature::ROUTE_METADATA:
    case HwAsic::Feature::P4_WARMBOOT:
    case HwAsic::Feature::IN_PAUSE_INCREMENTS_DISCARDS:
    case HwAsic::Feature::PMD_RX_LOCK_STATUS:
    case HwAsic::Feature::PMD_RX_SIGNAL_DETECT:
    case HwAsic::Feature::FEC_AM_LOCK_STATUS:
    case HwAsic::Feature::PCS_RX_LINK_STATUS:
    case HwAsic::Feature::SAI_MPLS_TTL_1_TRAP:
    case HwAsic::Feature::SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER:
    case HwAsic::Feature::SAI_MPLS_QOS:
    case HwAsic::Feature::SAI_ACL_TABLE_UPDATE:
    case HwAsic::Feature::ROUTE_COUNTERS:
    case HwAsic::Feature::MULTIPLE_ACL_TABLES:
    case HwAsic::Feature::UDF_HASH_FIELD_QUERY:
    case HwAsic::Feature::SAI_SAMPLEPACKET_TRAP:
    case HwAsic::Feature::QUEUE_ECN_COUNTER:
    case HwAsic::Feature::SAI_CONFIGURE_SEVEN_TAP:
    case HwAsic::Feature::SAI_CONFIGURE_SIX_TAP:
    case HwAsic::Feature::SEPARATE_BYTE_AND_PACKET_ACL_COUNTER:
    case HwAsic::Feature::L3_MTU_ERROR_TRAP:
    case HwAsic::Feature::ACL_COUNTER_LABEL:
    case HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING:
    case HwAsic::Feature::PORT_WRED_COUNTER:
    case HwAsic::Feature::BRIDGE_PORT_8021Q:
    case HwAsic::Feature::WARMBOOT:
    case HwAsic::Feature::ACL_METADATA_QUALIFER:
    case HwAsic::Feature::PORT_SERDES_ZERO_PREEMPHASIS:
    case HwAsic::Feature::SAI_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::ENABLE_DELAY_DROP_CONGESTION_THRESHOLD:
    case HwAsic::Feature::L3_INTF_MTU:
    case HwAsic::Feature::CPU_QUEUE_WATERMARK_STATS:
    case HwAsic::Feature::BUFFER_POOL_HEADROOM_WATERMARK:
    case HwAsic::Feature::SAI_SET_TC_WITH_USER_DEFINED_TRAP_CPU_ACTION:
    case HwAsic::Feature::ANY_ACL_DROP_COUNTER:
    case HwAsic::Feature::EMPTY_ACL_MATCHER:
      return true;
    case HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE:
    case HwAsic::Feature::HOSTTABLE:
    case HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION:
    case HwAsic::Feature::QCM:
    case HwAsic::Feature::SCHEDULER_PPS:
    case HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_INTERFACE_TYPE:
    case HwAsic::Feature::HSDK:
    case HwAsic::Feature::OBJECT_KEY_CACHE:
    case HwAsic::Feature::L3_EGRESS_MODE_AUTO_ENABLED:
    case HwAsic::Feature::PKTIO:
    case HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER:
    case HwAsic::Feature::PORT_TX_DISABLE:
    case HwAsic::Feature::ZERO_SDK_WRITE_WARMBOOT:
    case HwAsic::Feature::OBM_COUNTERS:
    case HwAsic::Feature::EGRESS_QUEUE_FLEX_COUNTER:
    case HwAsic::Feature::PFC:
    case HwAsic::Feature::PFC_XON_TO_XOFF_COUNTER:
    case HwAsic::Feature::INGRESS_L3_INTERFACE:
    case HwAsic::Feature::NON_UNICAST_HASH:
    case HwAsic::Feature::DETAILED_L2_UPDATE:
    case HwAsic::Feature::WIDE_ECMP:
    case HwAsic::Feature::ALPM_ROUTE_PROJECTION:
    case HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD:
    case HwAsic::Feature::EGRESS_MIRRORING:
    case HwAsic::Feature::EGRESS_SFLOW:
    case HwAsic::Feature::SAI_LAG_HASH:
    case HwAsic::Feature::MACSEC:
    case HwAsic::Feature::SAI_HASH_FIELDS_CLEAR_BEFORE_SET:
    case HwAsic::Feature::ROUTE_FLEX_COUNTERS:
    case HwAsic::Feature::FEC_DIAG_COUNTERS:
    case HwAsic::Feature::PORT_EYE_VALUES:
    case HwAsic::Feature::SAI_PORT_ERR_STATUS:
    case HwAsic::Feature::EXACT_MATCH:
    case HwAsic::Feature::FEC_CORRECTED_BITS:
    case HwAsic::Feature::RX_FREQUENCY_PPM:
    case HwAsic::Feature::SAI_FIRMWARE_PATH:
    case HwAsic::Feature::EXTENDED_FEC:
    case HwAsic::Feature::LINK_TRAINING:
    case HwAsic::Feature::SAI_RX_REASON_COUNTER:
    case HwAsic::Feature::VOQ:
    case HwAsic::Feature::XPHY_PORT_STATE_TOGGLE:
    case HwAsic::Feature::SAI_PORT_GET_PMD_LANES:
    case HwAsic::Feature::FABRIC_TX_QUEUES:
    case HwAsic::Feature::SAI_PORT_VCO_CHANGE:
    case HwAsic::Feature::SAI_TTL0_PACKET_FORWARD_ENABLE:
    case HwAsic::Feature::SHARED_INGRESS_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::ARS:
    case HwAsic::Feature::ASIC_RESET_NOTIFICATIONS:
    case HwAsic::Feature::TC_TO_QUEUE_QOS_MAP_ON_SYSTEM_PORT:
    case HwAsic::Feature::PORT_FABRIC_ISOLATE:
    case HwAsic::Feature::CPU_TX_VIA_RECYCLE_PORT:
    case HwAsic::Feature::SWITCH_DROP_STATS:
    case HwAsic::Feature::PACKET_INTEGRITY_DROP_STATS:
    case HwAsic::Feature::SAI_UDF_HASH:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_HEADROOM_WATERMARK:
    case HwAsic::Feature::RX_LANE_SQUELCH_ENABLE:
    case HwAsic::Feature::SAI_PORT_ETHER_STATS:
    case HwAsic::Feature::SLOW_STAT_UPDATE:
    case HwAsic::Feature::VOQ_DELETE_COUNTER:
    case HwAsic::Feature::DRAM_ENQUEUE_DEQUEUE_STATS:
    case HwAsic::Feature::ARS_PORT_ATTRIBUTES:
    // Ebro Asic does not support EAPOL trap for Macsec. Currently this is
    // needed only for FAUU and DMAG devices.
    case HwAsic::Feature::SAI_EAPOL_TRAP:
    case HwAsic::Feature::SAI_USER_DEFINED_TRAP:
    case HwAsic::Feature::CREDIT_WATCHDOG:
    case HwAsic::Feature::ECMP_DLB_OFFSET:
    case HwAsic::Feature::SAI_FEC_CORRECTED_BITS:
    case HwAsic::Feature::SAI_FEC_CODEWORDS_STATS:
    case HwAsic::Feature::SAI_FEC_SYMBOL_ERRORS:
    case HwAsic::Feature::LINK_INACTIVE_BASED_ISOLATE:
    case HwAsic::Feature::SWITCH_ISOLATE:
    case HwAsic::Feature::RX_SNR:
    case HwAsic::Feature::MANAGEMENT_PORT:
    case HwAsic::Feature::EGRESS_FORWARDING_DROP_COUNTER:
    case HwAsic::Feature::ANY_TRAP_DROP_COUNTER:
    case HwAsic::Feature::SWITCH_DROP_DEBUG_COUNTER:
    case HwAsic::Feature::FABRIC_PORTS:
    case HwAsic::Feature::RCI_WATERMARK_COUNTER:
    case HwAsic::Feature::DTL_WATERMARK_COUNTER:
    case HwAsic::Feature::LINK_ACTIVE_INACTIVE_NOTIFY:
    case HwAsic::Feature::WEIGHTED_NEXTHOPGROUP_MEMBER:
    case HwAsic::Feature::PQP_ERROR_EGRESS_DROP_COUNTER:
    case HwAsic::Feature::FABRIC_LINK_DOWN_CELL_DROP_COUNTER:
    case HwAsic::Feature::CRC_ERROR_DETECT:
    case HwAsic::Feature::EVENTOR_PORT_FOR_SFLOW:
    case HwAsic::Feature::SWITCH_REACHABILITY_CHANGE_NOTIFY:
    case HwAsic::Feature::CABLE_PROPOGATION_DELAY:
    case HwAsic::Feature::DATA_CELL_FILTER:
    case HwAsic::Feature::DRAM_BLOCK_TIME:
    case HwAsic::Feature::VOQ_LATENCY_WATERMARK_BIN:
    case HwAsic::Feature::ACL_ENTRY_ETHER_TYPE:
    case HwAsic::Feature::EGRESS_CORE_BUFFER_WATERMARK:
    case HwAsic::Feature::DELETED_CREDITS_STAT:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_DROPPED_PACKETS:
    case HwAsic::Feature::NO_RX_REASON_TRAP:
    case HwAsic::Feature::EGRESS_GVOQ_WATERMARK_BYTES:
    case HwAsic::Feature::INGRESS_PRIORITY_GROUP_SHARED_WATERMARK:
    case HwAsic::Feature::MULTIPLE_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::PORT_MTU_ERROR_TRAP:
    case HwAsic::Feature::DEDICATED_CPU_BUFFER_POOL:
    case HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE:
    case HwAsic::Feature::FAST_LLFC_COUNTER:
    case HwAsic::Feature::INGRESS_SRAM_MIN_BUFFER_WATERMARK:
    case HwAsic::Feature::FDR_FIFO_WATERMARK:
    case HwAsic::Feature::EGRESS_CELL_ERROR_STATS:
    case HwAsic::Feature::SAMPLE_RATE_CONFIG_PER_MIRROR:
    case HwAsic::Feature::SFLOW_SAMPLES_PACKING:
    case HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION:
    case HwAsic::Feature::SDK_REGISTER_DUMP:
    case HwAsic::Feature::FEC_ERROR_DETECT_ENABLE:
    case HwAsic::Feature::SAI_HOST_MISS_TRAP:
    case HwAsic::Feature::CPU_TX_PACKET_REQUIRES_VLAN_TAG:
    case HwAsic::Feature::DRAM_DATAPATH_PACKET_ERROR_STATS:
    case HwAsic::Feature::EGRESS_POOL_AVAILABLE_SIZE_ATTRIBUTE_SUPPORTED:
    case HwAsic::Feature::SWITCH_ASIC_SDK_HEALTH_NOTIFY:
    case HwAsic::Feature::VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS:
    case HwAsic::Feature::PFC_WATCHDOG_TIMER_GRANULARITY:
    case HwAsic::Feature::RX_SERDES_PARAMETERS:
    case HwAsic::Feature::SAI_PORT_IN_CONGESTION_DISCARDS:
    case HwAsic::Feature::TEMPERATURE_MONITORING:
    case HwAsic::Feature::ROUTER_INTERFACE_STATISTICS:
    case HwAsic::Feature::AGGREGATE_PORT_ROUTER_INTERFACE:
    case HwAsic::Feature::CPU_PORT_EGRESS_BUFFER_POOL:
    case HwAsic::Feature::ACL_SET_ECMP_HASH_ALGORITHM:
    case HwAsic::Feature::SET_NEXT_HOP_GROUP_HASH_ALGORITHM:
    case HwAsic::Feature::BULK_CREATE_ECMP_MEMBER:
    case HwAsic::Feature::TECH_SUPPORT:
    case HwAsic::Feature::DRAM_QUARANTINED_BUFFER_STATS:
    case HwAsic::Feature::MANAGEMENT_PORT_MULTICAST_QUEUE_ALPHA:
    case HwAsic::Feature::SAI_PORT_PG_DROP_STATUS:
    case HwAsic::Feature::FABRIC_INTER_CELL_JITTER_WATERMARK:
    case HwAsic::Feature::MAC_TRANSMIT_DATA_QUEUE_WATERMARK:
    case HwAsic::Feature::ARS_ALTERNATE_MEMBERS:
    case HwAsic::Feature::FABRIC_LINK_MONITORING:
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
    case HwAsic::Feature::SLL_HLL_DISCARD_COUNTERS:
    case HwAsic::Feature::HW_PROTECTION_SWITCHOVER_COUNTERS:
    case HwAsic::Feature::PBR_ACL_BYTE_COUNTER:
    case HwAsic::Feature::SRV6_MYSID_RESOURCE_COUNTER:
    case HwAsic::Feature::SRV6_BINDING_USID_UA_USD:
    case HwAsic::Feature::PBR_ACL:
    case HwAsic::Feature::DEVICE_WATERMARK_SUPPORT:
    case HwAsic::Feature::ECN_PROBABILISTIC_MARKING:
    case HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT:
    case HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT:
    case HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL:
    case HwAsic::Feature::CBFC:
    case HwAsic::Feature::LINK_LAYER_RETRANSMISSION:
    case HwAsic::Feature::PORT_DEBOUNCE:
    case HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE:
    case HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS:
    case HwAsic::Feature::NEXT_HOP_GROUP_MEMBER_MONITORED_OBJECT:
    case HwAsic::Feature::RX_PACKET_TYPE:
    case HwAsic::Feature::MY_MAC:
    case HwAsic::Feature::ISOLATION_GROUP:
    case HwAsic::Feature::CSIG:
    case HwAsic::Feature::PORT_SERDES_CREATED_WITH_PORT:
    case HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL:
    case HwAsic::Feature::SAI_MPLS_NEXTHOP_OUTSEG_TYPE:
    case HwAsic::Feature::ACL_ENTRY_OUT_PORT_QUALIFIER:
    case HwAsic::Feature::XPHY_SERDES_TX_FIR_POST3:
    case HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE:
    case HwAsic::Feature::PORT_LATCH_STATUS:
      return false;
    case HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER:
    case HwAsic::Feature::SAI_PRBS:
    case HwAsic::Feature::ACL_BYTE_COUNTER:
      /*
       * Source port ACL qualifier is used in certain HW tests
       * to trap packets to CPU. This feature is supported only
       * with P4 warmboot supported SDKs.
       * 1.42.* SDK uses DIP based ACL qualifier in HW tests
       * whereas P4 WB capable SDKs use Source Port qualifier
       */
      return isP4WarmbootEnabled();
  }
  return false;
}

bool EbroAsic::isSupportedFabric(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
      return true;
    default:
      return false;
  }
}

int EbroAsic::getDefaultNumPortQueues(
    cfg::StreamType streamType,
    cfg::PortType /*portType*/) const {
  switch (streamType) {
    case cfg::StreamType::MULTICAST:
      throw FbossError(
          "no queue exist for stream type: ",
          apache::thrift::util::enumNameSafe(streamType));
    case cfg::StreamType::FABRIC_TX:
      return 1;
    case cfg::StreamType::UNICAST:
    case cfg::StreamType::ALL:
      return 8;
  }

  throw FbossError("Unknown streamType", streamType);
}
std::set<cfg::StreamType> EbroAsic::getQueueStreamTypes(
    cfg::PortType portType) const {
  switch (portType) {
    case cfg::PortType::CPU_PORT:
    case cfg::PortType::INTERFACE_PORT:
    case cfg::PortType::MANAGEMENT_PORT:
      /*
       * For Ebro asic, SDK 1.42.* queue type is ALL whereas SDK 1.56.* queue
       * type is unicast. Once SDK 1.56.* is rolled out after P4 warmboot
       * support across the entire fleet, the stream type can be encoded as
       * Unicast.
       */
      return {getDefaultStreamType()};
    case cfg::PortType::FABRIC_PORT:
      return {cfg::StreamType::FABRIC_TX};
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      // TODO: handle when we start modeling
      // recycle port for Ebro ASIC
      break;
  }
  throw FbossError(
      "Ebro ASIC does not support:",
      apache::thrift::util::enumNameSafe(portType));
}
cfg::Range64 EbroAsic::getReservedEncapIndexRange() const {
  if (getSwitchType() == cfg::SwitchType::VOQ) {
    // Reserved range worked out with vendor. These ids
    // are reserved in SAI-SDK implementation for use
    // by NOS
    return makeRange(100, 4093);
  }
  return HwAsic::getReservedEncapIndexRange();
}

const std::map<cfg::PortType, cfg::PortLoopbackMode>&
EbroAsic::desiredLoopbackModes() const {
  switch (getSwitchType()) {
    case cfg::SwitchType::NPU: {
      static const std::map<cfg::PortType, cfg::PortLoopbackMode>
          kDefaultLoopbackMode = {
              {cfg::PortType::INTERFACE_PORT, cfg::PortLoopbackMode::MAC}};
      return kDefaultLoopbackMode;
    }
    case cfg::SwitchType::VOQ: {
      static const std::map<cfg::PortType, cfg::PortLoopbackMode>
          kDefaultLoopbackMode = {
              {cfg::PortType::INTERFACE_PORT, cfg::PortLoopbackMode::MAC},
              {cfg::PortType::FABRIC_PORT, cfg::PortLoopbackMode::MAC}};
      return kDefaultLoopbackMode;
    }
    case cfg::SwitchType::FABRIC: {
      static const std::map<cfg::PortType, cfg::PortLoopbackMode>
          kDefaultLoopbackMode = {
              {cfg::PortType::FABRIC_PORT, cfg::PortLoopbackMode::MAC}};
      return kDefaultLoopbackMode;
    }
    case cfg::SwitchType::PHY:
      /* unsupported */
      break;
  }
  throw FbossError("Unsupported switchType for Ebro: ", getSwitchType());
}
} // namespace facebook::fboss
