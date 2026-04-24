add_library(
  icts_source_module_topology_fast_clustering
  ${ICTS_MODULE_TOPOLOGY}/fast_clustering/FastClustering.cc
)

target_link_libraries(
  icts_source_module_topology_fast_clustering
  PUBLIC
  icts_source_database_spatial
  icts_source_module_topology_config
  PRIVATE
  icts_source_database_design
  icts_source_module_topology_linear_clustering
  icts_source_utils
  icts_source_utils_geometry
  icts_source_utils_logger
)

target_include_directories(
  icts_source_module_topology_fast_clustering
  PUBLIC
  ${ICTS_MODULE_TOPOLOGY}/fast_clustering
  PRIVATE
  ${ICTS_MODULE_TOPOLOGY}/clustering
)

if(DEBUG_ICTS_SOURCE_MODULE_TOPOLOGY_FAST_CLUSTERING)
  if(MSVC)
    target_compile_options(icts_source_module_topology_fast_clustering PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_topology_fast_clustering PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_topology_fast_clustering PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
