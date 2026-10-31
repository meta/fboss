// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fboss/lib/phy/gen-cpp2/phy_types.h>
#include <folly/MacAddress.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/lib/if/gen-cpp2/fboss_common_types.h"

namespace facebook::fboss {

class HwAsic {
 public:
  HwAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo,
      std::optional<cfg::SdkVersion> sdkVersion = std::nullopt,
      std::unordered_set<cfg::SwitchType> supportedModes = {
          cfg::SwitchType::NPU});
  enum class Feature {
    // ACL Features::
    // ==============
    //
    // ACL Entry:
    //   - Consists of Matcher and Action
    //   - Matcher: check if patcket matches criteria e.g. specific source IP.
    //   - Action: perform action on match e.g. drop packet
    //
    // ACL Counter:
    //   - Can be optionally attached to an ACL entry.
    //   - Can be configured to count the number of bytes and/or packets that
    //     matched that ACL entry.
    //
    // ACL Table:
    //  - Ordered list of ACL entries
    //  - Only the first matching ACL entry and its Action take effect.
    //
    // ACL Table Group:
    //  - Group of ACL Tables
    //  - First matching ACL entry in each table and its Action takes effect.
    //
    // Bind point:
    //  - Where to attach the ACL e.g. switch (global), per port etc.
    //  - FBOSS supports only switch level ACLs i.e. ACL will be checked for
    //    very packet in the switch.
    //
    // Stage:
    //  - Stage in the pipeline the ACL will be checked.
    //  - For example: ingress, egress etc.
    //

    // Set to true if Multiple ACL Tables are supported
    // TODO:
    //  - Candidate for removal: YES, ACL_TABLE_GROUP is enabled everywhere.
    //  - Consolidate to a single feature prefixed with ACL_
    MULTIPLE_ACL_TABLES,
    ACL_TABLE_GROUP,

    // Set to true if SAI implementations allow individually enabling Packet
    // counter and byte counter.
    // On some SAI implementations, supporting this is non-trivial. Those SAI
    // implementations enable bytes as well as packet counters even if only one
    // of the two is enabled.
    // TODO:
    //   - Candidate for removal: YES. FBOSS use case does not require enabling
    //     only one. Enforce that either both are enabled or neither is enabled
    //     on every platform, and then remove this feature.
    //   - If we decide to keep the feature, rename to carry ACL_ prefix.
    SEPARATE_BYTE_AND_PACKET_ACL_COUNTER,

    // Set to true if Ingress ACLs are supported.
    // Used to bind an ACL Table or ACL Table Group to a switch using
    // SAI_SWITCH_ATTR_INGRESS_ACL
    // TODO:
    //  - Candidate for removal: YES. All ASICs/SDKs already support this.
    SWITCH_ATTR_INGRESS_ACL,

    // Set to true if Egress ACL Table is supported.
    // However, this Table sits post lookup but before buffering.
    // Thus, named as INGRESS_POST_LOOKUP_ACL_TABLE.
    // For SAI, this maps to SAI_ACL_STAGE_EGRESS.
    // TODO:
    //  - Rename to carry ACL_ prefix.
    INGRESS_POST_LOOKUP_ACL_TABLE,

    // Set to true if Empty Matcher can be used to match all packets.
    // TODO:
    //  - Rename to carry ACL_ prefix.
    EMPTY_ACL_MATCHER,

    // Set to true if Metadata qualifier is supported for ACLs.
    // Metadata is an integer field that can be associated with an L2 entry,
    // Neighbor entry, Route entry etc. When that L2/Neighbor/Route enetry is
    // hit, the corresponding Metadata is  associated with the packet. This
    // packet can match any ACL that carries the same Metadata in the matcher
    // field and corresponding ACL action takes effect.
    ACL_METADATA_QUALIFER,

    // Set to true if the SAI implementation supports setting a
    // label (string identifier) for an ACL counter.
    // This label can be used to uniquely identify ACL counter
    // object during warmboot.  The support was added by SAI community PR 1430,
    // and is part of the SAI spec since v1.10.0.
    ACL_COUNTER_LABEL,

    // Set to true if the SAI implementation supports Ether type as ACL matcher
    // For SAI, this maps to whether SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE can be
    // set during ACL Table creation.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec sai_acl_capability_t to return the list of ACL
    //    matchers supported.
    //  - Work with SAI implementations to support this capability.
    //  - Extend FBOSS to query supported matchers for a given stage e.g.
    //    SAI_SWITCH_ATTR_ACL_STAGE_INGRESS
    ACL_ENTRY_ETHER_TYPE,

    // Set to true if the SAI implementation supports ACL matchers for
    // destination IPv6 word3 and word2.
    ACL_DST_IPV6_WORD_QUALIFIERS,

    // Set to true if the SAI implementation supports ACL Byte counters
    // For SAI, this maps to whether SAI_ACL_COUNTER_ATTR_BYTES can be queried.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec sai_acl_capability_t to return whether ACL Byte
    //    counters are supported.
    //  - Work with SAI implementations to support this capability.
    //  - Extend FBOSS to query whether ACL Byte counters are supported.
    ACL_BYTE_COUNTER,

    // Packet is dropped due to configured ACL rules, all stages/bind points
    // For SAI this maps to whether debug counter SAI_IN_DROP_REASON_ACL_ANY
    // can be queried.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec to support capability_t for Debug counters.
    //  - Work with SAI implementations to support this capability.
    //  - Extend FBOSS to query.
    //  - Rename to carry ACL_ prefix.
    ANY_ACL_DROP_COUNTER,

