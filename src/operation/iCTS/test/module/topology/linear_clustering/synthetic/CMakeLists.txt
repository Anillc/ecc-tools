add_subdirectory(support)

set(
  ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_SYNTHETIC_SOURCES
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticDistributionTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticConstraintTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticOrderTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticStrategyTest.cc
  ${ICTS_TEST}/module/topology/linear_clustering/synthetic/LinearClusteringSyntheticSweepScoringTest.cc
)

icts_add_test_executable(
  icts_test_module_topology_linear_clustering
  SOURCES
    ${ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_SYNTHETIC_SOURCES}
  LIBS
    icts_test_module_topology_linear_clustering_synthetic_support
)
