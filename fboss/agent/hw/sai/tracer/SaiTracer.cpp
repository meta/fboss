/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <tuple>

#include "fboss/agent/SysError.h"
#include "fboss/agent/hw/sai/api/LoggingUtil.h"
#include "fboss/agent/hw/sai/tracer/AclApiTracer.h"
#include "fboss/agent/hw/sai/tracer/ArsApiTracer.h"
#include "fboss/agent/hw/sai/tracer/ArsProfileApiTracer.h"
#include "fboss/agent/hw/sai/tracer/BridgeApiTracer.h"
#include "fboss/agent/hw/sai/tracer/BufferApiTracer.h"
#include "fboss/agent/hw/sai/tracer/CounterApiTracer.h"
#include "fboss/agent/hw/sai/tracer/DebugCounterApiTracer.h"
#include "fboss/agent/hw/sai/tracer/FdbApiTracer.h"
#include "fboss/agent/hw/sai/tracer/FirmwareApiTracer.h"
#include "fboss/agent/hw/sai/tracer/HashApiTracer.h"
#include "fboss/agent/hw/sai/tracer/HostifApiTracer.h"
#include "fboss/agent/hw/sai/tracer/IsolationGroupApiTracer.h"
#include "fboss/agent/hw/sai/tracer/LagApiTracer.h"
#include "fboss/agent/hw/sai/tracer/MacsecApiTracer.h"
#include "fboss/agent/hw/sai/tracer/MirrorApiTracer.h"
#include "fboss/agent/hw/sai/tracer/MplsApiTracer.h"
#include "fboss/agent/hw/sai/tracer/MyMacApiTracer.h"
#include "fboss/agent/hw/sai/tracer/NeighborApiTracer.h"
#include "fboss/agent/hw/sai/tracer/NextHopApiTracer.h"
#include "fboss/agent/hw/sai/tracer/NextHopGroupApiTracer.h"
#include "fboss/agent/hw/sai/tracer/PortApiTracer.h"
#include "fboss/agent/hw/sai/tracer/QosMapApiTracer.h"
#include "fboss/agent/hw/sai/tracer/QueueApiTracer.h"
#include "fboss/agent/hw/sai/tracer/RouteApiTracer.h"
#include "fboss/agent/hw/sai/tracer/RouterInterfaceApiTracer.h"
#include "fboss/agent/hw/sai/tracer/SaiTracer.h"
#include "fboss/agent/hw/sai/tracer/SamplePacketApiTracer.h"
#include "fboss/agent/hw/sai/tracer/SchedulerApiTracer.h"
#include "fboss/agent/hw/sai/tracer/Srv6ApiTracer.h" // @manual
#include "fboss/agent/hw/sai/tracer/SwitchApiTracer.h"
#include "fboss/agent/hw/sai/tracer/SwitchPipelineApiTracer.h" // NOLINT(facebook-unused-include-check)
#include "fboss/agent/hw/sai/tracer/SystemPortApiTracer.h"
#include "fboss/agent/hw/sai/tracer/TamApiTracer.h"
#include "fboss/agent/hw/sai/tracer/TamEventAgingGroupApiTracer.h" // NOLINT(facebook-unused-include-check)
#include "fboss/agent/hw/sai/tracer/TunnelApiTracer.h"
#include "fboss/agent/hw/sai/tracer/UdfApiTracer.h"
#include "fboss/agent/hw/sai/tracer/VendorSwitchApiTracer.h" // NOLINT(facebook-unused-include-check)
#include "fboss/agent/hw/sai/tracer/VirtualChannelApiTracer.h" // NOLINT(facebook-unused-include-check)
#include "fboss/agent/hw/sai/tracer/VirtualRouterApiTracer.h"
#include "fboss/agent/hw/sai/tracer/VlanApiTracer.h"
#include "fboss/agent/hw/sai/tracer/WredApiTracer.h"

#include <folly/FileUtil.h>
#include <folly/MacAddress.h>
#include <folly/MapUtil.h>
#include <folly/Singleton.h>
#include <folly/String.h>

extern "C" {
#include <sai.h>
}

DEFINE_bool(
    enable_replayer,
    false,
    "Flag to indicate whether function wrapper and logger is enabled.");

DEFINE_bool(
    enable_packet_log,
    false,
    "Flag to indicate whether to log the send_hostif_packet function."
    "At runtime, it should be disabled to reduce logging overhead."
    "However, it's needed for testing e.g. HwL4PortBlackHolingTest");

DEFINE_bool(
    enable_elapsed_time_log,
    false,
    "Flag to indicate whether to log the elapsed time of SDK API calls.");

DEFINE_bool(
    enable_get_attr_log,
    false,
    "Flag to indicate whether to log the get API calls. "
    "At runtime, it should be disabled to reduce logging overhead.");

DEFINE_string(
    sai_log,
    "/var/facebook/logs/fboss/sdk/sai_replayer.log",
    "File path to the SAI Replayer logs");

DEFINE_string(
    sai_replayer_sdk_log_level,
    "CRITICAL",
    "Turn on SAI SDK logging during replay. Options are DEBUG|INFO|NOTICE|WARN|ERROR|CRITICAL");

DEFINE_int32(
    default_list_size,
    4096,
    "Default size of the lists initialzied by SAI replayer");

DEFINE_int32(
    default_list_count,
    6,
    "Default number of the lists initialzied by SAI replayer");

DEFINE_int32(
    log_timeout,
    100,
    "Log timeout value in milliseconds. Logger will periodically"
    "flush logs even if the buffer is not full");

DEFINE_bool(
    log_variable_name,
    false,
    "Flag to indicate whether to log variable names or simply object ID");

using facebook::fboss::SaiTracer;
using folly::to;
using std::string;
using std::vector;

