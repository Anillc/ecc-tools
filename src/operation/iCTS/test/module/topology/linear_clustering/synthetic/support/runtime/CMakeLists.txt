add_library(
  icts_test_module_topology_linear_clustering_synthetic_runtime_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/support/runtime/LinearClusteringSyntheticRuntime.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_synthetic_runtime_support
  PUBLIC
  icts_test_base
  icts_test_common_io
  icts_test_common_linear_clustering_support
)
