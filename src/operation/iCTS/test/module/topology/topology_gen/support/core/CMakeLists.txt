add_library(
  icts_test_module_topology_gen_core_support
  STATIC
  ${ICTS_TEST}/module/topology/topology_gen/support/core/TopologyGenScenarioSupport.cc
)

target_link_libraries(
  icts_test_module_topology_gen_core_support
  PUBLIC
  icts_test_module_topology_gen_case_support
  icts_test_module_topology_gen_analysis_support
  icts_test_module_topology_gen_artifact_support
  PRIVATE
  log
  icts_test_common_io
  icts_test_common_logging_support
)