extern "C" {

// Real functions for Sai APIs
sai_status_t __real_sai_api_initialize(
    uint64_t flags,
    const sai_service_method_table_t* services);
sai_status_t __real_sai_api_uninitialize(void);

sai_status_t __real_sai_api_query(
    sai_api_t sai_api_id,
    void** api_method_table);
sai_status_t __real_sai_get_object_key(
    sai_object_id_t switch_id,
    sai_object_type_t object_type,
    uint32_t* object_count,
    sai_object_key_t* object_list);
// Wrap function for Sai APIs
sai_status_t __wrap_sai_api_initialize(
    uint64_t flags,
    const sai_service_method_table_t* services) {
  sai_status_t rv = __real_sai_api_initialize(flags, services);

  if (FLAGS_enable_replayer && services) {
    // Reset service table iterator
    services->profile_get_next_value(0, nullptr, nullptr);

    std::array<const char*, 32> variables;
    std::array<const char*, 32> values;
    int size = 0;

    while (services->profile_get_next_value(
               0, &variables[size], &values[size]) != -1) {
      size++;
    }

    SaiTracer::getInstance()->logApiInitialize(
        variables.data(), values.data(), size);
  }

  return rv;
}

sai_status_t __wrap_sai_api_uninitialize(void) {
  if (FLAGS_enable_replayer) {
    // Check if tracer is still there. If uninitialize() is called from
    // a singleton's destructor, there's a chance the SaiTracer singleton
    // is already destroyed as there's no ordering/dependency between the
    // two.
    //
    // It's fine to omit this output if tracer is gone. This is just software
    // cleanup and should have no effect in ASIC state reproduction to
    // vendors.
    if (auto tracer = SaiTracer::getInstance()) {
      tracer->logApiUninitialize();
    }
  }
  return __real_sai_api_uninitialize();
}

sai_status_t __wrap_sai_api_query(
    sai_api_t sai_api_id,
    void** api_method_table) {
  // Functions defined in sai.h can be properly wrapped using the linker trick
  // e.g. sai_api_query(), sai_api_initialize() ...
  // However, for other functions such as create_*, set_*, and get_*,
  // their function pointers are returned to the api_method_table in this call.
  // To 'wrap' those function pointers as well, we store the real
  // function pointers and return the 'wrapped' function pointers defined in
  // this tracer directory, adding an redirection layer for logging purpose.
  // (See 'AclApiTracer.h' for example).

  sai_status_t rv = __real_sai_api_query(sai_api_id, api_method_table);

  if (!FLAGS_enable_replayer) {
    return rv;
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if (UNLIKELY(sai_api_id >= SAI_API_MAX)) {
    switch (static_cast<sai_api_extensions_t>(sai_api_id)) {
      case SAI_API_TAM_EVENT_AGING_GROUP:
        SaiTracer::getInstance()->tamEventAgingGroupApi_ =
            static_cast<sai_tam_event_aging_group_api_t*>(*api_method_table);
        *api_method_table = facebook::fboss::wrappedTamEventAgingGroupApi();
        SaiTracer::getInstance()->logApiQuery(
            sai_api_id, "tam_event_aging_group_api");
        break;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      case SAI_API_VENDOR_SWITCH:
        SaiTracer::getInstance()->vendorSwitchApi_ =
            static_cast<sai_vendor_switch_api_t*>(*api_method_table);
        *api_method_table = facebook::fboss::wrappedVendorSwitchApi();
        SaiTracer::getInstance()->logApiQuery(sai_api_id, "vendor_switch_api");
        break;
      case SAI_API_SWITCH_PIPELINE:
        SaiTracer::getInstance()->switchPipelineApi_ =
            static_cast<sai_switch_pipeline_api_t*>(*api_method_table);
        *api_method_table = facebook::fboss::wrappedSwitchPipelineApi();
        SaiTracer::getInstance()->logApiQuery(
            sai_api_id, "switch_pipeline_api");
        break;
#endif
      case SAI_API_FIRMWARE:
        SaiTracer::getInstance()->firmwareApi_ =
            static_cast<sai_firmware_api_t*>(*api_method_table);
        *api_method_table = facebook::fboss::wrappedFirmwareApi();
        SaiTracer::getInstance()->logApiQuery(sai_api_id, "firmware_api");
        break;
      default:
        break;
    }
    return rv;
  }
#endif

  switch (sai_api_id) {
    case SAI_API_ACL:
      SaiTracer::getInstance()->aclApi_ =
          static_cast<sai_acl_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedAclApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "acl_api");
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    case SAI_API_ARS:
      SaiTracer::getInstance()->arsApi_ =
          static_cast<sai_ars_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedArsApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "ars_api");
      break;
    case SAI_API_ARS_PROFILE:
      SaiTracer::getInstance()->arsProfileApi_ =
          static_cast<sai_ars_profile_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedArsProfileApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "ars_profile_api");
      break;
#endif
    case SAI_API_BRIDGE:
      SaiTracer::getInstance()->bridgeApi_ =
          static_cast<sai_bridge_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedBridgeApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "bridge_api");
      break;
    case SAI_API_BUFFER:
      SaiTracer::getInstance()->bufferApi_ =
          static_cast<sai_buffer_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedBufferApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "buffer_api");
      break;
    case SAI_API_COUNTER:
      SaiTracer::getInstance()->counterApi_ =
          static_cast<sai_counter_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedCounterApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "counter_api");
      break;
    case SAI_API_DEBUG_COUNTER:
      SaiTracer::getInstance()->debugCounterApi_ =
          static_cast<sai_debug_counter_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedDebugCounterApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "debug_counter_api");
      break;
    case SAI_API_FDB:
      SaiTracer::getInstance()->fdbApi_ =
          static_cast<sai_fdb_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedFdbApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "fdb_api");
      break;
    case SAI_API_HASH:
      SaiTracer::getInstance()->hashApi_ =
          static_cast<sai_hash_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedHashApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "hash_api");
      break;
    case SAI_API_HOSTIF:
      SaiTracer::getInstance()->hostifApi_ =
          static_cast<sai_hostif_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedHostifApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "hostif_api");
      break;
    case SAI_API_LAG:
      SaiTracer::getInstance()->lagApi_ =
          static_cast<sai_lag_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedLagApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "lag_api");
      break;
    case SAI_API_MACSEC:
      SaiTracer::getInstance()->macsecApi_ =
          static_cast<sai_macsec_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedMacsecApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "macsec_api");
      break;
    case SAI_API_MIRROR:
      SaiTracer::getInstance()->mirrorApi_ =
          static_cast<sai_mirror_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedMirrorApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "mirror_api");
      break;
    case SAI_API_MPLS:
      SaiTracer::getInstance()->mplsApi_ =
          static_cast<sai_mpls_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedMplsApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "mpls_api");
      break;
    case SAI_API_NEIGHBOR:
      SaiTracer::getInstance()->neighborApi_ =
          static_cast<sai_neighbor_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedNeighborApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "neighbor_api");
      break;
    case SAI_API_NEXT_HOP:
      SaiTracer::getInstance()->nextHopApi_ =
          static_cast<sai_next_hop_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedNextHopApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "next_hop_api");
      break;
    case SAI_API_NEXT_HOP_GROUP:
      SaiTracer::getInstance()->nextHopGroupApi_ =
          static_cast<sai_next_hop_group_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedNextHopGroupApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "next_hop_group_api");
      break;
    case SAI_API_PORT:
      SaiTracer::getInstance()->portApi_ =
          static_cast<sai_port_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedPortApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "port_api");
      break;
    case SAI_API_QOS_MAP:
      SaiTracer::getInstance()->qosMapApi_ =
          static_cast<sai_qos_map_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedQosMapApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "qos_map_api");
      break;
    case SAI_API_QUEUE:
      SaiTracer::getInstance()->queueApi_ =
          static_cast<sai_queue_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedQueueApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "queue_api");
      break;
    case SAI_API_ROUTE:
      SaiTracer::getInstance()->routeApi_ =
          static_cast<sai_route_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedRouteApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "route_api");
      break;
    case SAI_API_ROUTER_INTERFACE:
      SaiTracer::getInstance()->routerInterfaceApi_ =
          static_cast<sai_router_interface_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedRouterInterfaceApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "router_interface_api");
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    case SAI_API_MY_MAC:
      SaiTracer::getInstance()->myMacApi_ =
          static_cast<sai_my_mac_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedMyMacApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "my_mac_api");
      break;
#endif
    case SAI_API_SAMPLEPACKET:
      SaiTracer::getInstance()->samplepacketApi_ =
          static_cast<sai_samplepacket_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedSamplePacketApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "samplepacket_api");
      break;
    case SAI_API_SCHEDULER:
      SaiTracer::getInstance()->schedulerApi_ =
          static_cast<sai_scheduler_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedSchedulerApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "scheduler_api");
      break;
    case SAI_API_SWITCH:
      SaiTracer::getInstance()->switchApi_ =
          static_cast<sai_switch_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedSwitchApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "switch_api");
      break;
    case SAI_API_SYSTEM_PORT:
      SaiTracer::getInstance()->systemPortApi_ =
          static_cast<sai_system_port_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedSystemPortApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "system_port_api");
      break;
    case SAI_API_TAM:
      SaiTracer::getInstance()->tamApi_ =
          static_cast<sai_tam_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedTamApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "tam_api");
      break;
    case SAI_API_TUNNEL:
      SaiTracer::getInstance()->tunnelApi_ =
          static_cast<sai_tunnel_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedTunnelApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "tunnel_api");
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_API_SRV6:
      SaiTracer::getInstance()->srv6Api_ =
          static_cast<sai_srv6_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedSrv6Api();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "srv6_api");
      break;
#endif
    case SAI_API_UDF:
      SaiTracer::getInstance()->udfApi_ =
          static_cast<sai_udf_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedUdfApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "udf_api");
      break;
#if defined(SAI_CBFC_SUPPORTED)
    case SAI_API_VIRTUAL_CHANNEL:
      SaiTracer::getInstance()->virtualChannelApi_ =
          static_cast<sai_virtual_channel_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedVirtualChannelApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "virtual_channel_api");
      break;
#endif
    case SAI_API_VIRTUAL_ROUTER:
      SaiTracer::getInstance()->virtualRouterApi_ =
          static_cast<sai_virtual_router_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedVirtualRouterApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "virtual_router_api");
      break;
    case SAI_API_ISOLATION_GROUP:
      SaiTracer::getInstance()->isolationGroupApi_ =
          static_cast<sai_isolation_group_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedIsolationGroupApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "isolation_group_api");
      break;
    case SAI_API_VLAN:
      SaiTracer::getInstance()->vlanApi_ =
          static_cast<sai_vlan_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedVlanApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "vlan_api");
      break;
    case SAI_API_WRED:
      SaiTracer::getInstance()->wredApi_ =
          static_cast<sai_wred_api_t*>(*api_method_table);
      *api_method_table = facebook::fboss::wrappedWredApi();
      SaiTracer::getInstance()->logApiQuery(sai_api_id, "wred_api");
      break;
    default:
      // TODO: For other APIs, create new API wrappers and invoke wrappedApi()
      // funtion here
      break;
  }

  return rv;
}

bool shouldLogObjectKey(sai_object_type_t object_type) {
  /*
   * Currently we only log oid based object keys.
   * TODO: Start logging non oid based keys as well
   * they are part of the sai_object_key object
   * https://github.com/opencomputeproject/SAI/blob/master/inc/saiobject.h#L68
   */
  return object_type != SAI_OBJECT_TYPE_INSEG_ENTRY &&
      object_type != SAI_OBJECT_TYPE_FDB_ENTRY &&
      object_type != SAI_OBJECT_TYPE_NEIGHBOR_ENTRY &&
      object_type != SAI_OBJECT_TYPE_ROUTE_ENTRY
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      && object_type != SAI_OBJECT_TYPE_MY_SID_ENTRY
#endif
      ;
}

