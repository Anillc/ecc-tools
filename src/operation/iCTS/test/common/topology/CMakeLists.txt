add_library(
  icts_test_common_topology_support
  STATIC
  ${ICTS_TEST}/common/topology/TopologyAnalysis.cc
)

target_link_libraries(
  icts_test_common_topology_support
  PRIVATE
  icts_test_common_types
)
