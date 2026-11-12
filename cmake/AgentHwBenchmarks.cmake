# CMake to build libraries and binaries in fboss/agent/hw/benchmarks

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

# Creates both hw_NAME (mono) and multi_switch_hw_NAME (multi-switch) variants.
# Each variant links the same sources and shared DEPS, but uses
# mono_agent_ensemble/mono_agent_benchmarks vs
# multi_switch_agent_ensemble/multi_switch_agent_benchmarks.
#
# Arguments:
#   NAME       - benchmark name (without hw_ prefix)
#   SRCS       - source files. Omit for header-only helpers; the libs are
#                created as INTERFACE in that case (deps propagate to
#                consumers but no archive is built).
#   DEPS       - mode-independent deps (shared between both variants)
#   HW_DEPS    - hw_* helper deps (without hw_ prefix); auto-prefixed
#                with hw_ for mono and multi_switch_hw_ for multi-switch
#   NO_AGENT_DEPS - if set, do not add agent ensemble/benchmarks deps
function(BUILD_HW_BENCHMARK_LIBS NAME)
  cmake_parse_arguments(ARG "NO_AGENT_DEPS" "" "SRCS;DEPS;HW_DEPS" ${ARGN})

  set(_mono_hw_deps "")
  set(_ms_hw_deps "")
  foreach(_dep ${ARG_HW_DEPS})
    list(APPEND _mono_hw_deps "hw_${_dep}")
    list(APPEND _ms_hw_deps "multi_switch_hw_${_dep}")
  endforeach()

  if(NOT ARG_NO_AGENT_DEPS)
    set(_mono_agent_deps mono_agent_ensemble mono_agent_benchmarks)
    set(_ms_agent_deps multi_switch_agent_ensemble multi_switch_agent_benchmarks)
  else()
    set(_mono_agent_deps "")
    set(_ms_agent_deps "")
  endif()

  if(ARG_SRCS)
    add_library(hw_${NAME} ${ARG_SRCS})
    target_link_libraries(hw_${NAME}
      ${_mono_agent_deps}
      ${_mono_hw_deps}
      ${ARG_DEPS}
    )

    add_library(multi_switch_hw_${NAME} ${ARG_SRCS})
    target_link_libraries(multi_switch_hw_${NAME}
      ${_ms_agent_deps}
      ${_ms_hw_deps}
      ${ARG_DEPS}
    )
  else()
    # Header-only helper: INTERFACE library propagates deps without
    # producing an archive of its own.
    add_library(hw_${NAME} INTERFACE)
    target_link_libraries(hw_${NAME} INTERFACE
      ${_mono_agent_deps}
      ${_mono_hw_deps}
      ${ARG_DEPS}
    )

    add_library(multi_switch_hw_${NAME} INTERFACE)
    target_link_libraries(multi_switch_hw_${NAME} INTERFACE
      ${_ms_agent_deps}
      ${_ms_hw_deps}
      ${ARG_DEPS}
    )
  endif()
endfunction()

# --- Helper libraries (must be defined before benchmarks that use them) ---

BUILD_HW_BENCHMARK_LIBS(init_and_exit_benchmark_helper
  SRCS fboss/agent/hw/benchmarks/HwInitAndExitBenchmarkHelper.cpp
  DEPS
    config_factory
    dsf_config_utils
    fabric_test_utils
    voq_test_utils
    route_scale_gen
    prod_config_utils
    Folly::folly
    Folly::follybenchmark
    function_call_time_reporter
)

BUILD_HW_BENCHMARK_LIBS(route_benchmark_helpers
  SRCS fboss/agent/hw/benchmarks/HwRouteScaleBenchmarkHelpers.cpp
  DEPS
    config_factory
    dsf_config_utils
    fabric_test_utils
    voq_test_utils
    route_scale_gen
    prod_config_utils
    Folly::folly
    Folly::follybenchmark
    function_call_time_reporter
)

