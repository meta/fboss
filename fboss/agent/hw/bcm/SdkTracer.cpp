/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <memory>

#include <folly/Singleton.h>

#include "fboss/agent/hw/bcm/BcmCinter.h"
#include "fboss/agent/hw/bcm/SdkWrapSettings.h"
#include "fboss/lib/FunctionCallTimeReporter.h"

extern "C" {

#include <bcm/cosq.h>
#include <bcm/field.h>
}

DECLARE_bool(enable_bcm_cinter);

extern "C" {

//
// the broadcom library symbols we want to use
//
int __real_bcm_switch_pkt_trace_info_get(
    int unit,
    uint32 options,
    uint8 port,
    int len,
    uint8* data,
    bcm_switch_pkt_trace_info_t* pkt_trace_info);

int __real_bcm_field_range_create(
    int unit,
    bcm_field_range_t* range,
    uint32 flags,
    bcm_l4_port_t min,
    bcm_l4_port_t max);

int __real_bcm_field_range_get(
    int unit,
    bcm_field_range_t range,
    uint32* flags,
    bcm_l4_port_t* min,
    bcm_l4_port_t* max);

int __real_bcm_port_control_get(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int* value);

int __real_bcm_port_phy_control_get(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32* value);

int __real_bcm_port_phy_tx_get(
    int unit,
    bcm_port_t port,
    bcm_port_phy_tx_t* tx);

int __real_bcm_port_phy_tx_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_tx_t* tx);

int __real_bcm_rx_active(int unit);

int __real_bcm_cosq_bst_stat_clear(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid);

int __real_bcm_rx_cosq_mapping_size_get(int unit, int* size);

int __real_bcm_stat_custom_add(
    int unit,
    bcm_port_t port,
    bcm_stat_val_t type,
    bcm_custom_stat_trigger_t trigger);

int __real_bcm_cosq_bst_profile_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile);

void __real_bcm_cosq_gport_discard_t_init(bcm_cosq_gport_discard_t* discard);

int __real_bcm_cosq_gport_discard_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_cosq_gport_discard_t* discard);

int __real_bcm_cosq_gport_mapping_set(
    int unit,
    bcm_gport_t ing_port,
    bcm_cos_t priority,
    uint32_t flags,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq);

int __real_bcm_cosq_gport_mapping_get(
    int unit,
    bcm_gport_t ing_port,
    bcm_cos_t priority,
    uint32_t flags,
    bcm_gport_t* gport,
    bcm_cos_queue_t* cosq);

int __real_bcm_rx_cosq_mapping_set(
    int unit,
    int index,
    bcm_rx_reasons_t reasons,
    bcm_rx_reasons_t reasons_mask,
    uint8 int_prio,
    uint8 int_prio_mask,
    uint32 packet_type,
    uint32 packet_type_mask,
    bcm_cos_queue_t cosq);

int __real_bcm_rx_cosq_mapping_get(
    int unit,
    int index,
    bcm_rx_reasons_t* reasons,
    bcm_rx_reasons_t* reasons_mask,
    uint8* int_prio,
    uint8* int_prio_mask,
    uint32* packet_type,
    uint32* packet_type_mask,
    bcm_cos_queue_t* cosq);

int __real_bcm_rx_cosq_mapping_delete(int unit, int index);

int __real_bcm_rx_cosq_mapping_extended_add(
    int unit,
    int options,
    bcm_rx_cosq_mapping_t* cosqMap);

int __real_bcm_rx_cosq_mapping_extended_delete(
    int unit,
    bcm_rx_cosq_mapping_t* cosqMap);

int __real_bcm_rx_cosq_mapping_extended_set(
    int unit,
    uint32 options,
    bcm_rx_cosq_mapping_t* cosqMap);

int __real_bcm_cosq_bst_stat_sync(int unit, bcm_bst_stat_id_t bid);

int __real_bcm_qos_map_create(int unit, uint32 flags, int* map_id);

int __real_bcm_qos_map_destroy(int unit, int map_id);

int __real_bcm_qos_map_add(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id);

int __real_bcm_qos_map_delete(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id);

int __real_bcm_qos_map_multi_get(
    int unit,
    uint32 flags,
    int map_id,
    int array_size,
    bcm_qos_map_t* array,
    int* array_count);

int __real_bcm_qos_multi_get(
    int unit,
    int array_size,
    int* map_ids_array,
    int* flags_array,
    int* array_count);

int __real_bcm_qos_port_map_set(
    int unit,
    bcm_gport_t gport,
    int ing_map,
    int egr_map);

int __real_bcm_qos_port_map_get(
    int unit,
    bcm_gport_t gport,
    int* ing_map,
    int* egr_map);

int __real_bcm_qos_port_map_type_get(
    int unit,
    bcm_gport_t gport,
    uint32 flags,
    int* map_id);

int __real_bcm_port_dscp_map_mode_get(int unit, bcm_port_t port, int* mode);

int __real_bcm_port_dscp_map_mode_set(int unit, bcm_port_t port, int mode);

int __real_bcm_l2_traverse(
    int unit,
    bcm_l2_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_port_subsidiary_ports_get(
    int unit,
    bcm_port_t port,
    bcm_pbmp_t* pbmp);

int __real_bcm_field_group_enable_get(
    int unit,
    bcm_field_group_t group,
    int* enable);

int __real_bcm_field_range_destroy(int unit, bcm_field_range_t range);

int __real_bcm_field_entry_destroy(int unit, bcm_field_entry_t entry);

int __real_bcm_field_group_destroy(int unit, bcm_field_group_t);

int __real_bcm_field_group_traverse(
    int unit,
    bcm_field_group_traverse_cb callback,
    void* user_data);

void __real_bcm_field_hint_t_init(bcm_field_hint_t* hint);

int __real_bcm_field_hints_create(int unit, bcm_field_hintid_t* hint_id);

int __real_bcm_field_hints_add(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint);

int __real_bcm_field_hints_destroy(int unit, bcm_field_hintid_t hint_id);

int __real_bcm_field_hints_get(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint);

int __real_bcm_l3_egress_ecmp_delete(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf);

int __real_bcm_l3_info(int unit, bcm_l3_info_t* l3info);

void __real_bcm_l3_egress_ecmp_t_init(bcm_l3_egress_ecmp_t* ecmp);

int __real_bcm_cosq_bst_profile_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile);

int __real_bcm_switch_object_count_multi_get(
    int unit,
    int object_size,
    bcm_switch_object_t* object_array,
    int* entries);

int __real_bcm_switch_object_count_get(
    int unit,
    bcm_switch_object_t object,
    int* entries);

#if (BCM_SDK_VERSION >= BCM_VERSION(6, 5, 19))
int __real_bcm_l3_alpm_resource_get(
    int unit,
    bcm_l3_route_group_t grp,
    bcm_l3_alpm_resource_t* resource);
#endif

int __real_bcm_field_entry_multi_get(
    int unit,
    bcm_field_group_t group,
    int entry_size,
    bcm_field_entry_t* entry_array,
    int* entry_count);

int __real_bcm_cosq_bst_stat_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value);

int __real_bcm_cosq_bst_stat_extended_get(
    int unit,
    bcm_cosq_object_id_t* id,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value);

int __real_bcm_udf_hash_config_add(
    int unit,
    uint32 options,
    bcm_udf_hash_config_t* config);

int __real_bcm_udf_hash_config_delete(int unit, bcm_udf_hash_config_t* config);

int __real_bcm_udf_create(
    int unit,
    bcm_udf_alloc_hints_t* hints,
    bcm_udf_t* udf_info,
    bcm_udf_id_t* udf_id);

int __real_bcm_udf_destroy(int unit, bcm_udf_id_t udf_id);

int __real_bcm_udf_pkt_format_create(
    int unit,
    bcm_udf_pkt_format_options_t options,
    bcm_udf_pkt_format_info_t* pkt_format,
    bcm_udf_pkt_format_id_t* pkt_format_id);

int __real_bcm_udf_pkt_format_add(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_port_pause_addr_set(int unit, bcm_port_t port, bcm_mac_t mac);

int __real_bcm_udf_pkt_format_destroy(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_udf_pkt_format_delete(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_udf_pkt_format_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    int max,
    bcm_udf_id_t* udf_id_list,
    int* actual);

int __real_bcm_udf_hash_config_get(int unit, bcm_udf_hash_config_t* config);

int __real_bcm_udf_pkt_format_info_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    bcm_udf_pkt_format_info_t* pkt_format);

void __real_bcm_udf_pkt_format_info_t_init(
    bcm_udf_pkt_format_info_t* pkt_format);

void __real_bcm_udf_alloc_hints_t_init(bcm_udf_alloc_hints_t* udf_hints);

void __real_bcm_udf_t_init(bcm_udf_t* udf_info);

void __real_bcm_udf_hash_config_t_init(bcm_udf_hash_config_t* config);

int __real_bcm_udf_init(int unit);

int __real_bcm_udf_get(int unit, bcm_udf_id_t udf_id, bcm_udf_t* udf_info);

int __real_bcm_field_qset_id_multi_set(
    int unit,
    bcm_field_qualify_t qualifier,
    int num_objects,
    int* object_list,
    bcm_field_qset_t* qset);

int __real_bcm_field_qualify_UdfClass(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask);

int __real_bcm_field_qualify_udf(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int length,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_udf_get(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int max_length,
    uint8* data,
    uint8* mask,
    int* actual_length);

void __real_bcm_field_group_config_t_init(
    bcm_field_group_config_t* group_config);

int __real_bcm_l3_egress_ecmp_get(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_size,
    bcm_if_t* intf_array,
    int* intf_count);

int __real_bcm_l3_enable_set(int unit, int enable);

int __real_bcm_rx_queue_max_get(int unit, bcm_cos_queue_t* cosq);

int __real_bcm_field_group_get(
    int unit,
    bcm_field_group_t group,
    bcm_field_qset_t* qset);

int __real_bcm_field_qset_id_multi_get(
    int unit,
    bcm_field_qset_t qset,
    bcm_field_qualify_t qualifier,
    int max_objects,
    int* object_list,
    int* count_objects);

int __real_bcm_cosq_control_get(
    int unit,
    bcm_gport_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_control_t type,
    int* arg);

int __real_bcm_port_pause_get(
    int unit,
    bcm_port_t port,
    int* pause_tx,
    int* pause_rx);

int __real_bcm_port_sample_rate_get(
    int unit,
    bcm_port_t port,
    int* ingress_rate,
    int* egress_rate);

int __real_bcm_field_action_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32* param0,
    uint32* param1);

int __real_bcm_info_get(int unit, bcm_info_t* info);

int __real_bcm_cosq_gport_discard_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_cosq_gport_discard_t* discard);

int __real_bcm_port_phy_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32 value);

int __real_bcm_port_ability_advert_set(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask);

int __real_bcm_port_autoneg_set(int unit, bcm_port_t port, int autoneg);

int __real_bcm_field_qualify_DstIp(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip_t data,
    bcm_ip_t mask);

int __real_bcm_field_entry_create_id(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t entry);

int __real_bcm_cosq_init(int unit);

int __real_bcm_cosq_gport_traverse(
    int unit,
    bcm_cosq_gport_traverse_cb cb,
    void* user_data);

int __real_bcm_cosq_gport_sched_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    int* mode,
    int* weight);

int __real_bcm_cosq_gport_sched_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    int mode,
    int weight);

int __real_bcm_cosq_control_set(
    int unit,
    bcm_gport_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_control_t type,
    int arg);

int __real_bcm_cosq_port_profile_get(
    int unit,
    bcm_gport_t port,
    bcm_cosq_profile_type_t profile_type,
    int* profile_id);

int __real_bcm_cosq_port_profile_set(
    int unit,
    bcm_gport_t port,
    bcm_cosq_profile_type_t profile_type,
    int profile_id);

int __real_bcm_cosq_gport_bandwidth_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32 kbits_sec_min,
    uint32 kbits_sec_max,
    uint32 flags);

int __real_bcm_cosq_gport_bandwidth_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32* kbits_sec_min,
    uint32* kbits_sec_max,
    uint32* flags);

int __real_bcm_field_init(int unit);

int __real_bcm_field_group_create_id(
    int unit,
    bcm_field_qset_t qset,
    int pri,
    bcm_field_group_t group);

int __real_bcm_field_group_config_create(
    int unit,
    bcm_field_group_config_t* group_config);

int __real_bcm_field_entry_create(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t* entry);

int __real_bcm_field_entry_install(int unit, bcm_field_entry_t entry);

int __real_bcm_field_action_add(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1);

int __real_bcm_field_action_remove(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action);

int __real_bcm_field_action_delete(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1);

int __real_bcm_field_entry_prio_set(
    int unit,
    bcm_field_entry_t entry,
    int prio);

int __real_bcm_field_entry_enable_set(
    int unit,
    bcm_field_entry_t entry,
    int enable);

int __real_bcm_field_qualify_DstIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask);

int __real_bcm_field_qualify_SrcIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask);

int __real_bcm_field_qualify_InPorts(
    int unit,
    bcm_field_entry_t entry,
    bcm_pbmp_t data,
    bcm_pbmp_t mask);

int __real_bcm_field_qualify_L4DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask);

int __real_bcm_field_qualify_L4SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask);

int __real_bcm_field_qualify_DstMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask);

int __real_bcm_field_qualify_SrcMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask);

int __real_bcm_field_qualify_IcmpTypeCode(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask);

int __real_bcm_switch_control_set(int unit, bcm_switch_control_t type, int arg);

int __real_bcm_switch_control_get(
    int unit,
    bcm_switch_control_t type,
    int* arg);

int __real_bcm_field_qualify_SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port);

int __real_bcm_l3_egress_get(int unit, bcm_if_t intf, bcm_l3_egress_t* egr);

int __real_bcm_l3_egress_create(
    int unit,
    uint32 flags,
    bcm_l3_egress_t* egr,
    bcm_if_t* if_id);

int __real_bcm_l3_egress_find(int unit, bcm_l3_egress_t* egr, bcm_if_t* intf);

int __real_bcm_l3_egress_traverse(
    int unit,
    bcm_l3_egress_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_l3_ecmp_member_add(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member);

int __real_bcm_l3_egress_ecmp_add(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf);

int __real_bcm_field_qualify_RangeCheck(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_range_t range,
    int invert);

int __real_bcm_field_qualify_DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port);

int __real_bcm_field_qualify_TcpControl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask);

int __real_bcm_field_qualify_IpFrag(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpFrag_t frag_info);

int __real_bcm_field_qualify_IpProtocol(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask);

int __real_bcm_port_phy_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32 value);

int __real_bcm_port_ability_advert_set(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask);

int __real_bcm_port_autoneg_set(int unit, bcm_port_t port, int autoneg);

int __real_bcm_port_pause_sym_set(int unit, bcm_port_t port, int pause);

int __real_bcm_port_pause_set(
    int unit,
    bcm_port_t port,
    int pause_tx,
    int pause_rx);

int __real_bcm_port_sample_rate_set(
    int unit,
    bcm_port_t port,
    int ingress_rate,
    int egress_rate);

int __real_bcm_port_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int value);

int __real_bcm_linkscan_update(int unit, bcm_pbmp_t pbmp);

int __real_bcm_trunk_find(
    int unit,
    bcm_module_t modid,
    bcm_port_t port,
    bcm_trunk_t* trunk);

int __real_bcm_trunk_destroy(int unit, bcm_trunk_t tid);

int __real_bcm_trunk_create(int unit, uint32 flags, bcm_trunk_t* tid);

int __real_bcm_trunk_bitmap_expand(int unit, bcm_pbmp_t* pbmp_ptr);

int __real_bcm_trunk_set(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* trunk_info,
    int member_count,
    bcm_trunk_member_t* member_array);

int __real_bcm_trunk_member_add(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member);

int __real_bcm_trunk_member_delete(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member);

int __real_bcm_port_loopback_get(int unit, bcm_port_t port, uint32* value);
int __real_bcm_port_loopback_set(int unit, bcm_port_t port, uint32 value);

int __real_bcm_trunk_init(int unit);
int __real_bcm_trunk_get(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* t_data,
    int member_max,
    bcm_trunk_member_t* member_array,
    int* member_count);
int __real_bcm_trunk_create(int unit, uint32 flags, bcm_trunk_t* tid);
int __real_bcm_trunk_destroy(int unit, bcm_trunk_t tid);
int __real_bcm_trunk_find(
    int unit,
    bcm_module_t modid,
    bcm_port_t port,
    bcm_trunk_t* trunk);
