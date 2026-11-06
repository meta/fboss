/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <typeindex>

#include "fboss/agent/AsyncLogger.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Traits.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

#include <folly/File.h>
#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/String.h>
#include <gflags/gflags.h>

extern "C" {
#include <sai.h>
// Note: All SAI extension headers are managed in SaiExtensionsIncludes.h.
// Please add new SAI extension headers there.
#include "fboss/agent/hw/sai/tracer/SaiExtensionsIncludes.h"
}

DECLARE_bool(enable_replayer);
DECLARE_bool(enable_packet_log);
DECLARE_bool(enable_elapsed_time_log);
DECLARE_bool(enable_get_attr_log);

using PrimitiveFunction = std::string (*)(const sai_attribute_t*, int);
using AttributeFunction =
    void (*)(const sai_attribute_t*, int, std::vector<std::string>&);
using ListFunction = void (*)(
    const sai_attribute_t*,
    int,
    uint32_t,
    std::vector<std::string>&,
    bool);

#define TYPE_INDEX(type) std::type_index(typeid(type)).hash_code()

namespace facebook::fboss {

class SaiTracer {
 public:
  explicit SaiTracer();
  ~SaiTracer();

  std::tuple<std::string, std::string> declareVariable(
      sai_object_id_t* object_id,
      sai_object_type_t object_type);

  static std::shared_ptr<SaiTracer> getInstance();

  void printHex(std::ostringstream& outStringStream, uint8_t u8);

  void logApiInitialize(const char** variables, const char** values, int size);
  void logApiUninitialize(void);

  void logApiQuery(sai_api_t api_id, const std::string& api_var);

  void logSwitchCreateFn(
      sai_object_id_t* switch_id,
      uint32_t attr_count,
      const sai_attribute_t* attr_list);

  void logRouteEntryCreateFn(
      const sai_route_entry_t* route_entry,
      uint32_t attr_count,
      const sai_attribute_t* attr_list);

  void logNeighborEntryCreateFn(
      const sai_neighbor_entry_t* neighbor_entry,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_status_t rv);

  void logFdbEntryCreateFn(
      const sai_fdb_entry_t* fdb_entry,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_status_t rv);

  void logInsegEntryCreateFn(
      const sai_inseg_entry_t* inseg_entry,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_status_t rv);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void logMySidEntryCreateFn(
      const sai_my_sid_entry_t* my_sid_entry,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_status_t rv);
#endif

  std::string logCreateFn(
      const std::string& fn_name,
      sai_object_id_t* create_object_id,
      sai_object_id_t switch_id,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_object_type_t object_type);