sai_status_t __wrap_sai_get_object_key(
    sai_object_id_t switch_id,
    sai_object_type_t object_type,
    uint32_t* object_count,
    sai_object_key_t* object_list) {
  sai_status_t rv = __real_sai_get_object_key(
      switch_id, object_type, object_count, object_list);

  if (!FLAGS_enable_replayer || *object_count == 0 ||
      !shouldLogObjectKey(object_type)) {
    return rv;
  }

  vector<string> getObjectKeyLines = {
      to<string>("expected_object_count=", *object_count),
      to<string>(
          "sai_get_object_count(switch_0, (_sai_object_type_t)",
          object_type,
          ", &object_count)"),
      "object_list.resize(object_count)",
      to<string>(
          "sai_get_object_key(switch_0, (_sai_object_type_t)",
          object_type,
          ", &object_count, object_list.data())"),
      to<string>(
          "if (object_count < expected_object_count) { printf(\"[WARNING] current switch reloaded %u ",
          facebook::fboss::saiObjectTypeToString(object_type),
          " objects, expected %u\\n\", expected_object_count, object_count); }"),
  };

  vector<string> declarationLines;
  if (object_list != nullptr) {
    declarationLines.reserve(*object_count);
    for (int i = 0; i < *object_count; ++i) {
      sai_object_key_t object = object_list[i];
      string declaration =
          std::get<0>(SaiTracer::getInstance()->declareVariable(
              &object.key.object_id, object_type));
      declarationLines.push_back(
          to<string>(
              declaration,
              "=assignObject(object_list.data(), object_count, ",
              i,
              ", ",
              object.key.object_id,
              ")"));
    }
  }

  vector<string> lines;
  lines.insert(lines.end(), getObjectKeyLines.begin(), getObjectKeyLines.end());
  lines.insert(lines.end(), declarationLines.begin(), declarationLines.end());
  SaiTracer::getInstance()->writeToFile(lines);
  return rv;
}

} // extern "C"

namespace {

folly::Singleton<facebook::fboss::SaiTracer> _saiTracer;
constexpr std::string_view kGlobalVarStart = "/* Global Variables Start */";
constexpr std::string_view kGlobalVarEnd = "/* Global Variables End */";

} // namespace

namespace facebook::fboss {

SaiTracer::SaiTracer() {
  if (FLAGS_enable_replayer) {
    asyncLogger_ = std::make_unique<AsyncLogger>(
        FLAGS_sai_log, FLAGS_log_timeout, AsyncLogger::SAI_REPLAYER);

    asyncLogger_->startFlushThread();
    asyncLogger_->appendLog(cpp_header_, strlen(cpp_header_));

    setupGlobals();
    initVarCounts();
  }
}

SaiTracer::~SaiTracer() {
  if (FLAGS_enable_replayer) {
    writeFooter();
    asyncLogger_->forceFlush();
    asyncLogger_->stopFlushThread();
  }
}

std::shared_ptr<SaiTracer> SaiTracer::getInstance() {
  return _saiTracer.try_get();
}

void SaiTracer::printHex(std::ostringstream& outStringStream, uint8_t u8) {
  outStringStream << "0x" << std::setfill('0') << std::setw(2) << std::hex
                  << static_cast<int>(u8);
}

void SaiTracer::writeToFile(const vector<string>& strVec, bool linefeed) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  auto constexpr lineEnd = ";\n";
  auto lines = folly::join(lineEnd, strVec) + lineEnd + (linefeed ? "\n" : "");

  asyncLogger_->appendLog(lines.c_str(), lines.size());
}

void SaiTracer::logApiInitialize(
    const char** variables,
    const char** values,
    int size) {
  vector<string> lines;

  for (int i = 0; i < size; ++i) {
    if (!strcmp(variables[i], SAI_KEY_WARM_BOOT_WRITE_FILE) ||
        !strcmp(variables[i], SAI_KEY_WARM_BOOT_READ_FILE)) {
      // Append '_replayer' suffix to sai adapter state file
      lines.push_back(
          to<string>(
              "kSaiProfileValues.emplace(\"",
              variables[i],
              "\",\"",
              values[i],
              "_replayer",
              "\")"));
    } else {
      lines.push_back(
          to<string>(
              "kSaiProfileValues.emplace(\"",
              variables[i],
              "\",\"",
              values[i],
              "\")"));
    }
  }

  lines.emplace_back("sai_api_initialize(0, &kSaiServiceMethodTable)");
  writeToFile(lines);
}

void SaiTracer::logApiUninitialize(void) {
  vector<string> lines{"sai_api_uninitialize()"};
  writeToFile(lines);
}

void SaiTracer::logApiQuery(sai_api_t api_id, const std::string& api_var) {
  // If replayer is not enabled or api is already initialized
  if (!FLAGS_enable_replayer || init_api_.find(api_id) != init_api_.end()) {
    return;
  }

  init_api_.emplace(api_id, api_var);

  writeToFile(
      {to<string>(kGlobalVarStart),
       to<string>("sai_", api_var, "_t* ", api_var),
       to<string>(kGlobalVarEnd),
       to<string>(
           "sai_api_query((sai_api_t)", api_id, ",(void**)&", api_var, ")"),
       to<string>(
           "rv = sai_log_set((sai_api_t)",
           api_id,
           ",(sai_log_level_t)",
           saiLogLevelFromString(FLAGS_sai_replayer_sdk_log_level),
           ")"),
       to<string>(
           "if(rv) printf(\"[ERROR] Failed to set log level for ",
           saiApiTypeToString(api_id),
           " API.\")")});
}

void SaiTracer::logSwitchCreateFn(
    sai_object_id_t* switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // First fill in attribute list
  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_SWITCH);

  // Then create new variable - declareVariable returns
  // 1. Declaration of the new variable
  // 2. name of the new variable
  auto [declaration, varName] =
      declareVariable(switch_id, SAI_OBJECT_TYPE_SWITCH);
  lines.push_back(declaration);

  // Make the function call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_SWITCH,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_switch(&",
          varName,
          ",",
          attr_count,
          ",s_a)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logRouteEntryCreateFn(
    const sai_route_entry_t* route_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // First fill in attribute list
  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_ROUTE_ENTRY);

  // Then setup route entry (switch, virtual router and destination)
  setRouteEntry(route_entry, lines);

  // Make the function call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_ROUTE_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_route_entry(&r_e,",
          attr_count,
          ",s_a)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logNeighborEntryCreateFn(
    const sai_neighbor_entry_t* neighbor_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // First fill in attribute list
  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_NEIGHBOR_ENTRY);

  // Then setup neighbor entry (switch, router interface and ip address)
  setNeighborEntry(neighbor_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make the function call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_NEIGHBOR_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_neighbor_entry(&n_e,",
          attr_count,
          ",s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logFdbEntryCreateFn(
    const sai_fdb_entry_t* fdb_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // First fill in attribute list
  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_FDB_ENTRY);

  // Then setup fdb entry (switch, router interface and ip address)
  setFdbEntry(fdb_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make the function call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_FDB_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_fdb_entry(&f_e,",
          attr_count,
          ",s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logInsegEntryCreateFn(
    const sai_inseg_entry_t* inseg_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // First fill in attribute list
  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_INSEG_ENTRY);

  // Then setup inseg entry (switch and label)
  setInsegEntry(inseg_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make the function call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_INSEG_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_inseg_entry(&i_e,",
          attr_count,
          ",s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
void SaiTracer::logMySidEntryCreateFn(
    const sai_my_sid_entry_t* my_sid_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_MY_SID_ENTRY);

  setMySidEntry(my_sid_entry, lines);

  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_MY_SID_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "create_my_sid_entry(&ms_e,",
          attr_count,
          ",s_a)"));

  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}
#endif

std::string SaiTracer::logCreateFn(
    const string& fn_name,
    sai_object_id_t* create_object_id,
    sai_object_id_t switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer) {
    return "";
  }

  // First fill in attribute list
  vector<string> lines = setAttrList(attr_list, attr_count, object_type);

  // Then create new variable - declareVariable returns
  // 1. Declaration of the new variable
  // 2. name of the new variable
  auto [declaration, varName] = declareVariable(create_object_id, object_type);
  lines.push_back(declaration);

  // Make the function call & write to file
  lines.push_back(createFnCall(
      fn_name, varName, getVariable(switch_id), attr_count, object_type));

  writeToFile(lines, /*linefeed*/ false);
  return varName;
}