int __real_bcm_trunk_set(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* trunk_info,
    int member_count,
    bcm_trunk_member_t* member_array);
int __real_bcm_trunk_member_add(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member);
int __real_bcm_trunk_member_delete(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member);

int __real_bcm_l3_egress_traverse(
    int unit,
    bcm_l3_egress_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_knet_netif_traverse(
    int unit,
    bcm_knet_netif_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_linkscan_mode_get(int unit, bcm_port_t port, int* mode);

int __real_bcm_rx_cfg_get(int unit, bcm_rx_cfg_t* cfg);

int __real_bcm_vlan_list(int unit, bcm_vlan_data_t** listp, int* countp);

void __real_bcm_l3_egress_t_init(bcm_l3_egress_t* egr);

int __real_bcm_rx_control_set(int unit, bcm_rx_control_t type, int arg);

int __real_bcm_l3_egress_find(int unit, bcm_l3_egress_t* egr, bcm_if_t* intf);

int __real_bcm_pkt_flags_init(int unit, bcm_pkt_t* pkt, uint32 init_flags);

int __real_bcm_rx_free(int unit, void* pkt_data);

int __real_bcm_knet_init(int unit);

int __real_bcm_pkt_alloc(int unit, int size, uint32 flags, bcm_pkt_t** pkt_buf);

int __real_bcm_linkscan_register(int unit, bcm_linkscan_handler_t f);

int __real_bcm_l3_route_delete_by_interface(int unit, bcm_l3_route_t* info);

int __real_bcm_switch_event_unregister(
    int unit,
    bcm_switch_event_cb_t cb,
    void* userdata);

int __real_bcm_l3_host_delete_all(int unit, bcm_l3_host_t* info);

int __real_bcm_port_untagged_vlan_get(
    int unit,
    bcm_port_t port,
    bcm_vlan_t* vid_ptr);

int __real_bcm_l3_intf_find(int unit, bcm_l3_intf_t* intf);

int __real_bcm_l3_intf_delete(int unit, bcm_l3_intf_t* intf);

int __real_bcm_vlan_destroy_all(int unit);

int __real_bcm_vlan_list_destroy(int unit, bcm_vlan_data_t* list, int count);

int __real_bcm_l3_egress_ecmp_ethertype_set(
    int unit,
    uint32 flags,
    int ethertype_count,
    int* ethertype_array);

int __real_bcm_l3_egress_ecmp_ethertype_get(
    int unit,
    uint32* flags,
    int ethertype_max,
    int* ethertype_array,
    int* ethertype_count);

int __real_bcm_l3_egress_ecmp_member_status_set(
    int unit,
    bcm_if_t intf,
    int status);

int __real_bcm_l3_egress_ecmp_member_status_get(
    int unit,
    bcm_if_t intf,
    int* status);

int __real_bcm_port_untagged_vlan_set(
    int unit,
    bcm_port_t port,
    bcm_vlan_t vid);

int __real_bcm_port_frame_max_set(int unit, bcm_port_t port, int size);

int __real_bcm_rx_control_get(int unit, bcm_rx_control_t type, int* arg);

int __real_bcm_stg_list_destroy(int unit, bcm_stg_t* list, int count);

int __real_bcm_cosq_mapping_get(
    int unit,
    bcm_cos_t priority,
    bcm_cos_queue_t* cosq);

int __real_bcm_switch_control_port_get(
    int unit,
    bcm_port_t port,
    bcm_switch_control_t type,
    int* arg);

int __real_bcm_l3_egress_ecmp_traverse(
    int unit,
    bcm_l3_egress_ecmp_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_cosq_mapping_set(
    int unit,
    bcm_cos_t priority,
    bcm_cos_queue_t cosq);

int __real_bcm_l3_ecmp_member_delete(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member);

int __real_bcm_l3_egress_ecmp_delete(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf);

int __real_bcm_detach(int unit);

int __real__bcm_shutdown(int unit);

int __real_soc_shutdown(int unit);

int __real_bcm_cosq_bst_stat_multi_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32 options,
    int max_values,
    bcm_bst_stat_id_t* id_list,
    uint64* values);

int __real_bcm_port_ability_advert_get(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask);

int __real_bcm_l2_age_timer_get(int unit, int* age_seconds);

int __real_bcm_port_queued_count_get(int unit, bcm_port_t port, uint32* count);

int __real_bcm_knet_filter_create(int unit, bcm_knet_filter_t* filter);

int __real_bcm_vlan_create(int unit, bcm_vlan_t vid);

int __real_bcm_l3_ecmp_destroy(int unit, bcm_if_t ecmp_group_id);

int __real_bcm_l3_egress_ecmp_destroy(int unit, bcm_l3_egress_ecmp_t* ecmp);

int __real_bcm_linkscan_mode_set_pbm(int unit, bcm_pbmp_t pbm, int mode);

int __real_bcm_l3_host_find(int unit, bcm_l3_host_t* info);

int __real_bcm_switch_pkt_trace_info_get(
    int unit,
    uint32 options,
    uint8 port,
    int len,
    uint8* data,
    bcm_switch_pkt_trace_info_t* pkt_trace_info);

void __real_bcm_port_config_t_init(bcm_port_config_t* config);

int __real_bcm_port_config_get(int unit, bcm_port_config_t* config);

int __real_bcm_l3_route_delete_all(int unit, bcm_l3_route_t* info);

int __real_bcm_l2_addr_add(int unit, bcm_l2_addr_t* l2addr);

int __real_bcm_l3_host_add(int unit, bcm_l3_host_t* info);

int __real_bcm_l3_ecmp_get(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp_info,
    int ecmp_member_size,
    bcm_l3_ecmp_member_t* ecmp_member_array,
    int* ecmp_member_count);

int __real_bcm_l3_egress_ecmp_get(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_size,
    bcm_if_t* intf_array,
    int* intf_count);

int __real_bcm_port_enable_set(int unit, bcm_port_t port, int enable);

int __real_bcm_port_stat_enable_set(int unit, bcm_gport_t port, int enable);

int __real_bcm_port_stat_attach(int unit, bcm_port_t port, uint32 counterID_);

int __real_bcm_port_stat_detach_with_id(
    int unit,
    bcm_gport_t gPort,
    uint32 counterID);

#if (BCM_SDK_VERSION >= BCM_VERSION(6, 5, 21))
int __real_bcm_port_fdr_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_config_t* fdr_config);

int __real_bcm_port_fdr_config_get(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_config_t* fdr_config);

int __real_bcm_port_fdr_stats_get(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_stats_t* fdr_stats);
#endif

bcm_ip_t __real_bcm_ip_mask_create(int len);

int __real_bcm_l3_egress_multipath_add(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf);

int __real_bcm_cosq_bst_stat_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value);

int __real_bcm_port_pause_addr_set(int unit, bcm_port_t port, bcm_mac_t mac);

int __real_bcm_cosq_bst_stat_extended_get(
    int unit,
    bcm_cosq_object_id_t* id,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value);

int __real_bcm_udf_hash_config_add(
    int unit,
    uint32 options,
    bcm_udf_hash_config_t* config);

int __real_bcm_udf_hash_config_delete(int unit, bcm_udf_hash_config_t* config);

int __real_bcm_udf_create(
    int unit,
    bcm_udf_alloc_hints_t* hints,
    bcm_udf_t* udf_info,
    bcm_udf_id_t* udf_id);

int __real_bcm_udf_destroy(int unit, bcm_udf_id_t udf_id);

int __real_bcm_udf_pkt_format_create(
    int unit,
    bcm_udf_pkt_format_options_t options,
    bcm_udf_pkt_format_info_t* pkt_format,
    bcm_udf_pkt_format_id_t* pkt_format_id);

int __real_bcm_udf_pkt_format_add(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_udf_pkt_format_destroy(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_udf_pkt_format_delete(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id);

int __real_bcm_udf_pkt_format_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    int max,
    bcm_udf_id_t* udf_id_list,
    int* actual);

int __real_bcm_udf_hash_config_get(int unit, bcm_udf_hash_config_t* config);

int __real_bcm_udf_pkt_format_info_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    bcm_udf_pkt_format_info_t* pkt_format);

void __real_bcm_udf_pkt_format_info_t_init(
    bcm_udf_pkt_format_info_t* pkt_format);

void __real_bcm_udf_alloc_hints_t_init(bcm_udf_alloc_hints_t* udf_hints);

void __real_bcm_udf_hash_config_t_init(bcm_udf_hash_config_t* config);

void __real_bcm_udf_t_init(bcm_udf_t* udf_info);

int __real_bcm_udf_init(int unit);

int __real_bcm_udf_get(int unit, bcm_udf_id_t udf_id, bcm_udf_t* udf_info);

int __real_bcm_field_qset_id_multi_set(
    int unit,
    bcm_field_qualify_t qualifier,
    int num_objects,
    int* object_list,
    bcm_field_qset_t* qset);

int __real_bcm_field_qualify_UdfClass(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask);

int __real_bcm_field_qualify_udf(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int length,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_udf_get(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int max_length,
    uint8* data,
    uint8* mask,
    int* actual_length);

void __real_bcm_field_group_config_t_init(
    bcm_field_group_config_t* group_config);

int __real_bcm_l3_route_get(int unit, bcm_l3_route_t* info);

int __real_bcm_l3_egress_multipath_traverse(
    int unit,
    bcm_l3_egress_multipath_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_port_vlan_member_set(int unit, bcm_port_t port, uint32 flags);

int __real_bcm_l3_egress_multipath_find(
    int unit,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_if_t* mpintf);

int __real_bcm_l3_intf_get(int unit, bcm_l3_intf_t* intf);

int __real_bcm_l3_egress_create(
    int unit,
    uint32 flags,
    bcm_l3_egress_t* egr,
    bcm_if_t* if_id);

int __real_bcm_l3_host_traverse(
    int unit,
    uint32 flags,
    uint32 start,
    uint32 end,
    bcm_l3_host_traverse_cb cb,
    void* user_data);

int __real_bcm_l3_host_delete_by_interface(int unit, bcm_l3_host_t* info);

int __real_bcm_linkscan_unregister(int unit, bcm_linkscan_handler_t f);

void __real_bcm_knet_filter_t_init(bcm_knet_filter_t* filter);

int __real_bcm_rx_unregister(int unit, bcm_rx_cb_f callback, uint8 priority);

int __real_bcm_l2_traverse(
    int unit,
    bcm_l2_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_l3_egress_multipath_get(
    int unit,
    bcm_if_t mpintf,
    int intf_size,
    bcm_if_t* intf_array,
    int* intf_count);

int __real_bcm_rx_register(
    int unit,
    const char* name,
    bcm_rx_cb_f callback,
    uint8 priority,
    void* cookie,
    uint32 flags);

int __real_bcm_ip6_mask_create(bcm_ip6_t ip6, int len);

int __real_bcm_port_phy_modify(
    int unit,
    bcm_port_t port,
    uint32 flags,
    uint32 phy_reg_addr,
    uint32 phy_data,
    uint32 phy_mask);

int __real_bcm_rx_start(int unit, bcm_rx_cfg_t* cfg);

int __real_bcm_l2_station_get(
    int unit,
    int station_id,
    bcm_l2_station_t* station);

int __real_bcm_l3_egress_multipath_destroy(int unit, bcm_if_t mpintf);

void __real_bcm_l3_egress_ecmp_t_init(bcm_l3_egress_ecmp_t* ecmp);

int __real_bcm_l3_egress_multipath_create(
    int unit,
    uint32 flags,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_if_t* mpintf);

int __real_bcm_stat_get(
    int unit,
    bcm_port_t port,
    bcm_stat_val_t type,
    uint64* value);

int __real_bcm_stat_sync_multi_get(
    int unit,
    bcm_port_t port,
    int nstat,
    bcm_stat_val_t* stat_arr,
    uint64* value_arr);

int __real_bcm_stk_my_modid_get(int unit, int* my_modid);

int __real_bcm_pkt_free(int unit, bcm_pkt_t* pkt);

int __real_bcm_tx(int unit, bcm_pkt_t* tx_pkt, void* cookie);

int __real_bcm_pktio_tx(int unit, bcm_pktio_pkt_t* tx_pkt);

int __real_bcm_pktio_txpmd_stat_attach(int unit, uint32 counter_id);

int __real_bcm_pktio_txpmd_stat_detach(int unit);

int __real_bcm_l3_egress_destroy(int unit, bcm_if_t intf);

int __real_bcm_port_control_get(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int* value);

int __real_bcm_switch_control_get(
    int unit,
    bcm_switch_control_t type,
    int* arg);

int __real_bcm_linkscan_enable_set(int unit, int us);

int __real_bcm_port_vlan_member_get(int unit, bcm_port_t port, uint32* flags);

int __real_bcm_l2_station_add(
    int unit,
    int* station_id,
    bcm_l2_station_t* station);

int __real_bcm_port_enable_get(int unit, bcm_port_t port, int* enable);

int __real_bcm_l3_host_delete(int unit, bcm_l3_host_t* ip_addr);

int __real_bcm_l3_route_max_ecmp_get(int unit, int* max);

void __real_bcm_l3_intf_t_init(bcm_l3_intf_t* intf);

int __real_bcm_l3_egress_get(int unit, bcm_if_t intf, bcm_l3_egress_t* egr);

int __real_bcm_linkscan_enable_get(int unit, int* us);

int __real_bcm_l3_intf_find_vlan(int unit, bcm_l3_intf_t* intf);

int __real_bcm_linkscan_detach(int unit);

int __real_bcm_vlan_port_add(
    int unit,
    bcm_vlan_t vid,
    bcm_pbmp_t pbmp,
    bcm_pbmp_t ubmp);

int __real_bcm_port_gport_get(int unit, bcm_port_t port, bcm_gport_t* gport);

int __real_bcm_cosq_bst_stat_sync(int unit, bcm_bst_stat_id_t bid);

void __real_bcm_port_ability_t_init(bcm_port_ability_t* ability);

int __real_bcm_port_speed_max(int unit, bcm_port_t port, int* speed);

int __real_bcm_cosq_bst_profile_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile);

int __real_bcm_vlan_destroy(int unit, bcm_vlan_t vid);

int __real_bcm_port_local_get(
    int unit,
    bcm_gport_t gport,
    bcm_port_t* local_port);

int __real_bcm_switch_control_port_set(
    int unit,
    bcm_port_t port,
    bcm_switch_control_t type,
    int arg);

int __real_bcm_switch_event_register(
    int unit,
    bcm_switch_event_cb_t cb,
    void* userdata);

int __real_bcm_knet_netif_create(int unit, bcm_knet_netif_t* netif);

int __real_bcm_l3_intf_create(int unit, bcm_l3_intf_t* intf);

int __real_bcm_port_ability_advert_set(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask);

void __real_bcm_knet_netif_t_init(bcm_knet_netif_t* netif);

int __real_bcm_l3_route_delete(int unit, bcm_l3_route_t* info);

int __real_bcm_stg_create(int unit, bcm_stg_t* stg_ptr);

int __real_bcm_l3_init(int unit);

int __real_bcm_port_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int value);

int __real_bcm_switch_control_set(int unit, bcm_switch_control_t type, int arg);

int __real_bcm_l3_egress_ecmp_find(
    int unit,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_l3_egress_ecmp_t* ecmp);

int __real_bcm_stg_default_set(int unit, bcm_stg_t stg);

int __real_bcm_port_dtag_mode_set(int unit, bcm_port_t port, int mode);

int __real_bcm_port_link_status_get(int unit, bcm_port_t port, int* status);

int __real_bcm_port_learn_set(int unit, bcm_port_t port, uint32 flags);

int __real_bcm_stg_vlan_add(int unit, bcm_stg_t stg, bcm_vlan_t vid);

int __real_bcm_port_dtag_mode_get(int unit, bcm_port_t port, int* mode);

int __real_bcm_stat_multi_get(
    int unit,
    bcm_port_t port,
    int nstat,
    bcm_stat_val_t* stat_arr,
    uint64* value_arr);

int __real_bcm_l3_info(int unit, bcm_l3_info_t* l3info);

int __real_bcm_knet_netif_destroy(int unit, int netif_id);

int __real_bcm_l2_addr_delete(int unit, bcm_mac_t mac, bcm_vlan_t vid);

int __real_bcm_vlan_control_port_set(
    int unit,
    int port,
    bcm_vlan_control_port_t type,
    int arg);

int __real_bcm_info_get(int unit, bcm_info_t* info);

int __real_bcm_cosq_bst_profile_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile);

