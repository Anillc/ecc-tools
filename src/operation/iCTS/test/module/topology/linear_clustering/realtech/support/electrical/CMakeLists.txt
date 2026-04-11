add_library(
  icts_test_module_topology_linear_clustering_realtech_electrical_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/support/electrical/LinearClusteringRealTechElectrical.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_realtech_electrical_support
  PUBLIC
  icts_test_module_topology_linear_clustering_realtech_cluster_support
  PRIVATE
  icts_test_realtech_base
)
