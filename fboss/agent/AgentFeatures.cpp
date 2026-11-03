// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/VoqConstants.h"

DEFINE_bool(enable_lacp, false, "Run LACP in agent");

DEFINE_int32(switchIndex, 0, "Switch Index for Asic");

DEFINE_bool(janga_test, false, "Enable Janga test fixture platform mapping");

DEFINE_bool(test_fixture, false, "Enable test fixture platform mapping");

DEFINE_bool(osfp_tray, false, "Enable OSFP tray platform mapping");

// TODO: Remove once rifs limitation is fixed
DEFINE_bool(
    minipack3bta_16rifs,
    false,
    "Enable 16rifs platform mapping for minipack3bta");

DEFINE_bool(dsf_4k, false, "Enable DSF Scale Test config");

DEFINE_bool(dsf_100g_nif_breakout, false, "Enable J3 DSF Scale Test config");

DEFINE_bool(
    sai_user_defined_trap,
    false,
    "Flag to use user defined trap when programming ACL action to punt packets to cpu queue.");

DEFINE_bool(enable_acl_table_chain_group, false, "Allow ACL table chaining");

DEFINE_int32(
    oper_sync_req_timeout,
    30,
    "request timeout for oper sync client in seconds");

DEFINE_bool(
    classid_for_unresolved_routes,
    false,
    "Flag to set the class ID for unresolved routes that points  to CPU port");

DEFINE_bool(hide_fabric_ports, false, "Elide ports of type fabric");

DEFINE_bool(hide_management_ports, false, "Elide ports of type management");

DEFINE_bool(hide_eventor_ports, false, "Elide ports of type eventor");

DEFINE_bool(hide_interface_ports, false, "Elide ports of type interface");

// DSF Subscriber flags
DEFINE_bool(
    dsf_subscribe,
    true,
    "Issue DSF subscriptions to all DSF Interface nodes");
DEFINE_bool(dsf_subscriber_skip_hw_writes, false, "Skip writing to HW");
DEFINE_bool(
    dsf_subscriber_cache_updated_state,
    false,
    "Cache switch state after update by dsf subsriber");
DEFINE_uint32(
    dsf_gr_hold_time,
    0,
    "GR hold time for FSDB DsfSubscription in sec");
DEFINE_bool(
    dsf_subscribe_patch,
    false,
    "Subscribe to remote FSDB using Patch apis");
DEFINE_int32(
    dsf_subscriber_reconnect_thread_heartbeat_ms,
    1000,
    "DSF subscriber reconnect thread heartbeat interval in msec");
DEFINE_int32(
    dsf_subscriber_stream_thread_heartbeat_ms,
    1000,
    "DSF subscriber stream thread heartbeat interval in msec");
DEFINE_bool(hyper_port, false, "Enable hyper port on edsw front panel ports");
// Remote neighbor entries are always flushed to avoid blackholing the traffic.
// However, by default, remote{systemPorts, Rifs} are not flushed but marked
// STALE in the software. This is to avoid hardware programmign churn.
// Setting this flag to True will cause Agent to flush remote{systemPorts,
// Rifs} from the hardware.
DEFINE_bool(
    dsf_flush_remote_sysports_and_rifs_on_gr,
    false,
    "Flush Remote{systemPorts, Rifs} on GR");
DEFINE_uint32(
    dsf_num_parallel_sessions_per_remote_interface_node,
    1,
    "Number of parallel DSF sessions per remote Interface Node. "
    "1 for Prod. > 1 for scale tests");

DEFINE_int32(
    dsf_num_fsdb_connect_threads,
    1,
    "Number of threads to use for DSF remote connection pool");

DEFINE_int32(
    dsf_num_fsdb_stream_threads,
    1,
    "Number of threads to use for DSF remote stream pool");

DEFINE_int32(
    dsf_session_conn_timeout_ms,
    4000,
    "Socket connection timeout for DSF session");

DEFINE_int32(
    dsf_session_recv_timeout_ms,
    12000,
    "Socket pkt receive timeout for DSF session");

DEFINE_bool(
    set_classid_for_my_subnet_and_ip_routes,
    false,
    "Flag to disable implicit route classid set by sai/sdk, and always explicitly set class ID for my subnet routes and my ip routes from fboss");