int __real_bcm_port_interface_set(
    int unit,
    bcm_port_t port,
    bcm_port_if_t intf);

int __real_bcm_vlan_default_get(int unit, bcm_vlan_t* vid_ptr);

int __real_bcm_linkscan_mode_set(int unit, bcm_port_t port, int mode);

int __real_bcm_knet_filter_traverse(
    int unit,
    bcm_knet_filter_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_port_speed_set(int unit, bcm_port_t port, int speed);

int __real_bcm_stg_stp_set(
    int unit,
    bcm_stg_t stg,
    bcm_port_t port,
    int stp_state);

int __real_bcm_l3_route_add(int unit, bcm_l3_route_t* info);

int __real_bcm_vlan_default_set(int unit, bcm_vlan_t vid);

int __real_bcm_port_speed_get(int unit, bcm_port_t port, int* speed);

int __real_bcm_port_learn_get(int unit, bcm_port_t port, uint32* flags);

int __real_bcm_stat_clear(int unit, bcm_port_t port);

int __real_bcm_l3_egress_multipath_delete(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf);

int __real_bcm_l2_station_delete(int unit, int station_id);

int __real_bcm_attach(int unit, char* type, char* subtype, int remunit);

int __real_bcm_l2_age_timer_set(int unit, int age_seconds);

int __real_bcm_port_selective_get(
    int unit,
    bcm_port_t port,
    bcm_port_info_t* info);

char* __real_bcm_port_name(int unit, int port);

int __real_bcm_l3_route_multipath_get(
    int unit,
    bcm_l3_route_t* the_route,
    bcm_l3_route_t* path_array,
    int max_path,
    int* path_count);

int __real_bcm_stg_list(int unit, bcm_stg_t** list, int* count);

int __real_bcm_cosq_bst_stat_clear(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid);

int __real_bcm_attach_check(int unit);

int __real_bcm_port_ability_local_get(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* local_ability_mask);

int __real_bcm_vlan_gport_delete_all(int unit, bcm_vlan_t vlan);

int __real_bcm_stg_destroy(int unit, bcm_stg_t stg);

int __real_bcm_stg_default_get(int unit, bcm_stg_t* stg_ptr);

int __real_bcm_l2_addr_get(
    int unit,
    bcm_mac_t mac_addr,
    bcm_vlan_t vid,
    bcm_l2_addr_t* l2addr);

int __real_bcm_port_interface_get(
    int unit,
    bcm_port_t port,
    bcm_port_if_t* intf);

int __real_bcm_stg_stp_get(
    int unit,
    bcm_stg_t stg,
    bcm_port_t port,
    int* stp_state);

int __real_bcm_l3_route_traverse(
    int unit,
    uint32 flags,
    uint32 start,
    uint32 end,
    bcm_l3_route_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_port_selective_set(
    int unit,
    bcm_port_t port,
    bcm_port_info_t* info);

int __real_bcm_port_frame_max_get(int unit, bcm_port_t port, int* size);

int __real_bcm_l3_ecmp_create(
    int unit,
    uint32 options,
    bcm_l3_egress_ecmp_t* ecmp_info,
    int ecmp_member_count,
    bcm_l3_ecmp_member_t* ecmp_member_array);

int __real_bcm_l3_egress_ecmp_create(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_count,
    bcm_if_t* intf_array);

void __real_bcm_l3_ecmp_dlb_port_quality_attr_t_init(
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr);

int __real_bcm_l3_ecmp_dlb_port_quality_attr_set(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr);

int __real_bcm_l3_ecmp_dlb_port_quality_attr_get(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr);

int __real_bcm_attach_max(int* max_units);

int __real_bcm_l3_egress_ecmp_add(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf);

int __real_bcm_rx_stop(int unit, bcm_rx_cfg_t* cfg);

int __real_bcm_l3_route_max_ecmp_set(int unit, int max);

int __real_bcm_vlan_port_remove(int unit, bcm_vlan_t vid, bcm_pbmp_t pbmp);

int __real_bcm_knet_filter_destroy(int unit, int filter_id);

int __real_bcm_field_group_status_get(
    int unit,
    bcm_field_group_t group,
    bcm_field_group_status_t* status);

int __real_bcm_field_stat_create(
    int unit,
    bcm_field_group_t group,
    int nstat,
    bcm_field_stat_t* stat_arr,
    int* stat_id);

int __real_bcm_field_entry_stat_attach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id);

int __real_bcm_field_entry_stat_detach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id);

int __real_bcm_field_entry_stat_get(
    int unit,
    bcm_field_entry_t entry,
    int* stat_id);

int __real_bcm_field_stat_destroy(int unit, int stat_id);

int __real_bcm_field_stat_get(
    int unit,
    int stat_id,
    bcm_field_stat_t stat,
    uint64* value);

int __real_bcm_field_stat_size(int unit, int stat_id, int* stat_size);

int __real_bcm_field_stat_config_get(
    int unit,
    int stat_id,
    int nstat,
    bcm_field_stat_t* stat_arr);

int __real_bcm_field_entry_reinstall(int unit, bcm_field_entry_t entry);

int __real_bcm_field_qualify_RangeCheck_get(
    int unit,
    bcm_field_entry_t entry,
    int max_count,
    bcm_field_range_t* range,
    int* invert,
    int* count);

int __real_bcm_field_entry_prio_get(
    int unit,
    bcm_field_entry_t entry,
    int* prio);

int __real_bcm_field_qualify_SrcIp6_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t* data,
    bcm_ip6_t* mask);

int __real_bcm_field_qualify_DstIp6_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t* data,
    bcm_ip6_t* mask);

int __real_bcm_field_qualify_L4SrcPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t* data,
    bcm_l4_port_t* mask);

int __real_bcm_field_qualify_L4DstPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t* data,
    bcm_l4_port_t* mask);

int __real_bcm_field_qualify_TcpControl_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_SrcPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t* data_modid,
    bcm_module_t* mask_modid,
    bcm_port_t* data_port,
    bcm_port_t* mask_port);

int __real_bcm_field_qualify_DstPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t* data_modid,
    bcm_module_t* mask_modid,
    bcm_port_t* data_port,
    bcm_port_t* mask_port);

int __real_bcm_field_qualify_IpFrag_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpFrag_t* frag_info);

int __real_bcm_field_qualify_IpProtocol_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_DSCP_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_IpType_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpType_t* type);

int __real_bcm_field_qualify_EtherType_get(
    int unit,
    bcm_field_entry_t entry,
    uint16* data,
    uint16* mask);

int __real_bcm_field_qualify_Ttl_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask);

int __real_bcm_field_qualify_DstMac_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t* data,
    bcm_mac_t* mask);

int __real_bcm_field_qualify_SrcMac_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t* data,
    bcm_mac_t* mask);

int __real_bcm_field_entry_enable_get(
    int unit,
    bcm_field_entry_t entry,
    int* enable_flag);

int __real_bcm_field_qualify_Ttl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask);

int __real_bcm_field_qualify_IpType(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpType_t type);

int __real_bcm_field_qualify_EtherType(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask);

int __real_bcm_field_qualify_DSCP(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask);

int __real_bcm_field_qualify_DstClassL2_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask);

int __real_bcm_field_qualify_DstClassL2(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask);

int __real_bcm_field_qualify_DstClassL3_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask);

int __real_bcm_field_qualify_DstClassL3(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask);

int __real_bcm_field_qualify_PacketRes(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask);

int __real_bcm_field_qualify_PacketRes_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask);

int __real_bcm_field_qualify_OuterVlanId(
    int unit,
    bcm_field_entry_t entry,
    bcm_vlan_t data,
    bcm_vlan_t mask);

int __real_bcm_field_qualify_OuterVlanId_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_vlan_t* data,
    bcm_vlan_t* mask);

int __real_bcm_mirror_init(int unit);

int __real_bcm_mirror_mode_set(int unit, int mode);

int __real_bcm_mirror_destination_create(
    int unit,
    bcm_mirror_destination_t* mirror_dest);

int __real_bcm_mirror_destination_get(
    int unit,
    bcm_gport_t mirror_dest_id,
    bcm_mirror_destination_t* mirror_dest);

int __real_bcm_mirror_destination_destroy(int unit, bcm_gport_t mirror_dest_id);

int __real_bcm_mirror_port_dest_add(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id);

int __real_bcm_mirror_port_dest_delete(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id);

int __real_bcm_mirror_port_dest_delete_all(
    int unit,
    bcm_port_t port,
    uint32 flags);

int __real_bcm_mirror_port_dest_get(
    int unit,
    bcm_port_t port,
    uint32 flags,
    int mirror_dest_size,
    bcm_gport_t* mirror_dest,
    int* mirror_dest_count);

int __real_bcm_mirror_destination_traverse(
    int unit,
    bcm_mirror_destination_traverse_cb cb,
    void* user_data);

// mpls
int __real_bcm_mpls_init(int unit);

int __real_bcm_mpls_tunnel_switch_add(int unit, bcm_mpls_tunnel_switch_t* info);

int __real_bcm_mpls_tunnel_switch_delete(
    int unit,
    bcm_mpls_tunnel_switch_t* info);

int __real_bcm_mpls_tunnel_switch_get(int unit, bcm_mpls_tunnel_switch_t* info);

int __real_bcm_mpls_tunnel_switch_traverse(
    int unit,
    bcm_mpls_tunnel_switch_traverse_cb cb,
    void* user_data);

int __real_bcm_mpls_tunnel_initiator_set(
    int unit,
    bcm_if_t intf,
    int num_labels,
    bcm_mpls_egress_label_t* label_array);

int __real_bcm_mpls_tunnel_initiator_clear(int unit, bcm_if_t intf);

int __real_bcm_mpls_tunnel_initiator_get(
    int unit,
    bcm_if_t intf,
    int label_max,
    bcm_mpls_egress_label_t* label_array,
    int* label_count);

int __real_bcm_port_resource_speed_get(
    int unit,
    bcm_gport_t port,
    bcm_port_resource_t* resource);

int __real_bcm_port_resource_speed_set(
    int unit,
    bcm_gport_t port,
    bcm_port_resource_t* resource);

int __real_bcm_port_resource_multi_set(
    int unit,
    int nport,
    bcm_port_resource_t* resource);

int __real_bcm_l2_addr_delete_by_port(
    int unit,
    bcm_module_t mod,
    bcm_port_t port,
    uint32 flags);

int __real_bcm_l3_ingress_create(
    int unit,
    bcm_l3_ingress_t* ing_intf,
    bcm_if_t* intf_id);

int __real_bcm_l3_ingress_destroy(int unit, bcm_if_t intf_id);

int __real_bcm_vlan_control_vlan_set(
    int unit,
    bcm_vlan_t vlan,
    bcm_vlan_control_vlan_t control);

int __real_bcm_vlan_control_vlan_get(
    int unit,
    bcm_vlan_t vlan,
    bcm_vlan_control_vlan_t* control);

int __real_sh_process_command(int unit, char* cmd);

int __real_bcm_cosq_priority_group_mapping_profile_get(
    int unit,
    int profile_index,
    bcm_cosq_priority_group_mapping_profile_type_t type,
    int array_max,
    int* arg,
    int* array_count);

int __real_bcm_cosq_priority_group_mapping_profile_set(
    int unit,
    int profile_index,
    bcm_cosq_priority_group_mapping_profile_type_t type,
    int array_max,
    int* arg);

int __real_bcm_cosq_priority_group_pfc_priority_mapping_profile_get(
    int unit,
    int profile_index,
    int array_max,
    int* pg_array,
    int* array_count);

int __real_bcm_cosq_priority_group_pfc_priority_mapping_profile_set(
    int unit,
    int profile_index,
    int array_count,
    int* pg_array);

int __real_bcm_cosq_pfc_class_config_profile_set(
    int unit,
    int profile_index,
    int count,
    bcm_cosq_pfc_class_map_config_t* config_array);

int __real_bcm_cosq_pfc_class_config_profile_get(
    int unit,
    int profile_index,
    int max_count,
    bcm_cosq_pfc_class_map_config_t* config_array,
    int* count);

int __real_bcm_port_priority_group_config_set(
    int unit,
    bcm_gport_t gport,
    int priority_group,
    bcm_port_priority_group_config_t* prigrp_config);

int __real_bcm_port_priority_group_config_get(
    int unit,
    bcm_gport_t gport,
    int priority_group,
    bcm_port_priority_group_config_t* prigrp_config);

int __real_bcm_cosq_port_priority_group_property_get(
    int unit,
    bcm_port_t gport,
    int priority_group_id,
    bcm_cosq_port_prigroup_control_t type,
    int* arg);

int __real_bcm_cosq_port_priority_group_property_set(
    int unit,
    bcm_port_t gport,
    int priority_group_id,
    bcm_cosq_port_prigroup_control_t type,
    int arg);

int __real_bcm_collector_create(
    int unit,
    uint32 options,
    bcm_collector_t* collector_id,
    bcm_collector_info_t* collector_info);

int __real_bcm_collector_destroy(int unit, bcm_collector_t id);

int __real_bcm_collector_export_profile_create(
    int unit,
    uint32 options,
    int* export_profile_id,
    bcm_collector_export_profile_t* export_profile_info);

int __real_bcm_collector_export_profile_destroy(
    int unit,
    int export_profile_id);

void __real_bcm_collector_export_profile_t_init(
    bcm_collector_export_profile_t* export_profile_info);

void __real_bcm_collector_info_t_init(bcm_collector_info_t* collector_info);

int __real_bcm_port_phy_timesync_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_timesync_config_t* conf);

void __real_bcm_port_phy_timesync_config_t_init(
    bcm_port_phy_timesync_config_t* conf);

int __real_bcm_port_timesync_config_set(
    int unit,
    bcm_port_t port,
    int config_count,
    bcm_port_timesync_config_t* config_array);

void __real_bcm_port_timesync_config_t_init(
    bcm_port_timesync_config_t* port_timesync_config);

int __real_bcm_time_interface_add(int unit, bcm_time_interface_t* intf);

int __real_bcm_time_interface_delete_all(int unit);

void __real_bcm_time_interface_t_init(bcm_time_interface_t* intf);

int __real_bcm_field_entry_flexctr_attach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg);

int __real_bcm_field_entry_flexctr_detach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg);

int __real_bcm_field_entry_remove(int unit, bcm_field_entry_t entry);

int __real_bcm_cosq_pfc_deadlock_control_set(
    int unit,
    bcm_port_t port,
    int priority,
    bcm_cosq_pfc_deadlock_control_t type,
    int arg);

int __real_bcm_cosq_pfc_deadlock_control_get(
    int unit,
    bcm_port_t port,
    int priority,
    bcm_cosq_pfc_deadlock_control_t type,
    int* arg);

int __real_bcm_cosq_pfc_deadlock_recovery_event_register(
    int unit,
    bcm_cosq_pfc_deadlock_recovery_event_cb_t callback,
    void* userdata);

int __real_bcm_cosq_pfc_deadlock_recovery_event_unregister(
    int unit,
    bcm_cosq_pfc_deadlock_recovery_event_cb_t callback,
    void* userdata);

int __real_bcm_flexctr_action_create(
    int unit,
    int options,
    bcm_flexctr_action_t* action,
    uint32* stat_counter_id);

int __real_bcm_flexctr_action_destroy(int unit, uint32 stat_counter_id);

int __real_bcm_flexctr_action_traverse(
    int unit,
    bcm_flexctr_action_traverse_cb trav_fn,
    void* user_data);

int __real_bcm_l3_route_stat_attach(
    int unit,
    bcm_l3_route_t* info,
    uint32 stat_counter_id);

int __real_bcm_l3_route_stat_detach(int unit, bcm_l3_route_t* info);

int __real_bcm_l3_route_flexctr_object_set(
    int unit,
    bcm_l3_route_t* info,
    uint32 value);

int __real_bcm_stat_custom_group_create(
    int unit,
    uint32 mode_id,
    bcm_stat_object_t object,
    uint32* stat_counter_id,
    uint32* num_entries);

int __real_bcm_stat_group_destroy(int unit, uint32 stat_counter_id);

int __real_bcm_stat_group_create(
    int unit,
    bcm_stat_object_t object,
    bcm_stat_group_mode_t group_mode,
    uint32* stat_counter_id,
    uint32* num_entries);