    // Set to true if the SAI implementation supports Ether type as ACL matcher
    // For SAI, this maps to whether SAI_ACL_TABLE_ATTR_FIELD_{IN, SRC}_PORT can
    // be set during ACL Table creation.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec sai_acl_capability_t to return the list of ACL
    //    matchers supported.
    //  - Work with SAI implementations to support this capability.
    //  - Extend FBOSS to query supported matchers for a given stage e.g.
    //    SAI_SWITCH_ATTR_ACL_STAGE_INGRESS
    //  - Rename to carry ACL_ prefix.
    SAI_ACL_ENTRY_SRC_PORT_QUALIFIER,

    // Set to true if the egress port (AclEntry.dstPort) can be used as an
    // ingress ACL matcher. For SAI, this maps to whether
    // SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT is set during ingress ACL table
    // creation.
    ACL_ENTRY_OUT_PORT_QUALIFIER,

    // Set to true if the SAI implementation supports ACL action to set hash
    // algorithm. For SAI, this maps to whether
    // SAI_ACL_ACTION_TYPE_SET_ECMP_HASH_ALGORITHM
    // ACL action is supported.
    // TODO:
    //  - Candidate for removal: YES.
    //  - switch_config.AclTable already carries list of actionTypes.
    //  - Start populating that field in the config.
    //  - Populate SAI_ACL_ACTION_TYPE_SET_ECMP_HASH_ALGORITHM action only for
    //    SAI implementations that support this action.
    ACL_SET_ECMP_HASH_ALGORITHM,

    // Set to true if flex counters are supported by ingress field processor.
    // TODO:
    //  - Candidate for removal: YES.
    //  - Only used by non-SAI. Once we completely migrate to SAI, remove.
    INGRESS_FIELD_PROCESSOR_FLEX_COUNTER,

    SAI_ACL_TABLE_UPDATE,

    // ECMP, Hashing Feature::
    // =======================
    //
    // ECMP (Equal Cost Multi Path):
    //  - Routing technique to efficiently utilize multiple paths
    //    between two endpoints when those paths have the same cost.
    //
    // UCMP (Unequal Cost Multi Path):
    //  - Routing technique to distribute traffic across multiple paths
    //    with different costs, in proportion to their assigned weights.
    //  - Native UCMP: Can program weights directly to hardware.
    //  - Legacy UCMP: Replicate ECMP members in proportion to their weights.
    //
    // Hash Algorithm:
    //  - Acts on a set of configured fields (L4 src/dst port, src/dst IP etc.)
    //    and computes a hash value. The value is used to select a path.
    //
    // ECMP Width:
    //  - Number of next hops (paths) in a ECMP group.
    //  - Property of ASIC/SAI implementation.
    //
    // Wide ECMP:
    //  - Some ASICs support every ECMP width value [0, ecmp_width]
    //  - For > ecmp_width, they only support fixed values.
    //  - e.g. TH4 ECMP widths: [1-512], 1K, 2K, 4K

    // Set to true if the ASIC supports Native UCMP.
    // Only used by BcmSwitch.
    // Thus, remove after migration to SaiSwitch is completed.
    WEIGHTED_NEXTHOPGROUP_MEMBER,

    // Set to true if the ASIC or SAI implementation supports Native UCMP.
    // In either case, FBOSS need not implement replication.
    // Only used by SaiSwitch.
    // TODO:
    //  - Candidate for removal: YES, enabled everywhere except Fake. Fix Fake
    //    support, then remove.
    //  - Rename to carry ECMP_ prefix.
    SAI_WEIGHTED_NEXTHOPGROUP_MEMBER,

    // Set to true if Wide ECMP (possibly with some restriction like fixed power
    // of 2) is supported by the ASIC.
    WIDE_ECMP,

    // Set to true if the SAI implementation supports querying the number of
    // ECMP members supported across all nexthop groups of the switch.
    // This maps to SAI_SWITCH_ATTR_MAX_ECMP_MEMBER_COUNT.
    // TODO:
    //  - Candidate for removal: YES, enabled everywhere except Chenab.
    //  - Collaborate to add this support on Chenab, then remove.
    ECMP_MEMBER_WIDTH_INTROSPECTION,

    // Set to true if the SAI implementation supports bulk add and
    // remove of the nexthop group members.
    // Bulk add/remove is signifacntly faster than individual member add/remove.
    // TODO:
    //  - Rename to carry ECMP_ prefix.
    BULK_CREATE_ECMP_MEMBER,

    // Set to true if the SAI implementation supports associating a specific
    // hashing policy for IPv4 packets.
    // This maps to SAI_SWITCH_ATTR_ECMP_HASH_IPV4.
    // Some ASICs support hashing natively and thus do not support this
    // configuration.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec to introduce sai_hash_capability_t to return the list
    //    of hash configuraton supported.
    ECMP_HASH_V4,

    // Set to true if the SAI implementation supports associating a specific
    // hashing policy for IPv6 packets.
    // This maps to SAI_SWITCH_ATTR_ECMP_HASH_IPV6.
    // Some ASICs support hashing natively and thus do not support this
    // configuration.
    // TODO:
    //  - Candidate for removal: YES, but non-trivial.
    //  - Extend SAI spec to introduce sai_hash_capability_t to return the list
    //    of hash configuraton supported.
    ECMP_HASH_V6,

    // Set to true if ECMP configuration is supported with MPLS.
    // TODO:
    //  - Rename to carry ECMP_ prefix.
    MPLS_ECMP,

    HASH_FIELDS_CUSTOMIZATION,
    TRAFFIC_HASHING,
    SAI_ECMP_HASH_ALGORITHM,
    SET_NEXT_HOP_GROUP_HASH_ALGORITHM,
    ECMP_DLB_OFFSET,
    ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL,

