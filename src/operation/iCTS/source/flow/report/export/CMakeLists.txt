add_library(icts_source_flow_report_export ${ICTS_FLOW_REPORT_EXPORT}/ResultExport.cc)

target_link_libraries(
  icts_source_flow_report_export
  PRIVATE
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_report_export
  PUBLIC
  ${ICTS_FLOW}
)