int __real_bcm_l3_ingress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id);

int __real_bcm_l3_egress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id);

int __real_bcm_stat_group_mode_id_create(
    int unit,
    uint32 flags,
    uint32 total_counters,
    uint32 num_selectors,
    bcm_stat_group_mode_attr_selector_t* attr_selectors,
    uint32* mode_id);

int __real_bcm_stat_group_mode_id_destroy(int unit, uint32 mode_id);

int __real_bcm_port_ifg_get(
    int unit,
    bcm_port_t port,
    int speed,
    bcm_port_duplex_t duplex,
    int* bit_times);

int __real_bcm_port_ifg_set(
    int unit,
    bcm_port_t port,
    int speed,
    bcm_port_duplex_t duplex,
    int bit_times);

int __real_bcm_port_control_phy_timesync_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_phy_timesync_t type,
    uint64 value);

} // extern "C"

using namespace facebook::fboss;

extern "C" {

#define CALL_WRAPPERS_RV(func_call)                                         \
  if (facebook::fboss::SdkWrapSettings::getInstance()->sdkCallsBlocked()) { \
    return 0;                                                               \
  }                                                                         \
  {                                                                         \
    TIME_CALL;                                                              \
    auto rv = __real_##func_call;                                           \
    if (FLAGS_enable_bcm_cinter) {                                          \
      facebook::fboss::BcmCinter::getInstance()->func_call;                 \
    }                                                                       \
    return rv;                                                              \
  }

#define CALL_WRAPPERS_RV_CINTER_FIRST(func_call)                            \
  if (facebook::fboss::SdkWrapSettings::getInstance()->sdkCallsBlocked()) { \
    return 0;                                                               \
  }                                                                         \
  if (FLAGS_enable_bcm_cinter) {                                            \
    facebook::fboss::BcmCinter::getInstance()->func_call;                   \
  }                                                                         \
  return __real_##func_call;

#define CALL_WRAPPERS_NO_RV(func_call)                                      \
  if (facebook::fboss::SdkWrapSettings::getInstance()->sdkCallsBlocked()) { \
    return;                                                                 \
  }                                                                         \
  {                                                                         \
    TIME_CALL;                                                              \
    __real_##func_call;                                                     \
  }                                                                         \
  if (FLAGS_enable_bcm_cinter) {                                            \
    facebook::fboss::BcmCinter::getInstance()->func_call;                   \
  }

// Getters

int __wrap_bcm_switch_pkt_trace_info_get(
    int unit,
    uint32 options,
    uint8 port,
    int len,
    uint8* data,
    bcm_switch_pkt_trace_info_t* pkt_trace_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_switch_pkt_trace_info_get(
      unit, options, port, len, data, pkt_trace_info));
#else
  return 0;
#endif
}

int __wrap_bcm_field_range_create(
    int unit,
    bcm_field_range_t* range,
    uint32 flags,
    bcm_l4_port_t min,
    bcm_l4_port_t max) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_range_create(unit, range, flags, min, max));
#else
  return 0;
#endif
}

int __wrap_bcm_field_range_get(
    int unit,
    bcm_field_range_t range,
    uint32* flags,
    bcm_l4_port_t* min,
    bcm_l4_port_t* max) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_range_get(unit, range, flags, min, max));
#else
  return 0;
#endif
}

int __wrap_bcm_port_control_get(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int* value) {
  CALL_WRAPPERS_RV(bcm_port_control_get(unit, port, type, value));
}

int __wrap_bcm_port_phy_control_get(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32* value) {
  CALL_WRAPPERS_RV(bcm_port_phy_control_get(unit, port, type, value));
}

int __wrap_bcm_rx_active(int unit) {
  CALL_WRAPPERS_RV(bcm_rx_active(unit));
}

int __wrap_bcm_cosq_bst_stat_clear(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_bst_stat_clear(unit, gport, cosq, bid));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_size_get(int unit, int* size) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_size_get(unit, size));
#else
  return 0;
#endif
}

int __wrap_bcm_l2_traverse(
    int unit,
    bcm_l2_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_l2_traverse(unit, trav_fn, user_data));
}

int __wrap_bcm_port_subsidiary_ports_get(
    int unit,
    bcm_port_t port,
    bcm_pbmp_t* pbmp) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_subsidiary_ports_get(unit, port, pbmp));
#else
  return 0;
#endif
}

int __wrap_bcm_field_group_enable_get(
    int unit,
    bcm_field_group_t group,
    int* enable) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_enable_get(unit, group, enable));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_info(int unit, bcm_l3_info_t* l3info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_info(unit, l3info));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_bst_profile_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_bst_profile_get(unit, gport, cosq, bid, profile));
#else
  return 0;
#endif
}

int __wrap_bcm_switch_object_count_multi_get(
    int unit,
    int object_size,
    bcm_switch_object_t* object_array,
    int* entries) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_switch_object_count_multi_get(
      unit, object_size, object_array, entries));
#else
  return 0;
#endif
}

int __wrap_bcm_switch_object_count_get(
    int unit,
    bcm_switch_object_t object,
    int* entries) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_switch_object_count_get(unit, object, entries));
#else
  return 0;
#endif
}

int __wrap_bcm_port_pause_addr_set(int unit, bcm_port_t port, bcm_mac_t mac) {
  CALL_WRAPPERS_RV(bcm_port_pause_addr_set(unit, port, mac));
}

#if (BCM_SDK_VERSION >= BCM_VERSION(6, 5, 19))
int __wrap_bcm_l3_alpm_resource_get(
    int unit,
    bcm_l3_route_group_t grp,
    bcm_l3_alpm_resource_t* resource) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_alpm_resource_get(unit, grp, resource));
#else
  return 0;
#endif
}
#endif

int __wrap_bcm_field_entry_multi_get(
    int unit,
    bcm_field_group_t group,
    int entry_size,
    bcm_field_entry_t* entry_array,
    int* entry_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_multi_get(
      unit, group, entry_size, entry_array, entry_count));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_bst_stat_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_bst_stat_get(unit, gport, cosq, bid, options, value));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_bst_stat_extended_get(
    int unit,
    bcm_cosq_object_id_t* id,
    bcm_bst_stat_id_t bid,
    uint32 options,
    uint64* value) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_bst_stat_extended_get(unit, id, bid, options, value));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_hash_config_add(
    int unit,
    uint32 options,
    bcm_udf_hash_config_t* config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_hash_config_add(unit, options, config));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_hash_config_delete(int unit, bcm_udf_hash_config_t* config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_hash_config_delete(unit, config));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_create(
    int unit,
    bcm_udf_alloc_hints_t* hints,
    bcm_udf_t* udf_info,
    bcm_udf_id_t* udf_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_create(unit, hints, udf_info, udf_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_destroy(int unit, bcm_udf_id_t udf_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_destroy(unit, udf_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_create(
    int unit,
    bcm_udf_pkt_format_options_t options,
    bcm_udf_pkt_format_info_t* pkt_format,
    bcm_udf_pkt_format_id_t* pkt_format_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_udf_pkt_format_create(unit, options, pkt_format, pkt_format_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_destroy(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_pkt_format_destroy(unit, pkt_format_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_add(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_pkt_format_add(unit, udf_id, pkt_format_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_delete(
    int unit,
    bcm_udf_id_t udf_id,
    bcm_udf_pkt_format_id_t pkt_format_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_pkt_format_delete(unit, udf_id, pkt_format_id));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    int max,
    bcm_udf_id_t* udf_id_list,
    int* actual) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_udf_pkt_format_get(unit, pkt_format_id, max, udf_id_list, actual));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_hash_config_get(int unit, bcm_udf_hash_config_t* config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_hash_config_get(unit, config));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_pkt_format_info_get(
    int unit,
    bcm_udf_pkt_format_id_t pkt_format_id,
    bcm_udf_pkt_format_info_t* pkt_format) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_udf_pkt_format_info_get(unit, pkt_format_id, pkt_format));
#else
  return 0;
#endif
}

void __wrap_bcm_udf_pkt_format_info_t_init(
    bcm_udf_pkt_format_info_t* pkt_format) {
  CALL_WRAPPERS_NO_RV(bcm_udf_pkt_format_info_t_init(pkt_format));
}

void __wrap_bcm_udf_alloc_hints_t_init(bcm_udf_alloc_hints_t* udf_hints) {
  CALL_WRAPPERS_NO_RV(bcm_udf_alloc_hints_t_init(udf_hints));
}

void __wrap_bcm_udf_t_init(bcm_udf_t* udf_info) {
  CALL_WRAPPERS_NO_RV(bcm_udf_t_init(udf_info));
}

int __wrap_bcm_udf_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_udf_get(int unit, bcm_udf_id_t udf_id, bcm_udf_t* udf_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_udf_get(unit, udf_id, udf_info));
#else
  return 0;
#endif
}

void __wrap_bcm_udf_hash_config_t_init(bcm_udf_hash_config_t* config) {
  CALL_WRAPPERS_NO_RV(bcm_udf_hash_config_t_init(config));
}

int __wrap_bcm_field_qset_id_multi_set(
    int unit,
    bcm_field_qualify_t qualifier,
    int num_objects,
    int* object_list,
    bcm_field_qset_t* qset) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qset_id_multi_set(
      unit, qualifier, num_objects, object_list, qset));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_UdfClass(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_UdfClass(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_udf(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int length,
    uint8* data,
    uint8* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_field_qualify_udf(unit, eid, udf_id, length, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_udf_get(
    int unit,
    bcm_field_entry_t eid,
    bcm_udf_id_t udf_id,
    int max_length,
    uint8* data,
    uint8* mask,
    int* actual_length) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_udf_get(
      unit, eid, udf_id, max_length, data, mask, actual_length));
#else
  return 0;
#endif
}

void __wrap_bcm_field_group_config_t_init(
    bcm_field_group_config_t* group_config) {
  CALL_WRAPPERS_NO_RV(bcm_field_group_config_t_init(group_config));
}

int __wrap_bcm_stat_custom_add(
    int unit,
    bcm_port_t port,
    bcm_stat_val_t type,
    bcm_custom_stat_trigger_t trigger) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_custom_add(unit, port, type, trigger));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_get(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_size,
    bcm_if_t* intf_array,
    int* intf_count) {
  CALL_WRAPPERS_RV(
      bcm_l3_egress_ecmp_get(unit, ecmp, intf_size, intf_array, intf_count));
}

int __wrap_bcm_l3_enable_set(int unit, int enable) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_enable_set(unit, enable));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_queue_max_get(int unit, bcm_cos_queue_t* cosq) {
  CALL_WRAPPERS_RV(bcm_rx_queue_max_get(unit, cosq));
}

int __wrap_bcm_field_group_get(
    int unit,
    bcm_field_group_t group,
    bcm_field_qset_t* qset) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_get(unit, group, qset));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qset_id_multi_get(
    int unit,
    bcm_field_qset_t qset,
    bcm_field_qualify_t qualifier,
    int max_objects,
    int* object_list,
    int* count_objects) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qset_id_multi_get(
      unit, qset, qualifier, max_objects, object_list, count_objects));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_control_get(
    int unit,
    bcm_gport_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_control_t type,
    int* arg) {
  CALL_WRAPPERS_RV(bcm_cosq_control_get(unit, port, cosq, type, arg));
}

int __wrap_bcm_port_pause_get(
    int unit,
    bcm_port_t port,
    int* pause_tx,
    int* pause_rx) {
  CALL_WRAPPERS_RV(bcm_port_pause_get(unit, port, pause_tx, pause_rx));
}

int __wrap_bcm_port_sample_rate_get(
    int unit,
    bcm_port_t port,
    int* ingress_rate,
    int* egress_rate) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_port_sample_rate_get(unit, port, ingress_rate, egress_rate));
#else
  return 0;
#endif
}

int __wrap_bcm_field_action_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32* param0,
    uint32* param1) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_action_get(unit, entry, action, param0, param1));
#else
  return 0;
#endif
}

int __wrap_bcm_info_get(int unit, bcm_info_t* info) {
  CALL_WRAPPERS_RV(bcm_info_get(unit, info));
}

int __wrap_bcm_port_phy_tx_get(
    int unit,
    bcm_port_t port,
    bcm_port_phy_tx_t* tx) {
  CALL_WRAPPERS_RV(bcm_port_phy_tx_get(unit, port, tx));
}

int __wrap_bcm_port_phy_tx_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_tx_t* tx) {
  CALL_WRAPPERS_RV(bcm_port_phy_tx_set(unit, port, tx));
}

// Port setters
int __wrap_bcm_port_phy_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_control_t type,
    uint32 value) {
  CALL_WRAPPERS_RV(bcm_port_phy_control_set(unit, port, type, value));
}

int __wrap_bcm_port_ability_advert_set(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_ability_advert_set(unit, port, ability_mask));
#else
  return 0;
#endif
}

int __wrap_bcm_port_autoneg_set(int unit, bcm_port_t port, int autoneg) {
  CALL_WRAPPERS_RV(bcm_port_autoneg_set(unit, port, autoneg));
}

int __wrap_bcm_port_pause_sym_set(int unit, bcm_port_t port, int pause) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_pause_sym_set(unit, port, pause));
#else
  return 0;
#endif
}

int __wrap_bcm_port_pause_set(
    int unit,
    bcm_port_t port,
    int pause_tx,
    int pause_rx) {
  CALL_WRAPPERS_RV(bcm_port_pause_set(unit, port, pause_tx, pause_rx));
}

int __wrap_bcm_port_sample_rate_set(
    int unit,
    bcm_port_t port,
    int ingress_rate,
    int egress_rate) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_port_sample_rate_set(unit, port, ingress_rate, egress_rate));
#else
  return 0;
#endif
}

int __wrap_bcm_port_control_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_t type,
    int value) {
  CALL_WRAPPERS_RV(bcm_port_control_set(unit, port, type, value));
}

int __wrap_bcm_linkscan_update(int unit, bcm_pbmp_t pbmp) {
  CALL_WRAPPERS_RV(bcm_linkscan_update(unit, pbmp));
}

// Cos

int __wrap_bcm_cosq_bst_profile_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_bst_stat_id_t bid,
    bcm_cosq_bst_profile_t* profile) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_bst_profile_set(unit, gport, cosq, bid, profile));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_set(
    int unit,
    int index,
    bcm_rx_reasons_t reasons,
    bcm_rx_reasons_t reasons_mask,
    uint8 int_prio,
    uint8 int_prio_mask,
    uint32 packet_type,
    uint32 packet_type_mask,
    bcm_cos_queue_t cosq) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_set(
      unit,
      index,
      reasons,
      reasons_mask,
      int_prio,
      int_prio_mask,
      packet_type,
      packet_type_mask,
      cosq));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_get(
    int unit,
    int index,
    bcm_rx_reasons_t* reasons,
    bcm_rx_reasons_t* reasons_mask,
    uint8* int_prio,
    uint8* int_prio_mask,
    uint32* packet_type,
    uint32* packet_type_mask,
    bcm_cos_queue_t* cosq) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_get(
      unit,
      index,
      reasons,
      reasons_mask,
      int_prio,
      int_prio_mask,
      packet_type,
      packet_type_mask,
      cosq));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_delete(int unit, int index) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_delete(unit, index));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_extended_add(
    int unit,
    int options,
    bcm_rx_cosq_mapping_t* cosqMap) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_extended_add(unit, options, cosqMap));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_extended_delete(
    int unit,
    bcm_rx_cosq_mapping_t* cosqMap) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_extended_delete(unit, cosqMap));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_cosq_mapping_extended_set(
    int unit,
    uint32 options,
    bcm_rx_cosq_mapping_t* cosqMap) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_cosq_mapping_extended_set(unit, options, cosqMap));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_bst_stat_sync(int unit, bcm_bst_stat_id_t bid) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_bst_stat_sync(unit, bid));
#else
  return 0;
#endif
}

void __wrap_bcm_cosq_gport_discard_t_init(bcm_cosq_gport_discard_t* discard) {
  CALL_WRAPPERS_NO_RV(bcm_cosq_gport_discard_t_init(discard));
}

int __wrap_bcm_cosq_gport_discard_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_cosq_gport_discard_t* discard) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_discard_set(unit, gport, cosq, discard));
}