  void logBulkCreateFn(
      const std::string& fn_name,
      sai_object_id_t switch_id,
      uint32_t object_count,
      const uint32_t* attr_count,
      const sai_attribute_t** attr_list,
      sai_bulk_op_error_mode_t mode,
      sai_object_id_t* object_id,
      sai_status_t* object_statuses,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logBulkRemoveFn(
      const std::string& fn_name,
      uint32_t object_count,
      const sai_object_id_t* object_id,
      sai_bulk_op_error_mode_t mode,
      sai_status_t* object_statuses,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logRouteEntryRemoveFn(const sai_route_entry_t* route_entry);

  void logNeighborEntryRemoveFn(
      const sai_neighbor_entry_t* neighbor_entry,
      sai_status_t rv);

  void logFdbEntryRemoveFn(const sai_fdb_entry_t* fdb_entry, sai_status_t rv);

  void logInsegEntryRemoveFn(
      const sai_inseg_entry_t* inseg_entry,
      sai_status_t rv);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void logMySidEntryRemoveFn(
      const sai_my_sid_entry_t* my_sid_entry,
      sai_status_t rv);
#endif

  void logRemoveFn(
      const std::string& fn_name,
      sai_object_id_t remove_object_id,
      sai_object_type_t object_type);

  void logRouteEntrySetAttrFn(
      const sai_route_entry_t* route_entry,
      const sai_attribute_t* attr);

  void logNeighborEntrySetAttrFn(
      const sai_neighbor_entry_t* neighbor_entry,
      const sai_attribute_t* attr,
      sai_status_t rv);

  void logFdbEntrySetAttrFn(
      const sai_fdb_entry_t* fdb_entry,
      const sai_attribute_t* attr,
      sai_status_t rv);

  void logInsegEntrySetAttrFn(
      const sai_inseg_entry_t* inseg_entry,
      const sai_attribute_t* attr,
      sai_status_t rv);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void logMySidEntrySetAttrFn(
      const sai_my_sid_entry_t* my_sid_entry,
      const sai_attribute_t* attr,
      sai_status_t rv);
#endif

  void logAttrPreGet(
      uint32_t attr_count,
      const sai_attribute_t* attr,
      sai_object_type_t object_type);

  void logGetAttrFn(
      const std::string& fn_name,
      sai_object_id_t get_object_id,
      uint32_t attr_count,
      const sai_attribute_t* attr,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logBulkGetAttrFn(
      const std::string& fn_name,
      uint32_t object_count,
      const sai_object_id_t* object_id,
      const uint32_t* attr_count,
      sai_attribute_t** attr_list,
      sai_bulk_op_error_mode_t mode,
      sai_status_t* object_statuses,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logSetAttrFn(
      const std::string& fn_name,
      sai_object_id_t set_object_id,
      const sai_attribute_t* attr,
      sai_object_type_t object_type);

  void logCommentedAttrFn(
      const std::string& fn_name,
      sai_object_id_t set_object_id,
      const sai_attribute_t* attr,
      sai_object_type_t object_type);

  void logBulkSetAttrFn(
      const std::string& fn_name,
      uint32_t object_count,
      const sai_object_id_t* object_id,
      const sai_attribute_t* attr_list,
      sai_bulk_op_error_mode_t mode,
      sai_status_t* object_statuses,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logSendHostifPacketFn(
      sai_object_id_t hostif_id,
      sai_size_t buffer_size,
      const uint8_t* buffer,
      uint32_t attr_count,
      const sai_attribute_t* attr_list,
      sai_status_t rv);

  void logGetStatsFn(
      const std::string& fn_name,
      sai_object_id_t object_id,
      uint32_t number_of_counters,
      const sai_stat_id_t* counter_ids,
      const uint64_t* counters,
      sai_object_type_t object_type,
      sai_status_t rv,
      int mode = 0);

  void logClearStatsFn(
      const std::string& fn_name,
      sai_object_id_t object_id,
      uint32_t number_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_object_type_t object_type,
      sai_status_t rv);

  void logShellCommand(const std::string& command);

  std::string getVariable(sai_object_id_t object_id);

  uint32_t
  checkListCount(uint32_t list_count, uint32_t elem_size, uint32_t elem_count);

  void writeToFile(
      const std::vector<std::string>& strVec,
      bool linefeed = true);

  void logPostInvocation(
      sai_status_t rv,
      sai_object_id_t object_id,
      std::chrono::system_clock::time_point begin,
      std::optional<std::string> varName = std::nullopt);

  sai_acl_api_t* aclApi_;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  sai_ars_api_t* arsApi_;
  sai_ars_profile_api_t* arsProfileApi_;
#endif
  sai_bridge_api_t* bridgeApi_;
  sai_buffer_api_t* bufferApi_;
  sai_counter_api_t* counterApi_;
  sai_debug_counter_api_t* debugCounterApi_;
  sai_fdb_api_t* fdbApi_;
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  sai_firmware_api_t* firmwareApi_;
#endif
  sai_hash_api_t* hashApi_;
  sai_hostif_api_t* hostifApi_;
  sai_isolation_group_api_t* isolationGroupApi_;
  sai_lag_api_t* lagApi_;
  sai_neighbor_api_t* neighborApi_;
  sai_next_hop_api_t* nextHopApi_;
  sai_next_hop_group_api_t* nextHopGroupApi_;
  sai_macsec_api_t* macsecApi_;
  sai_mirror_api_t* mirrorApi_;
  sai_mpls_api_t* mplsApi_;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  sai_my_mac_api_t* myMacApi_;
#endif
  sai_port_api_t* portApi_;
  sai_queue_api_t* queueApi_;
  sai_qos_map_api_t* qosMapApi_;
  sai_route_api_t* routeApi_;
  sai_router_interface_api_t* routerInterfaceApi_;
  sai_samplepacket_api_t* samplepacketApi_;
  sai_scheduler_api_t* schedulerApi_;
  sai_switch_api_t* switchApi_;
  sai_system_port_api_t* systemPortApi_;
  sai_tam_api_t* tamApi_;
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  sai_tam_event_aging_group_api_t* tamEventAgingGroupApi_;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  sai_srv6_api_t* srv6Api_{nullptr};
#endif
  sai_tunnel_api_t* tunnelApi_;
  sai_udf_api_t* udfApi_;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  sai_vendor_switch_api_t* vendorSwitchApi_;
  sai_switch_pipeline_api_t* switchPipelineApi_;
#endif
#if defined(SAI_CBFC_SUPPORTED)
  sai_virtual_channel_api_t* virtualChannelApi_;
#endif
  sai_virtual_router_api_t* virtualRouterApi_;
  sai_vlan_api_t* vlanApi_;
  sai_wred_api_t* wredApi_;

  std::map<sai_api_t, std::string> init_api_;

  std::unordered_map<std::size_t, PrimitiveFunction> primitiveFuncMap_{
      {TYPE_INDEX(sai_object_id_t), &oidAttr},
      {TYPE_INDEX(SaiObjectIdT), &oidAttr},
      {TYPE_INDEX(bool), &boolAttr},
      {TYPE_INDEX(sai_uint8_t), &u8Attr},
      {TYPE_INDEX(sai_int8_t), &s8Attr},
      {TYPE_INDEX(sai_int16_t), &s16Attr},
      {TYPE_INDEX(sai_uint16_t), &u16Attr},
      {TYPE_INDEX(sai_uint32_t), &u32Attr},
      {TYPE_INDEX(sai_int32_t), &s32Attr},
      {TYPE_INDEX(sai_uint64_t), &u64Attr},
  };

  std::unordered_map<std::size_t, AttributeFunction> attributeFuncMap_{
      {TYPE_INDEX(sai_u32_range_t), &u32RangeAttr},
      {TYPE_INDEX(sai_s32_range_t), &s32RangeAttr},
      {TYPE_INDEX(folly::MacAddress), &macAddressAttr},
      {TYPE_INDEX(folly::IPAddress), &ipAttr},
      /* Acl Entry attributes */
      {TYPE_INDEX(AclEntryFieldSaiObjectIdT), &aclEntryFieldSaiObjectIdAttr},
      {TYPE_INDEX(AclEntryFieldIpV6), &aclEntryFieldIpV6Attr},
      {TYPE_INDEX(AclEntryFieldIpV4), &aclEntryFieldIpV4Attr},
      {TYPE_INDEX(AclEntryActionSaiObjectIdT), &aclEntryActionSaiObjectIdAttr},
      {TYPE_INDEX(AclEntryFieldU32), &aclEntryFieldU32Attr},
      {TYPE_INDEX(AclEntryActionU32), &aclEntryActionU32Attr},
      {TYPE_INDEX(AclEntryFieldU16), &aclEntryFieldU16Attr},
      {TYPE_INDEX(AclEntryFieldU8), &aclEntryFieldU8Attr},
      {TYPE_INDEX(AclEntryActionU8), &aclEntryActionU8Attr},
      {TYPE_INDEX(AclEntryActionBool), &aclEntryActionBoolAttr},
      {TYPE_INDEX(AclEntryFieldMac), &aclEntryFieldMacAttr},
      // System port
      {TYPE_INDEX(sai_system_port_config_t), &systemPortConfigAttr},
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
      {TYPE_INDEX(sai_latch_status_t), &latchStatusAttr},
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
      {TYPE_INDEX(sai_prbs_rx_state_t), &prbsRxStateAttr},
#endif
  };

  std::unordered_map<std::size_t, ListFunction> listFuncMap_{
      {TYPE_INDEX(std::vector<sai_object_id_t>), &oidListAttr},
      {TYPE_INDEX(std::vector<sai_uint32_t>), &u32ListAttr},
      {TYPE_INDEX(std::vector<sai_int32_t>), &s32ListAttr},
      {TYPE_INDEX(std::vector<sai_int8_t>), &s8ListAttr},
      {TYPE_INDEX(std::vector<sai_qos_map_t>), &qosMapListAttr},
      {TYPE_INDEX(std::vector<sai_map_t>), &mapListAttr},
      {TYPE_INDEX(AclEntryActionSaiObjectIdList),
       &aclEntryActionSaiObjectIdListAttr},
      {TYPE_INDEX(std::vector<sai_system_port_config_t>),
       &systemPortConfigListAttr},
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
      {TYPE_INDEX(std::vector<sai_port_lane_latch_status_t>),
       &portLaneLatchStatusListAttr},
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
      {TYPE_INDEX(std::vector<sai_port_frequency_offset_ppm_values_t>),
       &portFrequencyOffsetPpmListAttr},
      {TYPE_INDEX(std::vector<sai_port_snr_values_t>), &portSnrListAttr},
      {TYPE_INDEX(AclEntryFieldU8List), &aclEntryFieldU8ListAttr},
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      {TYPE_INDEX(SaiSegmentListValueType), &segmentListAttr},
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
      {TYPE_INDEX(SaiJsonString), &jsonAttr},
#endif
  };

 private:
  // Helper methods for variables and attribute list
  std::vector<std::string> setAttrList(
      const sai_attribute_t* attr_list,
      uint32_t attr_count,
      sai_object_type_t object_type,
      sai_status_t rv = 0);

  // Dedicated method for operations with 2D attribute arrays
  std::vector<std::string> setBulkAttrList(
      uint32_t object_count,
      const uint32_t* attr_count,
      sai_attribute_t** attr_list,
      sai_object_type_t object_type,
      sai_status_t rv = 0);

  std::string createFnCall(
      const std::string& fn_name,
      const std::string& var1,
      const std::string& var2,
      uint32_t attr_count,
      sai_object_type_t object_type);

  void setFdbEntry(
      const sai_fdb_entry_t* fdb_entry,
      std::vector<std::string>& lines);

  void setInsegEntry(
      const sai_inseg_entry_t* inseg_entry,
      std::vector<std::string>& lines);

  void setNeighborEntry(
      const sai_neighbor_entry_t* neighbor_entry,
      std::vector<std::string>& lines);

  void setRouteEntry(
      const sai_route_entry_t* route_entry,
      std::vector<std::string>& lines);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void setMySidEntry(
      const sai_my_sid_entry_t* my_sid_entry,
      std::vector<std::string>& lines);
#endif

  std::string rvCheck(sai_status_t rv);

  std::string logTimeAndRv(
      sai_status_t rv,
      sai_object_id_t object_id = SAI_NULL_OBJECT_ID,
      std::chrono::system_clock::time_point begin =
          std::chrono::system_clock::time_point::min());

  void checkAttrCount(uint32_t attr_count);

  // Init functions
  void setupGlobals();
  void initVarCounts();

  void writeFooter();

  uint32_t maxAttrCount_;
  uint32_t maxListCount_;
  uint32_t numCalls_;
  std::unique_ptr<AsyncLogger> asyncLogger_;

  // Variables mappings in generated C code
  // varCounts map from object type to the current counter
  std::map<sai_object_type_t, std::atomic<uint32_t>> varCounts_;
  // variables_ map from object id to its variable name
  std::map<sai_object_id_t, std::string> variables_;

  // clang-format off
  std::map<sai_object_type_t, std::string> varNames_{
      {SAI_OBJECT_TYPE_ACL_COUNTER, "aclCounter_"},
      {SAI_OBJECT_TYPE_ACL_ENTRY, "aclEntry_"},
      {SAI_OBJECT_TYPE_ACL_TABLE, "aclTable_"},
      {SAI_OBJECT_TYPE_ACL_TABLE_GROUP, "aclTableGroup_"},
      {SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER, "aclTableGroupMember_"},
      {SAI_OBJECT_TYPE_ACL_RANGE, "aclRange_"},
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      {SAI_OBJECT_TYPE_ARS, "ars_"},
      {SAI_OBJECT_TYPE_ARS_PROFILE, "arsProfile_"},
#endif
      {SAI_OBJECT_TYPE_BRIDGE, "bridge_"},
      {SAI_OBJECT_TYPE_BRIDGE_PORT, "bridgePort_"},
      {SAI_OBJECT_TYPE_BUFFER_POOL, "bufferPool_"},
      {SAI_OBJECT_TYPE_BUFFER_PROFILE, "bufferProfile_"},
      {SAI_OBJECT_TYPE_COUNTER, "counter_"},
      {SAI_OBJECT_TYPE_DEBUG_COUNTER, "debugCounter_"},
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_FIRMWARE), "firmware_"},
#endif
      {SAI_OBJECT_TYPE_HASH, "hash_"},
      {SAI_OBJECT_TYPE_HOSTIF, "hostif_"},
      {SAI_OBJECT_TYPE_HOSTIF_TRAP, "hostifTrap_"},
      {SAI_OBJECT_TYPE_HOSTIF_USER_DEFINED_TRAP, "hostifUserDefinedTrap_"},
      {SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, "hostifTrapGroup_"},
      {SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, "ingressPriorityGroup_"},
      {SAI_OBJECT_TYPE_LAG, "lag_"},
      {SAI_OBJECT_TYPE_LAG_MEMBER, "lagMember_"},
      {SAI_OBJECT_TYPE_MACSEC, "macsec_"},
      {SAI_OBJECT_TYPE_MACSEC_PORT, "macsecPort_"},
      {SAI_OBJECT_TYPE_MACSEC_FLOW, "macsecFlow_"},
      {SAI_OBJECT_TYPE_MACSEC_SA, "macsecSa_"},
      {SAI_OBJECT_TYPE_MACSEC_SC, "macsecSc_"},
      {SAI_OBJECT_TYPE_MIRROR_SESSION, "mirrorSession_"},
      {SAI_OBJECT_TYPE_NEXT_HOP, "nextHop_"},
      {SAI_OBJECT_TYPE_NEXT_HOP_GROUP, "nextHopGroup_"},
      {SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, "nextHopGroupMember_"},
      {SAI_OBJECT_TYPE_POLICER, "policier_"},
      {SAI_OBJECT_TYPE_PORT, "port_"},
      {SAI_OBJECT_TYPE_PORT_SERDES, "portSerdes_"},
      {SAI_OBJECT_TYPE_PORT_CONNECTOR, "portConnector_"},
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      {SAI_OBJECT_TYPE_PORT_LLR_PROFILE, "portLlrProfile_"},
#endif
      {SAI_OBJECT_TYPE_QOS_MAP, "qosMap_"},
      {SAI_OBJECT_TYPE_QUEUE, "queue_"},
      {SAI_OBJECT_TYPE_ROUTER_INTERFACE, "routerInterface_"},
      {SAI_OBJECT_TYPE_SAMPLEPACKET, "samplepacket_"},
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      {SAI_OBJECT_TYPE_MY_MAC, "myMac_"},
#endif
      {SAI_OBJECT_TYPE_SCHEDULER, "scheduler_"},
      {SAI_OBJECT_TYPE_SCHEDULER_GROUP, "schedulerGroup_"},
      {SAI_OBJECT_TYPE_SWITCH, "switch_"},
      {SAI_OBJECT_TYPE_SYSTEM_PORT, "systemPort_"},
      {SAI_OBJECT_TYPE_TAM_COLLECTOR, "tamCollector_"},
      {SAI_OBJECT_TYPE_TAM_TRANSPORT, "tamTransport_"},
      {SAI_OBJECT_TYPE_TAM_REPORT, "tamReport_"},
      {SAI_OBJECT_TYPE_TAM_EVENT_ACTION, "tamEventAction_"},
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_TAM_EVENT_AGING_GROUP),
          "tamEventAgingGroup_"},
#endif
      {SAI_OBJECT_TYPE_TAM_EVENT, "tamEvent_"},
      {SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD, "tamEventThreshold_"},
      {SAI_OBJECT_TYPE_TAM, "tam_"},
      {SAI_OBJECT_TYPE_TUNNEL, "tunnel_"},
      {SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY, "tunnelTerm_"},
      {SAI_OBJECT_TYPE_TUNNEL_MAP, "tunnelMap_"},
      {SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, "tunnelMapEntry_"},
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      {SAI_OBJECT_TYPE_SRV6_SIDLIST, "srv6SidList_"},
#endif
      {SAI_OBJECT_TYPE_UDF, "udf_"},
      {SAI_OBJECT_TYPE_UDF_MATCH, "udfMatch_"},
      {SAI_OBJECT_TYPE_UDF_GROUP, "udfGroup_"},
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_VENDOR_SWITCH), "vendorSwitch_"},
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_SWITCH_PIPELINE), "switchPipeline_"},
#endif
#if defined(SAI_CBFC_SUPPORTED)
      {SAI_OBJECT_TYPE_VIRTUAL_CHANNEL, "virtualChannel_"},
      {SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE, "cbfcCreditProfile_"},