    // MPLS Features::
    // ================
    //
    // MPLS (Multi-Protocol Label Switching):
    //  - Layer 2.5 forwarding technique using short fixed-length labels
    //    instead of long network addresses to make forwarding decisions.
    //
    // Label:
    //  - 20-bit identifier in the MPLS shim header, along with 3-bit EXP
    //    (QoS), 1-bit bottom-of-stack, and 8-bit TTL fields.
    //
    // In-Segment (InSeg) Entry:
    //  - An entry in the Label Forwarding Information Base (LFIB) that maps
    //    an incoming label to a forwarding action (swap, push, or pop).
    //
    // EXP (Experimental) bits:
    //  - 3-bit field in the MPLS header used for QoS classification.
    //  - Can be mapped to/from internal Traffic Class (TC) and packet color.
    //
    // Two SDK paradigms:
    //  - Legacy BCM SDK: MPLS programmed via BCM API (Feature::MPLS).
    //  - SAI: MPLS programmed via SAI InSeg entries
    //  (Feature::SAI_MPLS_INSEGMENT).
    //

    // Set to true if the ASIC supports base MPLS label switching via the
    // legacy BCM SDK path. Gates CoPP MPLS no-match ACL rules, BCM label
    // switch action programming, and MPLS load balancing tests.
    // On SAI platforms, MPLS forwarding uses SAI_MPLS_INSEGMENT instead.
    // TODO(pshaikh):
    //  - Candidate for removal: YES, once all platforms migrate to SAI.
    //    The BCM SDK MPLS path will no longer be needed.
    MPLS,

    // Set to true if the SAI implementation supports MPLS EXP-to-TC QoS
    // mapping. When enabled, creates bidirectional mappings between the
    // 3-bit MPLS EXP field and internal Traffic Class (TC) / packet color.
    // For SAI, this maps to SAI_QOS_MAP_TYPE_MPLS_EXP_TO_TC (ingress) and
    // SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_MPLS_EXP (egress marking).
    SAI_MPLS_QOS,

    // Set to true if the SAI implementation supports trapping MPLS packets
    // with TTL=1 to the CPU. When enabled, CoPP maps PacketRxReason::MPLS_TTL_1
    // to a low-priority CPU queue for TTL-expired MPLS packet handling.
    // For SAI, this maps to SAI_HOSTIF_TRAP_TYPE_MPLS_TTL_ERROR with
    // SAI_PACKET_ACTION_TRAP.
    SAI_MPLS_TTL_1_TRAP,

    // Set to true if the SAI implementation supports matching on the TTL of
    // the outermost MPLS label in an ACL. For SAI, this maps to
    // SAI_ACL_TABLE_ATTR_FIELD_MPLS_LABEL0_TTL and the corresponding
    // SAI_ACL_ENTRY_ATTR_FIELD_MPLS_LABEL0_TTL.
    //
    // The SAI enumerators have existed since spec 1.6.3, so this gates the
    // implementation rather than the symbol: an SDK that does not implement
    // the qualifier fails sai_acl_table_create() with
    // SAI_STATUS_INVALID_ATTR_VALUE instead of ignoring it.
    SAI_ACL_MPLS_LABEL0_TTL,

    // Set to true if the SAI implementation honours
    // SAI_NEXT_HOP_ATTR_OUTSEG_TYPE on an MPLS next hop. When it is not set,
    // the adapter falls back to the SAI default of SAI_OUTSEG_TYPE_SWAP, so
    // head-end imposition silently forwards the packet unlabelled.
    SAI_MPLS_NEXTHOP_OUTSEG_TYPE,

    // Set to true if the SAI implementation supports counting packets dropped
    // due to MPLS label lookup failure. Creates a SAI debug counter with drop
    // reason SAI_IN_DROP_REASON_MPLS_MISS, exposed as a per-port stat via
    // hwPortStats.inLabelMissDiscards.
    // Requires SAI API version >= 1.9.0.
    // TODO(pshaikh):
    //  - Support in FakeAsic for testing.
    SAI_MPLS_LABEL_LOOKUP_FAIL_COUNTER,

    // Set to true if the SAI implementation supports programming MPLS
    // in-segment (InSeg) entries in the Label FIB. This is the SAI-native
    // replacement for the legacy BCM Feature::MPLS path. Gates LFIB delta
    // processing in SaiSwitch::stateChanged() via SaiInSegEntryManager,
    // which creates SAI InSeg entries with SAI_PACKET_ACTION_FORWARD
    // and pop count of 1 for swap, push, and POP_AND_LOOKUP operations.
    SAI_MPLS_INSEGMENT,

