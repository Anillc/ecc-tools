add_library(
  icts_test_common_cluster_visualization
  STATIC
  ${ICTS_TEST}/common/visualization/cluster/ClusterSvgWriter.cc
)

target_link_libraries(
  icts_test_common_cluster_visualization
  PRIVATE
  icts_test_common_visualization_core
  icts_test_base
)