#endif
      {SAI_OBJECT_TYPE_VIRTUAL_ROUTER, "virtualRouter_"},
      {SAI_OBJECT_TYPE_VLAN, "vlan_"},
      {SAI_OBJECT_TYPE_VLAN_MEMBER, "vlanMember_"},
      {SAI_OBJECT_TYPE_ISOLATION_GROUP, "isolationGroup_"},
      {SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER, "isolationGroupMember_"},
      {SAI_OBJECT_TYPE_WRED, "wred_"},
      {SAI_OBJECT_TYPE_SYSTEM_PORT, "systemPort_"}};

  std::map<sai_object_type_t, std::string> fnPrefix_{
      {SAI_OBJECT_TYPE_ACL_COUNTER, "acl_api->"},
      {SAI_OBJECT_TYPE_ACL_ENTRY, "acl_api->"},
      {SAI_OBJECT_TYPE_ACL_TABLE, "acl_api->"},
      {SAI_OBJECT_TYPE_ACL_TABLE_GROUP, "acl_api->"},
      {SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER, "acl_api->"},
      {SAI_OBJECT_TYPE_ACL_RANGE, "acl_api->"},
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      {SAI_OBJECT_TYPE_ARS, "ars_api->"},
      {SAI_OBJECT_TYPE_ARS_PROFILE, "ars_profile_api->"},
#endif
      {SAI_OBJECT_TYPE_BRIDGE, "bridge_api->"},
      {SAI_OBJECT_TYPE_BRIDGE_PORT, "bridge_api->"},
      {SAI_OBJECT_TYPE_BUFFER_POOL, "buffer_api->"},
      {SAI_OBJECT_TYPE_BUFFER_PROFILE, "buffer_api->"},
      {SAI_OBJECT_TYPE_COUNTER, "counter_api->"},
      {SAI_OBJECT_TYPE_DEBUG_COUNTER, "debug_counter_api->"},
      {SAI_OBJECT_TYPE_FDB_ENTRY, "fdb_api->"},
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_FIRMWARE), "firmware_api->"},
#endif
      {SAI_OBJECT_TYPE_HASH, "hash_api->"},
      {SAI_OBJECT_TYPE_HOSTIF, "hostif_api->"},
      {SAI_OBJECT_TYPE_HOSTIF_TRAP, "hostif_api->"},
      {SAI_OBJECT_TYPE_HOSTIF_USER_DEFINED_TRAP, "hostif_api->"},
      {SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, "hostif_api->"},
      {SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, "buffer_api->"},
      {SAI_OBJECT_TYPE_LAG, "lag_api->"},
      {SAI_OBJECT_TYPE_LAG_MEMBER, "lag_api->"},
      {SAI_OBJECT_TYPE_MACSEC, "macsec_api->"},
      {SAI_OBJECT_TYPE_MACSEC_PORT, "macsec_api->"},
      {SAI_OBJECT_TYPE_MACSEC_FLOW, "macsec_api->"},
      {SAI_OBJECT_TYPE_MACSEC_SA, "macsec_api->"},
      {SAI_OBJECT_TYPE_MACSEC_SC, "macsec_api->"},
      {SAI_OBJECT_TYPE_MIRROR_SESSION, "mirror_api->"},
      {SAI_OBJECT_TYPE_INSEG_ENTRY, "mpls_api->"},
      {SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, "neighbor_api->"},
      {SAI_OBJECT_TYPE_NEXT_HOP, "next_hop_api->"},
      {SAI_OBJECT_TYPE_NEXT_HOP_GROUP, "next_hop_group_api->"},
      {SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, "next_hop_group_api->"},
      {SAI_OBJECT_TYPE_PORT, "port_api->"},
      {SAI_OBJECT_TYPE_PORT_SERDES, "port_api->"},
      {SAI_OBJECT_TYPE_PORT_CONNECTOR, "port_api->"},
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      {SAI_OBJECT_TYPE_PORT_LLR_PROFILE, "port_api->"},
#endif
      {SAI_OBJECT_TYPE_ROUTE_ENTRY, "route_api->"},
      {SAI_OBJECT_TYPE_ROUTER_INTERFACE, "router_interface_api->"},
      {SAI_OBJECT_TYPE_QOS_MAP, "qos_map_api->"},
      {SAI_OBJECT_TYPE_QUEUE, "queue_api->"},
      {SAI_OBJECT_TYPE_SAMPLEPACKET, "samplepacket_api->"},
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      {SAI_OBJECT_TYPE_MY_MAC, "my_mac_api->"},
#endif
      {SAI_OBJECT_TYPE_SCHEDULER, "scheduler_api->"},
      {SAI_OBJECT_TYPE_SCHEDULER_GROUP, "scheduler_group_api->"},
      {SAI_OBJECT_TYPE_SWITCH, "switch_api->"},
      {SAI_OBJECT_TYPE_SYSTEM_PORT, "system_port_api->"},
      {SAI_OBJECT_TYPE_TAM_COLLECTOR, "tam_api->"},
      {SAI_OBJECT_TYPE_TAM_TRANSPORT, "tam_api->"},
      {SAI_OBJECT_TYPE_TAM_REPORT, "tam_api->"},
      {SAI_OBJECT_TYPE_TAM_EVENT_ACTION, "tam_api->"},
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_TAM_EVENT_AGING_GROUP),
          "tam_event_aging_group_api->"},