DEFINE_int32(
    stat_publish_interval_ms,
    1000,
    "How frequently to publish thread-local stats back to the "
    "global store.  This should generally be less than 1 second.");

DEFINE_int32(
    hwagent_port_base,
    5931,
    "The first thrift server port reserved for HwAgent");

DEFINE_bool(force_init_fp, true, "Force full field processor initialization");

DEFINE_bool(
    flowletSwitchingEnable,
    false,
    "Flag to turn on flowlet switching for DLB");

DEFINE_bool(
    enable_ecmp_random_spray,
    false,
    "Flag to turn on backup flowlet switching for DLB");

// TODO (ravi)
// This is more a safety tool for fast rollback if RTSWs run into an issue
DEFINE_bool(
    dlbResourceCheckEnable,
    true,
    "Flag to enable resource checks on DLB ecmp groups");

DEFINE_bool(
    use_full_dlb_scale,
    false,
    "FLAG to enable full DLB scale when using SAI");

DEFINE_bool(
    send_icmp_time_exceeded,
    true,
    "Flag to indicate whether to send ICMP time exceeded for hop limit exceeded");

DEFINE_bool(
    disable_looped_fabric_ports,
    true,
    "Disable fabric ports where loop is detected to stop traffic blackholing");

// Wrong fabric connection detection. Flag to enable/disable this mechanism in
// SDK
DEFINE_bool(
    detect_wrong_fabric_connections,
    true,
    "Enable wrong fabric connection. Done via SDK");

DEFINE_bool(dsf_edsw_platform_mapping, false, "Use EDSW platform mapping");

DEFINE_bool(
    exit_for_any_hw_disconnect,
    false,
    "Flag to indicate whether SwSwitch will crash if any hw switch connection is lost. This will be used in tests to ensure all hw agent running.");

// TODO: Need fix for the feature on single link configuration (CS00012375262)
DEFINE_bool(
    enable_balanced_input_mode,
    false,
    "Enable balanced input mode on fabric devices");

DEFINE_int32(
    hw_agent_connection_timeout_ms,
    0,
    "Time to wait for HwSwitch to connect before SwSwitch exits. "
    "By default, SwSwitch waits forever and hence default value is 0.");

DEFINE_bool(
    qgroup_guarantee_enable,
    false,
    "Enable setting of unicast and multicast queue guaranteed buffer sizes");

DEFINE_bool(skip_buffer_reservation, false, "Enable skip reservation");

DEFINE_bool(
    fix_lossless_mode_per_pg,
    false,
    "Flag to disruptively update lossless mode per pg");

DEFINE_int32(fboss_event_base_queue_limit, 10000, "FbossEventBase queue limit");

DEFINE_bool(
    dual_stage_rdsw_3q_2q,
    false,
    "Use platform mapping for dual stage RDSW with 3q and 2q model");

DEFINE_bool(
    dual_stage_edsw_3q_2q,
    false,
    "Use platform mapping for dual stage EDSW with 3q and 2q model");

DEFINE_bool(
    dual_stage_3q_2q_qos,
    false,
    "Use qos setting for dual stage 3q and 2q model");

bool isDualStage3Q2QMode() {
  return FLAGS_dual_stage_rdsw_3q_2q || FLAGS_dual_stage_edsw_3q_2q;
}

bool isDualStage3Q2QQos() {
  return isDualStage3Q2QMode() || FLAGS_dual_stage_3q_2q_qos;
}

DEFINE_bool(
    enable_hw_update_protection,
    false,
    "Enable Neighbor/MAC table hw update failure protection");

DEFINE_int32(
    max_l2_entries,
    5300,
    "Maximum L2 entries supported by Resource Accountant");

DEFINE_int32(
    max_ndp_entries,
    4000,
    "Maximum NDP entries supported by Resource Accountant");

DEFINE_int32(
    max_arp_entries,
    1280,
    "Maximum ARP entries supported by Resource Accountant");

DEFINE_bool(
    enforce_resource_hw_limits,
    true,
    "Weather to cap the configured maximum entries to the harwdware limit by Resource Accountant");

DEFINE_bool(
    fw_drained_unrecoverable_error,
    false,
    "Enable or disable whether firmware drained(isolation) can be unrecoverable error");

