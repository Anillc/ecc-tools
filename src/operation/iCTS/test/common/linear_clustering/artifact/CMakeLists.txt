add_library(
  icts_test_common_linear_clustering_artifact
  STATIC
  ${ICTS_TEST}/common/linear_clustering/artifact/ClusterArtifactSupport.cc
)

target_link_libraries(
  icts_test_common_linear_clustering_artifact
  PRIVATE
  icts_test_common_linear_clustering_metrics
)
