/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/FileUtil.h>
#include <folly/Singleton.h>

namespace {
struct singleton_tag_type {};
} // namespace

using facebook::fboss::FakeSai;
static folly::Singleton<FakeSai, singleton_tag_type> fakeSaiSingleton{};
std::shared_ptr<FakeSai> FakeSai::getInstance() {
  return fakeSaiSingleton.try_get();
}

void FakeSai::clear() {
  auto fs = FakeSai::getInstance();

  fs->aclTableGroupManager.clearWithMembers();
  fs->aclEntryManager.clear();
  fs->aclCounterManager.clear();
  fs->aclTableManager.clear();
  fs->arsManager.clear();
  fs->arsProfileManager.clear();
  fs->bridgeManager.clearWithMembers();
  fs->counterManager.clear();
  fs->debugCounterManager.clear();
  fs->fdbManager.clear();
  fs->hashManager.clear();
  fs->hostIfTrapManager.clear();
  fs->hostIfUserDefinedTrapManager.clear();
  fs->hostifTrapGroupManager.clear();
  fs->inSegEntryManager.clear();
  fs->isolationGroupManager.clearWithMembers();
  fs->neighborManager.clear();
  fs->mirrorManager.clear();
  fs->nextHopManager.clear();
  fs->nextHopGroupManager.clearWithMembers();
  fs->portManager.clear();
  fs->qosMapManager.clear();
  fs->queueManager.clear();
  fs->routeManager.clear();
  fs->routeInterfaceManager.clear();
  fs->myMacManager.clear();
  fs->samplePacketManager.clear();
  fs->scheduleManager.clear();
  fs->switchManager.clear();
  fs->udfManager.clear();
  fs->udfGroupManager.clear();
  fs->udfMatchManager.clear();
  fs->virtualRouteManager.clear();
  fs->vlanManager.clearWithMembers();
  fs->wredManager.clear();
  fs->tamManager.clear();
  fs->tamEventManager.clear();
  fs->tamEventActionManager.clear();
  fs->tamReportManager.clear();
  fs->tamTransportManager.clear();
  fs->tamCollectorManager.clear();
  fs->tunnelManager.clear();
  fs->tunnelTermManager.clear();
  fs->tunnelMapManager.clear();
  fs->tunnelMapEntryManager.clear();
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  fs->srv6SidListManager.clear();
  fs->mySidEntryManager.clear();
#endif
  fs->systemPortManager.clear();
}

sai_object_id_t FakeSai::getCpuPort() {
  return cpuPortId;
}

sai_object_id_t FakeSai::getCpuSystemPort() {
  return cpuSystemPortId;
}

void sai_create_cpu_port() {
  // Create the CPU port
  auto fs = FakeSai::getInstance();
  std::vector<uint32_t> cpuPortLanes{};
  uint32_t cpuPortSpeed = 0;
  sai_object_id_t portId = fs->portManager.create(cpuPortLanes, cpuPortSpeed);
  sai_system_port_config_t config;
  config.num_voq = 10;
  sai_object_id_t qosToTcMapId;
  sai_object_id_t switchId;
  sai_object_id_t systemPortId =
      fs->systemPortManager.create(config, switchId, true, qosToTcMapId);
  auto& port = fs->portManager.get(portId);
  auto& systemPort = fs->systemPortManager.get(systemPortId);
  for (uint8_t queueId = 0; queueId < 10; queueId++) {
    auto saiQueueId = fs->queueManager.create(
        SAI_QUEUE_TYPE_MULTICAST, portId, queueId, portId);
    port.queueIdList.push_back(saiQueueId);
    auto saiVoqId = fs->queueManager.create(
        SAI_QUEUE_TYPE_MULTICAST_VOQ, portId, queueId, portId);
    systemPort.queueIdList.push_back(saiVoqId);
  }
  fs->cpuPortId = portId;
  fs->cpuSystemPortId = systemPortId;
}

sai_status_t sai_api_initialize(
    uint64_t /* flags */,
    const sai_service_method_table_t* /* services */) {
  auto fs = FakeSai::getInstance();
  if (fs->initialized) {
    return SAI_STATUS_FAILURE;
  }
  // Create the default switch per the SAI spec
  fs->switchManager.create();
  // Create the default 1Q bridge per the SAI spec
  fs->bridgeManager.create();
  // Create the default virtual router per the SAI spec
  fs->virtualRouteManager.create();

  // Create the CPU port
  sai_create_cpu_port();

  fs->initialized = true;
  return SAI_STATUS_SUCCESS;
}

sai_status_t sai_api_uninitialize(void) {
  return SAI_STATUS_SUCCESS;
}

