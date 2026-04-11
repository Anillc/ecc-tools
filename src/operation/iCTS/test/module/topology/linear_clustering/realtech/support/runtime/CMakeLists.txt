add_library(
  icts_test_module_topology_linear_clustering_realtech_runtime_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/support/runtime/LinearClusteringRealTechRuntime.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_realtech_runtime_support
  PUBLIC
  icts_test_realtech_base
  icts_test_common_io
  icts_test_common_realtech_support
  icts_test_module_topology_linear_clustering_realtech_cluster_support
)
