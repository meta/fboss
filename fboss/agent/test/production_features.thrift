#
# Copyright 2004-present Facebook. All Rights Reserved.
#

package "facebook.com/fboss/agent/test/production_features"

namespace py neteng.fboss.test.production_features
namespace py3 neteng.fboss.test
namespace py.asyncio neteng.fboss.test.asyncio.production_features
namespace cpp2 facebook.fboss.test.production_features
namespace go neteng.fboss.test.production_features

# This enum is mirrored by hand in the configerator copy of this file,
# configerator/source/fboss/agent/test/production_features.thrift, which
# also holds the FeatureSupportSpec entries. Netcastle matches the two, so
# an id must mean the same thing in both. The copies have drifted: pick an
# id that is free in BOTH before adding a feature here.
enum ProductionFeature {
  VOQ = 0,
  FABRIC = 1,
  CPU_RX_TX = 2,
  L3_FORWARDING = 3,
  ACL_PORT_IN_DISCARDS_COUNTER = 4,
  NULL_ROUTE_IN_DISCARDS_COUNTER = 5,
  EGRESS_FORWARDING_DISCARDS_COUNTER = 6,
  PRBS = 7,
  COPP = 8,
  LAG = 9,
  DSCP_REMARKING = 10,
  L3_QOS = 11,
  ACL_COUNTER = 12,
  ECMP_LOAD_BALANCER = 13,
  LAG_LOAD_BALANCER = 14,
  VOQ_DNX_INTERRUPTS = 15,
  SINGLE_ACL_TABLE = 16,
  MULTI_ACL_TABLE = 17,
  BTH_OPCODE_ACL = 18,
  DLB = 19,
  ECN = 20,
  WRED = 21,
  QUEUE_PER_HOST = 22,
  JUMBO_FRAMES = 23, // Misc
  TRAP_DISCARDS_COUNTER = 24,
  MAC_LEARNING = 25,
  SCHEDULER_PPS = 26,
  ROUTE_COUNTERS = 27,
  PORT_TX_DISABLE = 28, // Misc
  SFLOWv4_SAMPLING = 29,
  SFLOWv6_SAMPLING = 30,
  MIRROR_PACKET_TRUNCATION = 31,
  WIDE_ECMP = 32,
  INTERFACE_NEIGHBOR_TABLE = 33,
  INGRESS_MIRRORING = 34,
  EGRESS_MIRRORING = 35,
  RSW_ROUTE_SCALE = 36, // Misc
  FSW_ROUTE_SCALE = 37, // Misc
  HGRID_DU_ROUTE_SCALE = 38, // Misc
  HGRID_UU_ROUTE_SCALE = 39, // Misc
  HUNDRED_THOUSAND_ROUTE_SCALE = 40, // Misc
  TH_ALPM_ROUTE_SCALE = 41,
  PFC = 42,
  UDF_WR_IMMEDIATE_ACL = 43,
  VLAN = 44,
  PORT_MTU_ERROR_TRAP = 45,
  LED_PROGRAMMING = 46,
  PFC_CAPTURE = 47,
  LINERATE_SFLOW = 48,
  MIRROR_ON_DROP = 49,
  PAUSE = 50,
  SEPARATE_INGRESS_EGRESS_BUFFER_POOL = 51,
  NIF_POLICER = 52,
  ERSPANV6_MIRRORING = 53,
  IP_IN_IP_DECAP = 54,
  LAG_MIRRORING = 55,
  INGRESS_ACL_MIRRORING = 56,
  EGRESS_ACL_MIRRORING = 57,
  SFLOW_SAMPLES_PACKING = 58,
  CLASS_ID_FOR_CONNECTED_ROUTE = 59,
  SEPARATE_BYTE_AND_PACKET_ACL_COUNTERS = 60,
  NETWORKAI_QOS = 61,
  EAPOL_TRAP = 62,
  UCMP = 63,
  MULTICAST_QUEUE = 64,
  MMU_TUNING = 65,
  OLYMPIC_QOS = 66,
  UNTRUNCATED_SFLOW = 67,
  MPLS = 68,
  PTP_TC = 69,
  SELF_HEALING_ECMP_LAG = 70,
  L4_DST_PORT_ACL = 71,
  MODIFY_ACL_QUALIFIERS = 72,
  ERSPANv4_SAMPLING = 73,
  ERSPANv6_SAMPLING = 74,
  GLOBAL_TC_RATE_LIMIT = 75,
  COPP_SHAPER = 76,
  COPP_SCHEDULER = 77,
  ECMP_RANDOM_SPRAY = 78,
  PTP_TC_PROVISIONING_TIME_HW_VALIDATION = 79,
  PFC_WATCHDOG_GRANULARITY = 80,
  ARS_FLOWLET = 81,
  ARS_SPRAY = 82,
  ROUTER_INTERFACE_COUNTERS = 83,
  STATIC_PG_SHARED_LIMIT = 84,
  HIGH_FREQUENCY_CPU_POLLING_STATS = 85,
  CLASS_ID_FOR_NEIGHBOR = 86,
  UDF_HASH = 87,
  INTER_PACKET_GAP = 88,
  AM_IDLES = 89,
  ALTERNATE_ARS_MEMBERS = 90,
  EGRESS_MIRROR_PACKET_TRUNCATION = 91,
  PFC_RX_DURATION = 92,
  PFC_TX_DURATION = 93,
  L2_QOS = 94,
  MIRROR_ON_DROP_DNX = 95,
  MIRROR_ON_DROP_XGS = 96,
  ECN_PROBABILISTIC_MARKING = 97,
  PFC_RX_TX_DURATION = 98,
  VIRTUAL_ARS_GROUP = 99,
  SRV6_ENCAP = 100,
  AIFM_PACKET_STREAM_HANDLER = 101,
  SRV6_DECAP = 102,
  CUT_THROUGH = 103,
  SRV6_MIDPOINT = 104,
  L4_DST_PORT_RANGE = 105,
  MPLS_HEADEND = 106,
  MPLS_MIDPOINT = 107,
  MPLS_TAILEND = 108,
  SAVE_SDK_STATE = 109,
  IP_IN_IP_ENCAP = 110,
  MIRROR_ON_DROP_STATELESS = 111,
  PORT_DEBOUNCE = 112,
  SRV6_BINDING_SID = 113,
  MPLS_BINDING_SID = 114,
  CUSTOM_DROP_BITMAP_SUPPORT = 115,
  PFC_NON_IDENTITY_PRIORITY_MAP = 116,
  CABLE_LENGTH_MEASUREMENT = 117,
  LINK_LAYER_RETRANSMISSION = 118,
  ADJACENCY_FRR = 119,
  ETSW_ROUTE_SCALE = 120,
  DST_IPV6_WORD_ACL_QUALIFIERS = 121,
  ARS_SOURCE_PORT_PRUNE = 122,
  DROP_REASON_LIST_SUPPORT = 123,
  PORT_BOUND_INGRESS_ACL = 125,
  ACL_DENY_DATA_AND_CONTROL_PLANE = 126,
  PORT_USER_METADATA = 127,
  MYSID_ADJACENCY_FRR = 128,
  BUFFER_MIN_GUARANTEE_WITH_DELAY_DROPS = 129,
  CREDIT_BASED_FLOW_CONTROL = 130,
  MPLS_TTL_ACL = 131,
  ACCESS_POLICY_CLASS_ID_ACL = 132,
  MY_MAC = 133,
  MIRROR_ON_DROP_AGGREGATE_RATE_LIMIT = 134,
  PBR_ACL = 135,
  VXLAN_ENCAP = 136,
  VXLAN_DECAP = 137,
  MIRROR_ON_DROP_STATEFUL = 138,
  MULTI_VRF = 139,
  # production feature which is present on all platforms, keep it at the end
  HW_SWITCH = 65536,
}

enum VendorSdk {
  BCM_NATIVE = 0,
  BRCM_SAI = 1,
  LEABA = 2,
  NVDA = 3,
}

struct FeatureSdkSupport {
  1: list<string> asics;
  // @lint-ignore FBTHRIFTCOMPAT FBTHRIFTCOMPAT2 THRIFTRENAME
  2: VendorSdk sdk;
  3: string minSdkVersion;
  4: optional string note;
  // Exact components required in the normalized Netcastle test config.
  5: optional list<string> requiredTestConfigOptions;
}

struct FeatureSupportSpec {
  1: ProductionFeature feature;
  // @lint-ignore FBTHRIFTCOMPAT FBTHRIFTCOMPAT2
  2: list<FeatureSdkSupport> supportedFrom;
}

struct AsicToProductionFeatures {
  1: map<string, list<ProductionFeature>> asicToFeatures;
  2: map<string, list<string>> asicToFeatureNames;
  // @lint-ignore FBTHRIFTCOMPAT FBTHRIFTCOMPAT2 FBTHRIFTCOMPAT6
  3: list<FeatureSupportSpec> featureSupportSpecs = [];
}
