add_library(
  icts_test_module_topology_linear_clustering_synthetic_config_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/support/config/LinearClusteringSyntheticConfig.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_synthetic_config_support
  PUBLIC
  icts_test_base
  PRIVATE
  icts_source_module_topology_linear_clustering
)
