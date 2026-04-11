add_library(
  icts_test_module_topology_linear_clustering_synthetic_core_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.cc
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticCore.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_synthetic_core_support
  PRIVATE
  icts_test_module_topology_linear_clustering_synthetic_config_support
  PUBLIC
  icts_test_base
  icts_test_common_linear_clustering_support
)