DEFINE_int32(
    neighbhor_resource_percentage,
    75,
    "Percentage of neighbor resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_bool(
    enable_route_resource_protection,
    true,
    "Enable route resource protection for Resource Accountant");

DEFINE_int32(
    max_mac_address_to_block,
    10000,
    "Max number of mac addresses to block");

DEFINE_int32(
    max_neighbors_to_block,
    10000,
    "Max number of neighbor entries to block");

DEFINE_bool(
    link_stress_test,
    false,
    "enable to run stress tests (longer duration + more iterations)");

DEFINE_int32(
    ecmp_resource_percentage,
    75,
    "Percentage of ECMP resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_int32(
    ars_resource_percentage,
    75,
    "Percentage of DLB ECMP resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_bool(
    enable_mysid_resource_protection,
    true,
    "Enable MySID resource protection in ResourceAccountant");

DEFINE_int32(
    mysid_resource_percentage,
    75,
    "Percentage of MySID resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_bool(
    srv6,
    false,
    "Enable SRv6 features, e.g. collecting the SRv6 MySID resource counter. "
    "Only valid on configs whose SDK is initialized with SRv6/mySid support "
    "(sai_stats_support); leave off otherwise.");

DEFINE_bool(
    enable_srv6_nexthop_resource_protection,
    true,
    "Enable SRv6 next hop resource protection in ResourceAccountant");

DEFINE_int32(
    srv6_nexthop_resource_percentage,
    75,
    "Percentage of SRv6 next hop resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_bool(
    enable_route_counter_resource_protection,
    true,
    "Enable route counter resource protection in ResourceAccountant");

DEFINE_int32(
    route_counter_resource_percentage,
    75,
    "Percentage of route counter resources (out of 100) allowed to use before ResourceAccountant rejects the update.");

DEFINE_int32(
    switch_index_for_testing,
    0,
    "switch index under test. Used for testing NPU specific features.");

DEFINE_int32(
    switch_id_for_testing,
    0,
    "switch ID under test. Used for testing NPU specific features.");

DEFINE_int32(
    num_npus_for_testing,
    1,
    "Number of hw agent NPUs started by the test runner.");

DEFINE_uint32(
    counter_refresh_interval,
    1,
    "Counter refresh interval in seconds. Set it to 0 to fetch stats from HW");

DEFINE_bool(run_forever, false, "run the test forever");
DEFINE_bool(run_forever_on_failure, false, "run the test forever on failure");

DEFINE_string(
    sdk_reg_dump_path_prefix,
    "/var/facebook/logs/fboss/sdk/reg_dump",
    "File path prefix for SDK register dump");

DEFINE_bool(
    skip_sdk_reg_dump,
    false,
    "Prevent the SDK from writing register/state dumps to disk. Used to "
    "selectively avoid devices that log heavily to disk.");

DEFINE_uint32(
    sdk_dump_rate_limit_window_ms,
    900000,
    "Time window in milliseconds within which the SDK allows at most one "
    "register/state dump write for NORMAL priority events. High priority "
    "events are never rate limited. 0 disables rate limiting, 0xFFFFFFFF "
    "disables dumps for NORMAL priority events altogether.");

DEFINE_bool(
    type_dctype1_janga,
    false,
    "Enable support for single NPU config on Janga for MTIA");

DEFINE_bool(
    prod_invariant_config_test,
    false,
    "This flag is used to enable prod config in invariant config test");

DEFINE_int32(
    max_unprocessed_switch_reachability_changes,
    1,
    "Max number of switch reachability changes that can be enqueued to bottom-half.");

DEFINE_bool(
    enable_ecmp_resource_manager,
    false,
    "This flag is used to enable ecmp resource manager feature");

DEFINE_bool(
    enable_nexthop_id_manager,
    false,
    "Enable NextHop ID allocation and management for routes");

DEFINE_bool(
    resolve_nexthops_from_id,
    false,
    "Resolve nexthops from NextHopSetID in FibInfo maps instead of inline nexthops");

DEFINE_int32(
    ecmp_resource_manager_make_before_break_buffer,
    2,
    "Buffer to keep in ECMP resource manager from actual ECMP limt");

DEFINE_int32(update_stats_interval_s, 1, "Update stats interval in seconds");

