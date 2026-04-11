add_library(
  icts_test_common_linear_clustering_metrics
  STATIC
  ${ICTS_TEST}/common/linear_clustering/metrics/ClusterGeometrySupport.cc
)

target_link_libraries(
  icts_test_common_linear_clustering_metrics
  PUBLIC
  icts_test_base
)