void SaiTracer::logBulkCreateFn(
    const std::string& fn_name,
    sai_object_id_t switch_id,
    uint32_t object_count,
    const uint32_t* attr_count,
    const sai_attribute_t** attr_list,
    sai_bulk_op_error_mode_t mode,
    sai_object_id_t* object_id,
    sai_status_t* object_statuses,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Calculate total attributes needed for all objects
  uint32_t totalAttrs = 0;
  for (uint32_t i = 0; i < object_count; ++i) {
    totalAttrs += attr_count[i];
  }
  checkAttrCount(totalAttrs);

  vector<string> lines{};
  // Pre-allocate capacity to avoid repeated reallocations in loops
  // Estimate: 2 memset + object_count attr_list_ptrs + 1 memset +
  // object_count attr_counts + object_count*attrs + 1 timestamp +
  // 1 bulk call + object_count statuses + 1 rv check
  lines.reserve(5 + 3 * object_count + totalAttrs);

  // Initialize the main attribute array and attr_list_ptrs
  lines.push_back(to<string>("memset(s_a,0,ATTR_SIZE*", maxAttrCount_, ")"));
  lines.push_back(
      to<string>(
          "memset(attr_list_ptrs,0,sizeof(sai_attribute_t*)*",
          FLAGS_default_list_size,
          ")"));

  // Set up attr_list_ptrs array to point to the correct positions in s_a
  int attributeIndexInSa = 0;
  for (uint32_t i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>("attr_list_ptrs[", i, "]=&s_a[", attributeIndexInSa, "]"));
    attributeIndexInSa += attr_count[i];
  }

  // Setup attr_count array
  lines.push_back(
      to<string>(
          "memset(attr_count_list,0,sizeof(uint32_t)*",
          FLAGS_default_list_size,
          ")"));
  for (uint32_t i = 0; i < object_count; ++i) {
    lines.push_back(to<string>("attr_count_list[", i, "]=", attr_count[i]));
  }

  // Process each object's attributes
  for (uint32_t i = 0; i < object_count; ++i) {
    lines.push_back(to<string>("// Object ", i, " attributes:"));

    // Get attribute values for this object using setAttrList
    auto objAttrLines =
        setAttrList(attr_list[i], attr_count[i], object_type, 0);

    // Skip the first line (memset) and replace all s_a occurrences with
    // attr_list_ptrs[i]
    for (size_t lineIdx = 1; lineIdx < objAttrLines.size(); ++lineIdx) {
      string line = objAttrLines[lineIdx];
      string replacement = to<string>("attr_list_ptrs[", i, "]");
      size_t pos = 0;
      while ((pos = line.find("s_a", pos)) != string::npos) {
        line.replace(pos, 3, replacement);
        pos += replacement.length();
      }
      lines.push_back(line);
    }
  }

  // Log current timestamp and return value
  lines.push_back(logTimeAndRv(rv, SAI_NULL_OBJECT_ID));

  // Make the bulk create call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(switch_id),
          ",",
          object_count,
          ",attr_count_list,(const sai_attribute_t**)attr_list_ptrs,(sai_bulk_op_error_mode_t)",
          mode,
          ",obj_list,object_statuses)"));

  // Log object IDs and statuses for each object
  for (uint32_t i = 0; i < object_count; ++i) {
    std::ostringstream outStringStream;
    outStringStream << "// object_id[" << i << "]: " << object_id[i] << " (0x"
                    << std::hex << object_id[i] << ") status: " << std::dec
                    << object_statuses[i];
    lines.push_back(outStringStream.str());
  }

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logBulkRemoveFn(
    const std::string& fn_name,
    uint32_t object_count,
    const sai_object_id_t* object_id,
    sai_bulk_op_error_mode_t mode,
    sai_status_t* object_statuses,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  // Pre-allocate capacity to avoid repeated reallocations in loops
  // Estimate: object_count obj_list + 1 timestamp + 1 bulk call +
  // object_count statuses + 1 rv check
  lines.reserve(3 + 2 * object_count);

  // Setup object ids in obj_list
  for (uint32_t i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>("obj_list[", i, "]=", getVariable(object_id[i])));
  }

  // Log current timestamp and return value
  lines.push_back(logTimeAndRv(rv, SAI_NULL_OBJECT_ID));

  // Make the bulk remove call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          object_count,
          ",obj_list,(sai_bulk_op_error_mode_t)",
          mode,
          ",object_statuses)"));

  // Log object IDs and statuses for each object
  for (uint32_t i = 0; i < object_count; ++i) {
    std::ostringstream outStringStream;
    outStringStream << "// object_id[" << i << "]: " << object_id[i] << " (0x"
                    << std::hex << object_id[i] << ") status: " << std::dec
                    << object_statuses[i];
    lines.push_back(outStringStream.str());
  }

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logRouteEntryRemoveFn(const sai_route_entry_t* route_entry) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  setRouteEntry(route_entry, lines);

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_ROUTE_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "remove_route_entry(&r_e)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logNeighborEntryRemoveFn(
    const sai_neighbor_entry_t* neighbor_entry,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  setNeighborEntry(neighbor_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_NEIGHBOR_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "remove_neighbor_entry(&n_e)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logFdbEntryRemoveFn(
    const sai_fdb_entry_t* fdb_entry,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  setFdbEntry(fdb_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_FDB_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "remove_fdb_entry(&f_e)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logInsegEntryRemoveFn(
    const sai_inseg_entry_t* inseg_entry,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  setInsegEntry(inseg_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_INSEG_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "remove_inseg_entry(&i_e)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
void SaiTracer::logMySidEntryRemoveFn(
    const sai_my_sid_entry_t* my_sid_entry,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};
  setMySidEntry(my_sid_entry, lines);

  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_MY_SID_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "remove_my_sid_entry(&ms_e)"));

  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}
#endif

void SaiTracer::logRemoveFn(
    const string& fn_name,
    sai_object_id_t remove_object_id,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines{};

  // Make the remove call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(remove_object_id),
          ")"));

  writeToFile(lines, /*linefeed*/ false);

  // Remove object from variables_
  if (FLAGS_log_variable_name) {
    variables_.erase(remove_object_id);
  }
}

void SaiTracer::logRouteEntrySetAttrFn(
    const sai_route_entry_t* route_entry,
    const sai_attribute_t* attr) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup one attribute
  vector<string> lines = setAttrList(attr, 1, SAI_OBJECT_TYPE_ROUTE_ENTRY);

  setRouteEntry(route_entry, lines);

  // Make setAttribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_ROUTE_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "set_route_entry_attribute(&r_e, s_a)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logNeighborEntrySetAttrFn(
    const sai_neighbor_entry_t* neighbor_entry,
    const sai_attribute_t* attr,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup one attribute
  vector<string> lines = setAttrList(attr, 1, SAI_OBJECT_TYPE_NEIGHBOR_ENTRY);

  setNeighborEntry(neighbor_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make setAttribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_NEIGHBOR_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "set_neighbor_entry_attribute(&n_e,s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logFdbEntrySetAttrFn(
    const sai_fdb_entry_t* fdb_entry,
    const sai_attribute_t* attr,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup one attribute
  vector<string> lines = setAttrList(attr, 1, SAI_OBJECT_TYPE_FDB_ENTRY);

  setFdbEntry(fdb_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make setAttribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_FDB_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "set_fdb_entry_attribute(&f_e,s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logInsegEntrySetAttrFn(
    const sai_inseg_entry_t* inseg_entry,
    const sai_attribute_t* attr,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup one attribute
  vector<string> lines = setAttrList(attr, 1, SAI_OBJECT_TYPE_INSEG_ENTRY);

  setInsegEntry(inseg_entry, lines);

  // Log timestamp and return value
  lines.push_back(logTimeAndRv(rv));

  // Make setAttribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_INSEG_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "set_inseg_entry_attribute(&i_e,s_a)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
void SaiTracer::logMySidEntrySetAttrFn(
    const sai_my_sid_entry_t* my_sid_entry,
    const sai_attribute_t* attr,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  vector<string> lines = setAttrList(attr, 1, SAI_OBJECT_TYPE_MY_SID_ENTRY);

  setMySidEntry(my_sid_entry, lines);

  lines.push_back(logTimeAndRv(rv));

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_MY_SID_ENTRY,
              "Unsupported Sai Object type in Sai Tracer"),
          "set_my_sid_entry_attribute(&ms_e,s_a)"));

  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}
#endif

// Prior to GET calls, log the attributes used for GET.
void SaiTracer::logAttrPreGet(
    uint32_t attr_count,
    const sai_attribute_t* attr,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_get_attr_log) {
    return;
  }

  writeToFile(std::move(setAttrList(attr, attr_count, object_type, 0)), false);
}