    // Other features
    SPAN,
    ERSPANv4,
    ERSPANv6,
    SFLOWv4,
    SFLOWv6,
    ECN,
    L3_QOS,
    QOS_MAP_GLOBAL,
    QCM,
    SCHEDULER_PPS,
    SMAC_EQUALS_DMAC_CHECK_ENABLED,
    NEXTHOP_TTL_DECREMENT_DISABLE,
    PORT_TTL_DECREMENT_DISABLE,
    PORT_INTERFACE_TYPE,
    BLACKHOLE_ROUTE_DROP_COUNTER,
    RESOURCE_USAGE_STATS,
    HSDK,
    OBJECT_KEY_CACHE,
    L3_EGRESS_MODE_AUTO_ENABLED,
    SAI_ECN_WRED,
    PKTIO,
    HOSTTABLE,
    PORT_TX_DISABLE,
    ZERO_SDK_WRITE_WARMBOOT,
    OBM_COUNTERS,
    BUFFER_POOL,
    MIRROR_PACKET_TRUNCATION,
    SFLOW_SAMPLING,
    PTP_TC,
    PTP_TC_PCS,
    PENDING_L2_ENTRY,
    EGRESS_QUEUE_FLEX_COUNTER,
    PFC,
    PFC_XON_TO_XOFF_COUNTER, // not supported on MORGAN800CC/KODIAK3
    INGRESS_L3_INTERFACE,
    NON_UNICAST_HASH,
    DETAILED_L2_UPDATE,
    COUNTER_REFRESH_INTERVAL,
    TELEMETRY_AND_MONITORING,
    ALPM_ROUTE_PROJECTION,
    MAC_AGING,
    SAI_PORT_SPEED_CHANGE,
    SFLOW_SHIM_VERSION_FIELD,
    REMOVE_PORTS_FOR_COLDBOOT,
    EGRESS_MIRRORING,
    EGRESS_SFLOW,
    DEFAULT_VLAN,
    SAI_LAG_HASH,
    MACSEC,
    CPU_PORT,
    VRF,
    SAI_HASH_FIELDS_CLEAR_BEFORE_SET,
    SAI_PORT_SERDES_FIELDS_RESET,
    ROUTE_COUNTERS,
    ROUTE_FLEX_COUNTERS,
    BRIDGE_PORT_8021Q,
    FEC_DIAG_COUNTERS,
    PORT_EYE_VALUES,
    PMD_RX_LOCK_STATUS,
    PMD_RX_SIGNAL_DETECT,
    SAI_FEC_COUNTERS,
    SAI_PORT_ERR_STATUS,
    PORT_LATCH_STATUS,
    EXACT_MATCH,
    ROUTE_PROGRAMMING,
    FEC_CORRECTED_BITS,
    MEDIA_TYPE,
    FEC,
    RX_FREQUENCY_PPM,
    RX_SERDES_PARAMETERS,
    FABRIC_PORTS,
    SAI_FIRMWARE_PATH,
    EXTENDED_FEC,
    LINK_TRAINING,
    SAI_RX_REASON_COUNTER,
    RESERVED_ENCAP_INDEX_RANGE,
    VOQ,
    XPHY_PORT_STATE_TOGGLE,
    SAI_PORT_GET_PMD_LANES,
    FABRIC_TX_QUEUES,
    SAI_PORT_VCO_CHANGE,
    SAI_TTL0_PACKET_FORWARD_ENABLE,
    WARMBOOT,
    SHARED_INGRESS_EGRESS_BUFFER_POOL,
    ROUTE_METADATA,
    ARS,
    P4_WARMBOOT,
    IN_PAUSE_INCREMENTS_DISCARDS,
    IN_DISCARDS_EXCLUDES_PFC,
    FEC_AM_LOCK_STATUS,
    PCS_RX_LINK_STATUS,
    TC_TO_QUEUE_QOS_MAP_ON_SYSTEM_PORT,
    SAI_CONFIGURE_SIX_TAP,
    UDF_HASH_FIELD_QUERY,
    SAI_SAMPLEPACKET_TRAP,
    PORT_FABRIC_ISOLATE,
    QUEUE_ECN_COUNTER,
    CPU_TX_VIA_RECYCLE_PORT,
    SAI_CONFIGURE_SEVEN_TAP,
    SWITCH_DROP_STATS,
    PACKET_INTEGRITY_DROP_STATS,
    SAI_UDF_HASH,
    INGRESS_PRIORITY_GROUP_HEADROOM_WATERMARK,
    RX_LANE_SQUELCH_ENABLE,
    SAI_PORT_ETHER_STATS,
    SLOW_STAT_UPDATE, // pending CS00012299308
    VOQ_DELETE_COUNTER,
    DRAM_ENQUEUE_DEQUEUE_STATS,
    ARS_PORT_ATTRIBUTES,
    SAI_EAPOL_TRAP,
    // pending CS00012311423
    L3_MTU_ERROR_TRAP,
    SAI_USER_DEFINED_TRAP,
    CREDIT_WATCHDOG,
    // SAI_PORT_STAT_IF_IN_FEC_CORRECTED_BITS
    SAI_FEC_CORRECTED_BITS,
    // SAI_PORT_STAT_IF_IN_FEC_SYMBOL_ERRORS,
    SAI_FEC_SYMBOL_ERRORS,
    SAI_FEC_CODEWORDS_STATS,
    LINK_INACTIVE_BASED_ISOLATE,
    SWITCH_ISOLATE,
    SAI_PORT_SERDES_PROGRAMMING,
    RX_SNR,
    MANAGEMENT_PORT,
    ANY_TRAP_DROP_COUNTER,
    PORT_WRED_COUNTER,
    PORT_SERDES_ZERO_PREEMPHASIS,
    EGRESS_FORWARDING_DROP_COUNTER,
    SWITCH_DROP_DEBUG_COUNTER,
    SAI_PRBS,
    RCI_WATERMARK_COUNTER,
    DTL_WATERMARK_COUNTER,
    LINK_ACTIVE_INACTIVE_NOTIFY,
    PQP_ERROR_EGRESS_DROP_COUNTER,
    FABRIC_LINK_DOWN_CELL_DROP_COUNTER,
    CRC_ERROR_DETECT,
    EVENTOR_PORT_FOR_SFLOW,
    SWITCH_REACHABILITY_CHANGE_NOTIFY,
    CABLE_PROPOGATION_DELAY,
    DRAM_BLOCK_TIME,
    VOQ_LATENCY_WATERMARK_BIN,
    DATA_CELL_FILTER,
    EGRESS_CORE_BUFFER_WATERMARK,
    DELETED_CREDITS_STAT,
    INGRESS_PRIORITY_GROUP_DROPPED_PACKETS,
    // replace all ACL based trap reasons by
    // explicitly ACL config programmed by FBOSS
    NO_RX_REASON_TRAP,
    EGRESS_GVOQ_WATERMARK_BYTES,
    INGRESS_PRIORITY_GROUP_SHARED_WATERMARK,
    MULTIPLE_EGRESS_BUFFER_POOL,
    ENABLE_DELAY_DROP_CONGESTION_THRESHOLD,
    PORT_MTU_ERROR_TRAP,
    L3_INTF_MTU,
    DEDICATED_CPU_BUFFER_POOL,
    FAST_LLFC_COUNTER,
    INGRESS_SRAM_MIN_BUFFER_WATERMARK,
    FDR_FIFO_WATERMARK,
    EGRESS_CELL_ERROR_STATS,
    CPU_QUEUE_WATERMARK_STATS,
    SAMPLE_RATE_CONFIG_PER_MIRROR,
    SFLOW_SAMPLES_PACKING,
    VENDOR_SWITCH_NOTIFICATION,
    SDK_REGISTER_DUMP,
    FEC_ERROR_DETECT_ENABLE,
    BUFFER_POOL_HEADROOM_WATERMARK,
    SAI_SET_TC_WITH_USER_DEFINED_TRAP_CPU_ACTION,
    SAI_HOST_MISS_TRAP,
    CPU_TX_PACKET_REQUIRES_VLAN_TAG,
    DRAM_DATAPATH_PACKET_ERROR_STATS,
    EGRESS_POOL_AVAILABLE_SIZE_ATTRIBUTE_SUPPORTED,
    SWITCH_ASIC_SDK_HEALTH_NOTIFY,
    VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS,
    PFC_WATCHDOG_TIMER_GRANULARITY,
    ASIC_RESET_NOTIFICATIONS,
    SAI_PORT_IN_CONGESTION_DISCARDS,
    TEMPERATURE_MONITORING,
    ROUTER_INTERFACE_STATISTICS,
    // Set to true if router interfaces can be bound directly to aggregate
    // ports (non-VLAN router interfaces on LAGs).
    AGGREGATE_PORT_ROUTER_INTERFACE,
    CPU_PORT_EGRESS_BUFFER_POOL,
    TECH_SUPPORT,
    DRAM_QUARANTINED_BUFFER_STATS,
    MANAGEMENT_PORT_MULTICAST_QUEUE_ALPHA,
    SAI_PORT_PG_DROP_STATUS,
    FABRIC_INTER_CELL_JITTER_WATERMARK,
    MAC_TRANSMIT_DATA_QUEUE_WATERMARK,
    FABRIC_LINK_MONITORING,
    ARS_ALTERNATE_MEMBERS,
    ARS_FUTURE_PORT_LOAD,
    ARS_CURRENT_PORT_LOAD,
    RESERVED_BYTES_FOR_BUFFER_POOL,
    // Indicates the buffer pool size excludes the headroom
    // pool size given the buffer pool size determination is
    // left to vendor SAI implementation.
    INGRESS_BUFFER_POOL_SIZE_EXCLUDES_HEADROOM,
    // Set to true if port level buffer configuration is supported.
    // This is configured via bufer profiles and attached to ports
    // with SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST
    // and SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST.
    PORT_LEVEL_BUFFER_CONFIGURATION_SUPPORT,
    // This feature should be true for platforms supporting CPU
    // side queuing along with CPU ports given not all platforms
    // are capable of CPU queuing.
    CPU_QUEUES,
    SAI_SERDES_RX_REACH,
    ECN_PROBABILISTIC_MARKING,
    SAI_SERDES_PRECODING,
    // Feature for platforms that can create wide DLB groups and
    // also increase DLB table scale by combining multiple DLBs
    VIRTUAL_ARS_GROUP,
    // Support for SAI_SWITCH_ATTR_SWITCHING_MODE to switch between
    // store-and-forward and cut-through packet forwarding modes.
    // Cut-through starts forwarding after reading just the header,
    // reducing latency for scale-up switches.
    CUT_THROUGH_FORWARDING,
    // Set SAI_NEXT_HOP_GROUP_MEMBER_ATTR_MONITORED_OBJECT on the PRIMARY
    // member of a PROTECTION next hop group, naming the egress port/LAG whose
    // going down drives the ASIC's autonomous switchover to the standby
    // member. Broadcom infers that object from the member's own next hop, so
    // FBOSS leaves the attribute unset there. Leaba does not infer it, so it
    // has to be programmed explicitly.
    NEXT_HOP_GROUP_MEMBER_MONITORED_OBJECT,
    SRV6_MYSID_DISCARD_COUNTER,
    SRV6_MYSID_RESOURCE_COUNTER,
    // Binding uSIDs use uA with USD and an SRv6 decap tunnel rather than
    // B6_ENCAPS_RED with no flavor or decap tunnel.
    SRV6_BINDING_USID_UA_USD,
    DEVICE_WATERMARK_SUPPORT,
    // Per-stage HW drop cause bitmaps via custom SAI switch stats
    // (SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_*). Each bitmap is an i64
    // where set bits indicate specific drop reasons within that pipeline
    // stage. Currently supported on Chenab with SDK >= 2511.36.
    SWITCH_CUSTOM_DROP_BITMAP_SUPPORT,
    // Policy based routing via a dedicated ACL table matching on next hop
    // group and traffic class, redirecting to a per-TC next hop group.
    PBR_ACL,
    // UEC Link Layer Retry (UE Spec 1.0.2 section 5.1): hop-by-hop link-layer
    // retransmission of LLR-eligible frames between link partners. Currently
    // supported only on Tomahawk Ultra.
    LINK_LAYER_RETRANSMISSION,
    CBFC,
    // Per-port link up/down debounce (hold-off timers) and the associated
    // debounce retrigger counters.
    PORT_DEBOUNCE,
    // The link up hold-off timer is a single switch wide control rather than a
    // per-port one, so every port configuring portUpHoldoffTimeMs has to
    // configure the same value. Applicable only when PORT_DEBOUNCE is enabled.
    SWITCH_WIDE_LINK_UP_DEBOUNCE,
    // Per-port Switch Lifetime Limit / Headroom Lifetime Limit egress discard
    // counters (SAI_PORT_STAT_IF_OUT_DISCARDS_SLL / _HLL). NVIDIA Spectrum
    // only; the counters are collected via fillInSupportedVendorExtStats().
    SLL_HLL_DISCARD_COUNTERS,
    // Active HW drop reasons via read only SAI switch extension attributes
    // (SAI_SWITCH_ATTR_EXT_PACKET_DROP_TYPE_INGRESS_LIST / _EGRESS_LIST).
    // Each attribute returns the drop reasons seen since the last read, which
    // is cleared on read. Counterpart to SWITCH_CUSTOM_DROP_BITMAP_SUPPORT.
    SWITCH_DROP_REASON_LIST_SUPPORT,
    // SDK reports the type of a received packet, so the application does
    // not have to classify the packet itself to identify specific types.
    RX_PACKET_TYPE,
    // Port isolation groups: traffic ingressing a port is not forwarded to the
    // members of the isolation group bound to it
    ISOLATION_GROUP,
    // Extra router MACs via the SAI MY_MAC object: packets destined to a
    // programmed MAC + VLAN are routed like packets to the switch's own MAC.
    MY_MAC,
    // XPHY only: the xphy SerDes has a TX FIR post3 tap, so program it and read
    // it back. Only consulted by phy/ code; NPU ASICs report false (n/a).
    // Agera3 (Condor SerDes) has no post3: the SDK drops it on set and returns
    // -1 on get.
    XPHY_SERDES_TX_FIR_POST3,
    // Per-port HW protection switchover counters
    // (SAI_PORT_STAT_IF_IN_HW_PROTECTION_SWITCHOVER_EVENTS and
    // SAI_PORT_STAT_IF_IN_HW_PROTECTION_SWITCHOVER_DROP_PKTS). Broadcom
    // Tomahawk 5/6 with XGS SDK 13.3+.
    HW_PROTECTION_SWITCHOVER_COUNTERS,
    // PBR ACL table byte counters, for ASICs without ACL_BYTE_COUNTER.
    PBR_ACL_BYTE_COUNTER,
    // CSIG (Congestion Signaling) per-port egress tag action and link locator
    CSIG,
    // TTL control on the aggregate's shared forwarding service.
    LAG_TTL_DECREMENT_DISABLE,
    // The adapter attaches a default serdes object to every port it creates
    // and rejects a second create_port_serdes; the agent adopts that object
    PORT_SERDES_CREATED_WITH_PORT,
  };

