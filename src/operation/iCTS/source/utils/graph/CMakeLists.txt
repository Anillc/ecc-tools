add_library(icts_source_utils_graph INTERFACE)

target_include_directories(
  icts_source_utils_graph
  INTERFACE
  ${ICTS_UTILS}
  ${ICTS_GRAPH}
)

if(DEBUG_ICTS_SOURCE_UTILS_GRAPH)
  if(MSVC)
    target_compile_options(icts_source_utils_graph INTERFACE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_utils_graph INTERFACE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