int __wrap_bcm_cosq_gport_mapping_set(
    int unit,
    bcm_gport_t ing_port,
    bcm_cos_t priority,
    uint32_t flags,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_gport_mapping_set(unit, ing_port, priority, flags, gport, cosq));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_gport_mapping_get(
    int unit,
    bcm_gport_t ing_port,
    bcm_cos_t priority,
    uint32_t flags,
    bcm_gport_t* gport,
    bcm_cos_queue_t* cosq) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_gport_mapping_get(unit, ing_port, priority, flags, gport, cosq));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_gport_discard_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    bcm_cosq_gport_discard_t* discard) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_discard_get(unit, gport, cosq, discard));
}

int __wrap_bcm_cosq_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_gport_traverse(
    int unit,
    bcm_cosq_gport_traverse_cb cb,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_traverse(unit, cb, user_data));
}

int __wrap_bcm_cosq_gport_sched_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    int mode,
    int weight) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_sched_set(unit, gport, cosq, mode, weight));
}

int __wrap_bcm_cosq_gport_sched_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    int* mode,
    int* weight) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_sched_get(unit, gport, cosq, mode, weight));
}

int __wrap_bcm_cosq_control_set(
    int unit,
    bcm_gport_t port,
    bcm_cos_queue_t cosq,
    bcm_cosq_control_t type,
    int arg) {
  CALL_WRAPPERS_RV(bcm_cosq_control_set(unit, port, cosq, type, arg));
}

int __wrap_bcm_cosq_port_profile_get(
    int unit,
    bcm_gport_t port,
    bcm_cosq_profile_type_t profile_type,
    int* profile_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_port_profile_get(unit, port, profile_type, profile_id));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_port_profile_set(
    int unit,
    bcm_gport_t port,
    bcm_cosq_profile_type_t profile_type,
    int profile_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_cosq_port_profile_set(unit, port, profile_type, profile_id));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_gport_bandwidth_set(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32 kbits_sec_min,
    uint32 kbits_sec_max,
    uint32 flags) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_bandwidth_set(
      unit, gport, cosq, kbits_sec_min, kbits_sec_max, flags));
}

int __wrap_bcm_cosq_gport_bandwidth_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32* kbits_sec_min,
    uint32* kbits_sec_max,
    uint32* flags) {
  CALL_WRAPPERS_RV(bcm_cosq_gport_bandwidth_get(
      unit, gport, cosq, kbits_sec_min, kbits_sec_max, flags));
}

// QoS
int __wrap_bcm_qos_map_create(int unit, uint32 flags, int* map_id) {
  CALL_WRAPPERS_RV(bcm_qos_map_create(unit, flags, map_id));
}

int __wrap_bcm_qos_map_destroy(int unit, int map_id) {
  CALL_WRAPPERS_RV(bcm_qos_map_destroy(unit, map_id));
}

int __wrap_bcm_qos_map_add(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id) {
  CALL_WRAPPERS_RV(bcm_qos_map_add(unit, flags, map, map_id));
}

int __wrap_bcm_qos_map_delete(
    int unit,
    uint32 flags,
    bcm_qos_map_t* map,
    int map_id) {
  CALL_WRAPPERS_RV(bcm_qos_map_delete(unit, flags, map, map_id));
}

int __wrap_bcm_qos_map_multi_get(
    int unit,
    uint32 flags,
    int map_id,
    int array_size,
    bcm_qos_map_t* array,
    int* array_count) {
  CALL_WRAPPERS_RV(bcm_qos_map_multi_get(
      unit, flags, map_id, array_size, array, array_count));
}

int __wrap_bcm_qos_multi_get(
    int unit,
    int array_size,
    int* map_ids_array,
    int* flags_array,
    int* array_count) {
  CALL_WRAPPERS_RV(bcm_qos_multi_get(
      unit, array_size, map_ids_array, flags_array, array_count));
}

int __wrap_bcm_qos_port_map_set(
    int unit,
    bcm_gport_t gport,
    int ing_map,
    int egr_map) {
  CALL_WRAPPERS_RV(bcm_qos_port_map_set(unit, gport, ing_map, egr_map));
}

int __wrap_bcm_qos_port_map_get(
    int unit,
    bcm_gport_t gport,
    int* ing_map,
    int* egr_map) {
  CALL_WRAPPERS_RV(bcm_qos_port_map_get(unit, gport, ing_map, egr_map));
}

int __wrap_bcm_qos_port_map_type_get(
    int unit,
    bcm_gport_t gport,
    uint32 flags,
    int* map_id) {
  CALL_WRAPPERS_RV(bcm_qos_port_map_type_get(unit, gport, flags, map_id));
}

int __wrap_bcm_port_dscp_map_mode_get(int unit, bcm_port_t port, int* mode) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_dscp_map_mode_get(unit, port, mode));
#else
  return 0;
#endif
}

int __wrap_bcm_port_dscp_map_mode_set(int unit, bcm_port_t port, int mode) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_dscp_map_mode_set(unit, port, mode));
#else
  return 0;
#endif
}

// FP

int __wrap_bcm_field_qualify_IpProtocol(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpProtocol(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstIp(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip_t data,
    bcm_ip_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstIp(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_create_id(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_create_id(unit, group, entry));
#else
  return 0;
#endif
}
int __wrap_bcm_field_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_field_group_create_id(
    int unit,
    bcm_field_qset_t qset,
    int pri,
    bcm_field_group_t group) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_create_id(unit, qset, pri, group));
#else
  return 0;
#endif
}

int __wrap_bcm_field_group_config_create(
    int unit,
    bcm_field_group_config_t* group_config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_config_create(unit, group_config));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_create(
    int unit,
    bcm_field_group_t group,
    bcm_field_entry_t* entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_create(unit, group, entry));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_install(int unit, bcm_field_entry_t entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_install(unit, entry));
#else
  return 0;
#endif
}

int __wrap_bcm_field_action_add(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_action_add(unit, entry, action, param0, param1));
#else
  return 0;
#endif
}

int __wrap_bcm_field_action_remove(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_action_remove(unit, entry, action));
#else
  return 0;
#endif
}

int __wrap_bcm_field_action_delete(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_action_t action,
    uint32 param0,
    uint32 param1) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_field_action_delete(unit, entry, action, param0, param1));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_prio_set(
    int unit,
    bcm_field_entry_t entry,
    int prio) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_prio_set(unit, entry, prio));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_enable_set(
    int unit,
    bcm_field_entry_t entry,
    int enable) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_enable_set(unit, entry, enable));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstIp6(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcIp6(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t data,
    bcm_ip6_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcIp6(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_InPorts(
    int unit,
    bcm_field_entry_t entry,
    bcm_pbmp_t data,
    bcm_pbmp_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_InPorts(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_L4DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_L4DstPort(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_L4SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t data,
    bcm_l4_port_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_L4SrcPort(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstMac(unit, entry, mac, macMask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcMac(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t mac,
    bcm_mac_t macMask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcMac(unit, entry, mac, macMask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IcmpTypeCode(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IcmpTypeCode(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcPort(
      unit, entry, data_modid, mask_modid, data_port, mask_port));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstPort(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t data_modid,
    bcm_module_t mask_modid,
    bcm_port_t data_port,
    bcm_port_t mask_port) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstPort(
      unit, entry, data_modid, mask_modid, data_port, mask_port));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_RangeCheck(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_range_t range,
    int invert) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_RangeCheck(unit, entry, range, invert));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_TcpControl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_TcpControl(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IpFrag(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpFrag_t frag_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpFrag(unit, entry, frag_info));
#else
  return 0;
#endif
}

int __wrap_bcm_field_range_destroy(int unit, bcm_field_range_t range) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_range_destroy(unit, range));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_destroy(int unit, bcm_field_entry_t entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_destroy(unit, entry));
#else
  return 0;
#endif
}

int __wrap_bcm_field_group_destroy(int unit, bcm_field_group_t group) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_destroy(unit, group));
#else
  return 0;
#endif
}

void __wrap_bcm_field_hint_t_init(bcm_field_hint_t* hint) {
  CALL_WRAPPERS_NO_RV(bcm_field_hint_t_init(hint));
}

int __wrap_bcm_field_hints_create(int unit, bcm_field_hintid_t* hint_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_hints_create(unit, hint_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_hints_add(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_hints_add(unit, hint_id, hint));
#else
  return 0;
#endif
}

int __wrap_bcm_field_hints_destroy(int unit, bcm_field_hintid_t hint_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_hints_destroy(unit, hint_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_hints_get(
    int unit,
    bcm_field_hintid_t hint_id,
    bcm_field_hint_t* hint) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_hints_get(unit, hint_id, hint));
#else
  return 0;
#endif
}

// Switch control
int __wrap_bcm_switch_control_set(
    int unit,
    bcm_switch_control_t type,
    int arg) {
  CALL_WRAPPERS_RV(bcm_switch_control_set(unit, type, arg));
}

int __wrap_bcm_switch_control_get(
    int unit,
    bcm_switch_control_t type,
    int* arg) {
  CALL_WRAPPERS_RV(bcm_switch_control_get(unit, type, arg));
}

// L3
int __wrap_bcm_l3_egress_get(int unit, bcm_if_t intf, bcm_l3_egress_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_egress_get(unit, intf, info));
}

int __wrap_bcm_l3_egress_create(
    int unit,
    uint32 flags,
    bcm_l3_egress_t* egr,
    bcm_if_t* if_id) {
  CALL_WRAPPERS_RV(bcm_l3_egress_create(unit, flags, egr, if_id));
}

int __wrap_bcm_l3_egress_find(int unit, bcm_l3_egress_t* egr, bcm_if_t* intf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_find(unit, egr, intf));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_traverse(
    int unit,
    bcm_l3_egress_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_l3_egress_traverse(unit, trav_fn, user_data));
}

// ECMP

int __wrap_bcm_l3_ecmp_member_add(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_ecmp_member_add(unit, ecmp_group_id, ecmp_member));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_add(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf) {
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_add(unit, ecmp, intf));
}

int __wrap_bcm_l3_ecmp_member_delete(
    int unit,
    bcm_if_t ecmp_group_id,
    bcm_l3_ecmp_member_t* ecmp_member) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_ecmp_member_delete(unit, ecmp_group_id, ecmp_member));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_delete(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    bcm_if_t intf) {
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_delete(unit, ecmp, intf));
}

// Trunks
int __wrap_bcm_trunk_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_trunk_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_trunk_create(int unit, uint32 flags, bcm_trunk_t* tid) {
  CALL_WRAPPERS_RV(bcm_trunk_create(unit, flags, tid));
}

int __wrap_bcm_trunk_destroy(int unit, bcm_trunk_t tid) {
  CALL_WRAPPERS_RV(bcm_trunk_destroy(unit, tid));
}

int __wrap_bcm_trunk_find(
    int unit,
    bcm_module_t modid,
    bcm_port_t port,
    bcm_trunk_t* trunk) {
  CALL_WRAPPERS_RV(bcm_trunk_find(unit, modid, port, trunk));
}

int __wrap_bcm_trunk_bitmap_expand(int unit, bcm_pbmp_t* pbmp_ptr) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_trunk_bitmap_expand(unit, pbmp_ptr));
#else
  return 0;
#endif
}

int __wrap_bcm_trunk_get(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* t_data,
    int member_max,
    bcm_trunk_member_t* member_array,
    int* member_count) {
  CALL_WRAPPERS_RV(
      bcm_trunk_get(unit, tid, t_data, member_max, member_array, member_count));
}

int __wrap_bcm_trunk_set(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_info_t* trunk_info,
    int member_count,
    bcm_trunk_member_t* member_array) {
  CALL_WRAPPERS_RV(
      bcm_trunk_set(unit, tid, trunk_info, member_count, member_array));
}

int __wrap_bcm_trunk_member_add(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member) {
  CALL_WRAPPERS_RV(bcm_trunk_member_add(unit, tid, member));
}

int __wrap_bcm_trunk_member_delete(
    int unit,
    bcm_trunk_t tid,
    bcm_trunk_member_t* member) {
  CALL_WRAPPERS_RV(bcm_trunk_member_delete(unit, tid, member));
}

// Port loopback mode
int __wrap_bcm_port_loopback_get(int unit, bcm_port_t port, uint32* value) {
  CALL_WRAPPERS_RV(bcm_port_loopback_get(unit, port, value));
}

int __wrap_bcm_port_loopback_set(int unit, bcm_port_t port, uint32 value) {
  CALL_WRAPPERS_RV(bcm_port_loopback_set(unit, port, value));
}

// Bcm
int __wrap_bcm_vlan_control_port_set(
    int unit,
    int port,
    bcm_vlan_control_port_t type,
    int arg) {
  CALL_WRAPPERS_RV(bcm_vlan_control_port_set(unit, port, type, arg));
}

int __wrap_bcm_stat_get(
    int unit,
    bcm_port_t port,
    bcm_stat_val_t type,
    uint64* value) {
  CALL_WRAPPERS_RV(bcm_stat_get(unit, port, type, value));
}

int __wrap_bcm_stat_sync_multi_get(
    int unit,
    bcm_port_t port,
    int nstat,
    bcm_stat_val_t* types,
    uint64* values) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_sync_multi_get(unit, port, nstat, types, values));
#else
  return 0;
#endif
}

int __wrap_bcm_linkscan_register(int unit, bcm_linkscan_handler_t f) {
  CALL_WRAPPERS_RV(bcm_linkscan_register(unit, f));
}

int __wrap_bcm_rx_register(
    int unit,
    const char* name,
    bcm_rx_cb_f callback,
    uint8 priority,
    void* cookie,
    uint32 flags) {
  CALL_WRAPPERS_RV(
      bcm_rx_register(unit, name, callback, priority, cookie, flags));
}

int __wrap_bcm_port_speed_get(int unit, bcm_port_t port, int* speed) {
  CALL_WRAPPERS_RV(bcm_port_speed_get(unit, port, speed));
}

int __wrap_bcm_stk_my_modid_get(int unit, int* my_modid) {
  CALL_WRAPPERS_RV(bcm_stk_my_modid_get(unit, my_modid));
}

int __wrap_bcm_stg_list_destroy(int unit, bcm_stg_t* list, int count) {
  CALL_WRAPPERS_RV(bcm_stg_list_destroy(unit, list, count));
}

int __wrap_bcm_rx_control_set(int unit, bcm_rx_control_t type, int arg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_control_set(unit, type, arg));
#else
  return 0;
#endif
}

int __wrap_bcm_stg_list(int unit, bcm_stg_t** list, int* count) {
  CALL_WRAPPERS_RV(bcm_stg_list(unit, list, count));
}

int __wrap_bcm_l3_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_init(unit));
#else
  return 0;
#endif
}

bcm_ip_t __wrap_bcm_ip_mask_create(int len) {
  CALL_WRAPPERS_RV(bcm_ip_mask_create(len));
}

int __wrap_bcm_port_dtag_mode_get(int unit, bcm_port_t port, int* mode) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_dtag_mode_get(unit, port, mode));
#else
  return 0;
#endif
}

int __wrap_bcm_port_ability_advert_get(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* ability_mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_ability_advert_get(unit, port, ability_mask));
#else
  return 0;
#endif
}

void __wrap_bcm_port_config_t_init(bcm_port_config_t* config) {
  CALL_WRAPPERS_NO_RV(bcm_port_config_t_init(config));
}

int __wrap_bcm_port_config_get(int unit, bcm_port_config_t* config) {
  CALL_WRAPPERS_RV(bcm_port_config_get(unit, config));
}

int __wrap_bcm_port_gport_get(int unit, bcm_port_t port, bcm_gport_t* gport) {
  CALL_WRAPPERS_RV(bcm_port_gport_get(unit, port, gport));
}

int __wrap_bcm_l3_route_delete_all(int unit, bcm_l3_route_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_route_delete_all(unit, info));
}

int __wrap_bcm_port_learn_get(int unit, bcm_port_t port, uint32* flags) {
  CALL_WRAPPERS_RV(bcm_port_learn_get(unit, port, flags));
}

int __wrap_bcm_port_queued_count_get(int unit, bcm_port_t port, uint32* count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_queued_count_get(unit, port, count));
#else
  return 0;
#endif
}

int __wrap_bcm_port_selective_set(
    int unit,
    bcm_port_t port,
    bcm_port_info_t* info) {
  CALL_WRAPPERS_RV(bcm_port_selective_set(unit, port, info));
}

int __wrap_bcm_rx_start(int unit, bcm_rx_cfg_t* cfg) {
  CALL_WRAPPERS_RV(bcm_rx_start(unit, cfg));
}

int __wrap_bcm_rx_free(int unit, void* pkt_data) {
  CALL_WRAPPERS_RV(bcm_rx_free(unit, pkt_data));
}

