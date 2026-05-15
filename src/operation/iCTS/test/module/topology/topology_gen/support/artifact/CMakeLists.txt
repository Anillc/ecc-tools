add_library(
  icts_test_module_topology_gen_artifact_support
  STATIC
  ${ICTS_TEST}/module/topology/topology_gen/support/artifact/TopologyGenArtifactSupport.cc
)

target_link_libraries(
  icts_test_module_topology_gen_artifact_support
  PUBLIC
  icts_test_common_topology_support
  icts_test_common_visualization
  PRIVATE
  icts_test_base
)