  enum class AsicMode {
    ASIC_MODE_SIM,
    ASIC_MODE_HW,
  };

  enum class AsicVendor {
    ASIC_VENDOR_BCM,
    ASIC_VENDOR_TAJO,
    ASIC_VENDOR_CREDO,
    ASIC_VENDOR_MARVELL,
    ASIC_VENDOR_CHENAB,
    ASIC_VENDOR_MOCK,
    ASIC_VENDOR_FAKE,
  };
  enum FabricNodeRole {
    SINGLE_STAGE_L1,
    DUAL_STAGE_L1,
    DUAL_STAGE_L2,
  };
  enum InterfaceNodeRole {
    IN_CLUSTER_NODE,
    DUAL_STAGE_EDGE_NODE,
    HYPER_PORT_EDGE_NODE,
  };
  virtual ~HwAsic() {}
  static std::unique_ptr<HwAsic> makeAsic(
      int64_t switchID,
      const cfg::SwitchInfo& switchInfo,
      std::optional<cfg::SdkVersion> sdkVersion,
      std::optional<HwAsic::FabricNodeRole> fabricNodeRole);

  virtual bool isSupported(Feature) const = 0;
  virtual cfg::AsicType getAsicType() const = 0;
  std::string getAsicTypeStr() const;
  virtual AsicVendor getAsicVendor() const = 0;
  virtual AsicMode getAsicMode() const {
    return AsicMode::ASIC_MODE_HW;
  }
  virtual phy::DataPlanePhyChipType getDataPlanePhyChipType() const = 0;
  virtual std::string getVendor() const = 0;
  virtual cfg::PortSpeed getMaxPortSpeed() const = 0;
  virtual std::set<cfg::StreamType> getQueueStreamTypes(
      cfg::PortType portType) const = 0;
  virtual int getDefaultNumPortQueues(
      cfg::StreamType streamType,
      cfg::PortType portType) const = 0;
  virtual int getBasePortQueueId(
      cfg::StreamType /* streamType */,
      cfg::PortType /* portType */) const {
    return 0;
  }
  virtual uint32_t getMaxLabelStackDepth() const = 0;
  virtual uint64_t getMMUSizeBytes() const = 0;
  virtual uint32_t getMaxMirrors() const = 0;
  virtual uint16_t getMirrorTruncateSize() const = 0;
  virtual std::optional<uint64_t> getDefaultReservedBytes(
      cfg::StreamType streamType,
      cfg::PortType portType) const = 0;
  virtual std::optional<cfg::MMUScalingFactor> getDefaultScalingFactor(
      cfg::StreamType streamType,
      bool cpu) const = 0;
  virtual const std::map<cfg::PortType, cfg::PortLoopbackMode>&
  desiredLoopbackModes() const;
  virtual bool mmuQgroupsEnabled() const {
    return false;
  }
  virtual FabricNodeRole getFabricNodeRole() const;
  virtual const std::set<uint16_t>& getL1FabricPortsToConnectToL2() const;

