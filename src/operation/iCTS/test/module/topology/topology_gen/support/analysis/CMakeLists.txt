add_library(
  icts_test_module_topology_gen_analysis_support
  STATIC
  ${ICTS_TEST}/module/topology/topology_gen/support/analysis/TopologyGenAnalysisSupport.cc
)

target_link_libraries(
  icts_test_module_topology_gen_analysis_support
  PUBLIC
  icts_test_common_topology_support
  PRIVATE
  icts_test_base
)