int __wrap_bcm_vlan_default_set(int unit, bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_vlan_default_set(unit, vid));
}

int __wrap_bcm_l3_route_max_ecmp_set(int unit, int max) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_max_ecmp_set(unit, max));
#else
  return 0;
#endif
}

int __wrap_bcm_vlan_destroy_all(int unit) {
  CALL_WRAPPERS_RV(bcm_vlan_destroy_all(unit));
}

int __wrap_bcm_detach(int unit) {
  if (!facebook::fboss::BcmCinter::getInstance()) {
    return 0;
  }
  CALL_WRAPPERS_RV(bcm_detach(unit));
}

int __wrap__bcm_shutdown(int unit) {
  if (!facebook::fboss::BcmCinter::getInstance()) {
  }
  CALL_WRAPPERS_RV(_bcm_shutdown(unit));
}

int __wrap_soc_shutdown(int unit) {
  if (!facebook::fboss::BcmCinter::getInstance()) {
  }
// TODO investigate why DNX builds don't include definition for soc_shutdown
#if BCM_VER_RELEASE < 26
  CALL_WRAPPERS_RV(soc_shutdown(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_intf_delete(int unit, bcm_l3_intf_t* intf) {
  CALL_WRAPPERS_RV(bcm_l3_intf_delete(unit, intf));
}

int __wrap_bcm_stg_vlan_add(int unit, bcm_stg_t stg, bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_stg_vlan_add(unit, stg, vid));
}

int __wrap_bcm_l3_egress_destroy(int unit, bcm_if_t intf) {
  CALL_WRAPPERS_RV(bcm_l3_egress_destroy(unit, intf));
}

int __wrap_bcm_linkscan_detach(int unit) {
  CALL_WRAPPERS_RV(bcm_linkscan_detach(unit));
}

int __wrap_bcm_l3_route_multipath_get(
    int unit,
    bcm_l3_route_t* the_route,
    bcm_l3_route_t* path_array,
    int max_path,
    int* path_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_multipath_get(
      unit, the_route, path_array, max_path, path_count));
#else
  return 0;
#endif
}

int __wrap_bcm_vlan_list(int unit, bcm_vlan_data_t** listp, int* countp) {
  CALL_WRAPPERS_RV(bcm_vlan_list(unit, listp, countp));
}

int __wrap_bcm_l3_route_delete_by_interface(int unit, bcm_l3_route_t* info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_delete_by_interface(unit, info));
#else
  return 0;
#endif
}

int __wrap_bcm_port_interface_set(
    int unit,
    bcm_port_t port,
    bcm_port_if_t intf) {
  CALL_WRAPPERS_RV(bcm_port_interface_set(unit, port, intf));
}

int __wrap_bcm_tx(int unit, bcm_pkt_t* tx_pkt, void* cookie) {
  // BcmTxPacket registers a callback which frees the packet and its data once
  // the SDK call is executed, so we call into BcmCinter->bcm_tx here first
  // since BcmCinter requires access to packet data in order to log it.
  {
    TIME_CALL;
    CALL_WRAPPERS_RV_CINTER_FIRST(bcm_tx(unit, tx_pkt, cookie));
  }
}

int __wrap_bcm_pktio_tx(int unit, bcm_pktio_pkt_t* tx_pkt) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  // The SDK will free the packet data once it executes bcm_pktio_tx, so
  // we call BcmCinter->bcm_pktio_tx here first since BcmCinter requires
  // access to packet data in order to log it.
  CALL_WRAPPERS_RV_CINTER_FIRST(bcm_pktio_tx(unit, tx_pkt));
#else
  return 0;
#endif
}

int __wrap_bcm_pktio_txpmd_stat_attach(int unit, uint32 counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_pktio_txpmd_stat_attach(unit, counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_pktio_txpmd_stat_detach(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_pktio_txpmd_stat_detach(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_stop(int unit, bcm_rx_cfg_t* cfg) {
  CALL_WRAPPERS_RV(bcm_rx_stop(unit, cfg));
}

char* __wrap_bcm_port_name(int unit, int port) {
  CALL_WRAPPERS_RV(bcm_port_name(unit, port));
}

int __wrap_bcm_l3_host_add(int unit, bcm_l3_host_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_host_add(unit, info));
}

int __wrap_bcm_l2_station_delete(int unit, int station_id) {
  CALL_WRAPPERS_RV(bcm_l2_station_delete(unit, station_id));
}

int __wrap_bcm_stg_destroy(int unit, bcm_stg_t stg) {
  CALL_WRAPPERS_RV(bcm_stg_destroy(unit, stg));
}

int __wrap_bcm_port_phy_modify(
    int unit,
    bcm_port_t port,
    uint32 flags,
    uint32 phy_reg_addr,
    uint32 phy_data,
    uint32 phy_mask) {
  CALL_WRAPPERS_RV(
      bcm_port_phy_modify(unit, port, flags, phy_reg_addr, phy_data, phy_mask));
}

int __wrap_bcm_port_speed_max(int unit, bcm_port_t port, int* speed) {
  CALL_WRAPPERS_RV(bcm_port_speed_max(unit, port, speed));
}

int __wrap_bcm_l2_addr_add(int unit, bcm_l2_addr_t* l2addr) {
  CALL_WRAPPERS_RV(bcm_l2_addr_add(unit, l2addr));
}

int __wrap_bcm_cosq_bst_stat_multi_get(
    int unit,
    bcm_gport_t gport,
    bcm_cos_queue_t cosq,
    uint32 options,
    int max_values,
    bcm_bst_stat_id_t* id_list,
    uint64* values) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_bst_stat_multi_get(
      unit, gport, cosq, options, max_values, id_list, values));
#else
  return 0;
#endif
}

int __wrap_bcm_rx_unregister(int unit, bcm_rx_cb_f callback, uint8 priority) {
  CALL_WRAPPERS_RV(bcm_rx_unregister(unit, callback, priority));
}

int __wrap_bcm_l3_intf_get(int unit, bcm_l3_intf_t* intf) {
  CALL_WRAPPERS_RV(bcm_l3_intf_get(unit, intf));
}

void __wrap_bcm_l3_egress_t_init(bcm_l3_egress_t* egr) {
  CALL_WRAPPERS_NO_RV(bcm_l3_egress_t_init(egr));
}

int __wrap_bcm_attach_check(int unit) {
  CALL_WRAPPERS_RV(bcm_attach_check(unit));
}

int __wrap_bcm_l3_egress_ecmp_traverse(
    int unit,
    bcm_l3_egress_ecmp_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_traverse(unit, trav_fn, user_data));
}

int __wrap_bcm_vlan_destroy(int unit, bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_vlan_destroy(unit, vid));
}

int __wrap_bcm_port_vlan_member_set(int unit, bcm_port_t port, uint32 flags) {
  CALL_WRAPPERS_RV(bcm_port_vlan_member_set(unit, port, flags));
}

int __wrap_bcm_linkscan_unregister(int unit, bcm_linkscan_handler_t f) {
  CALL_WRAPPERS_RV(bcm_linkscan_unregister(unit, f));
}

int __wrap_bcm_knet_filter_traverse(
    int unit,
    bcm_knet_filter_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_knet_filter_traverse(unit, trav_fn, user_data));
}

int __wrap_bcm_knet_netif_destroy(int unit, int netif_id) {
  CALL_WRAPPERS_RV(bcm_knet_netif_destroy(unit, netif_id));
}

int __wrap_bcm_stg_stp_set(
    int unit,
    bcm_stg_t stg,
    bcm_port_t port,
    int stp_state) {
  CALL_WRAPPERS_RV(bcm_stg_stp_set(unit, stg, port, stp_state));
}

int __wrap_bcm_rx_cfg_get(int unit, bcm_rx_cfg_t* cfg) {
  CALL_WRAPPERS_RV(bcm_rx_cfg_get(unit, cfg));
}

int __wrap_bcm_vlan_port_remove(int unit, bcm_vlan_t vid, bcm_pbmp_t pbmp) {
  CALL_WRAPPERS_RV(bcm_vlan_port_remove(unit, vid, pbmp));
}

int __wrap_bcm_l3_intf_find_vlan(int unit, bcm_l3_intf_t* intf) {
  CALL_WRAPPERS_RV(bcm_l3_intf_find_vlan(unit, intf));
}

int __wrap_bcm_l3_egress_multipath_destroy(int unit, bcm_if_t mpintf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_destroy(unit, mpintf));
#else
  return 0;
#endif
}

int __wrap_bcm_knet_netif_create(int unit, bcm_knet_netif_t* netif) {
  CALL_WRAPPERS_RV(bcm_knet_netif_create(unit, netif));
}

void __wrap_bcm_knet_filter_t_init(bcm_knet_filter_t* filter) {
  CALL_WRAPPERS_NO_RV(bcm_knet_filter_t_init(filter));
}

int __wrap_bcm_switch_event_unregister(
    int unit,
    bcm_switch_event_cb_t cb,
    void* userdata) {
  CALL_WRAPPERS_RV(bcm_switch_event_unregister(unit, cb, userdata));
}

int __wrap_bcm_port_link_status_get(int unit, bcm_port_t port, int* status) {
  CALL_WRAPPERS_RV(bcm_port_link_status_get(unit, port, status));
}

int __wrap_bcm_port_untagged_vlan_get(
    int unit,
    bcm_port_t port,
    bcm_vlan_t* vid_ptr) {
  CALL_WRAPPERS_RV(bcm_port_untagged_vlan_get(unit, port, vid_ptr));
}

void __wrap_bcm_knet_netif_t_init(bcm_knet_netif_t* netif) {
  CALL_WRAPPERS_NO_RV(bcm_knet_netif_t_init(netif))
}

int __wrap_bcm_port_stat_enable_set(int unit, bcm_gport_t port, int enable) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_stat_enable_set(unit, port, enable));
#else
  return 0;
#endif
}

int __wrap_bcm_port_stat_attach(int unit, bcm_port_t port, uint32 counterID_) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_stat_attach(unit, port, counterID_));
#else
  return 0;
#endif
}

int __wrap_bcm_port_stat_detach_with_id(
    int unit,
    bcm_gport_t gPort,
    uint32 counterID) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_stat_detach_with_id(unit, gPort, counterID));
#else
  return 0;
#endif
}

#if (BCM_SDK_VERSION >= BCM_VERSION(6, 5, 21))
int __wrap_bcm_port_fdr_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_config_t* fdr_config) {
  CALL_WRAPPERS_RV(bcm_port_fdr_config_set(unit, port, fdr_config));
}

int __wrap_bcm_port_fdr_config_get(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_config_t* fdr_config) {
  CALL_WRAPPERS_RV(bcm_port_fdr_config_get(unit, port, fdr_config));
}

int __wrap_bcm_port_fdr_stats_get(
    int unit,
    bcm_port_t port,
    bcm_port_fdr_stats_t* fdr_stats) {
  CALL_WRAPPERS_RV(bcm_port_fdr_stats_get(unit, port, fdr_stats));
}
#endif

int __wrap_bcm_stat_multi_get(
    int unit,
    bcm_port_t port,
    int nstat,
    bcm_stat_val_t* stat_arr,
    uint64* value_arr) {
  CALL_WRAPPERS_RV(bcm_stat_multi_get(unit, port, nstat, stat_arr, value_arr));
}

int __wrap_bcm_port_speed_set(int unit, bcm_port_t port, int speed) {
  CALL_WRAPPERS_RV(bcm_port_speed_set(unit, port, speed));
}
int __wrap_bcm_field_group_traverse(
    int unit,
    bcm_field_group_traverse_cb callback,
    void* user_data) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_traverse(unit, callback, user_data));
#else
  return 0;
#endif
}

int __wrap_bcm_stg_default_set(int unit, bcm_stg_t stg) {
  CALL_WRAPPERS_RV(bcm_stg_default_set(unit, stg));
}

int __wrap_bcm_stg_stp_get(
    int unit,
    bcm_stg_t stg,
    bcm_port_t port,
    int* stp_state) {
  CALL_WRAPPERS_RV(bcm_stg_stp_get(unit, stg, port, stp_state));
}

int __wrap_bcm_attach_max(int* max_units) {
  CALL_WRAPPERS_RV(bcm_attach_max(max_units));
}

int __wrap_bcm_l3_host_delete(int unit, bcm_l3_host_t* ip_addr) {
  CALL_WRAPPERS_RV(bcm_l3_host_delete(unit, ip_addr));
}

int __wrap_bcm_knet_filter_create(int unit, bcm_knet_filter_t* filter) {
  CALL_WRAPPERS_RV(bcm_knet_filter_create(unit, filter));
}

int __wrap_bcm_stg_create(int unit, bcm_stg_t* stg_ptr) {
  CALL_WRAPPERS_RV(bcm_stg_create(unit, stg_ptr));
}

void __wrap_bcm_l3_egress_ecmp_t_init(bcm_l3_egress_ecmp_t* ecmp) {
  CALL_WRAPPERS_NO_RV(bcm_l3_egress_ecmp_t_init(ecmp));
}

int __wrap_bcm_linkscan_mode_get(int unit, bcm_port_t port, int* mode) {
  CALL_WRAPPERS_RV(bcm_linkscan_mode_get(unit, port, mode));
}

int __wrap_bcm_switch_control_port_set(
    int unit,
    bcm_port_t port,
    bcm_switch_control_t type,
    int arg) {
  CALL_WRAPPERS_RV(bcm_switch_control_port_set(unit, port, type, arg));
}

int __wrap_bcm_linkscan_mode_set_pbm(int unit, bcm_pbmp_t pbm, int mode) {
  CALL_WRAPPERS_RV(bcm_linkscan_mode_set_pbm(unit, pbm, mode));
}

int __wrap_bcm_l2_station_get(
    int unit,
    int station_id,
    bcm_l2_station_t* station) {
  CALL_WRAPPERS_RV(bcm_l2_station_get(unit, station_id, station));
}

int __wrap_bcm_port_enable_get(int unit, bcm_port_t port, int* enable) {
  CALL_WRAPPERS_RV(bcm_port_enable_get(unit, port, enable));
}

int __wrap_bcm_l3_route_max_ecmp_get(int unit, int* max) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_max_ecmp_get(unit, max));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_multipath_find(
    int unit,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_if_t* mpintf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_l3_egress_multipath_find(unit, intf_count, intf_array, mpintf));
#else
  return 0;
#endif
}

int __wrap_bcm_port_local_get(
    int unit,
    bcm_gport_t gport,
    bcm_port_t* local_port) {
  CALL_WRAPPERS_RV(bcm_port_local_get(unit, gport, local_port));
}

int __wrap_bcm_stg_default_get(int unit, bcm_stg_t* stg_ptr) {
  CALL_WRAPPERS_RV(bcm_stg_default_get(unit, stg_ptr));
}

int __wrap_bcm_l2_station_add(
    int unit,
    int* station_id,
    bcm_l2_station_t* station) {
  CALL_WRAPPERS_RV(bcm_l2_station_add(unit, station_id, station));
}

int __wrap_bcm_vlan_create(int unit, bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_vlan_create(unit, vid));
}

int __wrap_bcm_cosq_mapping_set(
    int unit,
    bcm_cos_t priority,
    bcm_cos_queue_t cosq) {
  CALL_WRAPPERS_RV(bcm_cosq_mapping_set(unit, priority, cosq));
}

int __wrap_bcm_l3_host_delete_all(int unit, bcm_l3_host_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_host_delete_all(unit, info));
}

int __wrap_bcm_l3_ecmp_create(
    int unit,
    uint32 options,
    bcm_l3_egress_ecmp_t* ecmp_info,
    int ecmp_member_count,
    bcm_l3_ecmp_member_t* ecmp_member_array) {
  CALL_WRAPPERS_RV(bcm_l3_ecmp_create(
      unit, options, ecmp_info, ecmp_member_count, ecmp_member_array));
}

int __wrap_bcm_l3_egress_ecmp_create(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp,
    int intf_count,
    bcm_if_t* intf_array) {
  CALL_WRAPPERS_RV(
      bcm_l3_egress_ecmp_create(unit, ecmp, intf_count, intf_array));
}

void __wrap_bcm_l3_ecmp_dlb_port_quality_attr_t_init(
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr) {
  CALL_WRAPPERS_NO_RV(bcm_l3_ecmp_dlb_port_quality_attr_t_init(quality_attr));
}

