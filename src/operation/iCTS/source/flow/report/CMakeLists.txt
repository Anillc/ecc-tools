add_library(
  icts_source_flow_report
  STATIC
  ${ICTS_FLOW_REPORT}/CTSVisualizationReport.cc
)

target_link_libraries(
  icts_source_flow_report
  PRIVATE
  icts_source_database
  icts_source_module_routing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_report
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_REPORT)
  if(MSVC)
    target_compile_options(icts_source_flow_report PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_report PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_report PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