DEFINE_bool(
    update_route_with_dlb_type,
    false,
    "Flag to perform a DLB type update in FIB state");

DEFINE_int32(agent_exit_delay_s, 0, "Delay in seconds before the agent exits");

DEFINE_bool(
    dsf_single_stage_r192_f40_e32,
    false,
    "Use platform mapping for DSF Single Stage with 192 RDSWs, 40 FDSWs, 32 EDSWs");

DEFINE_bool(
    enable_high_frequency_stats_polling,
    false,
    "Enable high frequency stats polling");

DEFINE_bool(
    dsf_headroom_pool_size_multiplication_factor_fix,
    false,
    "Fix the headroom pool size multiplication factor for DSF");

DEFINE_bool(
    ignore_asic_hard_reset_notification,
    false,
    "Ignore ASIC hard reset notification received from SAI/SDK");

DEFINE_bool(
    cleanup_probed_kernel_data,
    false,
    "Remove probed routes, addresses, rules, and interfaces from the kernel");

DEFINE_bool(
    ndp_static_neighbor,
    false,
    "Initiate neighbor solicitation for static neighbors");

DEFINE_bool(
    dsf_single_stage_r128_f40_e16_8k_sys_ports,
    false,
    "Allow upto 8K system ports on single stage DSF (default=6144)");

DEFINE_bool(
    dsf_single_stage_r128_f40_e16_uniform_local_offset,
    false,
    "Use uniform local system port offset for single stage DSF");

DEFINE_uint32(
    ecmp_width,
    64,
    "Max ecmp width. Also implies ucmp normalization factor");

DEFINE_bool(
    check_wb_handles,
    false,
    "Fail if any warm boot handles are left unclaimed.");

// DSF specific feature to monitor fabric side links
DEFINE_bool(
    enable_fabric_link_monitoring,
    false,
    "Enable fabric link monitoring feature in DSF");

// Relocates fabric port logical IDs into the local port-ID range (uniform
// local offset). Currently scoped to meru800bia and janga800bic.
DEFINE_bool(
    fabric_ports_uniform_local_offset,
    false,
    "Relocate fabric port logical IDs into the local port-ID range");

DEFINE_bool(
    enable_cpu_latency_monitoring,
    false,
    "Enable CpuLatencyManager for always-on CPU round-trip latency monitoring "
    "via IP2ME probe packets on ethernet ports");

DEFINE_bool(
    lldp_port_drain_state,
    false,
    "Enable sending and receiving port drain state in LLDP packets");

DEFINE_bool(enable_agent_drain, false, "Enable drain in wedge agent");

DEFINE_bool(
    enable_state_delta_logging,
    false,
    "Enable logging of state deltas applied in applyUpdate()");

DEFINE_string(
    state_delta_log_file,
    "/tmp/state_delta.log",
    "Path to the state delta log file.");

DEFINE_bool(
    strip_vlan_for_pipeline_bypass,
    true,
    "Strip vlan tag for packet injected with pipeline bypass");

DEFINE_bool(
    observe_rx_packets_without_interface,
    false,
    "Notify packet observers before dropping RX packets with no interface");

DEFINE_string(
    state_delta_log_protocol,
    "COMPACT",
    "Serialization protocol for state delta logging (BINARY, SIMPLE_JSON, COMPACT)");

DEFINE_int32(
    state_delta_log_timeout_ms,
    100,
    "Log timeout value in milliseconds. Logger will periodically"
    "flush logs even if the buffer is not full");

DEFINE_bool(
    enable_pre_manager_delta_logging,
    false,
    "Enable pre-manager state delta logging via SwitchStateDeltaLogger (sibling of StateDeltaLogger). Off by default; gates construction of the sibling logger.");

DEFINE_string(
    pre_manager_delta_log_file,
    "/var/facebook/logs/fboss/pre_manager_deltas.log",
    "Path to the pre-manager state delta log file.");

DEFINE_string(
    pre_manager_delta_log_protocol,
    "COMPACT",
    "Serialization protocol for pre-manager state delta logging (BINARY, SIMPLE_JSON, COMPACT)");

DEFINE_int32(
    pre_manager_delta_log_timeout_ms,
    200,
    "Pre-manager delta log timeout in milliseconds. Logger will periodically "
    "flush logs even if the buffer is not full. Bounds the worst-case data-loss "
    "window on a machine crash.");