#endif
      {SAI_OBJECT_TYPE_TAM_EVENT, "tam_api->"},
      {SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD, "tam_api->"},
      {SAI_OBJECT_TYPE_TAM, "tam_api->"},
      {SAI_OBJECT_TYPE_TUNNEL, "tunnel_api->"},
      {SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY, "tunnel_api->"},
      {SAI_OBJECT_TYPE_TUNNEL_MAP, "tunnel_api->"},
      {SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, "tunnel_api->"},
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      {SAI_OBJECT_TYPE_SRV6_SIDLIST, "srv6_api->"},
      {SAI_OBJECT_TYPE_MY_SID_ENTRY, "srv6_api->"},
#endif
      {SAI_OBJECT_TYPE_UDF, "udf_api->"},
      {SAI_OBJECT_TYPE_UDF_MATCH, "udf_api->"},
      {SAI_OBJECT_TYPE_UDF_GROUP, "udf_api->"},
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_VENDOR_SWITCH),
          "vendor_switch_api->"},
      {static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_SWITCH_PIPELINE),
          "switch_pipeline_api->"},
#endif
#if defined(SAI_CBFC_SUPPORTED)
      {SAI_OBJECT_TYPE_VIRTUAL_CHANNEL, "virtual_channel_api->"},
      {SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE, "virtual_channel_api->"},