int __wrap_bcm_l3_ecmp_dlb_port_quality_attr_set(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_l3_ecmp_dlb_port_quality_attr_set(unit, port, quality_attr));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_ecmp_dlb_port_quality_attr_get(
    int unit,
    bcm_port_t port,
    bcm_l3_ecmp_dlb_port_quality_attr_t* quality_attr) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_l3_ecmp_dlb_port_quality_attr_get(unit, port, quality_attr));
#else
  return 0;
#endif
}

int __wrap_bcm_attach(int unit, char* type, char* subtype, int remunit) {
  CALL_WRAPPERS_RV(bcm_attach(unit, type, subtype, remunit));
}

int __wrap_bcm_port_vlan_member_get(int unit, bcm_port_t port, uint32* flags) {
  CALL_WRAPPERS_RV(bcm_port_vlan_member_get(unit, port, flags));
}

int __wrap_bcm_l3_egress_multipath_get(
    int unit,
    bcm_if_t mpintf,
    int intf_size,
    bcm_if_t* intf_array,
    int* intf_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_get(
      unit, mpintf, intf_size, intf_array, intf_count));
#else
  return 0;
#endif
}

int __wrap_bcm_ip6_mask_create(bcm_ip6_t ip6, int len) {
  CALL_WRAPPERS_RV(bcm_ip6_mask_create(ip6, len));
}

int __wrap_bcm_port_learn_set(int unit, bcm_port_t port, uint32 flags) {
  CALL_WRAPPERS_RV(bcm_port_learn_set(unit, port, flags));
}

int __wrap_bcm_vlan_gport_delete_all(int unit, bcm_vlan_t vlan) {
  CALL_WRAPPERS_RV(bcm_vlan_gport_delete_all(unit, vlan));
}

int __wrap_bcm_l3_intf_find(int unit, bcm_l3_intf_t* intf) {
  CALL_WRAPPERS_RV(bcm_l3_intf_find(unit, intf));
}

int __wrap_bcm_l3_route_delete(int unit, bcm_l3_route_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_route_delete(unit, info));
}

int __wrap_bcm_linkscan_enable_get(int unit, int* us) {
  CALL_WRAPPERS_RV(bcm_linkscan_enable_get(unit, us));
}

int __wrap_bcm_port_dtag_mode_set(int unit, bcm_port_t port, int mode) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_dtag_mode_set(unit, port, mode));
#else
  return 0;
#endif
}

int __wrap_bcm_knet_filter_destroy(int unit, int filter_id) {
  CALL_WRAPPERS_RV(bcm_knet_filter_destroy(unit, filter_id));
}

int __wrap_bcm_l3_host_traverse(
    int unit,
    uint32 flags,
    uint32 start,
    uint32 end,
    bcm_l3_host_traverse_cb cb,
    void* user_data) {
  CALL_WRAPPERS_RV(
      bcm_l3_host_traverse(unit, flags, start, end, cb, user_data));
}

int __wrap_bcm_pkt_alloc(
    int unit,
    int size,
    uint32 flags,
    bcm_pkt_t** pkt_buf) {
  CALL_WRAPPERS_RV(bcm_pkt_alloc(unit, size, flags, pkt_buf));
}

int __wrap_bcm_linkscan_enable_set(int unit, int us) {
  CALL_WRAPPERS_RV(bcm_linkscan_enable_set(unit, us));
}

int __wrap_bcm_l3_egress_multipath_add(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_add(unit, mpintf, intf));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_ecmp_get(
    int unit,
    bcm_l3_egress_ecmp_t* ecmp_info,
    int ecmp_member_size,
    bcm_l3_ecmp_member_t* ecmp_member_array,
    int* ecmp_member_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_ecmp_get(
      unit, ecmp_info, ecmp_member_size, ecmp_member_array, ecmp_member_count));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_find(
    int unit,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_l3_egress_ecmp_t* ecmp) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_find(unit, intf_count, intf_array, ecmp));
#else
  return 0;
#endif
}

int __wrap_bcm_vlan_port_add(
    int unit,
    bcm_vlan_t vid,
    bcm_pbmp_t pbmp,
    bcm_pbmp_t ubmp) {
  CALL_WRAPPERS_RV(bcm_vlan_port_add(unit, vid, pbmp, ubmp));
}

int __wrap_bcm_port_ability_local_get(
    int unit,
    bcm_port_t port,
    bcm_port_ability_t* local_ability_mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_ability_local_get(unit, port, local_ability_mask));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_host_delete_by_interface(int unit, bcm_l3_host_t* info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_host_delete_by_interface(unit, info));
#else
  return 0;
#endif
}

int __wrap_bcm_port_untagged_vlan_set(
    int unit,
    bcm_port_t port,
    bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_port_untagged_vlan_set(unit, port, vid));
}

int __wrap_bcm_stat_clear(int unit, bcm_port_t port) {
  CALL_WRAPPERS_RV(bcm_stat_clear(unit, port));
}

int __wrap_bcm_l2_age_timer_set(int unit, int age_seconds) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l2_age_timer_set(unit, age_seconds));
#else
  return 0;
#endif
}

int __wrap_bcm_knet_netif_traverse(
    int unit,
    bcm_knet_netif_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_knet_netif_traverse(unit, trav_fn, user_data));
}

int __wrap_bcm_port_enable_set(int unit, bcm_port_t port, int enable) {
  CALL_WRAPPERS_RV(bcm_port_enable_set(unit, port, enable));
}

int __wrap_bcm_l3_ecmp_destroy(int unit, bcm_if_t ecmp_group_id) {
  CALL_WRAPPERS_RV(bcm_l3_ecmp_destroy(unit, ecmp_group_id));
}

int __wrap_bcm_l3_egress_ecmp_destroy(int unit, bcm_l3_egress_ecmp_t* ecmp) {
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_destroy(unit, ecmp));
}

int __wrap_bcm_port_interface_get(
    int unit,
    bcm_port_t port,
    bcm_port_if_t* intf) {
  CALL_WRAPPERS_RV(bcm_port_interface_get(unit, port, intf));
}

int __wrap_bcm_knet_init(int unit) {
  CALL_WRAPPERS_RV(bcm_knet_init(unit));
}

int __wrap_bcm_l3_egress_multipath_delete(
    int unit,
    bcm_if_t mpintf,
    bcm_if_t intf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_delete(unit, mpintf, intf));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_route_add(int unit, bcm_l3_route_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_route_add(unit, info));
}

int __wrap_bcm_l3_route_traverse(
    int unit,
    uint32 flags,
    uint32 start,
    uint32 end,
    bcm_l3_route_traverse_cb trav_fn,
    void* user_data) {
  CALL_WRAPPERS_RV(
      bcm_l3_route_traverse(unit, flags, start, end, trav_fn, user_data));
}

int __wrap_bcm_vlan_list_destroy(int unit, bcm_vlan_data_t* list, int count) {
  CALL_WRAPPERS_RV(bcm_vlan_list_destroy(unit, list, count));
}

int __wrap_bcm_l3_egress_ecmp_ethertype_set(
    int unit,
    uint32 flags,
    int ethertype_count,
    int* ethertype_array) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_ethertype_set(
      unit, flags, ethertype_count, ethertype_array));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_ethertype_get(
    int unit,
    uint32* flags,
    int ethertype_max,
    int* ethertype_array,
    int* ethertype_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_ethertype_get(
      unit, flags, ethertype_max, ethertype_array, ethertype_count));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_member_status_set(
    int unit,
    bcm_if_t intf,
    int status) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_member_status_set(unit, intf, status));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_ecmp_member_status_get(
    int unit,
    bcm_if_t intf,
    int* status) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_ecmp_member_status_get(unit, intf, status));
#else
  return 0;
#endif
}

int __wrap_bcm_l2_addr_get(
    int unit,
    bcm_mac_t mac_addr,
    bcm_vlan_t vid,
    bcm_l2_addr_t* l2addr) {
  CALL_WRAPPERS_RV(bcm_l2_addr_get(unit, mac_addr, vid, l2addr));
}

int __wrap_bcm_l3_egress_multipath_traverse(
    int unit,
    bcm_l3_egress_multipath_traverse_cb trav_fn,
    void* user_data) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_traverse(unit, trav_fn, user_data));
#else
  return 0;
#endif
}

int __wrap_bcm_l2_addr_delete(int unit, bcm_mac_t mac, bcm_vlan_t vid) {
  CALL_WRAPPERS_RV(bcm_l2_addr_delete(unit, mac, vid));
}

int __wrap_bcm_pkt_flags_init(int unit, bcm_pkt_t* pkt, uint32 init_flags) {
  CALL_WRAPPERS_RV(bcm_pkt_flags_init(unit, pkt, init_flags));
}

int __wrap_bcm_pkt_free(int unit, bcm_pkt_t* pkt) {
  CALL_WRAPPERS_RV(bcm_pkt_free(unit, pkt));
}

int __wrap_bcm_l3_route_get(int unit, bcm_l3_route_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_route_get(unit, info));
}

int __wrap_bcm_l3_egress_multipath_create(
    int unit,
    uint32 flags,
    int intf_count,
    bcm_if_t* intf_array,
    bcm_if_t* mpintf) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_multipath_create(
      unit, flags, intf_count, intf_array, mpintf));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_mapping_get(
    int unit,
    bcm_cos_t priority,
    bcm_cos_queue_t* cosq) {
  CALL_WRAPPERS_RV(bcm_cosq_mapping_get(unit, priority, cosq));
}

int __wrap_bcm_switch_event_register(
    int unit,
    bcm_switch_event_cb_t cb,
    void* userdata) {
  CALL_WRAPPERS_RV(bcm_switch_event_register(unit, cb, userdata));
}

int __wrap_bcm_port_frame_max_get(int unit, bcm_port_t port, int* size) {
  CALL_WRAPPERS_RV(bcm_port_frame_max_get(unit, port, size));
}

int __wrap_bcm_linkscan_mode_set(int unit, bcm_port_t port, int mode) {
  CALL_WRAPPERS_RV(bcm_linkscan_mode_set(unit, port, mode));
}

int __wrap_bcm_l3_intf_create(int unit, bcm_l3_intf_t* intf) {
  CALL_WRAPPERS_RV(bcm_l3_intf_create(unit, intf));
}

int __wrap_bcm_rx_control_get(int unit, bcm_rx_control_t type, int* arg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_rx_control_get(unit, type, arg));
#else
  return 0;
#endif
}

int __wrap_bcm_l2_age_timer_get(int unit, int* age_seconds) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l2_age_timer_get(unit, age_seconds));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_host_find(int unit, bcm_l3_host_t* info) {
  CALL_WRAPPERS_RV(bcm_l3_host_find(unit, info));
}

int __wrap_bcm_switch_control_port_get(
    int unit,
    bcm_port_t port,
    bcm_switch_control_t type,
    int* arg) {
  CALL_WRAPPERS_RV(bcm_switch_control_port_get(unit, port, type, arg));
}

int __wrap_bcm_port_selective_get(
    int unit,
    bcm_port_t port,
    bcm_port_info_t* info) {
  CALL_WRAPPERS_RV(bcm_port_selective_get(unit, port, info));
}

int __wrap_bcm_port_frame_max_set(int unit, bcm_port_t port, int size) {
  CALL_WRAPPERS_RV(bcm_port_frame_max_set(unit, port, size));
}

int __wrap_bcm_vlan_default_get(int unit, bcm_vlan_t* vid_ptr) {
  CALL_WRAPPERS_RV(bcm_vlan_default_get(unit, vid_ptr));
}

// ACLS

int __wrap_bcm_field_group_status_get(
    int unit,
    bcm_field_group_t group,
    bcm_field_group_status_t* status) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_group_status_get(unit, group, status));
#else
  return 0;
#endif
}

int __wrap_bcm_field_stat_create(
    int unit,
    bcm_field_group_t group,
    int nstat,
    bcm_field_stat_t* stat_arr,
    int* stat_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_field_stat_create(unit, group, nstat, stat_arr, stat_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_stat_attach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_stat_attach(unit, entry, stat_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_stat_detach(
    int unit,
    bcm_field_entry_t entry,
    int stat_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_stat_detach(unit, entry, stat_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_stat_get(
    int unit,
    bcm_field_entry_t entry,
    int* stat_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_stat_get(unit, entry, stat_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_stat_destroy(int unit, int stat_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_stat_destroy(unit, stat_id));
#else
  return 0;
#endif
}

int __wrap_bcm_field_stat_get(
    int unit,
    int stat_id,
    bcm_field_stat_t stat,
    uint64* value) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_stat_get(unit, stat_id, stat, value));
#else
  return 0;
#endif
}

int __wrap_bcm_field_stat_size(int unit, int stat_id, int* stat_size) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_stat_size(unit, stat_id, stat_size));
#else
  return 0;
#endif
}

int __wrap_bcm_field_stat_config_get(
    int unit,
    int stat_id,
    int nstat,
    bcm_field_stat_t* stat_arr) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_stat_config_get(unit, stat_id, nstat, stat_arr));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_reinstall(int unit, bcm_field_entry_t entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_reinstall(unit, entry));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_RangeCheck_get(
    int unit,
    bcm_field_entry_t entry,
    int max_count,
    bcm_field_range_t* range,
    int* invert,
    int* count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_RangeCheck_get(
      unit, entry, max_count, range, invert, count));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_prio_get(
    int unit,
    bcm_field_entry_t entry,
    int* prio) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_prio_get(unit, entry, prio));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcIp6_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t* data,
    bcm_ip6_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcIp6_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstIp6_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_ip6_t* data,
    bcm_ip6_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstIp6_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_L4SrcPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t* data,
    bcm_l4_port_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_L4SrcPort_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_L4DstPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_l4_port_t* data,
    bcm_l4_port_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_L4DstPort_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_TcpControl_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_TcpControl_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t* data_modid,
    bcm_module_t* mask_modid,
    bcm_port_t* data_port,
    bcm_port_t* mask_port) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcPort_get(
      unit, entry, data_modid, mask_modid, data_port, mask_port));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstPort_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_module_t* data_modid,
    bcm_module_t* mask_modid,
    bcm_port_t* data_port,
    bcm_port_t* mask_port) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstPort_get(
      unit, entry, data_modid, mask_modid, data_port, mask_port));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IpFrag_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpFrag_t* frag_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpFrag_get(unit, entry, frag_info));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IpProtocol_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpProtocol_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DSCP_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DSCP_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IpType_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpType_t* type) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpType_get(unit, entry, type));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_EtherType_get(
    int unit,
    bcm_field_entry_t entry,
    uint16* data,
    uint16* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_EtherType_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_Ttl_get(
    int unit,
    bcm_field_entry_t entry,
    uint8* data,
    uint8* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_Ttl_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstMac_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t* data,
    bcm_mac_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstMac_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_SrcMac_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_mac_t* data,
    bcm_mac_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_SrcMac_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_enable_get(
    int unit,
    bcm_field_entry_t entry,
    int* enable_flag) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_enable_get(unit, entry, enable_flag));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_Ttl(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_Ttl(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_IpType(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_IpType_t type) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_IpType(unit, entry, type));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_EtherType(
    int unit,
    bcm_field_entry_t entry,
    uint16 data,
    uint16 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_EtherType(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DSCP(
    int unit,
    bcm_field_entry_t entry,
    uint8 data,
    uint8 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DSCP(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstClassL2_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstClassL2_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstClassL3_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstClassL3_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_PacketRes_get(
    int unit,
    bcm_field_entry_t entry,
    uint32* data,
    uint32* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_PacketRes_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_PacketRes(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_PacketRes(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstClassL2(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstClassL2(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_DstClassL3(
    int unit,
    bcm_field_entry_t entry,
    uint32 data,
    uint32 mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_DstClassL3(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_OuterVlanId(
    int unit,
    bcm_field_entry_t entry,
    bcm_vlan_t data,
    bcm_vlan_t mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_OuterVlanId(unit, entry, data, mask));
#else
  return 0;
#endif
}

int __wrap_bcm_field_qualify_OuterVlanId_get(
    int unit,
    bcm_field_entry_t entry,
    bcm_vlan_t* data,
    bcm_vlan_t* mask) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_qualify_OuterVlanId_get(unit, entry, data, mask));
#else
  return 0;
#endif
}

// MIRRORS

int __wrap_bcm_mirror_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_mirror_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_mirror_mode_set(int unit, int mode) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_mirror_mode_set(unit, mode));
#else
  return 0;
#endif
}

int __wrap_bcm_mirror_destination_create(
    int unit,
    bcm_mirror_destination_t* mirror_dest) {
  CALL_WRAPPERS_RV(bcm_mirror_destination_create(unit, mirror_dest));
}

int __wrap_bcm_mirror_destination_get(
    int unit,
    bcm_gport_t mirror_dest_id,
    bcm_mirror_destination_t* mirror_dest) {
  CALL_WRAPPERS_RV(
      bcm_mirror_destination_get(unit, mirror_dest_id, mirror_dest));
}

int __wrap_bcm_mirror_destination_destroy(
    int unit,
    bcm_gport_t mirror_dest_id) {
  CALL_WRAPPERS_RV(bcm_mirror_destination_destroy(unit, mirror_dest_id));
}

int __wrap_bcm_mirror_port_dest_add(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id) {
  CALL_WRAPPERS_RV(bcm_mirror_port_dest_add(unit, port, flags, mirror_dest_id));
}

int __wrap_bcm_mirror_port_dest_delete(
    int unit,
    bcm_port_t port,
    uint32 flags,
    bcm_gport_t mirror_dest_id) {
  CALL_WRAPPERS_RV(
      bcm_mirror_port_dest_delete(unit, port, flags, mirror_dest_id));
}

int __wrap_bcm_mirror_port_dest_delete_all(
    int unit,
    bcm_port_t port,
    uint32 flags) {
  CALL_WRAPPERS_RV(bcm_mirror_port_dest_delete_all(unit, port, flags));
}

int __wrap_bcm_mirror_port_dest_get(
    int unit,
    bcm_port_t port,
    uint32 flags,
    int mirror_dest_size,
    bcm_gport_t* mirror_dest,
    int* mirror_dest_count) {
  CALL_WRAPPERS_RV(bcm_mirror_port_dest_get(
      unit, port, flags, mirror_dest_size, mirror_dest, mirror_dest_count));
}

int __wrap_bcm_mirror_destination_traverse(
    int unit,
    bcm_mirror_destination_traverse_cb cb,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_mirror_destination_traverse(unit, cb, user_data));
}

// MPLS
int __wrap_bcm_mpls_init(int unit) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_mpls_init(unit));
#else
  return 0;
#endif
}

int __wrap_bcm_mpls_tunnel_switch_add(
    int unit,
    bcm_mpls_tunnel_switch_t* info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_switch_add(unit, info));
#else
  return 0;
#endif
}

int __wrap_bcm_mpls_tunnel_switch_delete(
    int unit,
    bcm_mpls_tunnel_switch_t* info) {
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_switch_delete(unit, info));
}

int __wrap_bcm_mpls_tunnel_switch_get(
    int unit,
    bcm_mpls_tunnel_switch_t* info) {
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_switch_get(unit, info));
}

int __wrap_bcm_mpls_tunnel_switch_traverse(
    int unit,
    bcm_mpls_tunnel_switch_traverse_cb cb,
    void* user_data) {
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_switch_traverse(unit, cb, user_data));
}

int __wrap_bcm_mpls_tunnel_initiator_set(
    int unit,
    bcm_if_t intf,
    int num_labels,
    bcm_mpls_egress_label_t* label_array) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_mpls_tunnel_initiator_set(unit, intf, num_labels, label_array));