DEFINE_bool(
    enable_post_manager_delta_logging,
    false,
    "Enable post-manager state delta logging via SwitchStateDeltaLogger (sibling of StateDeltaLogger). Off by default; gates logging of split sub-deltas at the post-manager capture point (record type kTypePostManagerDelta). Reserved — no-op today.");

DEFINE_int32(
    fsdbStatsStreamIntervalSeconds,
    5,
    "Interval at which stats subscriptions are served");

DEFINE_int32(
    update_phy_info_interval_s,
    10,
    "Update phy info interval in seconds");

DEFINE_bool(
    recover_from_hw_switch,
    false,
    "On SW agent only crash, it can collect the switch state from hw"
    " switches and recover from it. This enables hitless restarts"
    " on SW agent. This is only used for Sw Switch.");

DEFINE_bool(
    verify_recover_from_hw_switch,
    false,
    "When set, assert that SW agent actually warmbooted from HW switch."
    " Used in tests to verify the warmboot-from-HW-switch path.");

DEFINE_int32(
    fabric_link_monitoring_max_l1_l2_switch_ids,
    facebook::fboss::kDualStageMaxL1L2FabricLinkMonitoringSwitchIds,
    "Max number of L1-L2 fabric link monitoring switch IDs to allocate");

DEFINE_bool(
    montblanc_odd_ports_8x100G,
    false,
    "Enables platform mapping with 8x100G on odd ports");

DEFINE_bool(
    montblanc_gtsw_yolo,
    false,
    "Enables montblanc platform mapping with 4x200G on odd ports and 6x100G on even ports");

DEFINE_bool(
    montblanc_precoding,
    false,
    "Enables montblanc platform mapping with precoding on downlinks, used by VR200 (San Miguel) racks");

DEFINE_bool(can_warm_boot, true, "Enable/disable warm boot functionality");

DEFINE_string(
    thrift_switch_state_file,
    "thrift_switch_state",
    "File for dumping switch state in serialized thrift format on exit");

DEFINE_bool(
    qsfp_port_manager_mode,
    false,
    "Set to true to enable Port Manager mode. This means PortManager object will manage all port-level logic and TransceiverManager object will only manage transceiver-level logic.");

DEFINE_bool(
    verify_fib_nexthop_id_consistency,
    false,
    "Verify FIB route NextHop IDs are consistent with inline nexthops.");

DEFINE_bool(
    enforce_single_nbr_mac_per_intf,
    false,
    "Enforce that each RIF has at most one neighbor MAC address");

DEFINE_int32(
    max_tx_packets,
    100000, // 1 gb / 10 kb
    "the point at which we start dropping tx packets");

DEFINE_bool(
    enable_route_counters_for_named_nhg,
    false,
    "Implicitly associate a route counter for routes using named NHG");

DEFINE_bool(
    enable_acl_table_redirect_action,
    false,
    "Add redirect action type to ACL table");

DEFINE_bool(
    enable_bulk_create_ecmp_members,
    false,
    "Enable bulk programming of ECMP members");

DEFINE_int32(
    pbr_acl_priority,
    50000,
    "Priority shared by every synthesized PBR ACL entry. Must stay below "
    "AclTable::kDataplaneAclMaxPriority so no config ACL can be assigned it");

DEFINE_bool(
    enable_pfc_priority_to_pg_map,
    false,
    "Enable programming the PFC priority to priority group QoS map on ports");

DEFINE_bool(
    enable_port_cl72_retry,
    false,
    "Enable CL72 link training retry on the switch (XGS, BRCM SDK >= 14.2 only)");

DEFINE_bool(
    enable_remote_intf_route_reconcile,
    false,
    "Reconcile remote interface routes (RIB/FIB drift) on VOQ-switch warmboot");

DEFINE_string(
    bcm_sdk_log_file,
    "",
    "If set, path to a Broadcom SDK SOC/diag command file (e.g. containing "
    "'debug bcm stat verbose'). When set, the sai_preinit_cmd_file and "
    "sai_postinit_cmd_file SOC properties are added to the SDK config pointing "
    "to this file, so the SDK runs the commands at init time. Used to enable "
    "native BCM SDK debug logging (analogous to --enable_sai_log for SAI).");
