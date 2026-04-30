add_library(
  icts_source_flow_visualization
  STATIC
  ${ICTS_FLOW_VISUALIZATION}/ClockTreeGdsVisualization.cc
  ${ICTS_FLOW_VISUALIZATION}/ClockTreeGdsWriter.cc
  ${ICTS_FLOW_VISUALIZATION}/ClockTreeVisualizationLayerPolicy.cc
  ${ICTS_FLOW_VISUALIZATION}/ClockTreeSvgVisualization.cc
)

target_link_libraries(
  icts_source_flow_visualization
  PUBLIC
  icts_source_flow_clock_tree_view
  icts_source_database_spatial
  PRIVATE
  icts_source_database
  icts_source_module_routing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_visualization
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_VISUALIZATION)
  if(MSVC)
    target_compile_options(icts_source_flow_visualization PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_visualization PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_visualization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