void SaiTracer::logGetAttrFn(
    const string& fn_name,
    sai_object_id_t get_object_id,
    uint32_t attr_count,
    const sai_attribute_t* attr,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_get_attr_log) {
    return;
  }

  // Make getAttribute call and copy results to get_attribute
  vector<string> lines{
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(get_object_id),
          ",",
          attr_count,
          ",s_a)"),
      to<string>("memcpy(get_attribute, s_a, ATTR_SIZE*", maxAttrCount_, ")")};

  // Log the values retrieved at runtime
  auto runtimeAttr = setAttrList(attr, attr_count, object_type, rv);
  lines.insert(lines.end(), runtimeAttr.begin(), runtimeAttr.end());

  // Compare values
  lines.push_back(
      to<string>(
          "attrCheck(get_attribute, s_a, ",
          maxAttrCount_,
          ", ",
          numCalls_,
          ")"));
  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logBulkGetAttrFn(
    const std::string& fn_name,
    uint32_t object_count,
    const sai_object_id_t* object_id,
    const uint32_t* attr_count,
    sai_attribute_t** attr_list,
    sai_bulk_op_error_mode_t mode,
    sai_status_t* object_statuses,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_get_attr_log) {
    return;
  }

  vector<string> lines;

  // Reserve minimum predictable size: 4*object_count (for the 4 main loops)
  lines.reserve(object_count * 4);
  for (int i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>("obj_list[", i, "]=", getVariable(object_id[i])));
  }

  for (int i = 0; i < object_count; ++i) {
    lines.push_back(to<string>("attr_count_list[", i, "]=", attr_count[i]));
  }

  // Setup attr_list_ptrs and prepare attributes for bulk GET (rv=0 for pre-GET
  // setup)
  auto attrSetupLines =
      setBulkAttrList(object_count, attr_count, attr_list, object_type, 0);
  lines.insert(lines.end(), attrSetupLines.begin(), attrSetupLines.end());

  // Log current timestamp and return value
  lines.push_back(logTimeAndRv(rv, SAI_NULL_OBJECT_ID));

  // Make get bulk attribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          object_count,
          ",obj_list,attr_count_list,attr_list_ptrs,(sai_bulk_op_error_mode_t)",
          mode,
          ",object_statuses)"));

  // Copy results for validation
  lines.push_back(
      to<string>("memcpy(get_attribute, s_a, ATTR_SIZE*", maxAttrCount_, ")"));

  lines.push_back(
      to<string>(
          "memset(get_attr_list_ptrs,0,sizeof(sai_attribute_t*)*",
          FLAGS_default_list_size,
          ")"));

  // Point get_attr_list_ptrs to preserved attribute copy
  int globalAttrIndex = 0;
  for (int i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>(
            "get_attr_list_ptrs[",
            i,
            "]=&get_attribute[",
            globalAttrIndex,
            "]"));
    globalAttrIndex += attr_count[i];
  }

  // Setup attr_list_ptrs with runtime values for comparison (rv=actual return
  // value)
  vector<string> runtimeAttr =
      setBulkAttrList(object_count, attr_count, attr_list, object_type, rv);
  lines.insert(lines.end(), runtimeAttr.begin(), runtimeAttr.end());

  // Log object status
  string objectStatusStr = "// Status:";
  for (int i = 0; i < object_count; ++i) {
    objectStatusStr += to<string>(" ", object_statuses[i]);
  }
  lines.push_back(objectStatusStr);

  // Compare values for each object separately using the structured pointers
  for (int i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>(
            "attrCheck((sai_attribute_t*)get_attr_list_ptrs[",
            i,
            "], (sai_attribute_t*)attr_list_ptrs[",
            i,
            "], attr_count_list[",
            i,
            "], ",
            numCalls_,
            ")"));
  }

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logSetAttrFn(
    const string& fn_name,
    sai_object_id_t set_object_id,
    const sai_attribute_t* attr,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup one attribute
  vector<string> lines = setAttrList(attr, 1, object_type);

  // Make setAttribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(set_object_id),
          ",s_a)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logCommentedAttrFn(
    const string& fn_name,
    sai_object_id_t set_object_id,
    const sai_attribute_t* attr,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Log as a comment to avoid executing problematic attributes
  vector<string> lines = {to<string>("// Commented out attribute ", attr->id)};

  // Setup one attribute (but commented out)
  vector<string> attrLines = setAttrList(attr, 1, object_type);
  for (auto& line : attrLines) {
    lines.push_back(to<string>("// ", line));
  }

  // Comment out the actual API call
  lines.push_back(
      to<string>(
          "// rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(set_object_id),
          ",s_a)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logBulkSetAttrFn(
    const std::string& fn_name,
    uint32_t object_count,
    const sai_object_id_t* object_id,
    const sai_attribute_t* attr_list,
    sai_bulk_op_error_mode_t mode,
    sai_status_t* object_statuses,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  // Setup attributes
  vector<string> lines = setAttrList(attr_list, object_count, object_type);

  // Setup object ids
  for (int i = 0; i < object_count; ++i) {
    lines.push_back(
        to<string>("obj_list[", i, "]=", getVariable(object_id[i])));
  }

  // Log current timestamp, object id and return value
  lines.push_back(logTimeAndRv(rv, SAI_NULL_OBJECT_ID));

  // Make set bulk attribute call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          object_count,
          ",obj_list,s_a,(sai_bulk_op_error_mode_t)",
          mode,
          ",object_statuses)"));

  // Log object status
  string objectStatusStr = "// Status:";
  for (int i = 0; i < object_count; ++i) {
    objectStatusStr += to<string>(" ", object_statuses[i]);
  }
  lines.push_back(objectStatusStr);

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logSendHostifPacketFn(
    sai_object_id_t hostif_id,
    sai_size_t buffer_size,
    const uint8_t* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_packet_log) {
    return;
  }

  vector<string> lines =
      setAttrList(attr_list, attr_count, SAI_OBJECT_TYPE_HOSTIF_PACKET);

  // packet_buffer is declared in local space for each send packet call
  // Therefore, we open the bracket here and then declare the buffer
  lines.emplace_back("{");

  std::ostringstream outStringStream;
  outStringStream << "uint8_t packet_buffer[" << buffer_size << "] = {";
  for (int i = 0; i < buffer_size - 1; i += 32) {
    outStringStream << "\n";
    uint64_t j_end = i + 32;

    for (int j = i; j < std::min(j_end, buffer_size - 1); ++j) {
      printHex(outStringStream, buffer[j]);
      outStringStream << ", ";
    }
  }
  printHex(outStringStream, buffer[buffer_size - 1]);
  outStringStream << "}";
  lines.push_back(outStringStream.str());

  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              SAI_OBJECT_TYPE_HOSTIF,
              "Unsupported Sai Object type in Sai Tracer"),
          "send_hostif_packet(",
          getVariable(hostif_id),
          ",",
          buffer_size,
          ",packet_buffer, ",
          attr_count,
          ",s_a)"));

  // Close bracket for local scope
  lines.emplace_back("}");

  lines.push_back(rvCheck(rv));
  writeToFile(lines);
}

void SaiTracer::logGetStatsFn(
    const std::string& fn_name,
    sai_object_id_t object_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    const uint64_t* counters,
    sai_object_type_t object_type,
    sai_status_t rv,
    int mode) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_get_attr_log) {
    return;
  }
  vector<string> lines = {
      to<string>("memset(counter_list,0,4*", maxAttrCount_, ")"),
      to<string>("memset(counter_vals,0,8*", maxAttrCount_, ")")};
  for (int i = 0; i < number_of_counters; ++i) {
    lines.push_back(to<string>("// Value:", counters[i]));
    lines.push_back(to<string>("counter_list[", i, "]=", counter_ids[i]));
  }

  // Make clearStats call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(object_id),
          ",",
          number_of_counters,
          ",(const sai_stat_id_t*)&counter_list,",
          mode ? to<string>("(sai_stats_mode_t)", mode, ",") : "",
          "(uint64_t*)&counter_vals)"));

  writeToFile(lines, /*linefeed*/ false);
}

void SaiTracer::logClearStatsFn(
    const std::string& fn_name,
    sai_object_id_t object_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer || !FLAGS_enable_get_attr_log) {
    return;
  }

  vector<string> lines = {
      to<string>("memset(counter_list,0,4*", maxAttrCount_, ")")};
  for (int i = 0; i < number_of_counters; ++i) {
    lines.push_back(to<string>("counter_list[", i, "]=", counter_ids[i]));
  }

  // Log current timestamp, object id and return value
  lines.push_back(logTimeAndRv(rv, object_id));

  // Make clearStats call
  lines.push_back(
      to<string>(
          "rv=",
          folly::get_or_throw(
              fnPrefix_,
              object_type,
              "Unsupported Sai Object type in Sai Tracer"),
          fn_name,
          "(",
          getVariable(object_id),
          ",",
          number_of_counters,
          ",&counter_list)"));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::logShellCommand(const std::string& command) {
  if (!FLAGS_enable_replayer) {
    return;
  }

  if (!asyncLogger_) {
    XLOG(ERR) << "SaiTracer: AsyncLogger is null!";
    return;
  }

  try {
    // Log shell command as a C comment with a timestamp
    std::string timestampLine = "\n" + logTimeAndRv(0);
    std::string commentLine = "\n// Shell command: " + command;
    asyncLogger_->appendLog(timestampLine.c_str(), timestampLine.size());
    asyncLogger_->appendLog(commentLine.c_str(), commentLine.size());
  } catch (const std::exception& e) {
    XLOG(ERR) << "SaiTracer: ERROR writing to asyncLogger: " << e.what();
  }
}

