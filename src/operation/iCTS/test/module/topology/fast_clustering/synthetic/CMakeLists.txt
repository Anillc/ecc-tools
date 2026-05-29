icts_add_test_executable(
  icts_test_module_topology_fast_clustering
  SOURCES
    ${ICTS_TEST}/module/topology/fast_clustering/synthetic/FastClusteringSyntheticTest.cc
  LIBS
    icts_test_common_data_pin_factory
    icts_source_module_topology
)

target_link_libraries(
  icts_test_module_topology_fast_clustering
  PRIVATE
  "$<LINK_GROUP:RESCAN,icts_test_base,icts_test_common_io,icts_test_common_data_pin_factory,icts_source_module_topology,report_table,fort>"
)
