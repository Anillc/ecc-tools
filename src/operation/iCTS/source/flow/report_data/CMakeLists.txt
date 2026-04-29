add_library(
  icts_source_flow_report_data
  ${ICTS_FLOW}/report_data/ClockTreeReportDataBuilder.cc
  ${ICTS_FLOW}/report_data/ClockTreeReportData.cc
)

target_link_libraries(
  icts_source_flow_report_data
  PUBLIC
  icts_source_database_spatial
  icts_source_flow_synthesis
  PRIVATE
  icts_source_database
  icts_source_module_routing
)

target_include_directories(
  icts_source_flow_report_data
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_REPORT_DATA)
  if(MSVC)
    target_compile_options(icts_source_flow_report_data PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_report_data PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_report_data PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