#endif
      {SAI_OBJECT_TYPE_VIRTUAL_ROUTER, "virtual_router_api->"},
      {SAI_OBJECT_TYPE_VLAN, "vlan_api->"},
      {SAI_OBJECT_TYPE_VLAN_MEMBER, "vlan_api->"},
      {SAI_OBJECT_TYPE_ISOLATION_GROUP, "isolation_group_api->"},
      {SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER, "isolation_group_api->"},
      {SAI_OBJECT_TYPE_WRED, "wred_api->"}};
  // clang-format on

  const char* cpp_header_ =
      "/*\n"
      " *  Copyright (c) 2004-present, Facebook, Inc.\n"
      " *  All rights reserved.\n"
      " *\n"
      " *  This source code is licensed under the BSD-style license found in the\n"
      " *  LICENSE file in the root directory of this source tree. An additional grant\n"
      " *  of patent rights can be found in the PATENTS file in the same directory.\n"
      " *\n"
      " */\n"
      "\n"
      "#include <string>\n"
      "#include <unordered_map>\n"
      "#include <vector>\n"
      "\n"
      "#include \"fboss/agent/hw/sai/tracer/run/SaiLog.h\"\n"
      "\n"
      "#define ATTR_SIZE sizeof(sai_attribute_t)\n"
      "\n"
      "namespace {\n"
      "\n"
      "static std::unordered_map<std::string, std::string> kSaiProfileValues;\n"
      "\n"
      "const char* saiProfileGetValue(\n"
      "    sai_switch_profile_id_t /*profile_id*/,\n"
      "    const char* variable) {\n"
      "  auto saiProfileValItr = kSaiProfileValues.find(variable);\n"
      "  return saiProfileValItr != kSaiProfileValues.end()\n"
      "      ? saiProfileValItr->second.c_str()\n"
      "      : nullptr;\n"
      "}\n"
      "\n"
      "int saiProfileGetNextValue(\n"
      "    sai_switch_profile_id_t /* profile_id */,\n"
      "    const char** variable,\n"
      "    const char** value) {\n"
      "  static auto saiProfileValItr = kSaiProfileValues.begin();\n"
      "  if (!value) {\n"
      "    saiProfileValItr = kSaiProfileValues.begin();\n"
      "    return 0;\n"
      "  }\n"
      "  if (saiProfileValItr == kSaiProfileValues.end()) {\n"
      "    return -1;\n"
      "  }\n"
      "  *variable = saiProfileValItr->first.c_str();\n"
      "  *value = saiProfileValItr->second.c_str();\n"
      "  ++saiProfileValItr;\n"
      "  return 0;\n"
      "}\n"
      "\n"
      "sai_service_method_table_t kSaiServiceMethodTable = {\n"
      "    .profile_get_value = saiProfileGetValue,\n"
      "    .profile_get_next_value = saiProfileGetNextValue,\n"
      "};\n"
      "\n"
      "__attribute__((unused))\n"
      "inline void rvCheck(int rv, int expected, int count) {\n"
      "  if (rv != expected) printf(\"Unexpected rv at %d with status %d\\n\", count, rv);\n"
      "  else if (rv != 0) printf(\"Non 0 rv at %d with status %d\\n\", count, rv);\n"
      "}\n"
      "\n"
      "__attribute__((unused))\n"
      "inline void attrCheck(sai_attribute_t *expected, sai_attribute_t *actual, int32_t num, int count) {\n"
      "  if (memcmp((void*)expected, (void*)actual, ATTR_SIZE * num)) printf(\"Diff in GET attribute %d\\n\", count);\n"
      "}\n"
      "\n"
      "__attribute__((unused))\n"
      "sai_object_id_t assignObject(sai_object_key_t* object_list, int object_count, int i, sai_object_id_t default_id) {\n"
      "  if (i < object_count) {\n"
      "    return object_list[i].key.object_id;\n"
      "  }\n"
      "  return default_id;\n"
      "}\n"
      "\n"
      "} //namespace\n"
      "\n"
      "namespace facebook::fboss {\n"
      "\n"
      "void run_trace() {\n";
};

#define SET_ATTRIBUTE_FUNC_DECLARATION(obj_type) \
  void set##obj_type##Attributes(                \
      const sai_attribute_t* attr_list,          \
      uint32_t attr_count,                       \
      std::vector<std::string>& attrLines,       \
      sai_status_t rv);

