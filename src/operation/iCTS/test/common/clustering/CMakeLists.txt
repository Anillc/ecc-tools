add_subdirectory(metrics)
add_subdirectory(artifact)

add_library(icts_test_common_clustering_support INTERFACE)

target_link_libraries(
  icts_test_common_clustering_support
  INTERFACE
  icts_test_common_clustering_metrics
  icts_test_common_clustering_artifact
)