std::tuple<string, string> SaiTracer::declareVariable(
    sai_object_id_t* object_id,
    sai_object_type_t object_type) {
  if (!FLAGS_enable_replayer) {
    return std::make_tuple("", "");
  }

  auto constexpr varType = "sai_object_id_t ";

  // Get the prefix name for pbject type
  string varPrefix = folly::get_or_throw(
      varNames_, object_type, "Unsupported Sai Object type in Sai Tracer");

  // Get the variable count for declaration (e.g. aclTable_1)
  uint num = folly::get_or_throw(
      varCounts_, object_type, "Unsupported Sai Object type in Sai Tracer")++;
  string varName = to<string>(varPrefix, num);

  return std::make_tuple(
      to<string>("[[maybe_unused]] ", varType, varName), varName);
}

string SaiTracer::getVariable(sai_object_id_t object_id) {
  if (!FLAGS_enable_replayer) {
    return "";
  }
  if (object_id == SAI_NULL_OBJECT_ID) {
    return "SAI_NULL_OBJECT_ID";
  }
  if (!FLAGS_log_variable_name) {
    return to<string>(object_id, "U");
  }
  auto& varName = variables_[object_id];
  return varName.empty() ? to<string>(object_id, "U") : varName;
}

vector<string> SaiTracer::setAttrList(
    const sai_attribute_t* attr_list,
    uint32_t attr_count,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return {};
  }

  checkAttrCount(attr_count);

  auto constexpr sai_attribute = "s_a";
  vector<string> attrLines;

  attrLines.push_back(
      to<string>("memset(s_a,0,ATTR_SIZE*", maxAttrCount_, ")"));

  // Setup ids
  for (int i = 0; i < attr_count; ++i) {
    attrLines.push_back(
        to<string>(sai_attribute, "[", i, "].id=", attr_list[i].id));
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if (UNLIKELY(object_type >= SAI_OBJECT_TYPE_MAX)) {
    switch (static_cast<sai_object_type_extensions_t>(object_type)) {
      case SAI_OBJECT_TYPE_TAM_EVENT_AGING_GROUP:
        setTamEventAgingGroupAttributes(attr_list, attr_count, attrLines, rv);
        break;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      case SAI_OBJECT_TYPE_VENDOR_SWITCH:
        setVendorSwitchAttributes(attr_list, attr_count, attrLines, rv);
        break;
      case SAI_OBJECT_TYPE_SWITCH_PIPELINE:
        setSwitchPipelineAttributes(attr_list, attr_count, attrLines, rv);
        break;
#endif
      case SAI_OBJECT_TYPE_FIRMWARE:
        setFirmwareAttributes(attr_list, attr_count, attrLines, rv);
        break;
      default:
        break;
    }
    return attrLines;
  }
#endif

  // Call functions defined in *ApiTracer.h to serialize attributes
  // that are specific to each Sai object type
  switch (object_type) {
    case SAI_OBJECT_TYPE_ACL_COUNTER:
      setAclCounterAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ACL_RANGE:
      setAclRangeAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ACL_ENTRY:
      setAclEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ACL_TABLE:
      setAclTableAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP:
      setAclTableGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER:
      setAclTableGroupMemberAttributes(attr_list, attr_count, attrLines, rv);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    case SAI_OBJECT_TYPE_ARS:
      setArsAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ARS_PROFILE:
      setArsProfileAttributes(attr_list, attr_count, attrLines, rv);
      break;
#endif
    case SAI_OBJECT_TYPE_BRIDGE:
      setBridgeAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_BRIDGE_PORT:
      setBridgePortAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_BUFFER_POOL:
      setBufferPoolAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_BUFFER_PROFILE:
      setBufferProfileAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_COUNTER:
      setCounterAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_DEBUG_COUNTER:
      setDebugCounterAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_FDB_ENTRY:
      setFdbEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_HASH:
      setHashAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_HOSTIF_PACKET:
      setHostifPacketAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_HOSTIF_TRAP:
      setHostifTrapAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_HOSTIF_USER_DEFINED_TRAP:
      setHostifUserDefinedTrapAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP:
      setHostifTrapGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_INSEG_ENTRY:
      setInsegEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP:
      setIngressPriorityGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_LAG:
      setLagAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_LAG_MEMBER:
      setLagMemberAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MACSEC:
      setMacsecAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MACSEC_PORT:
      setMacsecPortAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MACSEC_FLOW:
      setMacsecFlowAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MACSEC_SA:
      setMacsecSAAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MACSEC_SC:
      setMacsecSCAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MIRROR_SESSION:
      setMirrorSessionAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_NEIGHBOR_ENTRY:
      setNeighborEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP:
      setNextHopAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP:
      setNextHopGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER:
      setNextHopGroupMemberAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_PORT:
      setPortAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_PORT_SERDES:
      setPortSerdesAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_PORT_CONNECTOR:
      setPortConnectorAttributes(attr_list, attr_count, attrLines, rv);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    case SAI_OBJECT_TYPE_PORT_LLR_PROFILE:
      setPortLlrProfileAttributes(attr_list, attr_count, attrLines, rv);
      break;
#endif
    case SAI_OBJECT_TYPE_QOS_MAP:
      setQosMapAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_QUEUE:
      setQueueAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ROUTE_ENTRY:
      setRouteEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ROUTER_INTERFACE:
      setRouterInterfaceAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_SAMPLEPACKET:
      setSamplePacketAttributes(attr_list, attr_count, attrLines, rv);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    case SAI_OBJECT_TYPE_MY_MAC:
      setMyMacAttributes(attr_list, attr_count, attrLines, rv);
      break;
#endif
    case SAI_OBJECT_TYPE_SCHEDULER:
      setSchedulerAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_SWITCH:
      setSwitchAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_SYSTEM_PORT:
      setSystemPortAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM:
      setTamAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT:
      setTamEventAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD:
      setTamEventThresholdAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT_ACTION:
      setTamEventActionAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_REPORT:
      setTamReportAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_TRANSPORT:
      setTamTransportAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TAM_COLLECTOR:
      setTamCollectorAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TUNNEL:
      setTunnelAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY:
      setTunnelTermAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TUNNEL_MAP:
      setTunnelMapAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY:
      setTunnelMapEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_OBJECT_TYPE_SRV6_SIDLIST:
      setSrv6SidListAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_MY_SID_ENTRY:
      setMySidEntryAttributes(attr_list, attr_count, attrLines, rv);
      break;
#endif
    case SAI_OBJECT_TYPE_UDF:
      setUdfAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_UDF_MATCH:
      setUdfMatchAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_UDF_GROUP:
      setUdfGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
#if defined(SAI_CBFC_SUPPORTED)
    case SAI_OBJECT_TYPE_VIRTUAL_CHANNEL:
      setVirtualChannelAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE:
      setCbfcCreditProfileAttributes(attr_list, attr_count, attrLines, rv);
      break;
#endif
    case SAI_OBJECT_TYPE_VIRTUAL_ROUTER:
      setVirtualRouterAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_VLAN:
      setVlanAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_VLAN_MEMBER:
      setVlanMemberAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ISOLATION_GROUP:
      setIsolationGroupAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER:
      setIsolationGroupMemberAttributes(attr_list, attr_count, attrLines, rv);
      break;
    case SAI_OBJECT_TYPE_WRED:
      setWredAttributes(attr_list, attr_count, attrLines, rv);
      break;
    default:
      // TODO: For other APIs, create new API wrappers and invoke
      // setAttributes() function here
      break;
  }

  return attrLines;
}