#define WRAP_CREATE_FUNC(obj_type, sai_obj_type, api_type)                 \
  sai_status_t wrap_create_##obj_type(                                     \
      sai_object_id_t* obj_type##_id,                                      \
      sai_object_id_t switch_id,                                           \
      uint32_t attr_count,                                                 \
      const sai_attribute_t* attr_list) {                                  \
    auto varName = SaiTracer::getInstance()->logCreateFn(                  \
        "create_" #obj_type,                                               \
        obj_type##_id,                                                     \
        switch_id,                                                         \
        attr_count,                                                        \
        attr_list,                                                         \
        sai_obj_type);                                                     \
    auto begin = FLAGS_enable_elapsed_time_log                             \
        ? std::chrono::system_clock::now()                                 \
        : std::chrono::system_clock::time_point::min();                    \
    auto rv = SaiTracer::getInstance()->api_type##Api_->create_##obj_type( \
        obj_type##_id, switch_id, attr_count, attr_list);                  \
    SaiTracer::getInstance()->logPostInvocation(                           \
        rv, *obj_type##_id, begin, varName);                               \
    return rv;                                                             \
  }

#define WRAP_REMOVE_FUNC(obj_type, sai_obj_type, api_type)                 \
  sai_status_t wrap_remove_##obj_type(sai_object_id_t obj_type##_id) {     \
    SaiTracer::getInstance()->logRemoveFn(                                 \
        "remove_" #obj_type, obj_type##_id, sai_obj_type);                 \
    auto begin = FLAGS_enable_elapsed_time_log                             \
        ? std::chrono::system_clock::now()                                 \
        : std::chrono::system_clock::time_point::min();                    \
    auto rv = SaiTracer::getInstance()->api_type##Api_->remove_##obj_type( \
        obj_type##_id);                                                    \
    SaiTracer::getInstance()->logPostInvocation(rv, obj_type##_id, begin); \
                                                                           \
    return rv;                                                             \
  }

// Sentinel value for attribute filtering - a very large number guaranteed
// not to match any real attribute ID
#define SAI_TRACER_ATTR_FILTER_SENTINEL 0xFFFFFFFF

// Default wrapper for attribute setter function - uses sentinel value to
// disable filtering
#define WRAP_SET_ATTR_FUNC(obj_type, sai_obj_type, api_type) \
  WRAP_SET_ATTR_FUNC_WITH_FILTER(                            \
      obj_type, sai_obj_type, api_type, SAI_TRACER_ATTR_FILTER_SENTINEL)

#define WRAP_SET_ATTR_FUNC_WITH_FILTER(                                       \
    obj_type, sai_obj_type, api_type, filter_attr_id)                         \
  sai_status_t wrap_set_##obj_type##_attribute(                               \
      sai_object_id_t obj_type##_id, const sai_attribute_t* attr) {           \
    if (attr->id == filter_attr_id) {                                         \
      SaiTracer::getInstance()->logCommentedAttrFn(                           \
          "set_" #obj_type "_attribute", obj_type##_id, attr, sai_obj_type);  \
    } else {                                                                  \
      SaiTracer::getInstance()->logSetAttrFn(                                 \
          "set_" #obj_type "_attribute", obj_type##_id, attr, sai_obj_type);  \
    }                                                                         \
    auto begin = FLAGS_enable_elapsed_time_log                                \
        ? std::chrono::system_clock::now()                                    \
        : std::chrono::system_clock::time_point::min();                       \
    auto rv =                                                                 \
        SaiTracer::getInstance()->api_type##Api_->set_##obj_type##_attribute( \
            obj_type##_id, attr);                                             \
    SaiTracer::getInstance()->logPostInvocation(rv, obj_type##_id, begin);    \
                                                                              \
    return rv;                                                                \
  }

#define WRAP_GET_ATTR_FUNC(obj_type, sai_obj_type, api_type)                 \
  sai_status_t wrap_get_##obj_type##_attribute(                              \
      sai_object_id_t obj_type##_id,                                         \
      uint32_t attr_count,                                                   \
      sai_attribute_t* attr_list) {                                          \
    if (FLAGS_enable_replayer && FLAGS_enable_get_attr_log) {                \
      SaiTracer::getInstance()->logAttrPreGet(                               \
          attr_count, attr_list, sai_obj_type);                              \
      auto begin = FLAGS_enable_elapsed_time_log                             \
          ? std::chrono::system_clock::now()                                 \
          : std::chrono::system_clock::time_point::min();                    \
      auto rv = SaiTracer::getInstance()                                     \
                    ->api_type##Api_->get_##obj_type##_attribute(            \
                        obj_type##_id, attr_count, attr_list);               \
      SaiTracer::getInstance()->logGetAttrFn(                                \
          "get_" #obj_type "_attribute",                                     \
          obj_type##_id,                                                     \
          attr_count,                                                        \
          attr_list,                                                         \
          sai_obj_type,                                                      \
          rv);                                                               \
      SaiTracer::getInstance()->logPostInvocation(rv, obj_type##_id, begin); \
      return rv;                                                             \
    }                                                                        \
    return SaiTracer::getInstance()                                          \
        ->api_type##Api_->get_##obj_type##_attribute(                        \
            obj_type##_id, attr_count, attr_list);                           \
  }

#define WRAP_BULK_GET_ATTR_FUNC(obj_type, sai_obj_type, api_type)              \
  sai_status_t wrap_get_##obj_type##s_attribute(                               \
      uint32_t object_count,                                                   \
      const sai_object_id_t* object_id,                                        \
      const uint32_t* attr_count,                                              \
      sai_attribute_t** attr_list,                                             \
      sai_bulk_op_error_mode_t mode,                                           \
      sai_status_t* object_statuses) {                                         \
    auto rv =                                                                  \
        SaiTracer::getInstance()->api_type##Api_->get_##obj_type##s_attribute( \
            object_count,                                                      \
            object_id,                                                         \
            attr_count,                                                        \
            attr_list,                                                         \
            mode,                                                              \
            object_statuses);                                                  \
                                                                               \
    SaiTracer::getInstance()->logBulkGetAttrFn(                                \
        "get_" #obj_type "s_attribute",                                        \
        object_count,                                                          \
        object_id,                                                             \
        attr_count,                                                            \
        attr_list,                                                             \
        mode,                                                                  \
        object_statuses,                                                       \
        sai_obj_type,                                                          \
        rv);                                                                   \
    return rv;                                                                 \
  }

#define WRAP_BULK_SET_ATTR_FUNC(obj_type, sai_obj_type, api_type)              \
  sai_status_t wrap_set_##obj_type##s_attribute(                               \
      uint32_t object_count,                                                   \
      const sai_object_id_t* object_id,                                        \
      const sai_attribute_t* attr_list,                                        \
      sai_bulk_op_error_mode_t mode,                                           \
      sai_status_t* object_statuses) {                                         \
    auto rv =                                                                  \
        SaiTracer::getInstance()->api_type##Api_->set_##obj_type##s_attribute( \
            object_count, object_id, attr_list, mode, object_statuses);        \
                                                                               \
    SaiTracer::getInstance()->logBulkSetAttrFn(                                \
        "set_" #obj_type "s_attribute",                                        \
        object_count,                                                          \
        object_id,                                                             \
        attr_list,                                                             \
        mode,                                                                  \
        object_statuses,                                                       \
        sai_obj_type,                                                          \
        rv);                                                                   \
    return rv;                                                                 \
  }

// Bulk create functions - calls logBulkCreateFn for proper bulk logging
#define WRAP_BULK_CREATE_FUNC(obj_type, sai_obj_type, api_type)               \
  sai_status_t wrap_create_##obj_type##s(                                     \
      sai_object_id_t switch_id,                                              \
      uint32_t object_count,                                                  \
      const uint32_t* attr_count,                                             \
      const sai_attribute_t** attr_list,                                      \
      sai_bulk_op_error_mode_t mode,                                          \
      sai_object_id_t* object_id,                                             \
      sai_status_t* object_statuses) {                                        \
    auto begin = FLAGS_enable_elapsed_time_log                                \
        ? std::chrono::system_clock::now()                                    \
        : std::chrono::system_clock::time_point::min();                       \
    auto rv = SaiTracer::getInstance()->api_type##Api_->create_##obj_type##s( \
        switch_id,                                                            \
        object_count,                                                         \
        attr_count,                                                           \
        attr_list,                                                            \
        mode,                                                                 \
        object_id,                                                            \
        object_statuses);                                                     \
    SaiTracer::getInstance()->logBulkCreateFn(                                \
        "create_" #obj_type "s",                                              \
        switch_id,                                                            \
        object_count,                                                         \
        attr_count,                                                           \
        attr_list,                                                            \
        mode,                                                                 \
        object_id,                                                            \
        object_statuses,                                                      \
        sai_obj_type,                                                         \
        rv);                                                                  \
    SaiTracer::getInstance()->logPostInvocation(                              \
        rv, object_count > 0 ? object_id[0] : SAI_NULL_OBJECT_ID, begin);     \
    return rv;                                                                \
  }

