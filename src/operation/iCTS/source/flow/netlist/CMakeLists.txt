add_library(icts_source_flow_netlist ${ICTS_FLOW}/netlist/ClockNetEditor.cc)

target_link_libraries(
  icts_source_flow_netlist
  PRIVATE
  icts_source_flow_report_data
  icts_source_database
  icts_source_database_design
  icts_source_utils
)

target_include_directories(
  icts_source_flow_netlist
  PUBLIC
  ${ICTS_FLOW}
)