  // Get the smallest packet buffer unit for ASIC, cell size for BCM
  virtual uint32_t getPacketBufferUnitSize() const = 0;

  // Get the size of the buffer descriptor associated with each packet
  virtual uint32_t getPacketBufferDescriptorSize() const = 0;

  /*
    This function will be used in calculating lane assignment, physical port
    assignment when adjusting ports in a port group. Most platforms have
    physical lanes : physical port = 1:1, but due to limited port id, some
    platforms will have different mappings. Note phyiscal port here is referring
    to the chip's physical ports and not our front panel ports
  */
  virtual int getNumLanesPerPhysicalPort() const {
    return 1;
  }

  virtual uint32_t getNumLanesPerCore() const {
    return 1;
  }

  virtual uint32_t getSaiPhysicalLaneId(
      PlatformType platformType,
      cfg::PortType portType,
      uint32_t chipId,
      uint32_t logicalLane) const;

  virtual uint32_t getNumCellsAvailable(PlatformType platformType) const;

  /*
   * Default Content Aware Processor group ID for ACLs
   */
  virtual int getDefaultACLGroupID() const;

  /*
   * Default Content Aware Processor group ID for TeFlows
   */
  virtual int getDefaultTeFlowGroupID() const;

  /*
   * station entry id for vlan interface
   */
  virtual int getStationID(int intfID) const;
  virtual int getDefaultDropEgressID() const;

