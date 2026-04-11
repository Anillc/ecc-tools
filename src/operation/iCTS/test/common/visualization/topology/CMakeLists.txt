add_library(
  icts_test_common_topology_visualization
  STATIC
  ${ICTS_TEST}/common/visualization/topology/TopologySvgWriter.cc
)

target_link_libraries(
  icts_test_common_topology_visualization
  PRIVATE
  icts_test_common_visualization_core
  icts_test_base
)
