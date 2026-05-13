add_library(icts_source_flow_report_visualization_svg ${ICTS_FLOW_REPORT_VISUALIZATION_SVG}/SvgVisualization.cc)

target_link_libraries(
  icts_source_flow_report_visualization_svg
  PRIVATE
  icts_source_database_design
  icts_source_flow_report_visualization_drawing
  icts_source_utils
)

target_include_directories(
  icts_source_flow_report_visualization_svg
  PUBLIC
  ${ICTS_FLOW}
)