#else
  return 0;
#endif
}

int __wrap_bcm_mpls_tunnel_initiator_clear(int unit, bcm_if_t intf) {
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_initiator_clear(unit, intf));
}

int __wrap_bcm_mpls_tunnel_initiator_get(
    int unit,
    bcm_if_t intf,
    int label_max,
    bcm_mpls_egress_label_t* label_array,
    int* label_count) {
  CALL_WRAPPERS_RV(bcm_mpls_tunnel_initiator_get(
      unit, intf, label_max, label_array, label_count));
}

int __wrap_bcm_port_resource_speed_get(
    int unit,
    bcm_gport_t port,
    bcm_port_resource_t* resource) {
  CALL_WRAPPERS_RV(bcm_port_resource_speed_get(unit, port, resource));
}

int __wrap_bcm_port_resource_speed_set(
    int unit,
    bcm_gport_t port,
    bcm_port_resource_t* resource) {
  CALL_WRAPPERS_RV(bcm_port_resource_speed_set(unit, port, resource));
}

int __wrap_bcm_port_resource_multi_set(
    int unit,
    int nport,
    bcm_port_resource_t* resource) {
  CALL_WRAPPERS_RV(bcm_port_resource_multi_set(unit, nport, resource));
}

int __wrap_bcm_l2_addr_delete_by_port(
    int unit,
    bcm_module_t mod,
    bcm_port_t port,
    uint32 flags) {
  CALL_WRAPPERS_RV(bcm_l2_addr_delete_by_port(unit, mod, port, flags));
}

int __wrap_bcm_l3_ingress_create(
    int unit,
    bcm_l3_ingress_t* ing_intf,
    bcm_if_t* intf_id) {
  CALL_WRAPPERS_RV(bcm_l3_ingress_create(unit, ing_intf, intf_id));
}

int __wrap_bcm_l3_ingress_destroy(int unit, bcm_if_t intf_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_ingress_destroy(unit, intf_id));
#else
  return 0;
#endif
}

int __wrap_bcm_vlan_control_vlan_set(
    int unit,
    bcm_vlan_t vlan,
    bcm_vlan_control_vlan_t control) {
  CALL_WRAPPERS_RV(bcm_vlan_control_vlan_set(unit, vlan, control));
}

int __wrap_bcm_vlan_control_vlan_get(
    int unit,
    bcm_vlan_t vlan,
    bcm_vlan_control_vlan_t* control) {
  CALL_WRAPPERS_RV(bcm_vlan_control_vlan_get(unit, vlan, control));
}

int __wrap_sh_process_command(int unit, char* cmd) {
  CALL_WRAPPERS_RV(sh_process_command(unit, cmd));
}

int __wrap_bcm_cosq_priority_group_mapping_profile_get(
    int unit,
    int profile_index,
    bcm_cosq_priority_group_mapping_profile_type_t type,
    int array_max,
    int* arg,
    int* array_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_priority_group_mapping_profile_get(
      unit, profile_index, type, array_max, arg, array_count));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_priority_group_mapping_profile_set(
    int unit,
    int profile_index,
    bcm_cosq_priority_group_mapping_profile_type_t type,
    int array_max,
    int* arg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_priority_group_mapping_profile_set(
      unit, profile_index, type, array_max, arg));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_priority_group_pfc_priority_mapping_profile_get(
    int unit,
    int profile_index,
    int array_max,
    int* pg_array,
    int* array_count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_priority_group_pfc_priority_mapping_profile_get(
      unit, profile_index, array_max, pg_array, array_count));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_priority_group_pfc_priority_mapping_profile_set(
    int unit,
    int profile_index,
    int array_count,
    int* pg_array) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_priority_group_pfc_priority_mapping_profile_set(
      unit, profile_index, array_count, pg_array));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_pfc_class_config_profile_set(
    int unit,
    int profile_index,
    int count,
    bcm_cosq_pfc_class_map_config_t* config_array) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_pfc_class_config_profile_set(
      unit, profile_index, count, config_array));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_pfc_class_config_profile_get(
    int unit,
    int profile_index,
    int max_count,
    bcm_cosq_pfc_class_map_config_t* config_array,
    int* count) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_pfc_class_config_profile_get(
      unit, profile_index, max_count, config_array, count));
#else
  return 0;
#endif
}

int __wrap_bcm_port_priority_group_config_set(
    int unit,
    bcm_gport_t gport,
    int priority_group,
    bcm_port_priority_group_config_t* prigrp_config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_priority_group_config_set(
      unit, gport, priority_group, prigrp_config));
#else
  return 0;
#endif
}

int __wrap_bcm_port_priority_group_config_get(
    int unit,
    bcm_gport_t gport,
    int priority_group,
    bcm_port_priority_group_config_t* prigrp_config) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_port_priority_group_config_get(
      unit, gport, priority_group, prigrp_config));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_port_priority_group_property_get(
    int unit,
    bcm_port_t gport,
    int priority_group_id,
    bcm_cosq_port_prigroup_control_t type,
    int* arg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_port_priority_group_property_get(
      unit, gport, priority_group_id, type, arg));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_port_priority_group_property_set(
    int unit,
    bcm_port_t gport,
    int priority_group_id,
    bcm_cosq_port_prigroup_control_t type,
    int arg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_cosq_port_priority_group_property_set(
      unit, gport, priority_group_id, type, arg));
#else
  return 0;
#endif
}

int __wrap_bcm_collector_create(
    int unit,
    uint32 options,
    bcm_collector_t* collector_id,
    bcm_collector_info_t* collector_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_collector_create(unit, options, collector_id, collector_info));
#else
  return 0;
#endif
}

int __wrap_bcm_collector_destroy(int unit, bcm_collector_t id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_collector_destroy(unit, id));
#else
  return 0;
#endif
}

int __wrap_bcm_collector_export_profile_create(
    int unit,
    uint32 options,
    int* export_profile_id,
    bcm_collector_export_profile_t* export_profile_info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_collector_export_profile_create(
      unit, options, export_profile_id, export_profile_info));
#else
  return 0;
#endif
}

int __wrap_bcm_collector_export_profile_destroy(
    int unit,
    int export_profile_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_collector_export_profile_destroy(unit, export_profile_id));
#else
  return 0;
#endif
}
void __wrap_bcm_collector_export_profile_t_init(
    bcm_collector_export_profile_t* export_profile_info) {
  CALL_WRAPPERS_NO_RV(bcm_collector_export_profile_t_init(export_profile_info));
}

void __wrap_bcm_collector_info_t_init(bcm_collector_info_t* collector_info) {
  CALL_WRAPPERS_NO_RV(bcm_collector_info_t_init(collector_info));
}

int __wrap_bcm_port_phy_timesync_config_set(
    int unit,
    bcm_port_t port,
    bcm_port_phy_timesync_config_t* conf) {
  CALL_WRAPPERS_RV(bcm_port_phy_timesync_config_set(unit, port, conf));
}

void __wrap_bcm_port_phy_timesync_config_t_init(
    bcm_port_phy_timesync_config_t* conf) {
  CALL_WRAPPERS_NO_RV(bcm_port_phy_timesync_config_t_init(conf));
}

int __wrap_bcm_port_timesync_config_set(
    int unit,
    bcm_port_t port,
    int config_count,
    bcm_port_timesync_config_t* config_array) {
  CALL_WRAPPERS_RV(
      bcm_port_timesync_config_set(unit, port, config_count, config_array));
}

void __wrap_bcm_port_timesync_config_t_init(
    bcm_port_timesync_config_t* port_timesync_config) {
  CALL_WRAPPERS_NO_RV(bcm_port_timesync_config_t_init(port_timesync_config));
}

int __wrap_bcm_time_interface_add(int unit, bcm_time_interface_t* intf) {
  CALL_WRAPPERS_RV(bcm_time_interface_add(unit, intf));
}

int __wrap_bcm_time_interface_delete_all(int unit) {
  CALL_WRAPPERS_RV(bcm_time_interface_delete_all(unit));
}

void __wrap_bcm_time_interface_t_init(bcm_time_interface_t* intf) {
  CALL_WRAPPERS_NO_RV(bcm_time_interface_t_init(intf));
}

int __wrap_bcm_field_entry_flexctr_attach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_flexctr_attach(unit, entry, flexctr_cfg));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_flexctr_detach(
    int unit,
    bcm_field_entry_t entry,
    bcm_field_flexctr_config_t* flexctr_cfg) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_flexctr_detach(unit, entry, flexctr_cfg));
#else
  return 0;
#endif
}

int __wrap_bcm_field_entry_remove(int unit, bcm_field_entry_t entry) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_field_entry_remove(unit, entry));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_route_stat_attach(
    int unit,
    bcm_l3_route_t* info,
    uint32 stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_stat_attach(unit, info, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_route_flexctr_object_set(
    int unit,
    bcm_l3_route_t* info,
    uint32 value) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_flexctr_object_set(unit, info, value));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_route_stat_detach(int unit, bcm_l3_route_t* info) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_route_stat_detach(unit, info));
#else
  return 0;
#endif
}

int __wrap_bcm_stat_custom_group_create(
    int unit,
    uint32 mode_id,
    bcm_stat_object_t object,
    uint32* stat_counter_id,
    uint32* num_entries) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_custom_group_create(
      unit, mode_id, object, stat_counter_id, num_entries));
#else
  return 0;
#endif
}

int __wrap_bcm_stat_group_destroy(int unit, uint32 stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_group_destroy(unit, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_stat_group_create(
    int unit,
    bcm_stat_object_t object,
    bcm_stat_group_mode_t group_mode,
    uint32* stat_counter_id,
    uint32* num_entries) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_group_create(
      unit, object, group_mode, stat_counter_id, num_entries));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_ingress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_ingress_stat_attach(unit, intf_id, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_l3_egress_stat_attach(
    int unit,
    bcm_if_t intf_id,
    uint32 stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_l3_egress_stat_attach(unit, intf_id, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_stat_group_mode_id_create(
    int unit,
    uint32 flags,
    uint32 total_counters,
    uint32 num_selectors,
    bcm_stat_group_mode_attr_selector_t* attr_selectors,
    uint32* mode_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_group_mode_id_create(
      unit, flags, total_counters, num_selectors, attr_selectors, mode_id));
#else
  return 0;
#endif
}

int __wrap_bcm_stat_group_mode_id_destroy(int unit, uint32 mode_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_stat_group_mode_id_destroy(unit, mode_id));
#else
  return 0;
#endif
}

int __wrap_bcm_cosq_pfc_deadlock_control_set(
    int unit,
    bcm_port_t port,
    int priority,
    bcm_cosq_pfc_deadlock_control_t type,
    int arg) {
  CALL_WRAPPERS_RV(
      bcm_cosq_pfc_deadlock_control_set(unit, port, priority, type, arg));
}

int __wrap_bcm_cosq_pfc_deadlock_control_get(
    int unit,
    bcm_port_t port,
    int priority,
    bcm_cosq_pfc_deadlock_control_t type,
    int* arg) {
  CALL_WRAPPERS_RV(
      bcm_cosq_pfc_deadlock_control_get(unit, port, priority, type, arg));
}

int __wrap_bcm_cosq_pfc_deadlock_recovery_event_register(
    int unit,
    bcm_cosq_pfc_deadlock_recovery_event_cb_t callback,
    void* userdata) {
  CALL_WRAPPERS_RV(
      bcm_cosq_pfc_deadlock_recovery_event_register(unit, callback, userdata));
}

int __wrap_bcm_cosq_pfc_deadlock_recovery_event_unregister(
    int unit,
    bcm_cosq_pfc_deadlock_recovery_event_cb_t callback,
    void* userdata) {
  CALL_WRAPPERS_RV(bcm_cosq_pfc_deadlock_recovery_event_unregister(
      unit, callback, userdata));
}

int __wrap_bcm_flexctr_action_create(
    int unit,
    int options,
    bcm_flexctr_action_t* action,
    uint32* stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(
      bcm_flexctr_action_create(unit, options, action, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_flexctr_action_destroy(int unit, uint32 stat_counter_id) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_flexctr_action_destroy(unit, stat_counter_id));
#else
  return 0;
#endif
}

int __wrap_bcm_flexctr_action_traverse(
    int unit,
    bcm_flexctr_action_traverse_cb trav_fn,
    void* user_data) {
#ifndef BCM_SDK_TYPE_DNX_ONLY
  CALL_WRAPPERS_RV(bcm_flexctr_action_traverse(unit, trav_fn, user_data));
#else
  return 0;
#endif
}

int __wrap_bcm_port_ifg_get(
    int unit,
    bcm_port_t port,
    int speed,
    bcm_port_duplex_t duplex,
    int* bit_times) {
  CALL_WRAPPERS_RV(bcm_port_ifg_get(unit, port, speed, duplex, bit_times));
}

int __wrap_bcm_port_ifg_set(
    int unit,
    bcm_port_t port,
    int speed,
    bcm_port_duplex_t duplex,
    int bit_times) {
  CALL_WRAPPERS_RV(bcm_port_ifg_set(unit, port, speed, duplex, bit_times));
}

int __wrap_bcm_port_control_phy_timesync_set(
    int unit,
    bcm_port_t port,
    bcm_port_control_phy_timesync_t type,
    uint64 value) {
  CALL_WRAPPERS_RV(bcm_port_control_phy_timesync_set(unit, port, type, value));
}

} // extern "C"