  /*
   * The maximum number of logical ports
   */
  virtual int getMaxNumLogicalPorts() const = 0;
  virtual uint32_t getMaxWideEcmpSize() const = 0;

  /*
   * The maximum number of lag member
   */
  virtual uint32_t getMaxLagMemberSize() const = 0;

  /*
   * Port ID offset for system port. Default is 0.
   * This will be added to PortID and will be carried in the
   * sflow shim header
   */
  virtual int getSflowPortIDOffset() const {
    return 0;
  }

  /**
   * Number of forwarding engines / units with its own buffer pool.
   */
  virtual uint32_t getNumCores() const = 0;

  /**
   * Number of physical dies in the ASIC package. Multi-die ASICs (e.g. Q4D
   * with 2 dies) have ports spread across dies. Knowing the die count allows
   * test infrastructure to distribute port selection across dies for better
   * coverage. Default is 1 (single-die).
   */
  virtual uint32_t getNumDies() const {
    return 1;
  }

  /**
   * Returns which die a given core belongs to. Default assumes cores are
   * evenly distributed across dies in ascending order (e.g. for Q4D with
   * 8 cores and 2 dies: cores 0-3 → die 0, cores 4-7 → die 1).
   */
  virtual uint32_t getDieIdForCore(uint32_t coreId) const {
    auto numDies = getNumDies();
    if (numDies <= 1) {
      return 0;
    }
    auto coresPerDie = getNumCores() / numDies;
    return coreId / coresPerDie;
  }

  virtual uint32_t getSflowShimHeaderSize() const = 0;

  virtual std::optional<uint32_t> getPortSerdesPreemphasis() const = 0;

  static std::vector<cfg::AsicType> getAllHwAsicList();

  virtual uint32_t getMaxVariableWidthEcmpSize() const = 0;

  virtual uint32_t getMaxEcmpSize() const = 0;

  virtual std::optional<uint32_t> getMaxEcmpGroups() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxEcmpMembers() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxArsGroups() const = 0;
  virtual std::optional<uint32_t> getArsBaseIndex() const = 0;
  virtual std::optional<uint32_t> getMaxArsWidth() const {
    return std::nullopt;
  }
  // CBFC sender credit sizes, in bytes, that the SAI implementation accepts.
  // Empty on ASICs without CBFC.
  virtual std::vector<int16_t> getCbfcSenderCreditSizes() const {
    return {};
  }
  // CBFC sender per-packet overheads, in bytes, that the SAI implementation
  // accepts. Empty on ASICs without CBFC.
  virtual std::vector<int16_t> getCbfcSenderPacketOverheads() const {
    return {};
  }
  // TODO(zecheng): Define more specific limits for v4/v6 routes with different
  // mask lengths
  virtual std::optional<uint32_t> getMaxRoutes() const {
    return 75000;
  }

  //  SAI implementaion doen not support attribute
  //  SAI_SWITCH_ATTR_L3_NEIGHBOR_TABLE_SIZE yet, so decided to add these
  //  functions to return max neighbot table size
  virtual std::optional<uint32_t> getMaxNdpTableSize() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxArpTableSize() const {
    return std::nullopt;
  }

  // Set this if NDP/ARP uses a single unified neighbor table
  virtual std::optional<uint32_t> getMaxUnifiedNeighborTableSize() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxMySidEntries() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxVrfs() const {
    return std::nullopt;
  }

  virtual std::optional<uint32_t> getMaxRouteCounters() const {
    return std::nullopt;
  }

  // SRv6 next hops used as ECMP group members (NextHopSet size > 1)
  virtual std::optional<uint32_t> getMaxSrv6EcmpNextHops() const {
    return std::nullopt;
  }

  // SRv6 next hops used by single-nhop routes (NextHopSet size == 1)
  virtual std::optional<uint32_t> getMaxSrv6SingleNextHops() const {
    return std::nullopt;
  }

  // Distinct member weights in one SRv6 UCMP next hop group
  virtual std::optional<uint32_t> getMaxSrv6UcmpDistinctWeights() const {
    return std::nullopt;
  }

  virtual bool scalingFactorBasedDynamicThresholdSupported() const = 0;

  virtual int getBufferDynThreshFromScalingFactor(
      cfg::MMUScalingFactor scalingFactor) const;

  virtual uint32_t getStaticQueueLimitBytes() const = 0;

  virtual cfg::Range64 getReservedEncapIndexRange() const;

  virtual uint32_t getNumMemoryBuffers() const = 0;

  cfg::SwitchType getSwitchType() const {
    return switchType_;
  }
  std::optional<uint64_t> getSwitchId() const {
    return switchId_;
  }
  int16_t getSwitchIndex() const {
    return switchIndex_;
  }
  const cfg::SystemPortRanges& getSystemPortRanges() const {
    return systemPortRanges_;
  }

  virtual cfg::StreamType getDefaultStreamType() const {
    return defaultStreamType_;
  }

  void setDefaultStreamType(cfg::StreamType streamType) {
    defaultStreamType_ = streamType;
  }