vector<string> SaiTracer::setBulkAttrList(
    uint32_t object_count,
    const uint32_t* attr_count,
    sai_attribute_t** attr_list,
    sai_object_type_t object_type,
    sai_status_t rv) {
  if (!FLAGS_enable_replayer) {
    return {};
  }

  // Calculate total attributes needed for all objects
  uint32_t totalAttrs = 0;
  for (int i = 0; i < object_count; ++i) {
    totalAttrs += attr_count[i];
  }
  checkAttrCount(totalAttrs);

  vector<string> attrLines;
  attrLines.push_back(
      to<string>("memset(s_a,0,ATTR_SIZE*", maxAttrCount_, ")"));
  attrLines.push_back(
      to<string>(
          "memset(attr_list_ptrs,0,sizeof(sai_attribute_t*)*",
          FLAGS_default_list_size,
          ")"));

  // Set up attr_list_ptrs array to point to the correct positions in s_a
  int attributeIndexInSa = 0;
  for (int i = 0; i < object_count; ++i) {
    attrLines.push_back(
        to<string>("attr_list_ptrs[", i, "]=&s_a[", attributeIndexInSa, "]"));
    attributeIndexInSa += attr_count[i];
  }

  // Process each object
  for (int i = 0; i < object_count; ++i) {
    attrLines.push_back(to<string>("// Object ", i, " attributes:"));

    // Get attribute values for this object using setAttrList
    auto objAttrLines =
        setAttrList(attr_list[i], attr_count[i], object_type, rv);

    // Skip the first line (memset) and replace s_a with attr_list_ptrs[i]
    // Example transformation for object i=1:
    //   setAttrList() returns:
    //                "s_a[0].id=166";
    //                "s_a[0].value.u32=100";
    //   After replacement:
    //                "attr_list_ptrs[1][0].id=166",
    //                "attr_list_ptrs[1][0].value.u32=100"
    for (size_t lineIdx = 1; lineIdx < objAttrLines.size(); ++lineIdx) {
      string line = objAttrLines[lineIdx];
      size_t pos = line.find("s_a");
      if (pos != string::npos) {
        line.replace(pos, 3, to<string>("attr_list_ptrs[", i, "]"));
      }
      attrLines.push_back(line);
    }
  }

  return attrLines;
}

string SaiTracer::createFnCall(
    const string& fn_name,
    const string& var1,
    const string& var2,
    uint32_t attr_count,
    sai_object_type_t object_type) {
  // This helper method produces the create call. For example -
  // bridge_api->create_bridge_port(&bridgePort_1, switch_0, 4, s_a);
  return to<string>(
      "rv=",
      folly::get_or_throw(
          fnPrefix_, object_type, "Unsupported Sai Object type in Sai Tracer"),
      fn_name,
      "(&",
      var1,
      ",",
      var2,
      ",",
      attr_count,
      ",s_a)");
}

void SaiTracer::setFdbEntry(
    const sai_fdb_entry_t* fdb_entry,
    std::vector<std::string>& lines) {
  lines.push_back(
      to<string>("f_e.switch_id=", getVariable(fdb_entry->switch_id)));
  lines.push_back(to<string>("f_e.bv_id=", getVariable(fdb_entry->bv_id)));

  // The underlying type of sai_mac_t is uint8_t[6]
  lines.emplace_back("mac=f_e.mac_address");
  for (int i = 0; i < 6; ++i) {
    lines.push_back(to<string>("mac[", i, "]=", fdb_entry->mac_address[i]));
  }
}

void SaiTracer::setInsegEntry(
    const sai_inseg_entry_t* inseg_entry,
    std::vector<std::string>& lines) {
  lines.push_back(
      to<string>("i_e.switch_id=", getVariable(inseg_entry->switch_id)));
  lines.push_back(to<string>("i_e.label=", inseg_entry->label));
}

void SaiTracer::setNeighborEntry(
    const sai_neighbor_entry_t* neighbor_entry,
    vector<string>& lines) {
  lines.push_back(
      to<string>("n_e.switch_id=", getVariable(neighbor_entry->switch_id)));
  lines.push_back(
      to<string>("n_e.rif_id=", getVariable(neighbor_entry->rif_id)));

  folly::IPAddress ipAddress =
      facebook::fboss::fromSaiIpAddress(neighbor_entry->ip_address);
  lines.push_back(to<string>("// ", ipAddress.str()));

  if (neighbor_entry->ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV4) {
    lines.emplace_back("n_e.ip_address.addr_family=SAI_IP_ADDR_FAMILY_IPV4");
    lines.push_back(
        to<string>(
            "n_e.ip_address.addr.ip4=", neighbor_entry->ip_address.addr.ip4));

  } else if (
      neighbor_entry->ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV6) {
    lines.emplace_back("n_e.ip_address.addr_family=SAI_IP_ADDR_FAMILY_IPV6");

    // Underlying type of sai_ip6_t is uint8_t[16]
    lines.emplace_back("u=n_e.ip_address.addr.ip6");
    lines.emplace_back("memset(u,0,16)");
    std::ostringstream addrOutStringStream;
    for (int i = 0; i < 16; ++i) {
      if (neighbor_entry->ip_address.addr.ip6[i] != 0) {
        addrOutStringStream
            << to<string>("u[", i, "]=", neighbor_entry->ip_address.addr.ip6[i])
            << ";";
      }
    }
    lines.push_back(addrOutStringStream.str());
  }
}

void SaiTracer::setRouteEntry(
    const sai_route_entry_t* route_entry,
    vector<string>& lines) {
  lines.push_back(
      to<string>("r_e.switch_id=", getVariable(route_entry->switch_id)));
  lines.push_back(to<string>("r_e.vr_id=", getVariable(route_entry->vr_id)));

  auto [ipAddr, mask] =
      facebook::fboss::fromSaiIpPrefix(route_entry->destination);
  lines.push_back(to<string>("// ", ipAddr.str(), "/", mask));

  if (route_entry->destination.addr_family == SAI_IP_ADDR_FAMILY_IPV4) {
    lines.emplace_back("r_e.destination.addr_family=SAI_IP_ADDR_FAMILY_IPV4");
    lines.push_back(
        to<string>(
            "r_e.destination.addr.ip4=", route_entry->destination.addr.ip4));
    lines.push_back(
        to<string>(
            "r_e.destination.mask.ip4=", route_entry->destination.mask.ip4));
  } else if (route_entry->destination.addr_family == SAI_IP_ADDR_FAMILY_IPV6) {
    lines.emplace_back("r_e.destination.addr_family=SAI_IP_ADDR_FAMILY_IPV6");

    // Underlying type of sai_ip6_t is uint8_t[16]
    lines.emplace_back("u=r_e.destination.addr.ip6");
    lines.emplace_back("memset(u,0,16)");
    std::ostringstream addrOutStringStream;
    for (int i = 0; i < 16; ++i) {
      if (route_entry->destination.addr.ip6[i] != 0) {
        addrOutStringStream
            << to<string>("u[", i, "]=", route_entry->destination.addr.ip6[i])
            << ";";
      }
    }
    lines.push_back(addrOutStringStream.str());

    lines.emplace_back("u=r_e.destination.mask.ip6");
    lines.emplace_back("memset(u,0,16)");
    std::ostringstream maskOutStringStream;
    for (int i = 0; i < 16; ++i) {
      if (route_entry->destination.mask.ip6[i] != 0) {
        maskOutStringStream
            << to<string>("u[", i, "]=", route_entry->destination.mask.ip6[i])
            << ";";
      }
    }
    lines.push_back(maskOutStringStream.str());
  }
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
void SaiTracer::setMySidEntry(
    const sai_my_sid_entry_t* my_sid_entry,
    vector<string>& lines) {
  lines.push_back(
      to<string>("ms_e.switch_id=", getVariable(my_sid_entry->switch_id)));
  lines.push_back(to<string>("ms_e.vr_id=", getVariable(my_sid_entry->vr_id)));
  lines.push_back(
      to<string>("ms_e.locator_block_len=", my_sid_entry->locator_block_len));
  lines.push_back(
      to<string>("ms_e.locator_node_len=", my_sid_entry->locator_node_len));
  lines.push_back(to<string>("ms_e.function_len=", my_sid_entry->function_len));
  lines.push_back(to<string>("ms_e.args_len=", my_sid_entry->args_len));

  folly::IPAddressV6 sidAddr =
      facebook::fboss::fromSaiIpAddress(my_sid_entry->sid);
  lines.push_back(to<string>("// ", sidAddr.str()));

  // SID is sai_ip6_t (uint8_t[16])
  lines.emplace_back("u=ms_e.sid");
  lines.emplace_back("memset(u,0,16)");
  std::ostringstream sidOutStringStream;
  for (int i = 0; i < 16; ++i) {
    if (my_sid_entry->sid[i] != 0) {
      sidOutStringStream << to<string>("u[", i, "]=", my_sid_entry->sid[i])
                         << ";";
    }
  }
  lines.push_back(sidOutStringStream.str());
}
#endif

string SaiTracer::rvCheck(sai_status_t rv) {
  return to<string>("rvCheck(rv,", rv, ",", numCalls_++, ")");
}

string SaiTracer::logTimeAndRv(
    sai_status_t rv,
    sai_object_id_t object_id,
    std::chrono::system_clock::time_point begin) {
  auto now = std::chrono::system_clock::now();
  auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    now.time_since_epoch()) %
      1000000;
  auto timer = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  localtime_r(&timer, &tm);

  std::ostringstream outStringStream;
  outStringStream << "// " << std::put_time(&tm, "%Y-%m-%d %T");
  outStringStream << "." << std::setfill('0') << std::setw(6) << now_us.count();

  // Log object id if it's provided
  if (object_id != SAI_NULL_OBJECT_ID) {
    outStringStream << " object_id: " << object_id << " (0x";
    outStringStream << std::hex << object_id;
    outStringStream << ")";
  }

  outStringStream << " rv: " << rv;

  if (begin != std::chrono::system_clock::time_point::min()) {
    outStringStream << " elapsed time " << std::dec
                    << std::chrono::duration_cast<std::chrono::microseconds>(
                           now - begin)
                           .count()
                    << " μs";
  }

  return outStringStream.str();
}

