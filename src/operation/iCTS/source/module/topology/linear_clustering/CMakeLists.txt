add_library(
  icts_source_module_topology_linear_clustering
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering/LinearOrderGenerator.cc
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering/ConstraintEvaluator.cc
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering/ClusteringEvaluator.cc
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering/SequenceSplitter.cc
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering/LinearClustering.cc
)

target_link_libraries(
  icts_source_module_topology_linear_clustering
  PUBLIC
  icts_source_database_spatial
  icts_source_module_topology_config
  PRIVATE
  icts_source_database_design
  icts_source_database_adapter_sta
  icts_source_database_io
  icts_source_module_routing_helper
  icts_source_module_routing
  icts_source_module_timing
  icts_source_utils_geometry
  icts_source_utils_logger
)

target_include_directories(
  icts_source_module_topology_linear_clustering
  PUBLIC
  ${ICTS_MODULE_TOPOLOGY}/linear_clustering
  PRIVATE
  ${ICTS_MODULE_TOPOLOGY}/clustering
)

if(DEBUG_ICTS_SOURCE_MODULE_TOPOLOGY_LINEAR_CLUSTERING)
  if(MSVC)
    target_compile_options(icts_source_module_topology_linear_clustering PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_topology_linear_clustering PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_topology_linear_clustering PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
