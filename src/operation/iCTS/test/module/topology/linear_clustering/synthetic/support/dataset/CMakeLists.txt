add_library(
  icts_test_module_topology_linear_clustering_synthetic_dataset_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/support/dataset/LinearClusteringSyntheticDataset.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_synthetic_dataset_support
  PUBLIC
  icts_test_common_data
)
