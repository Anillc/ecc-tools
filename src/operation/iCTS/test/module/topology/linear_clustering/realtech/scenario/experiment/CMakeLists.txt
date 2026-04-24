add_library(
  icts_test_module_topology_linear_clustering_realtech_experiment_scenario_support
  STATIC
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentInput.cc
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentReporting.cc
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc
  ${ICTS_TEST}/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentSyntheticInput.cc
)

target_link_libraries(
  icts_test_module_topology_linear_clustering_realtech_experiment_scenario_support
  PUBLIC
    icts_test_module_topology_linear_clustering_realtech_shared_support
  PRIVATE
    icts_test_common_logging_support
)
