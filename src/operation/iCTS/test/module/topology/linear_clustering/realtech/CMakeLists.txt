add_subdirectory(support)
add_subdirectory(scenario)

set(
  ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_SOURCES
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechSetupTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechSweepTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechElectricalTest.cc
)

icts_add_test_executable(
  icts_test_module_topology_linear_clustering_realtech
  REALTECH
  SOURCES
    ${ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_SOURCES}
  LIBS
    icts_test_module_topology_linear_clustering_realtech_support
)
