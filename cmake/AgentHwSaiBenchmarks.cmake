# CMake to build libraries and binaries in fboss/agent/hw/sai/benchmarks

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

# NOTE: All the benchmark executables need to link in ${SAI_IMPL_ARG}
# using '--whole-archive' flag in order to ensure SAI_IMPL symbols are included
#
# All benchmarks are consolidated into a single binary (sai_all_benchmarks-*).
# Individual benchmarks are selected at runtime via --bm_regex.

set(SAI_BENCHMARKS "")
list(APPEND SAI_BENCHMARKS fsw_scale_route_add_speed)
list(APPEND SAI_BENCHMARKS fsw_scale_route_del_speed)
list(APPEND SAI_BENCHMARKS th_alpm_scale_route_add_speed)
list(APPEND SAI_BENCHMARKS th_alpm_scale_route_del_speed)
list(APPEND SAI_BENCHMARKS hgrid_du_scale_route_add_speed)
list(APPEND SAI_BENCHMARKS hgrid_du_scale_route_del_speed)
list(APPEND SAI_BENCHMARKS hgrid_uu_scale_route_add_speed)
list(APPEND SAI_BENCHMARKS hgrid_uu_scale_route_del_speed)
list(APPEND SAI_BENCHMARKS anticipated_scale_route_add_speed)
list(APPEND SAI_BENCHMARKS anticipated_scale_route_del_speed)
list(APPEND SAI_BENCHMARKS stats_collection_speed)
list(APPEND SAI_BENCHMARKS tx_slow_path_rate)
list(APPEND SAI_BENCHMARKS ecmp_shrink_speed)
list(APPEND SAI_BENCHMARKS ecmp_shrink_with_competing_route_updates_speed)
list(APPEND SAI_BENCHMARKS rx_slow_path_rate)
list(APPEND SAI_BENCHMARKS init_and_exit_40Gx10G)
list(APPEND SAI_BENCHMARKS init_and_exit_100Gx10G)
list(APPEND SAI_BENCHMARKS init_and_exit_100Gx25G)
list(APPEND SAI_BENCHMARKS init_and_exit_100Gx50G)
list(APPEND SAI_BENCHMARKS init_and_exit_100Gx100G)
list(APPEND SAI_BENCHMARKS init_and_exit_400Gx400G)
list(APPEND SAI_BENCHMARKS rib_resolution_speed)
list(APPEND SAI_BENCHMARKS rib_sync_fib_speed)
list(APPEND SAI_BENCHMARKS ucmp_scale_benchmark)
list(APPEND SAI_BENCHMARKS srv6_scale_benchmark)
list(APPEND SAI_BENCHMARKS srv6_mysid_scale_benchmark)
list(APPEND SAI_BENCHMARKS pbr_scale_benchmark)
list(APPEND SAI_BENCHMARKS cpu_latency_benchmark)
list(APPEND SAI_BENCHMARKS system_scale_churn_memory_benchmark)
list(APPEND SAI_BENCHMARKS tun_manager_probe_and_cleanup_speed)
list(APPEND SAI_BENCHMARKS ecmp_backup_group_spillover)
list(APPEND SAI_BENCHMARKS ecmp_group_scale_benchmark)
list(APPEND SAI_BENCHMARKS bgp_rx_slow_path_rate)
list(APPEND SAI_BENCHMARKS rx_slow_path_arp_rate)
list(APPEND SAI_BENCHMARKS system_scale_memory_benchmark)
list(APPEND SAI_BENCHMARKS flowlet_stats_collection_speed)
list(APPEND SAI_BENCHMARKS clear_interface_counters_phy_benchmark)
if (SAI_BRCM_IMPL OR BUILD_SAI_FAKE)
  list(APPEND SAI_BENCHMARKS init_and_exit_voq)
  list(APPEND SAI_BENCHMARKS init_and_exit_fabric)
  list(APPEND SAI_BENCHMARKS voq_scale_route_add_speed)
  list(APPEND SAI_BENCHMARKS voq_scale_route_del_speed)
  list(APPEND SAI_BENCHMARKS switch_reachability_change_speed)
  list(APPEND SAI_BENCHMARKS voq_sys_port_programming)
  list(APPEND SAI_BENCHMARKS voq_remote_entity_programming)
  list(APPEND SAI_BENCHMARKS voq_route_competing_remote_neighbor_benchmark)
endif()

# Build consolidated benchmark binaries (mono and multi-switch).
# Use --bm_regex at runtime to select which benchmark to run.
function(BUILD_ALL_SAI_BENCHMARKS SAI_IMPL_NAME SAI_IMPL_ARG)
  message(STATUS "Building consolidated SAI benchmark binaries: sai_all_benchmarks-${SAI_IMPL_NAME}")

  set(_all_mono_hw_libs "")
  set(_all_ms_hw_libs "")
  foreach(SAI_BENCHMARK IN LISTS SAI_BENCHMARKS)
    list(APPEND _all_mono_hw_libs hw_${SAI_BENCHMARK})
    list(APPEND _all_ms_hw_libs multi_switch_hw_${SAI_BENCHMARK})
  endforeach()

  # Mono benchmark binary
  add_executable(sai_all_benchmarks-${SAI_IMPL_NAME} /dev/null)
  add_sai_sdk_dependencies(
    sai_all_benchmarks-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")
  target_link_libraries(sai_all_benchmarks-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    mono_sai_agent_benchmarks_main
    ${_all_mono_hw_libs}
    route_scale_gen
    setup_thrift_prod
    ${SAI_IMPL_ARG}
    -Wl,--no-whole-archive
  )
  set_target_properties(sai_all_benchmarks-${SAI_IMPL_NAME}
    PROPERTIES COMPILE_FLAGS
    "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
    -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
    -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
  )

  # Multi-switch benchmark binary
  add_executable(sai_multi_switch_all_benchmarks-${SAI_IMPL_NAME} /dev/null)
  add_sai_sdk_dependencies(
    sai_multi_switch_all_benchmarks-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")
  target_link_libraries(sai_multi_switch_all_benchmarks-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    multi_switch_sai_agent_benchmarks_main
    ${_all_ms_hw_libs}
    route_scale_gen
    setup_thrift_prod
    ${SAI_IMPL_ARG}
    -Wl,--no-whole-archive
  )
  set_target_properties(sai_multi_switch_all_benchmarks-${SAI_IMPL_NAME}
    PROPERTIES COMPILE_FLAGS
    "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
    -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
    -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
  )
endfunction()

if(BUILD_SAI_FAKE AND BUILD_SAI_FAKE_BENCHMARKS)
  BUILD_ALL_SAI_BENCHMARKS("fake" fake_sai)
endif()

# If libsai_impl is provided, build sai tests linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(SAI_IMPL AND BENCHMARK_INSTALL)
  BUILD_ALL_SAI_BENCHMARKS("sai_impl" ${SAI_IMPL})
  install(TARGETS sai_all_benchmarks-sai_impl)
  install(TARGETS sai_multi_switch_all_benchmarks-sai_impl)
endif()
