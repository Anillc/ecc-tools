add_subdirectory(core)
add_subdirectory(cluster)
add_subdirectory(topology)

add_library(
  icts_test_common_visualization
  STATIC
  ${ICTS_TEST}/common/visualization/TestVisualization.cc
)

target_link_libraries(
  icts_test_common_visualization
  PRIVATE
  icts_test_base
  icts_test_common_cluster_visualization
  icts_test_common_topology_visualization
)
