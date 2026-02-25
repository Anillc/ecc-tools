add_library(
  icts_source_module_topology
  ${ICTS_MODULE_TOPOLOGY}/TopologyGen.cc
  ${ICTS_MODULE_TOPOLOGY}/clustering/Clustering.cc
)

target_link_libraries(
  icts_source_module_topology
  PUBLIC
  icts_source_database
  icts_source_utils
  lemon
)

target_include_directories(icts_source_module_topology PUBLIC ${ICTS_SOURCE})

if(DEBUG_ICTS_SOURCE_MODULE_TOPOLOGY)
  if(MSVC)
    target_compile_options(icts_source_module_topology PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_topology PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_topology PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
