add_library(
  icts_test_module_topology_gen_case_support
  STATIC
  ${ICTS_TEST}/module/topology/topology_gen/support/case/TopologyGenCaseSupport.cc
)

target_link_libraries(
  icts_test_module_topology_gen_case_support
  PUBLIC
  icts_test_common_data
)