// Bulk remove functions - calls logBulkRemoveFn for proper bulk logging
#define WRAP_BULK_REMOVE_FUNC(obj_type, sai_obj_type, api_type)               \
  sai_status_t wrap_remove_##obj_type##s(                                     \
      uint32_t object_count,                                                  \
      const sai_object_id_t* object_id,                                       \
      sai_bulk_op_error_mode_t mode,                                          \
      sai_status_t* object_statuses) {                                        \
    auto begin = FLAGS_enable_elapsed_time_log                                \
        ? std::chrono::system_clock::now()                                    \
        : std::chrono::system_clock::time_point::min();                       \
    auto rv = SaiTracer::getInstance()->api_type##Api_->remove_##obj_type##s( \
        object_count, object_id, mode, object_statuses);                      \
    SaiTracer::getInstance()->logBulkRemoveFn(                                \
        "remove_" #obj_type "s",                                              \
        object_count,                                                         \
        object_id,                                                            \
        mode,                                                                 \
        object_statuses,                                                      \
        sai_obj_type,                                                         \
        rv);                                                                  \
    SaiTracer::getInstance()->logPostInvocation(                              \
        rv, object_count > 0 ? object_id[0] : SAI_NULL_OBJECT_ID, begin);     \
    return rv;                                                                \
  }

#define WRAP_GET_STATS_FUNC(obj_type, sai_obj_type, api_type)                \
  sai_status_t wrap_get_##obj_type##_stats(                                  \
      sai_object_id_t obj_type##_id,                                         \
      uint32_t num_of_counters,                                              \
      const sai_stat_id_t* counter_ids,                                      \
      uint64_t* counters) {                                                  \
    if (FLAGS_enable_replayer && FLAGS_enable_get_attr_log) {                \
      auto begin = FLAGS_enable_elapsed_time_log                             \
          ? std::chrono::system_clock::now()                                 \
          : std::chrono::system_clock::time_point::min();                    \
      auto rv =                                                              \
          SaiTracer::getInstance()->api_type##Api_->get_##obj_type##_stats(  \
              obj_type##_id, num_of_counters, counter_ids, counters);        \
                                                                             \
      SaiTracer::getInstance()->logGetStatsFn(                               \
          "get_" #obj_type "_stats",                                         \
          obj_type##_id,                                                     \
          num_of_counters,                                                   \
          counter_ids,                                                       \
          counters,                                                          \
          sai_obj_type,                                                      \
          rv);                                                               \
      SaiTracer::getInstance()->logPostInvocation(rv, obj_type##_id, begin); \
      return rv;                                                             \
    }                                                                        \
    return SaiTracer::getInstance()->api_type##Api_->get_##obj_type##_stats( \
        obj_type##_id, num_of_counters, counter_ids, counters);              \
  }

#define WRAP_GET_STATS_EXT_FUNC(obj_type, sai_obj_type, api_type)            \
  sai_status_t wrap_get_##obj_type##_stats_ext(                              \
      sai_object_id_t obj_type##_id,                                         \
      uint32_t num_of_counters,                                              \
      const sai_stat_id_t* counter_ids,                                      \
      sai_stats_mode_t mode,                                                 \
      uint64_t* counters) {                                                  \
    if (FLAGS_enable_replayer && FLAGS_enable_get_attr_log) {                \
      auto begin = FLAGS_enable_elapsed_time_log                             \
          ? std::chrono::system_clock::now()                                 \
          : std::chrono::system_clock::time_point::min();                    \
      auto rv = SaiTracer::getInstance()                                     \
                    ->api_type##Api_->get_##obj_type##_stats_ext(            \
                        obj_type##_id,                                       \
                        num_of_counters,                                     \
                        counter_ids,                                         \
                        mode,                                                \
                        counters);                                           \
                                                                             \
      SaiTracer::getInstance()->logGetStatsFn(                               \
          "get_" #obj_type "_stats_ext",                                     \
          obj_type##_id,                                                     \
          num_of_counters,                                                   \
          counter_ids,                                                       \
          counters,                                                          \
          sai_obj_type,                                                      \
          rv,                                                                \
          mode);                                                             \
      SaiTracer::getInstance()->logPostInvocation(rv, obj_type##_id, begin); \
      return rv;                                                             \
    }                                                                        \
    return SaiTracer::getInstance()                                          \
        ->api_type##Api_->get_##obj_type##_stats_ext(                        \
            obj_type##_id, num_of_counters, counter_ids, mode, counters);    \
  }

#define WRAP_CLEAR_STATS_FUNC(obj_type, sai_obj_type, api_type)             \
  sai_status_t wrap_clear_##obj_type##_stats(                               \
      sai_object_id_t obj_type##_id,                                        \
      uint32_t num_of_counters,                                             \
      const sai_stat_id_t* counter_ids) {                                   \
    auto rv =                                                               \
        SaiTracer::getInstance()->api_type##Api_->clear_##obj_type##_stats( \
            obj_type##_id, num_of_counters, counter_ids);                   \
                                                                            \
    SaiTracer::getInstance()->logClearStatsFn(                              \
        "clear_" #obj_type "_stats",                                        \
        obj_type##_id,                                                      \
        num_of_counters,                                                    \
        counter_ids,                                                        \
        sai_obj_type,                                                       \
        rv);                                                                \
    return rv;                                                              \
  }

#define SAI_ATTR_MAP(obj_type, attr_name)                                  \
  {facebook::fboss::Sai##obj_type##Traits::Attributes::attr_name::Id,      \
   std::make_pair(                                                         \
       #attr_name,                                                         \
       TYPE_INDEX(                                                         \
           facebook::fboss::Sai##obj_type##Traits::Attributes::attr_name:: \
               ExtractSelectionType))}

#define SAI_EXT_ATTR_MAP(obj_type, attr_name)                             \
  if (facebook::fboss::Sai##obj_type##Traits::Attributes::attr_name::     \
          AttributeId()()                                                 \
              .has_value()) {                                             \
    _##obj_type##Map[facebook::fboss::Sai##obj_type##Traits::Attributes:: \
                         attr_name::AttributeId()()                       \
                             .value()] =                                  \
        std::make_pair(                                                   \
            #attr_name,                                                   \
            TYPE_INDEX(                                                   \
                facebook::fboss::Sai##obj_type##Traits::Attributes::      \
                    attr_name::ExtractSelectionType));                    \
  }