  const folly::MacAddress& getAsicMac() const {
    return asicMac_;
  }
  virtual std::optional<uint32_t> computePortGroupSkew(
      const std::map<PortID, uint32_t>& portId2cableLen) const;

  struct RecyclePortInfo {
    uint32_t coreId;
    uint32_t corePortIndex;
    uint32_t speedMbps;
    uint32_t inbandPortId;
  };

  using CpuPortCoreAndPortIndex =
      std::map<uint32_t, std::pair<uint32_t, uint32_t>>;

  struct InternalSystemPortConfig {
    uint32_t portId;
    uint32_t switchId;
    uint32_t coreIndex;
    uint32_t corePortIndex;
    uint32_t speedMbps;
    uint32_t numVoqs;
  };

  class AcceptedValues {
   public:
    using Range = std::tuple<uint32_t, uint32_t>;
    using Values = std::vector<uint32_t>;

    static AcceptedValues range(uint32_t minInclusive, uint32_t maxInclusive) {
      return AcceptedValues(std::make_tuple(minInclusive, maxInclusive));
    }
    static AcceptedValues oneOf(Values values) {
      return AcceptedValues(std::move(values));
    }

    bool isRange() const {
      return std::holds_alternative<Range>(accepted_);
    }
    const Range& asRange() const {
      return std::get<Range>(accepted_);
    }
    const Values& asValues() const {
      return std::get<Values>(accepted_);
    }

    bool accepts(uint32_t value) const;
    std::string str() const;

   private:
    explicit AcceptedValues(std::variant<Range, Values> accepted)
        : accepted_(std::move(accepted)) {}

    std::variant<Range, Values> accepted_;
  };

  std::optional<cfg::SdkVersion> getSdkVersion() const {
    return sdkVersion_;
  }

  /*
   * True if the configured SAI SDK is at least minVersion, e.g.
   * saiSdkAtLeast("16.0_ea_odp"). Both sides are parsed the same way, so the
   * caller states the minimum it needs and the early access / GA distinction
   * follows from the version named rather than from a separate argument:
   *
   *   saiSdkAtLeast("15.4.0.0_odp")  15.4_ea_odp is below the bar
   *   saiSdkAtLeast("16.0_ea_odp")   16.0_ea_odp meets it
   *
   * Ordering is (major, minor, GA), so an early access drop sorts below its
   * own line's GA. Only those three are compared: trailing patch and build
   * fields are not ordered across vendors - tajo's 5210/5211 are variant
   * codes, not successive versions - so they are deliberately ignored.
   *
   * Fails closed if either side cannot be parsed. This does not use the
   * shared getAsicSdkVersion(), which assigns components by dot count and so
   * misreads single-dot early access strings like "16.0_ea_odp".
   */
  bool saiSdkAtLeast(folly::StringPiece minVersion) const;

  virtual RecyclePortInfo getRecyclePortInfo(
      InterfaceNodeRole /* intfRole */) const;
  virtual std::vector<InternalSystemPortConfig> getInternalSystemPortConfig(
      const CpuPortCoreAndPortIndex& cpuPortsCoreAndPortIdx) const;
  cfg::PortLoopbackMode getDesiredLoopbackMode(
      cfg::PortType portType = cfg::PortType::INTERFACE_PORT) const;

  virtual uint32_t getMaxPorts() const;
  virtual uint32_t getVirtualDevices() const;
  virtual std::vector<prbs::PrbsPolynomial> getSupportedPrbsPolynomials()
      const {
    return {};
  }

  virtual std::optional<uint32_t> getMaxAclTables() const {
    return std::nullopt;
  }
  virtual std::optional<uint32_t> getMaxAclEntries() const {
    return std::nullopt;
  }

  virtual uint32_t getThresholdGranularity() const {
    return 1;
  }

  virtual uint32_t getMaxHashSeedLength() const {
    return 32;
  }

  virtual int getMidPriCpuQueueId() const = 0;
  virtual int getHiPriCpuQueueId() const = 0;

  virtual uint64_t getSramSizeBytes() const = 0;
  std::optional<int32_t> getGlobalSystemPortOffset() const {
    return globalSystemPortOffset_;
  }
  std::optional<int32_t> getLocalSystemPortOffset() const {
    return localSystemPortOffset_;
  }
  std::optional<int32_t> getInbandPortId() const {
    return inbandPortId_;
  }
  virtual uint32_t getMaxSwitchId() const;

  virtual uint16_t getGreProtocol() const {
    return 0x88be;
  }

  // Applicable only when IP_IN_IP_DECAP feature is enabled.
  virtual cfg::TunnelMode getTunnelDscpMode() const {
    return cfg::TunnelMode::PIPE;
  }

  // Applicable only when PORT_DEBOUNCE feature is enabled. True means each
  // read reports the count since the previous read, false means the retrigger
  // counters report a running total.
  virtual bool isPortDebounceRetriggerCountClearOnRead() const {
    return true;
  }

  virtual std::optional<AcceptedValues> getAcceptedLinkUpHoldoffTimeMs() const {
    return std::nullopt;
  }

  virtual uint64_t getCpuPortEgressPoolSize() const;

  virtual bool portMtuSupported(cfg::PortType portType) const;

  virtual cfg::L2LearningMode getDefaultL2LearningMode() const {
    return cfg::L2LearningMode::HARDWARE;
  }

 protected:
  static cfg::Range64 makeRange(int64_t min, int64_t max);

 private:
  cfg::SwitchType switchType_;
  std::optional<int64_t> switchId_;
  int16_t switchIndex_;
  cfg::SystemPortRanges systemPortRanges_;
  cfg::StreamType defaultStreamType_{cfg::StreamType::ALL};
  folly::MacAddress asicMac_;
  std::optional<cfg::SdkVersion> sdkVersion_;
  std::optional<int32_t> localSystemPortOffset_, globalSystemPortOffset_,
      inbandPortId_;
};

} // namespace facebook::fboss