void SaiTracer::checkAttrCount(uint32_t attr_count) {
  // If any object has more than the current sai_attribute list has
  // (FLAGS_default_list_size by default), s_a will be reallocated to have
  // enough space for all attributes
  if (attr_count > maxAttrCount_) {
    maxAttrCount_ = attr_count;
    writeToFile({to<string>(
        "s_a = (sai_attribute_t*)realloc(s_a, ATTR_SIZE * ",
        maxAttrCount_,
        ")")});
  }
}

uint32_t SaiTracer::checkListCount(
    uint32_t list_count,
    uint32_t elem_size,
    uint32_t elem_count) {
  // If any object uses more than the current number of lists (6 by default),
  // sai replayer will initialize more lists on stack
  if (list_count > maxListCount_) {
    writeToFile({to<string>(
        "[[maybe_unused]] int list_",
        maxListCount_,
        "[",
        FLAGS_default_list_size,
        "]")});
    maxListCount_ = list_count;
  }

  // TODO(zecheng): Handle list size that's larger than
  // FLAGS_default_list_size * 4 bytes.
  if (elem_size * elem_count > FLAGS_default_list_size * sizeof(int)) {
    writeToFile({to<string>(
        "printf(\"[ERROR] The replayed program is using ",
        elem_size * elem_count,
        " bytes, which is more than ",
        FLAGS_default_list_size * sizeof(int),
        " bytes in a list attribute. Excess bytes will not be included.\")")});
  }

  // Return the maximum number of elements can be written into the list
  return FLAGS_default_list_size * sizeof(int) / elem_size;
}

void SaiTracer::logPostInvocation(
    sai_status_t rv,
    sai_object_id_t object_id,
    std::chrono::system_clock::time_point begin,
    std::optional<std::string> varName) {
  // In the case of create fn, objectID is known after invocation.
  // Therefore, add it to the variable mapping here.
  if (varName && FLAGS_log_variable_name) {
    variables_.emplace(object_id, *varName);
  }

  vector<string> lines;
  // Log current timestamp, object id and return value
  lines.push_back(logTimeAndRv(rv, object_id, begin));

  // Check return value to be the same as the original run
  lines.push_back(rvCheck(rv));

  writeToFile(lines);
}

void SaiTracer::setupGlobals() {
  // TODO(zecheng): Handle list size that's larger than 512 bytes.
  vector<string> globalVar = {to<string>(
      to<string>(kGlobalVarStart),
      "\n",
      "[[maybe_unused]] sai_attribute_t *s_a=(sai_attribute_t*)malloc(ATTR_SIZE * ",
      FLAGS_default_list_size,
      ")")};

  if (FLAGS_enable_get_attr_log) {
    globalVar.push_back(
        to<string>(
            "[[maybe_unused]] sai_attribute_t *get_attribute=(sai_attribute_t*)malloc(ATTR_SIZE * ",
            FLAGS_default_list_size,
            ")"));
    // For bulk operations - preserve the array structure
    globalVar.push_back(
        to<string>(
            "[[maybe_unused]] sai_attribute_t* get_attr_list_ptrs[",
            FLAGS_default_list_size,
            "]"));
  }

  for (int i = 0; i < FLAGS_default_list_count; i++) {
    globalVar.push_back(
        to<string>(
            "[[maybe_unused]] int list_",
            i,
            "[",
            FLAGS_default_list_size,
            "]"));
  }

  globalVar.emplace_back("[[maybe_unused]] uint8_t* mac");
  globalVar.emplace_back("[[maybe_unused]] uint8_t* u");
  globalVar.emplace_back("[[maybe_unused]] sai_status_t rv");
  globalVar.emplace_back("[[maybe_unused]] sai_route_entry_t r_e");
  globalVar.emplace_back("[[maybe_unused]] sai_neighbor_entry_t n_e");
  globalVar.emplace_back("[[maybe_unused]] sai_fdb_entry_t f_e");
  globalVar.emplace_back("[[maybe_unused]] sai_inseg_entry_t i_e");
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  globalVar.emplace_back("[[maybe_unused]] sai_my_sid_entry_t ms_e");
#endif
  globalVar.emplace_back("[[maybe_unused]] uint32_t expected_object_count");
  globalVar.emplace_back("[[maybe_unused]] uint32_t object_count");
  globalVar.emplace_back(
      "[[maybe_unused]] std::vector<sai_object_key_t> object_list");
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] sai_status_t object_statuses[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] sai_object_id_t obj_list[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] sai_stat_id_t counter_list[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] uint64_t counter_vals[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] uint32_t attr_count_list[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(
      to<string>(
          "[[maybe_unused]] sai_attribute_t* attr_list_ptrs[",
          FLAGS_default_list_size,
          "]"));
  globalVar.push_back(to<string>(kGlobalVarEnd));
  writeToFile(globalVar);

  maxAttrCount_ = FLAGS_default_list_size;
  maxListCount_ = FLAGS_default_list_count;
  numCalls_ = 0;
}

void SaiTracer::writeFooter() {
  string footer = "free(s_a);\n}\n} // namespace facebook::fboss\n";
  if (FLAGS_enable_get_attr_log) {
    footer = "free(get_attribute);\n" + footer;
  }
  asyncLogger_->appendLog(footer.c_str(), footer.size());
}

void SaiTracer::initVarCounts() {
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_COUNTER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_RANGE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_ENTRY, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_TABLE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_TABLE_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER, 0);
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  varCounts_.emplace(SAI_OBJECT_TYPE_ARS, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ARS_PROFILE, 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_BRIDGE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_BRIDGE_PORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_BUFFER_POOL, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_BUFFER_PROFILE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_COUNTER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_DEBUG_COUNTER, 0);
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  varCounts_.emplace(
      static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_FIRMWARE), 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_HASH, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_HOSTIF, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_HOSTIF_TRAP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_HOSTIF_USER_DEFINED_TRAP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_LAG, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_LAG_MEMBER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MACSEC, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MACSEC_PORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MACSEC_FLOW, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MACSEC_SA, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MACSEC_SC, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MIRROR_SESSION, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_NEXT_HOP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_POLICER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_PORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_PORT_SERDES, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_PORT_CONNECTOR, 0);
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  varCounts_.emplace(SAI_OBJECT_TYPE_PORT_LLR_PROFILE, 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_QOS_MAP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_QUEUE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ROUTER_INTERFACE, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_SAMPLEPACKET, 0);
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  varCounts_.emplace(SAI_OBJECT_TYPE_MY_MAC, 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_SCHEDULER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_SCHEDULER_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_SWITCH, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_SYSTEM_PORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_COLLECTOR, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_TRANSPORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_REPORT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_EVENT_ACTION, 0);
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  varCounts_.emplace(
      static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_TAM_EVENT_AGING_GROUP), 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_EVENT, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TAM, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TUNNEL, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TUNNEL_MAP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, 0);
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  varCounts_.emplace(SAI_OBJECT_TYPE_SRV6_SIDLIST, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_MY_SID_ENTRY, 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_UDF, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_UDF_MATCH, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_UDF_GROUP, 0);
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  varCounts_.emplace(
      static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_VENDOR_SWITCH), 0);
  varCounts_.emplace(
      static_cast<sai_object_type_t>(SAI_OBJECT_TYPE_SWITCH_PIPELINE), 0);
#endif
#if defined(SAI_CBFC_SUPPORTED)
  varCounts_.emplace(SAI_OBJECT_TYPE_VIRTUAL_CHANNEL, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE, 0);
#endif
  varCounts_.emplace(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_VLAN, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_VLAN_MEMBER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ISOLATION_GROUP, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER, 0);
  varCounts_.emplace(SAI_OBJECT_TYPE_WRED, 0);
}

} // namespace facebook::fboss
