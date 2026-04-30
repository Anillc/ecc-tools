add_library(
  icts_source_flow_clock_tree_view
  ${ICTS_FLOW_CLOCK_TREE_VIEW}/ClockTreeViewBuilder.cc
  ${ICTS_FLOW_CLOCK_TREE_VIEW}/ClockTreeView.cc
  ${ICTS_FLOW_CLOCK_TREE_VIEW}/ClockTreeVisualizationModel.cc
)

target_link_libraries(
  icts_source_flow_clock_tree_view
  PUBLIC
  icts_source_database_spatial
  PRIVATE
  icts_source_database
  icts_source_module_routing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_clock_tree_view
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_CLOCK_TREE_VIEW)
  if(MSVC)
    target_compile_options(icts_source_flow_clock_tree_view PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_clock_tree_view PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_clock_tree_view PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