#define SAI_EXT_ATTR_MAP_2(obj_type, obj_sub_type, attr_name)                 \
  if (facebook::fboss::Sai##obj_sub_type##Traits::Attributes::attr_name::     \
          AttributeId()()                                                     \
              .has_value()) {                                                 \
    _##obj_type##Map[facebook::fboss::Sai##obj_sub_type##Traits::Attributes:: \
                         attr_name::AttributeId()()                           \
                             .value()] =                                      \
        std::make_pair(                                                       \
            #attr_name,                                                       \
            TYPE_INDEX(                                                       \
                facebook::fboss::Sai##obj_sub_type##Traits::Attributes::      \
                    attr_name::ExtractSelectionType));                        \
  }

#define SET_SAI_REGULAR_ATTRIBUTES(obj_type)                                 \
  void set##obj_type##Attributes(                                            \
      const sai_attribute_t* attr_list,                                      \
      uint32_t attr_count,                                                   \
      std::vector<std::string>& attrLines,                                   \
      sai_status_t rv) {                                                     \
    uint32_t listCount = 0;                                                  \
                                                                             \
    for (int i = 0; i < attr_count; ++i) {                                   \
      auto iter = _##obj_type##Map.find(attr_list[i].id);                    \
      if (iter != _##obj_type##Map.end()) {                                  \
        auto attrName = _##obj_type##Map[attr_list[i].id].first;             \
        auto typeIndex = _##obj_type##Map[attr_list[i].id].second;           \
        attrLines.push_back(to<std::string>("// ", attrName));               \
        auto primitiveFuncMatch =                                            \
            SaiTracer::getInstance()->primitiveFuncMap_.find(typeIndex);     \
        if (primitiveFuncMatch !=                                            \
            SaiTracer::getInstance()->primitiveFuncMap_.end()) {             \
          attrLines.push_back((*primitiveFuncMatch->second)(attr_list, i));  \
          continue;                                                          \
        }                                                                    \
        auto attributeFuncMatch =                                            \
            SaiTracer::getInstance()->attributeFuncMap_.find(typeIndex);     \
        if (attributeFuncMatch !=                                            \
            SaiTracer::getInstance()->attributeFuncMap_.end()) {             \
          (*attributeFuncMatch->second)(attr_list, i, attrLines);            \
          continue;                                                          \
        }                                                                    \
        auto listFuncMatch =                                                 \
            SaiTracer::getInstance()->listFuncMap_.find(typeIndex);          \
        if (listFuncMatch != SaiTracer::getInstance()->listFuncMap_.end()) { \
          (*listFuncMatch->second)(                                          \
              attr_list, i, listCount, attrLines, rv == 0);                  \
          if (typeIndex == TYPE_INDEX(AclEntryFieldU8List)) {                \
            listCount += 2;                                                  \
          } else {                                                           \
            listCount++;                                                     \
          }                                                                  \
          continue;                                                          \
        }                                                                    \
      }

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
#define SET_SAI_STRING_ATTRIBUTES(obj_type)                                  \
  /* For attributes cannot handled by the aboved method */                   \
  switch (attr_list[i].id) {                                                 \
    case SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO: /* 113 */                     \
    case SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME: /* 114 */                       \
      s8ListAttr(attr_list, i, listCount++, attrLines, true);                \
      break;                                                                 \
    case SAI_LAG_ATTR_LABEL: /* 9 */                                         \
    case SAI_COUNTER_ATTR_LABEL: /* 1 */                                     \
      charDataAttr(attr_list, i, attrLines);                                 \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_AUTH_KEY:                                        \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecauthkey),     \
          16,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecauthkey");                                                  \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_SAK:                                             \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecsak),         \
          32,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecsak");                                                      \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_SALT:                                            \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecsalt),        \
          12,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecsalt");                                                     \
      break;                                                                 \
    default:                                                                 \
      XLOG(WARN) << "Unsupported object type " << #obj_type << " attribute " \
                 << attr_list[i].id << " in Sai Replayer";                   \
  }                                                                          \
  }                                                                          \
  }
#else
#define SET_SAI_STRING_ATTRIBUTES(obj_type)                                  \
  /* For attributes cannot handled by the aboved method */                   \
  switch (attr_list[i].id) {                                                 \
    case SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO: /* 113 */                     \
    case SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME: /* 114 */                       \
      s8ListAttr(attr_list, i, listCount++, attrLines, true);                \
      break;                                                                 \
    case SAI_LAG_ATTR_LABEL: /* 9 */                                         \
      charDataAttr(attr_list, i, attrLines);                                 \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_AUTH_KEY:                                        \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecauthkey),     \
          16,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecauthkey");                                                  \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_SAK:                                             \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecsak),         \
          32,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecsak");                                                      \
      break;                                                                 \
    case SAI_MACSEC_SA_ATTR_SALT:                                            \
      u8ArrGenericAttr(                                                      \
          static_cast<const uint8_t*>(attr_list[i].value.macsecsalt),        \
          12,                                                                \
          i,                                                                 \
          attrLines,                                                         \
          "macsecsalt");                                                     \
      break;                                                                 \
    default:                                                                 \
      XLOG(WARN) << "Unsupported object type " << #obj_type << " attribute " \
                 << attr_list[i].id << " in Sai Replayer";                   \
  }                                                                          \
  }                                                                          \
  }
#endif

#define SET_SAI_STRING_ATTRIBUTES_ACL_COUNTER(obj_type)                      \
  switch (attr_list[i].id) {                                                 \
    case SAI_ACL_COUNTER_ATTR_LABEL: /* 5 */                                 \
      charDataAttr(attr_list, i, attrLines);                                 \
      break;                                                                 \
    default:                                                                 \
      XLOG(WARN) << "Unsupported object type " << #obj_type << " attribute " \
                 << attr_list[i].id << " in Sai Replayer";                   \
  }                                                                          \
  }                                                                          \
  }

// TODO - Combine this to once macro once the SAI SDK dependency is gone
#define SET_SAI_ATTRIBUTES(obj_type)   \
  SET_SAI_REGULAR_ATTRIBUTES(obj_type) \
  SET_SAI_STRING_ATTRIBUTES(obj_type)

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
#define SET_SAI_ATTRIBUTES_ACL_COUNTER(obj_type) \
  SET_SAI_REGULAR_ATTRIBUTES(obj_type)           \
  SET_SAI_STRING_ATTRIBUTES_ACL_COUNTER(obj_type)
#else
#define SET_SAI_ATTRIBUTES_ACL_COUNTER(obj_type) \
  SET_SAI_REGULAR_ATTRIBUTES(obj_type)           \
  }                                              \
  }
#endif

} // namespace facebook::fboss
