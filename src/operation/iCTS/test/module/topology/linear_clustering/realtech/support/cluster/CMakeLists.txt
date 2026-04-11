add_library(
  icts_test_module_topology_linear_clustering_realtech_cluster_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/support/cluster/LinearClusteringRealTechCluster.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_realtech_cluster_support
  PUBLIC
  icts_test_base
)
