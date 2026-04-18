add_subdirectory(support)
add_subdirectory(scenario)

set(
  ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_SMOKE_SOURCES
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechSweepTest.cc
)

icts_add_test_executable(
  icts_test_module_topology_linear_clustering_realtech
  REALTECH
  SOURCES
    ${ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_SMOKE_SOURCES}
  LIBS
    icts_test_module_topology_linear_clustering_realtech_sweep_scenario_support
)

target_compile_definitions(
  icts_test_module_topology_linear_clustering_realtech
  PRIVATE
    ICTS_ENABLE_SLOW_REALTECH_REGRESSION=0
)

if(ICTS_BUILD_SLOW_REALTECH_TESTS)
  set(
    ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_REGRESSION_SOURCES
    ${ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_SMOKE_SOURCES}
    ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechSetupTest.cc
    ${ICTS_TEST}/module/topology/linear_clustering/realtech/LinearClusteringRealTechElectricalTest.cc
  )

  icts_add_test_executable(
    icts_test_module_topology_linear_clustering_realtech_regression
    REALTECH
    SOURCES
      ${ICTS_TEST_MODULE_TOPOLOGY_LINEAR_CLUSTERING_REALTECH_REGRESSION_SOURCES}
    LIBS
      icts_test_module_topology_linear_clustering_realtech_setup_scenario_support
      icts_test_module_topology_linear_clustering_realtech_sweep_scenario_support
      icts_test_module_topology_linear_clustering_realtech_electrical_scenario_support
  )

  target_compile_definitions(
    icts_test_module_topology_linear_clustering_realtech_regression
    PRIVATE
      ICTS_ENABLE_SLOW_REALTECH_REGRESSION=1
  )
endif()