BUILD_HW_BENCHMARK_LIBS(cpu_latency_benchmark_helper
  SRCS fboss/agent/hw/benchmarks/HwCpuLatencyBenchmarkHelper.cpp
  DEPS
    agent_test_utils
    ecmp_helper
    config_utils
    copp_test_utils
    common_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(tun_manager_probe_benchmark_helper
  DEPS
    config_factory
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(voq_route_competing_remote_neighbor_benchmark_helper
  NO_AGENT_DEPS
  DEPS
    core
    fib_helpers
    config_factory
    agent_ensemble
    voq_test_utils
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

# --- Benchmarks ---

BUILD_HW_BENCHMARK_LIBS(tx_slow_path_rate
  SRCS fboss/agent/hw/benchmarks/HwTxSlowPathBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(stats_collection_speed
  SRCS fboss/agent/hw/benchmarks/HwStatsCollectionBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    dsf_config_utils
    voq_test_utils
    ecmp_helper
    load_balancer_test_utils
    copp_test_utils
    network_ai_qos_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(fsw_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwFswScaleRouteAddBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(fsw_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwFswScaleRouteDelBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(th_alpm_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwThAlpmScaleRouteAddBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(th_alpm_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwThAlpmScaleRouteDelBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(hgrid_du_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwHgridDUScaleRouteAddBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(hgrid_du_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwHgridDUScaleRouteDelBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(hgrid_uu_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwHgridUUScaleRouteAddBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(hgrid_uu_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwHgridUUScaleRouteDelBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(anticipated_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwAnticipatedScaleRouteAddBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(anticipated_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwAnticipatedScaleRouteDelBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(rib_resolution_speed
  SRCS fboss/agent/hw/benchmarks/HwRibResolutionBenchmark.cpp
  DEPS
    config_factory
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(rib_sync_fib_speed
  SRCS fboss/agent/hw/benchmarks/HwRibSyncFibBenchmark.cpp
  DEPS
    config_factory
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(ecmp_shrink_speed
  SRCS fboss/agent/hw/benchmarks/HwEcmpShrinkSpeedBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    ecmp_test_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(ecmp_shrink_with_competing_route_updates_speed
  SRCS fboss/agent/hw/benchmarks/HwEcmpShrinkWithCompetingRouteUpdatesBenchmark.cpp
  DEPS
    route_distribution_gen
    config_factory
    hw_packet_utils
    ecmp_test_utils
    ecmp_helper
    function_call_time_reporter
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(rx_slow_path_rate
  SRCS fboss/agent/hw/benchmarks/HwRxSlowPathBenchmark.cpp
  DEPS
    agent_test_utils
    config_factory
    copp_test_utils
    hw_qos_utils
    hw_copp_utils
    hw_test_acl_utils
    ecmp_helper
    Folly::folly
    Folly::follybenchmark
    trap_packet_utils
)

BUILD_HW_BENCHMARK_LIBS(rx_slow_path_arp_rate
  SRCS fboss/agent/hw/benchmarks/HwRxSlowPathArpBenchmark.cpp
  DEPS
    agent_test_utils
    config_factory
    packet_factory
    copp_test_utils
    asic_utils
    hw_copp_utils
    hw_test_acl_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(bgp_rx_slow_path_rate
  SRCS fboss/agent/hw/benchmarks/HwBGPRxSlowPathBenchmark.cpp
  DEPS
    agent_test_utils
    packet
    packet_factory
    agent_ensemble
    ecmp_helper
    route_scale_gen
    copp_test_utils
    qos_test_utils
    stress_test_utils
    port_flap_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_40Gx10G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit40Gx10GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_100Gx10G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit100Gx10GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_100Gx25G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit100Gx25GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_100Gx50G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit100Gx50GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_100Gx100G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit100Gx100GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_400Gx400G
  SRCS fboss/agent/hw/benchmarks/HwInitAndExit400Gx400GBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_voq
  SRCS fboss/agent/hw/benchmarks/HwInitAndExitVoqBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(init_and_exit_fabric
  SRCS fboss/agent/hw/benchmarks/HwInitAndExitFabricBenchmark.cpp
  NO_AGENT_DEPS
  DEPS config_factory
  HW_DEPS init_and_exit_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(voq_scale_route_add_speed
  SRCS fboss/agent/hw/benchmarks/HwVoqScaleRouteAddBenchmark.cpp
  NO_AGENT_DEPS
  DEPS
    fabric_test_utils
    route_scale_gen
    dsf_config_utils
    voq_test_utils
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(voq_scale_route_del_speed
  SRCS fboss/agent/hw/benchmarks/HwVoqScaleRouteDelBenchmark.cpp
  NO_AGENT_DEPS
  DEPS
    fabric_test_utils
    route_scale_gen
    dsf_config_utils
    voq_test_utils
    Folly::folly
    Folly::follybenchmark
  HW_DEPS route_benchmark_helpers
)

BUILD_HW_BENCHMARK_LIBS(switch_reachability_change_speed
  SRCS
    fboss/agent/hw/benchmarks/HwSwitchReachabilityChangeBenchmarkHelper.cpp
    fboss/agent/hw/benchmarks/HwSwitchReachabilityChangeFabricBenchmark.cpp
    fboss/agent/hw/benchmarks/HwSwitchReachabilityChangeVoqBenchmark.cpp
  DEPS
    config_factory
    dsf_config_utils
    voq_test_utils
    fabric_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(voq_sys_port_programming
  SRCS fboss/agent/hw/benchmarks/HwVoqSysPortProgrammingBenchmark.cpp
  NO_AGENT_DEPS
  DEPS
    voq_test_utils
    dsf_config_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(voq_remote_entity_programming
  SRCS fboss/agent/hw/benchmarks/HwVoqRemoteEntityProgrammingBenchmark.cpp
  NO_AGENT_DEPS
  DEPS
    voq_test_utils
    dsf_config_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(system_scale_memory_benchmark
  SRCS fboss/agent/hw/benchmarks/HwSystemScaleMemoryBenchmark.cpp
  DEPS
    agent_fsdb_integ_bench_helper
    system_scale_test_utils
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(flowlet_stats_collection_speed
  SRCS fboss/agent/hw/benchmarks/HwFlowletStatsCollectionBenchmark.cpp
  DEPS
    config_factory
    hw_packet_utils
    dsf_config_utils
    voq_test_utils
    ecmp_helper
    agent_ensemble
    config_utils
    scale_test_utils
    common_utils
    load_balancer_utils
    load_balancer_test_utils
    copp_test_utils
    network_ai_qos_utils
    Folly::folly
    Folly::follybenchmark
    udf_test_utils
)

BUILD_HW_BENCHMARK_LIBS(clear_interface_counters_phy_benchmark
  SRCS fboss/agent/hw/benchmarks/HwClearInterfacePhyCountersBenchmark.cpp
  DEPS
    config_factory
    Folly::follybenchmark
    port_flap_helper
    mac_learning_flood_helper
)

BUILD_HW_BENCHMARK_LIBS(ucmp_scale_benchmark
  SRCS fboss/agent/hw/benchmarks/HwUcmpScaleBenchmark.cpp
  DEPS
    fib_helpers
    agent_ensemble
    ecmp_helper
    config_utils
    scale_test_utils
    config_factory
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(srv6_scale_benchmark
  SRCS fboss/agent/hw/benchmarks/HwSrv6ScaleBenchmark.cpp
  DEPS
    config_factory
    ecmp_helper
    handler
    scale_test_utils
    srv6_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(pbr_scale_benchmark
  SRCS fboss/agent/hw/benchmarks/HwPbrScaleBenchmark.cpp
  DEPS
    acl_test_utils
    config_factory
    ecmp_helper
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(srv6_mysid_scale_benchmark
  SRCS fboss/agent/hw/benchmarks/HwSrv6MySidScaleBenchmark.cpp
  DEPS
    config_factory
    ecmp_helper
    srv6_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(cpu_latency_benchmark
  SRCS fboss/agent/hw/benchmarks/HwCpuLatencyBenchmark.cpp
  DEPS
    Folly::folly
    Folly::follybenchmark
  HW_DEPS cpu_latency_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(system_scale_churn_memory_benchmark
  SRCS fboss/agent/hw/benchmarks/HwSystemScaleChurnMemoryBenchmark.cpp
  DEPS
    agent_fsdb_integ_bench_helper
    system_scale_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(tun_manager_probe_and_cleanup_speed
  SRCS fboss/agent/hw/benchmarks/HwTunManagerProbeBenchmark.cpp
  DEPS
    Folly::folly
    Folly::follybenchmark
  HW_DEPS tun_manager_probe_benchmark_helper
)

BUILD_HW_BENCHMARK_LIBS(ecmp_backup_group_spillover
  SRCS fboss/agent/hw/benchmarks/HwEcmpBackupGroupSpilloverBenchmark.cpp
  DEPS
    config_factory
    ecmp_helper
    ecmp_test_utils
    load_balancer_test_utils
    scale_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(ecmp_group_scale_benchmark
  SRCS fboss/agent/hw/benchmarks/HwEcmpGroupScaleBenchmark.cpp
  DEPS
    config_factory
    ecmp_helper
    ecmp_test_utils
    load_balancer_test_utils
    scale_test_utils
    Folly::folly
    Folly::follybenchmark
)

BUILD_HW_BENCHMARK_LIBS(voq_route_competing_remote_neighbor_benchmark
  SRCS fboss/agent/hw/benchmarks/HwVoqRouteCompetingRemoteNeighborBenchmark.cpp
  DEPS
    Folly::folly
    Folly::follybenchmark
  HW_DEPS voq_route_competing_remote_neighbor_benchmark_helper
)