sai_status_t sai_api_query(sai_api_t sai_api_id, void** api_method_table) {
  auto fs = FakeSai::getInstance();
  if (!fs->initialized) {
    return SAI_STATUS_FAILURE;
  }
  sai_status_t res;
  switch (sai_api_id) {
    case SAI_API_ACL:
      facebook::fboss::populate_acl_api((sai_acl_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_ARS:
      facebook::fboss::populate_ars_api((sai_ars_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_ARS_PROFILE:
      facebook::fboss::populate_ars_profile_api(
          (sai_ars_profile_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_BRIDGE:
      facebook::fboss::populate_bridge_api(
          (sai_bridge_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_BUFFER:
      facebook::fboss::populate_buffer_api(
          (sai_buffer_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_COUNTER:
      facebook::fboss::populate_counter_api(
          (sai_counter_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_DEBUG_COUNTER:
      facebook::fboss::populate_debug_counter_api(
          (sai_debug_counter_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_FDB:
      facebook::fboss::populate_fdb_api((sai_fdb_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_HASH:
      facebook::fboss::populate_hash_api((sai_hash_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_HOSTIF:
      facebook::fboss::populate_hostif_api(
          (sai_hostif_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_ISOLATION_GROUP:
      facebook::fboss::populate_isolation_group_api(
          (sai_isolation_group_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_LAG:
      facebook::fboss::populate_lag_api((sai_lag_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_MIRROR:
      facebook::fboss::populate_mirror_api(
          (sai_mirror_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_MPLS:
      facebook::fboss::populate_mpls_api((sai_mpls_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_NEIGHBOR:
      facebook::fboss::populate_neighbor_api(
          (sai_neighbor_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_NEXT_HOP:
      facebook::fboss::populate_next_hop_api(
          (sai_next_hop_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_NEXT_HOP_GROUP:
      facebook::fboss::populate_next_hop_group_api(
          (sai_next_hop_group_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_PORT:
      facebook::fboss::populate_port_api((sai_port_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_QOS_MAP:
      facebook::fboss::populate_qos_map_api(
          (sai_qos_map_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_QUEUE:
      facebook::fboss::populate_queue_api((sai_queue_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_ROUTE:
      facebook::fboss::populate_route_api((sai_route_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_ROUTER_INTERFACE:
      facebook::fboss::populate_router_interface_api(
          (sai_router_interface_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_MY_MAC:
      facebook::fboss::populate_my_mac_api(
          (sai_my_mac_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_SAMPLEPACKET:
      facebook::fboss::populate_samplepacket_api(
          (sai_samplepacket_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_SCHEDULER:
      facebook::fboss::populate_scheduler_api(
          (sai_scheduler_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_SWITCH:
      facebook::fboss::populate_switch_api(
          (sai_switch_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_SYSTEM_PORT:
      facebook::fboss::populate_system_port_api(
          (sai_system_port_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_VIRTUAL_ROUTER:
      facebook::fboss::populate_virtual_router_api(
          (sai_virtual_router_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_UDF:
      facebook::fboss::populate_udf_api((sai_udf_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_VLAN:
      facebook::fboss::populate_vlan_api((sai_vlan_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_WRED:
      facebook::fboss::populate_wred_api((sai_wred_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_VIRTUAL_CHANNEL:
      facebook::fboss::populate_virtual_channel_api(
          (sai_virtual_channel_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_TAM:
      facebook::fboss::populate_tam_api((sai_tam_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_API_TUNNEL:
      facebook::fboss::populate_tunnel_api(
          (sai_tunnel_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_API_SRV6:
      facebook::fboss::populate_srv6_api((sai_srv6_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
#endif
    case SAI_API_MACSEC:
      facebook::fboss::populate_macsec_api(
          (sai_macsec_api_t**)api_method_table);
      res = SAI_STATUS_SUCCESS;
      break;
    default:
      res = SAI_STATUS_INVALID_PARAMETER;
      break;
  }
  return res;
}

sai_status_t sai_log_set(sai_api_t /*api*/, sai_log_level_t /*log_level*/) {
  return SAI_STATUS_SUCCESS;
}

sai_status_t sai_dbg_generate_dump(const char* dump_file_name) {
  folly::writeFile(
      std::string("Hello from land of FakeSai. All hunky dory here"),
      dump_file_name);
  return SAI_STATUS_SUCCESS;
}

sai_object_type_t sai_object_type_query(sai_object_id_t /*object_id*/) {
  // FIXME: implement this
  return SAI_OBJECT_TYPE_NEXT_HOP;
}

sai_status_t sai_object_type_get_availability(
    sai_object_id_t /*switch_id*/,
    sai_object_type_t /*object_type*/,
    uint32_t /*attr_count*/,
    const sai_attribute_t* /*attr_list*/,
    uint64_t* count) {
  *count = 10000;
  return SAI_STATUS_SUCCESS;
}
